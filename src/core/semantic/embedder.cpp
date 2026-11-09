#include "semantic/embedder.h"

#include <QLoggingCategory>
#include <QThread>

#include <ggml-backend.h>
#include <llama.h>

#include <mutex>
#include <vector>

using namespace Qt::StringLiterals;

namespace brulion::semantic {

Q_LOGGING_CATEGORY(lcEmbedder, "brulion.semantic.embedder", QtWarningMsg)

namespace {

void logCallback(ggml_log_level level, const char *text, void *)
{
    if (level == GGML_LOG_LEVEL_ERROR)
        qCWarning(lcEmbedder, "%s", text);
    else
        qCDebug(lcEmbedder, "%s", text);
}

void initBackend()
{
    static std::once_flag once;
    std::call_once(once, [] {
        llama_log_set(logCallback, nullptr);
        // The CPU variants of a GGML_BACKEND_DL build (CMakeLists.txt); a static build has its
        // backends linked in and loads nothing here.
        QByteArray dir = qgetenv("BRULION_GGML_BACKEND_DIR");
#ifdef BRULION_GGML_BACKEND_DIR
        if (dir.isEmpty())
            dir = BRULION_GGML_BACKEND_DIR;
#endif
        if (!dir.isEmpty())
            ggml_backend_load_all_from_path(dir.constData());
        if (ggml_backend_dev_count() == 0)
            ggml_backend_load_all();
        llama_backend_init();
    });
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

} // namespace

struct Embedder::Impl {
    struct ModelFree {
        void operator()(llama_model *m) const { llama_model_free(m); }
    };
    struct ContextFree {
        void operator()(llama_context *c) const { llama_free(c); }
    };

    Options options;
    std::unique_ptr<llama_model, ModelFree> model;
    std::unique_ptr<llama_context, ContextFree> ctx; ///< after `model`: freed before it
    const llama_vocab *vocab = nullptr;
    QString deviceName;
    int dim = 0;
    int threads = 0; ///< for passages (batches); queries use options.queryThreads
    int activeThreads = 0;
    qint64 tokens = 0;

    void useThreads(int n)
    {
        if (n != activeThreads) {
            llama_set_n_threads(ctx.get(), n, n);
            activeThreads = n;
        }
    }

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

    /// Encode texts [begin, end) in one call; false on failure.
    bool encode(const std::vector<std::vector<llama_token>> &toks, size_t begin, size_t end, QList<Vector> *out)
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
        if (llama_memory_t mem = llama_get_memory(ctx.get()))
            llama_memory_clear(mem, true);
        const int32_t rc = llama_encode(ctx.get(), batch);
        bool ok = rc == 0;
        if (!ok)
            qCWarning(lcEmbedder, "llama_encode failed: %d", rc);
        for (size_t i = begin; ok && i < end; ++i) {
            const float *e = llama_get_embeddings_seq(ctx.get(), llama_seq_id(i - begin));
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
};

Embedder::Embedder(std::unique_ptr<Impl> impl) : d(std::move(impl)) {}
Embedder::~Embedder() = default;

std::unique_ptr<Embedder> Embedder::open(const QString &ggufPath, const Options &options, QString *error)
{
    initBackend();
    auto d = std::make_unique<Impl>();
    d->options = options;
    d->options.maxTokens = std::max(8, options.maxTokens);
    d->options.batchTokens = std::max(d->options.maxTokens, options.batchTokens);
    d->options.maxSequences = std::max(1, options.maxSequences);

    // Hardware acceleration: the whole model on the first discrete GPU (also an integrated one
    // with Device::AnyGpu) — Vulkan on desktops and Android, CUDA/Metal where llama.cpp is built
    // with them — else the CPU.
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
    d->deviceName = gpu ? u"%1 (%2)"_s.arg(QString::fromUtf8(ggml_backend_dev_name(gpu)),
                                            QString::fromUtf8(ggml_backend_dev_description(gpu)))
                        : u"CPU"_s;
    d->model.reset(llama_model_load_from_file(ggufPath.toUtf8().constData(), mp));
    if (!d->model) {
        if (error)
            *error = u"cannot load the model %1"_s.arg(ggufPath);
        return nullptr;
    }
    d->options.maxTokens = std::min(d->options.maxTokens, int(llama_model_n_ctx_train(d->model.get())));
    d->vocab = llama_model_get_vocab(d->model.get());
    d->dim = llama_model_n_embd(d->model.get());

    llama_context_params cp = llama_context_default_params();
    cp.embeddings = true;
    cp.pooling_type = LLAMA_POOLING_TYPE_UNSPECIFIED; // the model's own: mean for e5, CLS for mmlw
    cp.n_ctx = uint32_t(d->options.batchTokens);
    cp.n_batch = uint32_t(d->options.batchTokens);
    cp.n_ubatch = uint32_t(d->options.batchTokens); // non-causal: a whole text in one ubatch
    cp.n_seq_max = uint32_t(d->options.maxSequences);
    // At most 4 threads, half the logical cores: SMT siblings and little cores slow llama.cpp's
    // barriers down (laptop i5-13450HX: 4 threads 3.9k tokens/s, 8 threads 3.4k; Pixel 9 Pro:
    // 3 threads 1.6k, 4 threads 1.3k, 8 threads 0.8k).
    d->threads = options.threads > 0 ? options.threads : std::clamp(QThread::idealThreadCount() / 2, 1, 4);
    d->options.queryThreads = options.queryThreads > 0 ? options.queryThreads : 1;
    cp.n_threads = d->threads;
    cp.n_threads_batch = d->threads;
    cp.op_offload = gpu != nullptr;
    d->ctx.reset(llama_init_from_model(d->model.get(), cp));
    if (!d->ctx) {
        if (error)
            *error = u"cannot create a llama.cpp context for %1"_s.arg(ggufPath);
        return nullptr;
    }
    d->activeThreads = d->threads;
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
    return d->tokens;
}

QList<Vector> Embedder::embed(const QStringList &texts)
{
    std::vector<std::vector<llama_token>> toks;
    toks.reserve(size_t(texts.size()));
    for (const QString &t : texts)
        toks.push_back(d->tokenize(t));
    QList<Vector> out;
    out.reserve(texts.size());
    size_t begin = 0;
    while (begin < toks.size()) {
        size_t end = begin, total = 0;
        while (end < toks.size() && end - begin < size_t(d->options.maxSequences)
               && total + toks[end].size() <= size_t(d->options.batchTokens)) {
            total += toks[end].size();
            ++end;
        }
        if (!d->encode(toks, begin, end, &out))
            return {};
        begin = end;
    }
    return out;
}

QList<Vector> Embedder::embedQueries(const QStringList &queries)
{
    d->useThreads(d->options.queryThreads);
    return embed(prefixed(u"query: "_s, queries));
}

QList<Vector> Embedder::embedPassages(const QStringList &passages)
{
    d->useThreads(d->threads);
    return embed(prefixed(u"passage: "_s, passages));
}

} // namespace brulion::semantic
