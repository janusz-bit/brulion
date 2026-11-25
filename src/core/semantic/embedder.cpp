#include "semantic/embedder.h"

#include "ai/cpus.h"
#include "ai/localrunner.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QThread>

// llama.cpp's headers are not written for Brulion's warnings.
QT_WARNING_PUSH
QT_WARNING_DISABLE_GCC("-Wshadow")
QT_WARNING_DISABLE_GCC("-Wold-style-cast")
QT_WARNING_DISABLE_CLANG("-Wshadow")
QT_WARNING_DISABLE_CLANG("-Wold-style-cast")
#include <ggml-backend.h>
#include <llama.h>
QT_WARNING_POP

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

using namespace Qt::StringLiterals;

namespace brulion::semantic {

Q_LOGGING_CATEGORY(lcEmbedder, "brulion.semantic.embedder", QtWarningMsg)

namespace {

/// The processor plugins of this build, the best first ("libggml-cpu-haswell.so|…": CMake,
/// 3rdparty/llama/CMakeLists.txt).
#ifndef BRULION_LLAMA_CPU_VARIANTS
#define BRULION_LLAMA_CPU_VARIANTS ""
#endif

void logCallback(ggml_log_level level, const char *text, void *)
{
    // (llama.cpp ends its lines itself, and writes some in pieces.)
    const QByteArray line = QByteArray(text).trimmed();
    if (line.isEmpty())
        return;
    if (level == GGML_LOG_LEVEL_ERROR)
        qCWarning(lcEmbedder, "%s", line.constData());
    else
        qCDebug(lcEmbedder, "%s", line.constData());
}

/// "libggml-cpu-haswell.so" → "ggml-cpu-haswell".
QString pluginBaseName(const QString &file)
{
    QString name = QFileInfo(file).fileName();
    const qsizetype dot = name.lastIndexOf(u'.');
    if (dot > 0)
        name.truncate(dot);
    return name.size() > 3 && name.startsWith(u"lib") ? name.mid(3) : name;
}

struct Backend {
    bool ok = false;
    QString name; ///< the processor plugin computing ("ggml-cpu-haswell")
    QString error;
};

/// llama.cpp's processor plugin, once per process: the one the model on the device or the speech
/// recognizer loaded already (ggml's registry is one for the whole process), else the first of this
/// build's that the processor can run (each refuses to load on one that lacks its instructions).
const Backend &backend(const QString &dir)
{
    static Backend loaded;
    static std::once_flag once;
    std::call_once(once, [&] {
        llama_log_set(logCallback, nullptr);
        if (ggml_backend_dev_t cpu = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU)) {
            loaded.name = QString::fromUtf8(ggml_backend_reg_name(ggml_backend_dev_backend_reg(cpu)));
        } else {
            QString from = qEnvironmentVariable("BRULION_GGML_BACKEND_DIR");
            if (from.isEmpty())
                from = dir;
            const QStringList files =
                    QString::fromLatin1(BRULION_LLAMA_CPU_VARIANTS).split(u'|', Qt::SkipEmptyParts);
            for (const QString &file : files) {
                if (ggml_backend_load(ai::backendPluginPath(from, file).constData())) {
                    loaded.name = pluginBaseName(file);
                    break;
                }
            }
            if (files.isEmpty()) {
                // (A llama.cpp of its own build, not this one's: its plugins wherever it keeps them.)
                if (!from.isEmpty())
                    ggml_backend_load_all_from_path(from.toUtf8().constData());
                if (ggml_backend_dev_count() == 0)
                    ggml_backend_load_all();
                if (ggml_backend_dev_t found = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU))
                    loaded.name = QString::fromUtf8(ggml_backend_reg_name(ggml_backend_dev_backend_reg(found)));
            }
            if (loaded.name.isEmpty()) {
                loaded.error = files.isEmpty() ? u"this build has no processor plugin of llama.cpp"_s
                                               : u"this device's processor is too old to search by meaning (it "
                                                 u"lacks the instructions llama.cpp is built for here)"_s;
                return;
            }
        }
        llama_backend_init();
        loaded.ok = true;
    });
    return loaded;
}

/// Every text with `prefix` in front.
QStringList prefixed(const QString &prefix, const QStringList &texts)
{
    QStringList out;
    out.reserve(texts.size());
    for (const QString &t : texts)
        out.append(prefix + t);
    return out;
}

/// Threads for passages when none are asked for: one fewer than the fast physical cores (a phone:
/// 3 of the Pixel 9 Pro's 4 fast ones measured best), else half the logical cores; at most 4 (SMT
/// siblings and little cores slow llama.cpp's barriers down: laptop i5-13450HX 4 threads 3.9k
/// tokens/s, 8 threads 3.4k; Pixel 9 Pro 3 threads 1.6k, 4 threads 1.3k, 8 threads 0.8k).
int defaultThreads()
{
    const QList<int> fast = ai::computeCores(ai::cpuCores());
    const int n = fast.isEmpty() ? QThread::idealThreadCount() / 2 : int(fast.size()) - 1;
    return std::clamp(n, 1, 4);
}

} // namespace

struct Embedder::Impl {
    struct ModelFree {
        void operator()(llama_model *m) const { llama_model_free(m); }
    };
    struct ContextFree {
        void operator()(llama_context *c) const { llama_free(c); }
    };
    using Context = std::unique_ptr<llama_context, ContextFree>;

    Options options;
    std::unique_ptr<llama_model, ModelFree> model;
    Context passages; ///< after `model`: freed before it
    Context queries;
    const llama_vocab *vocab = nullptr;
    QString deviceName;
    int dim = 0;
    int threads = 0; ///< for passages; queries use options.queryThreads
    bool onGpu = false;
    std::atomic<qint64> tokens = 0;

    std::vector<llama_token> tokenize(const QString &text) const
    {
        const QByteArray utf8 = text.toUtf8();
        std::vector<llama_token> out(size_t(utf8.size()) + 8);
        int n = llama_tokenize(vocab, utf8.constData(), int32_t(utf8.size()), out.data(), int32_t(out.size()),
                               /*add_special*/ true, /*parse_special*/ false);
        if (n < 0) {
            out.resize(size_t(-n));
            n = llama_tokenize(vocab, utf8.constData(), int32_t(utf8.size()), out.data(), int32_t(out.size()), true,
                               false);
        }
        out.resize(size_t(std::max(n, 0)));
        const size_t max = size_t(options.maxTokens);
        if (out.size() > max) { // keep the closing </s>
            const llama_token last = out.back();
            out.resize(max);
            out.back() = last;
        }
        return out;
    }

    /// Encode texts [begin, end) in one call of `ctx`; false on failure.
    bool encode(llama_context *ctx, const std::vector<std::vector<llama_token>> &toks, size_t begin, size_t end,
                QList<Vector> *out)
    {
        size_t total = 0;
        for (size_t i = begin; i < end; ++i)
            total += toks[i].size();
        llama_batch batch = llama_batch_init(int32_t(total), 0, 1);
        for (size_t i = begin; i < end; ++i) {
            const auto seq = llama_seq_id(i - begin);
            for (size_t p = 0; p < toks[i].size(); ++p) {
                const int32_t j = batch.n_tokens++;
                batch.token[j] = toks[i][p];
                batch.pos[j] = llama_pos(p);
                batch.n_seq_id[j] = 1;
                batch.seq_id[j][0] = seq;
                batch.logits[j] = 1;
            }
        }
        if (llama_memory_t mem = llama_get_memory(ctx))
            llama_memory_clear(mem, true);
        const int32_t rc = llama_encode(ctx, batch);
        bool ok = rc == 0;
        if (!ok)
            qCWarning(lcEmbedder, "llama_encode failed: %d", rc);
        for (size_t i = begin; ok && i < end; ++i) {
            const float *e = llama_get_embeddings_seq(ctx, llama_seq_id(i - begin));
            if (!e) {
                ok = false;
                break;
            }
            Vector v(e, e + dim);
            normalize(v);
            out->append(std::move(v));
        }
        llama_batch_free(batch);
        tokens += qint64(total);
        return ok;
    }

    QList<Vector> embed(llama_context *ctx, const QStringList &texts)
    {
        std::vector<std::vector<llama_token>> toks;
        toks.reserve(size_t(texts.size()));
        for (const QString &t : texts)
            toks.push_back(tokenize(t));
        QList<Vector> out;
        out.reserve(texts.size());
        size_t begin = 0;
        while (begin < toks.size()) {
            size_t end = begin, total = 0;
            while (end < toks.size() && end - begin < size_t(options.maxSequences)
                   && total + toks[end].size() <= size_t(options.batchTokens)) {
                total += toks[end].size();
                ++end;
            }
            if (!encode(ctx, toks, begin, end, &out))
                return {};
            begin = end;
        }
        return out;
    }

    Context newContext(int n) const
    {
        llama_context_params cp = llama_context_default_params();
        cp.embeddings = true;
        cp.pooling_type = LLAMA_POOLING_TYPE_UNSPECIFIED; // the model's own: mean for e5, CLS for mmlw
        cp.n_ctx = uint32_t(options.batchTokens);
        cp.n_batch = uint32_t(options.batchTokens);
        cp.n_ubatch = uint32_t(options.batchTokens); // non-causal: a whole text in one ubatch
        cp.n_seq_max = uint32_t(options.maxSequences);
        cp.n_threads = n;
        cp.n_threads_batch = n;
        cp.op_offload = onGpu;
        return Context(llama_init_from_model(model.get(), cp));
    }
};

Embedder::Embedder(std::unique_ptr<Impl> impl) : d(std::move(impl)) {}
Embedder::~Embedder() = default;

std::unique_ptr<Embedder> Embedder::open(const QString &ggufPath, const Options &options, QString *error)
{
    const Backend &b = backend(options.backendDir);
    if (!b.ok || ggufPath.isEmpty()) {
        if (error)
            *error = b.error; // (no model: only the check that llama.cpp loads, semanticrunner.h)
        return nullptr;
    }
    auto d = std::make_unique<Impl>();
    d->options = options;
    d->options.maxTokens = std::max(8, options.maxTokens);
    d->options.batchTokens = std::max(d->options.maxTokens, options.batchTokens);
    d->options.maxSequences = std::max(1, options.maxSequences);

    // Hardware acceleration: the whole model on the first discrete GPU (also an integrated one
    // with Device::AnyGpu) — Vulkan, CUDA or Metal where llama.cpp is built with them — else the CPU.
    ggml_backend_dev_t gpu = nullptr;
    if (options.device != Device::Cpu) {
        QList<enum ggml_backend_dev_type> types{GGML_BACKEND_DEVICE_TYPE_GPU};
        if (options.device == Device::AnyGpu)
            types.append(GGML_BACKEND_DEVICE_TYPE_IGPU);
        for (auto type : std::as_const(types)) {
            for (size_t i = 0; !gpu && i < ggml_backend_dev_count(); ++i) {
                if (ggml_backend_dev_type(ggml_backend_dev_get(i)) == type)
                    gpu = ggml_backend_dev_get(i);
            }
        }
    }
    // On the CPU an empty device list: with nullptr llama.cpp still sets up every GPU and moves
    // big operations there (op_offload) — on a Pixel 9 Pro that made the "CPU" run 3x slower
    // (weights copied to the Mali GPU on every call), on a laptop it silently used the NVIDIA GPU.
    ggml_backend_dev_t devices[2] = {gpu, nullptr};
    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = gpu ? 999 : 0;
    mp.devices = gpu ? devices : devices + 1;
    d->onGpu = gpu != nullptr;
    d->threads = options.threads > 0 ? options.threads : defaultThreads();
    d->options.queryThreads = options.queryThreads > 0 ? options.queryThreads : 1;
    d->deviceName = gpu ? u"%1 (%2)"_s.arg(QString::fromUtf8(ggml_backend_dev_name(gpu)),
                                            QString::fromUtf8(ggml_backend_dev_description(gpu)))
                        : u"%1, %2 threads"_s.arg(b.name).arg(d->threads);
    d->model.reset(llama_model_load_from_file(ggufPath.toUtf8().constData(), mp));
    if (!d->model) {
        if (error)
            *error = u"cannot load the model %1"_s.arg(ggufPath);
        return nullptr;
    }
    d->options.maxTokens = std::min(d->options.maxTokens, int(llama_model_n_ctx_train(d->model.get())));
    d->vocab = llama_model_get_vocab(d->model.get());
    d->dim = llama_model_n_embd(d->model.get());
    d->passages = d->newContext(d->threads);
    d->queries = d->newContext(d->options.queryThreads);
    if (!d->passages || !d->queries) {
        if (error)
            *error = u"cannot create a llama.cpp context for %1"_s.arg(ggufPath);
        return nullptr;
    }
    return std::unique_ptr<Embedder>(new Embedder(std::move(d)));
}

QString Embedder::device() const
{
    return d->deviceName;
}

int Embedder::dimension() const
{
    return d->dim;
}

qint64 Embedder::tokenCount() const
{
    return d->tokens.load();
}

QList<Vector> Embedder::embed(const QStringList &texts)
{
    return d->embed(d->passages.get(), texts);
}

QList<Vector> Embedder::embedQueries(const QStringList &queries)
{
    return d->embed(d->queries.get(), prefixed(u"query: "_s, queries));
}

QList<Vector> Embedder::embedPassages(const QStringList &passages)
{
    return d->embed(d->passages.get(), prefixed(u"passage: "_s, passages));
}

Vector Embedder::embedQuery(const QString &query)
{
    const QList<Vector> v = d->embed(d->queries.get(), {u"query: "_s + query});
    return v.isEmpty() ? Vector() : v.first();
}

} // namespace brulion::semantic
