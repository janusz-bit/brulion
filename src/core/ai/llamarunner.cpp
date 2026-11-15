#include "ai/llamarunner.h"

#include "ai/cpus.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <QTimeZone>

// llama.cpp's headers are not written for Brulion's warnings.
QT_WARNING_PUSH
QT_WARNING_DISABLE_GCC("-Wshadow")
QT_WARNING_DISABLE_GCC("-Wold-style-cast")
QT_WARNING_DISABLE_CLANG("-Wshadow")
QT_WARNING_DISABLE_CLANG("-Wold-style-cast")
#include <ggml-backend.h>
#include <ggml-cpu.h>
#include <llama.h>
#include <mtmd-helper.h>
#include <mtmd.h>
QT_WARNING_POP

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstring>
#include <mutex>
#include <vector>

using namespace Qt::StringLiterals;

namespace brulion::ai {

Q_LOGGING_CATEGORY(lcLlama, "brulion.ai.llama", QtWarningMsg)

namespace {

/// The processor plugins of this build, the best first ("libggml-cpu-haswell.so|…": CMake).
#ifndef BRULION_LLAMA_CPU_VARIANTS
#define BRULION_LLAMA_CPU_VARIANTS ""
#endif

/// The release of llama.cpp compiled ("v0.9.0": CMake) — a state kept on disk is its own.
#ifndef BRULION_LLAMA_TAG
#define BRULION_LLAMA_TAG ""
#endif

/// Conversations kept in the model's memory at once (see Lane).
constexpr int LaneCount = 2;
/// A lane is continued when the new prompt starts with this many of its tokens; else the lane
/// used longest ago is taken.
constexpr size_t SharedStart = 64;
/// Tokens read in one step.
constexpr int BatchTokens = 512;
/// The states kept per lane to step back to (the first — after the system text — always stays).
constexpr size_t MaxCheckpoints = 4;

/// A prompt's stable part is kept on disk (Options::stateDir) from this many cells on — a shorter
/// one is read in a moment —, at most this many states together, of at most this many bytes
/// together (the assistant's instructions and tools: some 2 500 tokens, 30 MB).
constexpr int MinStoredCells = 256;
constexpr int MaxStoredStates = 4;
constexpr qint64 MaxStoredBytes = 192LL * 1024 * 1024;
/// A kept state's file: this, its version, its key (StateKeyBytes), its cells, the bytes of
/// llama.cpp's state that follow (numbers the lowest byte first).
constexpr char StateMagic[4] = {'B', 'R', 'S', 'T'};
constexpr quint32 StateVersion = 1;
constexpr qint64 StateKeyBytes = 32; // SHA-256
constexpr qint64 StateHeaderBytes = qint64(sizeof StateMagic) + 4 + StateKeyBytes + 4 + 8;

void logCallback(ggml_log_level level, const char *text, void *)
{
    // (llama.cpp ends its lines itself, and writes some in pieces.)
    const QByteArray line = QByteArray(text).trimmed();
    if (line.isEmpty())
        return;
    if (level == GGML_LOG_LEVEL_ERROR)
        qCWarning(lcLlama, "%s", line.constData());
    else
        qCDebug(lcLlama, "%s", line.constData());
}

struct Backend {
    bool ok = false;
    QString name; ///< the processor plugin loaded ("ggml-cpu-haswell")
    QString error;
};

/// llama.cpp's processor plugin, loaded once per process: the first of this build's that the
/// processor can run (each refuses to load on one that lacks its instructions).
const Backend &backend(const QString &dir)
{
    static Backend loaded;
    static std::once_flag once;
    std::call_once(once, [&] {
        llama_log_set(logCallback, nullptr);
        mtmd_helper_log_set(logCallback, nullptr);
        const QStringList files = QString::fromLatin1(BRULION_LLAMA_CPU_VARIANTS).split(u'|', Qt::SkipEmptyParts);
        for (const QString &file : files) {
            const QString path = dir.isEmpty() ? file : dir + u'/' + file;
            if (ggml_backend_load(path.toUtf8().constData())) {
                loaded.name = pluginName(file);
                break;
            }
        }
        if (loaded.name.isEmpty()) {
            loaded.error = files.isEmpty() ? u"this build has no processor plugin of llama.cpp"_s
                                           : u"this device's processor is too old to run a model (it lacks the "
                                             "instructions llama.cpp is built for here)"_s;
            return;
        }
        llama_backend_init();
        loaded.ok = true;
    });
    return loaded;
}

struct ModelFree {
    void operator()(llama_model *m) const { llama_model_free(m); }
};
struct ContextFree {
    void operator()(llama_context *c) const { llama_free(c); }
};
struct MtmdFree {
    void operator()(mtmd_context *c) const { mtmd_free(c); }
};
struct SamplerFree {
    void operator()(llama_sampler *s) const { llama_sampler_free(s); }
};
struct ChunkFree {
    void operator()(mtmd_input_chunk *c) const { mtmd_input_chunk_free(c); }
};
/// (ggml_threadpool_free of the processor plugin: llama.cpp's libraries do not export it.)
struct PoolFree {
    void (*release)(ggml_threadpool *) = nullptr;
    void operator()(ggml_threadpool *pool) const
    {
        if (release)
            release(pool);
    }
};
using ThreadPool = std::unique_ptr<ggml_threadpool, PoolFree>;

/// `threads` threads kept for the life of a model, on `cores` (none: wherever the system puts
/// them), spinning between two steps or asleep (LocalRunner::Options::spinWait); nullptr when the
/// processor plugin gives none. The thread that makes it is kept on those cores too: it computes
/// beside them.
ThreadPool makePool(int threads, const QList<int> &cores, bool spin)
{
    ggml_backend_dev_t cpu = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU);
    ggml_backend_reg_t reg = cpu ? ggml_backend_dev_backend_reg(cpu) : nullptr;
    void *create = reg ? ggml_backend_reg_get_proc_address(reg, "ggml_threadpool_new") : nullptr;
    void *release = reg ? ggml_backend_reg_get_proc_address(reg, "ggml_threadpool_free") : nullptr;
    if (!create || !release)
        return {};
    ggml_threadpool_params params = ggml_threadpool_params_default(threads);
    // Between two steps llama.cpp's threads spin for a while (its "poll" level, 50 by default:
    // some 6.5 million rounds) before they sleep; 0: they sleep at once.
    if (!spin)
        params.poll = 0;
    for (const int core : cores) {
        if (core >= 0 && core < GGML_MAX_N_THREADS)
            params.cpumask[core] = true;
    }
    ggml_threadpool *pool = std::bit_cast<decltype(&ggml_threadpool_new)>(create)(&params);
    return ThreadPool(pool, PoolFree{std::bit_cast<decltype(&ggml_threadpool_free)>(release)});
}

/// `value` in `bytes` bytes, the lowest first.
void appendNumber(QByteArray *out, quint64 value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out->append(char((value >> (8 * i)) & 0xFF));
}

quint64 readNumber(const uchar *at, int bytes)
{
    quint64 value = 0;
    for (int i = bytes - 1; i >= 0; --i)
        value = (value << 8) | at[i];
    return value;
}

class LlamaRunner final : public LocalRunner {
public:
    ~LlamaRunner() override = default;
    int contextTokens() const override { return m_contextTokens; }
    bool sees() const override { return bool(m_mtmd); }
    QString device() const override
    {
        QString threads = u"%1, %2 threads"_s.arg(m_backend).arg(m_threads);
        if (!m_cores.isEmpty())
            threads += u" on cores "_s + coreList(m_cores);
        return m_spin ? threads + u", spinning"_s : threads;
    }
    Result generate(const local::Prompt &prompt, const Params &params) override;

    bool open(const Options &options, QString *error);

private:
    /// A stretch of the prompt as the model reads it: text tokens, or a picture's embeddings.
    struct Segment {
        std::vector<llama_token> tokens;
        std::shared_ptr<mtmd_input_chunk> picture; ///< set: a picture of `cells` cells
        int cells = 0;                             ///< tokens.size(), or the picture's
        int32_t pictureCell = 0;                   ///< what its cells hold (negative: per picture)
    };

    /// The model's state after a lane's first `cells` cells, for stepping back there: the part
    /// its layers cannot rebuild (the convolutions' state, the sliding windows of attention).
    struct Checkpoint {
        int cells = 0;
        std::vector<uint8_t> state;
    };

    /// A conversation in the model's memory (a llama.cpp sequence): what it has read, cell by
    /// cell — a token, or for a picture's cells a negative number of that picture. Two lanes: a
    /// request about something else (a look at a picture in the middle of a tool loop) takes the
    /// other lane and leaves the conversation's state where it was.
    struct Lane {
        llama_seq_id seq = 0;
        std::vector<int32_t> cells;
        std::vector<Checkpoint> checkpoints;
        quint64 stamp = 0; ///< when it was last used (m_clock)
    };

    bool tokenize(const local::Prompt &prompt, std::vector<Segment> *segments, int *stableCells, QString *error);
    bool tokenizeText(const QString &text, std::vector<llama_token> *out) const;
    llama_token controlToken(const QString &text, QString *error);
    bool tokenizePicture(const QByteArray &file, std::vector<llama_token> *run, std::vector<Segment> *segments,
                         QString *error);
    Lane &chooseLane(const std::vector<int32_t> &cells);
    void clearLane(Lane &lane);
    void rewind(Lane &lane, size_t keep);
    void checkpoint(Lane &lane);
    /// How reading into a lane ended.
    enum class Read {
        Ok,
        NoRoom, ///< llama.cpp found no free stretch of cells: nothing of the step was read
        Failed, ///< it failed or was told to stop: the lane is cleared (its state is unknown)
    };
    Read readTokens(Lane &lane, const llama_token *tokens, int count, bool wantLogits);
    Read readPicture(Lane &lane, const Segment &picture, bool wantLogits);
    Read readPrompt(Lane &lane, const std::vector<Segment> &segments, int stableCells);
    QByteArray stateKey(const std::vector<int32_t> &cells, size_t count) const;
    QString statePath(const QByteArray &key) const;
    int restoreStart(Lane &lane, const std::vector<int32_t> &cells, int stableCells);
    void storeStart(const Lane &lane);
    void pruneStates() const;
    bool isControl(llama_token token) const;
    static bool aborted(void *self);

    std::unique_ptr<llama_model, ModelFree> m_model;
    ThreadPool m_pool;                                 ///< the context's threads: freed after it
    std::unique_ptr<llama_context, ContextFree> m_ctx; ///< after the model: freed before it
    std::unique_ptr<mtmd_context, MtmdFree> m_mtmd;
    const llama_vocab *m_vocab = nullptr;
    llama_memory_t m_memory = nullptr;
    int m_contextTokens = 0;
    int m_threads = 0;
    QList<int> m_cores; ///< what the threads are kept on (empty: wherever the system puts them)
    bool m_spin = false; ///< they spin between steps (Options::spinWait)
    QString m_backend;
    QString m_stateDir;
    QByteArray m_stateIdentity;    ///< what a state kept on disk holds for: llama.cpp, its plugin, the model's file
    QSet<QByteArray> m_storedKeys; ///< the states known to be on disk: kept or restored by this runner
    std::array<Lane, LaneCount> m_lanes;
    quint64 m_clock = 0;
    /// The texts of the model's control tokens: never read as one inside a text (tokenizeText).
    QList<QByteArray> m_controlTexts;
    QHash<QString, llama_token> m_controlTokens;
    QHash<QByteArray, int32_t> m_pictureCells; ///< a picture (the hash of its file) → its cells' value
    // The request being answered (the abort callback of llama.cpp reads these).
    const std::atomic<bool> *m_cancel = nullptr;
    QElapsedTimer m_timer;
    int m_timeoutMs = 0;
};

bool LlamaRunner::open(const Options &options, QString *error)
{
    const auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    const Backend &b = backend(options.backendDir);
    if (!b.ok)
        return fail(b.error);
    m_backend = b.name;

    // The processor only (an empty list of devices: llama.cpp sets up nothing else).
    ggml_backend_dev_t devices[1] = {nullptr};
    llama_model_params mp = llama_model_default_params();
    mp.devices = devices;
    mp.n_gpu_layers = 0;
    m_model.reset(llama_model_load_from_file(options.modelPath.toUtf8().constData(), mp));
    if (!m_model)
        return fail(u"cannot load the model %1"_s.arg(options.modelPath));
    m_vocab = llama_model_get_vocab(m_model.get());

    // One thread per fast physical core (ai/cpus.h: llama.cpp's threads wait for the slowest at
    // every operation, so a phone's slow cores only hold the rest back, and an SMT sibling only
    // halves its core's time); where the cores do not tell their speed (an older kernel, another
    // system), half the processor's threads — its SMT siblings and efficiency cores left over.
    const QList<int> cores = options.threads > 0 ? QList<int>() : computeCores(cpuCores());
    if (options.threads > 0)
        m_threads = options.threads;
    else if (!cores.isEmpty())
        m_threads = int(cores.size());
    else
        m_threads = std::clamp(QThread::idealThreadCount() / 2, 1, MaxComputeThreads);
    m_contextTokens = std::max(512, options.contextTokens);

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = uint32_t(m_contextTokens);
    cp.n_batch = BatchTokens;
    cp.n_ubatch = BatchTokens;
    cp.n_seq_max = LaneCount;
    cp.n_threads = m_threads;
    cp.n_threads_batch = m_threads;
    cp.no_perf = true;
    cp.op_offload = false;
    cp.kv_unified = true; // the lanes share the context's cells
    // The sliding-window layers keep their window only (memory): stepping back goes through a
    // Checkpoint, which has the window as it was.
    cp.swa_full = false;
    cp.abort_callback = &LlamaRunner::aborted;
    cp.abort_callback_data = this;
    m_ctx.reset(llama_init_from_model(m_model.get(), cp));
    if (!m_ctx)
        return fail(u"cannot set up the model %1 (not enough memory for %2 tokens?)"_s.arg(options.modelPath)
                            .arg(m_contextTokens));
    m_memory = llama_get_memory(m_ctx.get());
    for (int i = 0; i < LaneCount; ++i)
        m_lanes[size_t(i)].seq = i;
    // Its threads stay for the life of the model, on those cores. Without a pool of its own
    // llama.cpp starts and ends them for every step — for every token of an answer —, and a
    // phone's system first puts a thread that just started on a slow core, where all the others
    // then wait for it. The thread that asks the model (the app's one worker) is kept on those
    // cores as well, and so are the threads it starts to look at a picture. Between two steps
    // they sleep — or spin a while first, where the fast cores are the model's own
    // (Options::spinWait): on a processor other programs keep busy a thread spinning on a core
    // keeps the one it waits for off it, and a step takes many times longer.
    m_pool = makePool(m_threads, cores, options.spinWait);
    if (m_pool) {
        llama_attach_threadpool(m_ctx.get(), m_pool.get(), nullptr);
        m_cores = cores;
        m_spin = options.spinWait;
    }

    // What a state kept on disk holds for (stateKey).
    m_stateDir = options.stateDir;
    const QFileInfo modelFile(options.modelPath);
    m_stateIdentity = "brulion model state " + QByteArray::number(StateVersion) + '\n' + QByteArray(BRULION_LLAMA_TAG)
                      + '\n' + m_backend.toUtf8() + '\n' + modelFile.absoluteFilePath().toUtf8() + '\n'
                      + QByteArray::number(modelFile.size()) + '\n'
                      + QByteArray::number(modelFile.lastModified(QTimeZone::UTC).toMSecsSinceEpoch()) + '\n';

    if (!options.projectorPath.isEmpty()) {
        mtmd_context_params pp = mtmd_context_params_default();
        pp.use_gpu = false;
        pp.print_timings = false;
        pp.n_threads = m_threads;
        pp.warmup = false;
        pp.image_max_tokens = std::max(64, options.imageTokens);
        m_mtmd.reset(mtmd_init_from_file(options.projectorPath.toUtf8().constData(), m_model.get(), pp));
        if (!m_mtmd)
            return fail(u"cannot load the model's picture part %1"_s.arg(options.projectorPath));
        if (!mtmd_support_vision(m_mtmd.get()) || mtmd_decode_use_mrope(m_mtmd.get()))
            return fail(u"the model's picture part %1 is of a kind not supported here"_s.arg(options.projectorPath));
    }

    // The texts of the control tokens (turn ends, tool calls …), longest first.
    const int32_t n = llama_vocab_n_tokens(m_vocab);
    for (llama_token token = 0; token < n; ++token) {
        if (!isControl(token))
            continue;
        const QByteArray text(llama_vocab_get_text(m_vocab, token));
        if (text.size() >= 2)
            m_controlTexts.append(text);
    }
    std::sort(m_controlTexts.begin(), m_controlTexts.end(),
              [](const QByteArray &left, const QByteArray &right) { return left.size() > right.size(); });
    return true;
}

bool LlamaRunner::isControl(llama_token token) const
{
    return (llama_vocab_get_attr(m_vocab, token) & (LLAMA_TOKEN_ATTR_CONTROL | LLAMA_TOKEN_ATTR_USER_DEFINED)) != 0;
}

bool LlamaRunner::aborted(void *self)
{
    const auto *r = static_cast<const LlamaRunner *>(self);
    if (r->m_cancel && r->m_cancel->load(std::memory_order_relaxed))
        return true;
    return r->m_timeoutMs > 0 && r->m_timer.elapsed() > r->m_timeoutMs;
}

/// Plain text as tokens: nothing in it is a control token. llama.cpp's tokenizer still reads some
/// of them inside a text (the tool tags, which the model's vocabulary holds as ordinary added
/// tokens), so the text is tokenized in parts, split inside every place that spells one.
bool LlamaRunner::tokenizeText(const QString &text, std::vector<llama_token> *out) const
{
    const QByteArray utf8 = text.toUtf8();
    std::vector<qsizetype> cuts;
    for (const QByteArray &control : m_controlTexts) {
        // After its first letter (a whole UTF-8 letter).
        qsizetype first = 1;
        while (first < control.size() && (uchar(control.at(first)) & 0xC0) == 0x80)
            ++first;
        for (qsizetype at = utf8.indexOf(control); at >= 0; at = utf8.indexOf(control, at + 1))
            cuts.push_back(at + first);
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    cuts.push_back(utf8.size());

    qsizetype from = 0;
    for (const qsizetype to : cuts) {
        if (to <= from)
            continue;
        const int32_t length = int32_t(to - from);
        const size_t had = out->size();
        out->resize(had + size_t(length) + 8);
        int32_t n =
                llama_tokenize(m_vocab, utf8.constData() + from, length, out->data() + had, int32_t(out->size() - had),
                               /*add_special*/ false, /*parse_special*/ false);
        if (n < 0 && n != INT32_MIN) {
            out->resize(had + size_t(-n));
            n = llama_tokenize(m_vocab, utf8.constData() + from, length, out->data() + had, -n, false, false);
        }
        if (n < 0) {
            out->resize(had);
            return false;
        }
        out->resize(had + size_t(n));
        from = to;
    }
    return true;
}

/// The token of a control text of the model's format: exactly one, and one the vocabulary marks
/// as such (else this is not a model of that format).
llama_token LlamaRunner::controlToken(const QString &text, QString *error)
{
    if (const auto it = m_controlTokens.constFind(text); it != m_controlTokens.constEnd())
        return it.value();
    const QByteArray utf8 = text.toUtf8();
    llama_token tokens[4];
    const int32_t n =
            llama_tokenize(m_vocab, utf8.constData(), int32_t(utf8.size()), tokens, 4, false, /*parse_special*/ true);
    if (n != 1 || !isControl(tokens[0])) {
        if (error)
            *error = u"the model has no token \"%1\": its file is not of the model chosen"_s.arg(text);
        return LLAMA_TOKEN_NULL;
    }
    m_controlTokens.insert(text, tokens[0]);
    return tokens[0];
}

/// A picture (the JPEG or PNG the app wrote) as the model reads it: its embeddings between the
/// model's own picture tokens. The file is decoded by Qt; llama.cpp gets pixels.
bool LlamaRunner::tokenizePicture(const QByteArray &file, std::vector<llama_token> *run, std::vector<Segment> *segments,
                                  QString *error)
{
    const auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (!m_mtmd)
        return fail(u"this model does not see pictures"_s);
    const QImage image = QImage::fromData(file).convertToFormat(QImage::Format_RGB888);
    if (image.isNull() || image.width() < 1 || image.height() < 1)
        return fail(u"a picture cannot be read"_s);
    std::vector<uchar> pixels(size_t(image.width()) * size_t(image.height()) * 3);
    for (int y = 0; y < image.height(); ++y)
        std::memcpy(pixels.data() + size_t(y) * size_t(image.width()) * 3, image.constScanLine(y),
                    size_t(image.width()) * 3);

    const QByteArray hash = QCryptographicHash::hash(file, QCryptographicHash::Sha1);
    struct BitmapFree {
        void operator()(mtmd_bitmap *b) const { mtmd_bitmap_free(b); }
    };
    struct ChunksFree {
        void operator()(mtmd_input_chunks *c) const { mtmd_input_chunks_free(c); }
    };
    const std::unique_ptr<mtmd_bitmap, BitmapFree> bitmap(
            mtmd_bitmap_init(uint32_t(image.width()), uint32_t(image.height()), pixels.data()));
    const std::unique_ptr<mtmd_input_chunks, ChunksFree> chunks(mtmd_input_chunks_init());
    if (!bitmap || !chunks)
        return fail(u"a picture cannot be read"_s);
    mtmd_bitmap_set_id(bitmap.get(), hash.toHex().constData());
    const char *marker = mtmd_default_marker();
    const mtmd_input_text marked{marker, std::strlen(marker), /*add_special*/ false, /*parse_special*/ true};
    const mtmd_bitmap *bitmaps[1] = {bitmap.get()};
    if (mtmd_tokenize(m_mtmd.get(), chunks.get(), &marked, bitmaps, 1) != 0)
        return fail(u"a picture cannot be prepared for the model"_s);

    for (size_t i = 0; i < mtmd_input_chunks_size(chunks.get()); ++i) {
        const mtmd_input_chunk *chunk = mtmd_input_chunks_get(chunks.get(), i);
        if (mtmd_input_chunk_get_type(chunk) == MTMD_INPUT_CHUNK_TYPE_TEXT) {
            // The model's own tokens around a picture.
            size_t count = 0;
            const llama_token *tokens = mtmd_input_chunk_get_tokens_text(chunk, &count);
            run->insert(run->end(), tokens, tokens + count);
            continue;
        }
        const int cells = int(mtmd_input_chunk_get_n_tokens(chunk));
        if (cells < 1 || mtmd_input_chunk_get_n_pos(chunk) != cells)
            return fail(u"the model's pictures are of a kind not supported here"_s);
        if (!run->empty()) {
            Segment words;
            words.cells = int(run->size());
            words.tokens = std::move(*run);
            run->clear();
            segments->push_back(std::move(words));
        }
        int32_t &cell = m_pictureCells[hash];
        if (cell == 0)
            cell = -int32_t(m_pictureCells.size());
        Segment picture;
        picture.picture = std::shared_ptr<mtmd_input_chunk>(mtmd_input_chunk_copy(chunk), ChunkFree());
        picture.cells = cells;
        picture.pictureCell = cell;
        segments->push_back(std::move(picture));
    }
    return true;
}

bool LlamaRunner::tokenize(const local::Prompt &prompt, std::vector<Segment> *segments, int *stableCells,
                           QString *error)
{
    std::vector<llama_token> run; // the text tokens since the last picture
    int before = 0;               // the cells of the segments done
    QString words;                // plain text not tokenized yet
    const auto flushWords = [&] {
        if (words.isEmpty())
            return true;
        const bool ok = tokenizeText(words, &run);
        words.clear();
        if (!ok && error)
            *error = u"the request cannot be read as tokens"_s;
        return ok;
    };
    *stableCells = 0;
    for (qsizetype i = 0; i < prompt.pieces.size(); ++i) {
        const local::Piece &piece = prompt.pieces.at(i);
        if (i == prompt.stable) {
            if (!flushWords())
                return false;
            *stableCells = before + int(run.size());
        }
        if (!piece.image.isEmpty()) {
            if (!flushWords())
                return false;
            const size_t had = segments->size();
            if (!tokenizePicture(piece.image, &run, segments, error))
                return false;
            for (size_t s = had; s < segments->size(); ++s)
                before += segments->at(s).cells;
        } else if (piece.special) {
            if (!flushWords())
                return false;
            const llama_token token = controlToken(piece.text, error);
            if (token == LLAMA_TOKEN_NULL)
                return false;
            run.push_back(token);
        } else {
            words += piece.text;
        }
    }
    if (!flushWords())
        return false;
    if (!run.empty()) {
        Segment last;
        last.cells = int(run.size());
        last.tokens = std::move(run);
        segments->push_back(std::move(last));
    }
    return true;
}

void LlamaRunner::clearLane(Lane &lane)
{
    llama_memory_seq_rm(m_memory, lane.seq, -1, -1);
    lane.cells.clear();
    lane.checkpoints.clear();
}

/// The lane for a prompt: the one that already holds its start (the longest), else an empty
/// one, else the one used longest ago.
LlamaRunner::Lane &LlamaRunner::chooseLane(const std::vector<int32_t> &cells)
{
    Lane *best = nullptr;
    size_t bestShared = 0;
    for (Lane &lane : m_lanes) {
        size_t shared = 0;
        const size_t n = std::min(lane.cells.size(), cells.size());
        while (shared < n && lane.cells[shared] == cells[shared])
            ++shared;
        if (shared >= SharedStart && shared > bestShared) {
            best = &lane;
            bestShared = shared;
        }
    }
    if (!best) {
        for (Lane &lane : m_lanes) {
            if (!best || (lane.cells.empty() && !best->cells.empty())
                || (lane.cells.empty() == best->cells.empty() && lane.stamp < best->stamp))
                best = &lane;
        }
    }
    best->stamp = ++m_clock;
    return *best;
}

/// Makes the lane hold at most its first `keep` cells. The model's state steps back by itself
/// only where its layers can (plain attention); else from the last Checkpoint before `keep`, or
/// from nothing.
void LlamaRunner::rewind(Lane &lane, size_t keep)
{
    if (keep >= lane.cells.size())
        return;
    const auto dropCheckpointsAfter = [&](size_t cells) {
        std::erase_if(lane.checkpoints, [&](const Checkpoint &c) { return size_t(c.cells) > cells; });
    };
    if (keep == 0) {
        clearLane(lane);
        return;
    }
    if (llama_memory_seq_rm(m_memory, lane.seq, llama_pos(keep), -1)) {
        lane.cells.resize(keep);
        dropCheckpointsAfter(keep);
        return;
    }
    for (auto it = lane.checkpoints.rbegin(); it != lane.checkpoints.rend(); ++it) {
        if (size_t(it->cells) > keep)
            continue;
        const size_t cells = size_t(it->cells);
        const size_t read = llama_state_seq_set_data_ext(m_ctx.get(), it->state.data(), it->state.size(), lane.seq,
                                                         LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY);
        if (read != it->state.size() || !llama_memory_seq_rm(m_memory, lane.seq, llama_pos(cells), -1))
            break;
        lane.cells.resize(cells);
        dropCheckpointsAfter(cells);
        return;
    }
    clearLane(lane);
}

/// Keeps the model's state as it is now, after all of the lane's cells.
void LlamaRunner::checkpoint(Lane &lane)
{
    const int cells = int(lane.cells.size());
    if (cells == 0 || std::ranges::any_of(lane.checkpoints, [&](const Checkpoint &c) { return c.cells == cells; }))
        return;
    const size_t size = llama_state_seq_get_size_ext(m_ctx.get(), lane.seq, LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY);
    if (size == 0)
        return; // a model whose state always steps back by itself
    Checkpoint c;
    c.cells = cells;
    c.state.resize(size);
    const size_t written = llama_state_seq_get_data_ext(m_ctx.get(), c.state.data(), size, lane.seq,
                                                        LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY);
    if (written == 0)
        return;
    c.state.resize(written);
    lane.checkpoints.push_back(std::move(c));
    std::sort(lane.checkpoints.begin(), lane.checkpoints.end(),
              [](const Checkpoint &a, const Checkpoint &b) { return a.cells < b.cells; });
    while (lane.checkpoints.size() > MaxCheckpoints)
        lane.checkpoints.erase(lane.checkpoints.begin() + 1); // the first (the system text's) stays
}

/// Reads `count` tokens into the lane, after what it holds.
LlamaRunner::Read LlamaRunner::readTokens(Lane &lane, const llama_token *tokens, int count, bool wantLogits)
{
    std::vector<llama_pos> positions(BatchTokens);
    std::vector<int32_t> counts(BatchTokens, 1);
    std::vector<llama_seq_id> ids(BatchTokens, lane.seq);
    std::vector<llama_seq_id *> idPointers(BatchTokens);
    std::vector<int8_t> outputs(BatchTokens);
    std::vector<llama_token> batchTokens(BatchTokens);
    for (int done = 0; done < count;) {
        const int n = std::min(BatchTokens, count - done);
        for (int i = 0; i < n; ++i) {
            batchTokens[size_t(i)] = tokens[done + i];
            positions[size_t(i)] = llama_pos(lane.cells.size()) + i;
            idPointers[size_t(i)] = &ids[size_t(i)];
            outputs[size_t(i)] = wantLogits && done + i == count - 1;
        }
        llama_batch batch{};
        batch.n_tokens = n;
        batch.token = batchTokens.data();
        batch.pos = positions.data();
        batch.n_seq_id = counts.data();
        batch.seq_id = idPointers.data();
        batch.logits = outputs.data();
        const int32_t rc = llama_decode(m_ctx.get(), batch);
        if (rc == 1)
            return Read::NoRoom; // (the context is as before this step)
        if (rc != 0) {
            clearLane(lane);
            return Read::Failed;
        }
        lane.cells.insert(lane.cells.end(), tokens + done, tokens + done + n);
        done += n;
    }
    return Read::Ok;
}

/// Looks at a picture and reads what the model sees of it into the lane.
LlamaRunner::Read LlamaRunner::readPicture(Lane &lane, const Segment &picture, bool wantLogits)
{
    Q_UNUSED(wantLogits) // (a prompt never ends with a picture: the answer's start follows it)
    if (mtmd_encode_chunk(m_mtmd.get(), picture.picture.get()) != 0) {
        clearLane(lane);
        return Read::Failed;
    }
    const llama_pos start = llama_pos(lane.cells.size());
    llama_pos next = start;
    const int32_t rc = mtmd_helper_decode_image_chunk(m_mtmd.get(), m_ctx.get(), picture.picture.get(),
                                                      mtmd_get_output_embd(m_mtmd.get()), start, lane.seq, BatchTokens,
                                                      &next, nullptr, nullptr);
    if (rc == 0 && next == start + picture.cells) {
        lane.cells.insert(lane.cells.end(), size_t(picture.cells), picture.pictureCell);
        return Read::Ok;
    }
    // Part of the picture may be in the model's memory: out again, or the lane with it.
    const bool noRoom = rc == 1;
    if (!noRoom || (next != start && !llama_memory_seq_rm(m_memory, lane.seq, start, -1))) {
        clearLane(lane);
        return noRoom ? Read::NoRoom : Read::Failed;
    }
    return Read::NoRoom;
}

/// Reads what the lane lacks of the prompt, keeping the model's state after the stable part,
/// after every picture and after the whole prompt (the last token's logits are the answer's start).
LlamaRunner::Read LlamaRunner::readPrompt(Lane &lane, const std::vector<Segment> &segments, int stableCells)
{
    size_t at = 0;
    for (size_t i = 0; i < segments.size(); ++i) {
        const Segment &s = segments[i];
        const size_t end = at + size_t(s.cells);
        const bool last = i + 1 == segments.size();
        if (end <= lane.cells.size()) {
            at = end;
            continue;
        }
        if (aborted(this))
            return Read::Failed;
        if (s.picture) {
            if (lane.cells.size() != at) { // (never part of a picture)
                clearLane(lane);
                return Read::Failed;
            }
            if (const Read read = readPicture(lane, s, last); read != Read::Ok)
                return read;
            // A picture is slow to look at: the state after it is kept, so another question
            // about the same picture starts here.
            checkpoint(lane);
        } else {
            size_t from = lane.cells.size() - at; // within this segment
            // Up to the end of the stable part first, when it ends inside this segment.
            const size_t stable = size_t(stableCells);
            if (stable > at + from && stable < end) {
                if (const Read read = readTokens(lane, s.tokens.data() + from, int(stable - at - from), false);
                    read != Read::Ok)
                    return read;
                checkpoint(lane);
                storeStart(lane);
                from = lane.cells.size() - at;
            }
            if (const Read read = readTokens(lane, s.tokens.data() + from, int(size_t(s.cells) - from), last);
                read != Read::Ok)
                return read;
        }
        if (int(lane.cells.size()) == stableCells) {
            checkpoint(lane);
            storeStart(lane);
        }
        at = end;
    }
    checkpoint(lane);
    return Read::Ok;
}

/// What a state kept on disk is found by: the model (its file, llama.cpp and its processor plugin
/// that computed it) and the cells it holds.
QByteArray LlamaRunner::stateKey(const std::vector<int32_t> &cells, size_t count) const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(m_stateIdentity);
    hash.addData(QByteArrayView(reinterpret_cast<const char *>(cells.data()), qsizetype(count * sizeof(int32_t))));
    return hash.result();
}

QString LlamaRunner::statePath(const QByteArray &key) const
{
    return QDir(m_stateDir).filePath(QString::fromLatin1(key.toHex().left(32)) + u".state"_s);
}

/// The first request after the model was opened: the prompt's stable part (the system text and
/// the tools) from the state kept on disk, where this lane lacks it and such a state was kept.
/// Returns how many cells that added (0: none — the lane reads them then).
int LlamaRunner::restoreStart(Lane &lane, const std::vector<int32_t> &cells, int stableCells)
{
    const size_t had = lane.cells.size();
    // (Never all of the prompt — its last token is read for the answer's first —, never a picture.)
    if (m_stateDir.isEmpty() || stableCells < MinStoredCells || size_t(stableCells) >= cells.size()
        || had >= size_t(stableCells)
        || std::any_of(cells.begin(), cells.begin() + stableCells, [](int32_t cell) { return cell < 0; }))
        return 0;
    const QByteArray key = stateKey(cells, size_t(stableCells));
    QFile file(statePath(key));
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const qint64 size = file.size();
    const uchar *map = size > StateHeaderBytes ? file.map(0, size) : nullptr;
    const auto number = [&](qint64 at, int bytes) { return readNumber(map + at, bytes); };
    constexpr qint64 Version = qint64(sizeof StateMagic);
    constexpr qint64 Key = Version + 4;
    constexpr qint64 Cells = Key + StateKeyBytes;
    constexpr qint64 Bytes = Cells + 4;
    if (!map || std::memcmp(map, StateMagic, sizeof StateMagic) != 0 || number(Version, 4) != StateVersion
        || std::memcmp(map + Key, key.constData(), size_t(StateKeyBytes)) != 0
        || number(Cells, 4) != quint64(stableCells) || number(Bytes, 8) != quint64(size - StateHeaderBytes)) {
        // Not one this runner wrote, or written only in part: it is read anew and kept again.
        file.close();
        QFile::remove(file.fileName());
        return 0;
    }
    clearLane(lane); // (what it had is the start of what comes)
    const size_t bytes = size_t(size - StateHeaderBytes);
    if (llama_state_seq_set_data_ext(m_ctx.get(), map + StateHeaderBytes, bytes, lane.seq, 0) != bytes) {
        // (No room for it in one stretch, or not of this context: llama.cpp left the lane empty.)
        clearLane(lane);
        return 0;
    }
    lane.cells.assign(cells.begin(), cells.begin() + stableCells);
    checkpoint(lane);
    m_storedKeys.insert(key);
    file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime); // (the newest go last)
    return stableCells - int(had);
}

/// Keeps the model's state after the lane's cells — a prompt's stable part, just read — on disk,
/// for the first request after the model was opened again (restoreStart).
void LlamaRunner::storeStart(const Lane &lane)
{
    const size_t count = lane.cells.size();
    if (m_stateDir.isEmpty() || count < size_t(MinStoredCells)
        || std::ranges::any_of(lane.cells, [](int32_t cell) { return cell < 0; }))
        return;
    const QByteArray key = stateKey(lane.cells, count);
    if (m_storedKeys.contains(key))
        return;
    m_storedKeys.insert(key); // (tried once: a disk that refuses it is not asked again)
    const size_t size = llama_state_seq_get_size_ext(m_ctx.get(), lane.seq, 0);
    if (size == 0 || qint64(size) > MaxStoredBytes)
        return;
    std::vector<uint8_t> state(size);
    const size_t written = llama_state_seq_get_data_ext(m_ctx.get(), state.data(), size, lane.seq, 0);
    if (written == 0 || written > size || !QDir().mkpath(m_stateDir))
        return;
    QByteArray header(StateMagic, sizeof StateMagic);
    appendNumber(&header, StateVersion, 4);
    header += key;
    appendNumber(&header, quint64(count), 4);
    appendNumber(&header, quint64(written), 8);
    QSaveFile file(statePath(key));
    if (!file.open(QIODevice::WriteOnly) || file.write(header) != header.size()
        || file.write(reinterpret_cast<const char *>(state.data()), qint64(written)) != qint64(written)
        || !file.commit()) {
        qCWarning(lcLlama).noquote() << u"cannot keep the model's state in %1: %2"_s.arg(file.fileName(),
                                                                                         file.errorString());
        return;
    }
    pruneStates();
}

/// At most MaxStoredStates states on disk, of MaxStoredBytes together: those used longest ago go.
void LlamaRunner::pruneStates() const
{
    const QFileInfoList files = QDir(m_stateDir).entryInfoList({u"*.state"_s}, QDir::Files, QDir::Time);
    int kept = 0;
    qint64 bytes = 0;
    for (const QFileInfo &file : files) { // the newest first
        if (kept < MaxStoredStates && bytes + file.size() <= MaxStoredBytes) {
            ++kept;
            bytes += file.size();
        } else {
            QFile::remove(file.absoluteFilePath());
        }
    }
}

LocalRunner::Result LlamaRunner::generate(const local::Prompt &prompt, const Params &params)
{
    Result r;
    m_timer.start();
    m_cancel = params.cancel;
    m_timeoutMs = params.timeoutMs;
    // (What llama.cpp's abort callback reads: only while this request is answered.)
    struct Forget {
        LlamaRunner *runner;
        ~Forget()
        {
            runner->m_cancel = nullptr;
            runner->m_timeoutMs = 0;
        }
    } forget{this};
    const auto stopped = [&] {
        if (params.cancel && params.cancel->load(std::memory_order_relaxed)) {
            r.status = Cancelled;
            r.error = u"stopped"_s;
            return true;
        }
        if (params.timeoutMs > 0 && m_timer.elapsed() > params.timeoutMs) {
            r.status = TimedOut;
            r.error = u"no answer in time"_s;
            return true;
        }
        return false;
    };

    // The prompt as cells.
    std::vector<Segment> segments;
    int stableCells = 0;
    if (!tokenize(prompt, &segments, &stableCells, &r.error))
        return r;
    std::vector<int32_t> cells;
    for (const Segment &s : segments) {
        if (s.picture)
            cells.insert(cells.end(), size_t(s.cells), s.pictureCell);
        else
            cells.insert(cells.end(), s.tokens.begin(), s.tokens.end());
    }
    r.promptTokens = int(cells.size());
    const int reserve = std::clamp(params.reserveTokens, 16, m_contextTokens / 2);
    r.promptLimit = m_contextTokens - reserve;
    if (cells.empty() || segments.back().picture) {
        // (A prompt ends where the answer starts: with the format's own tokens, never a picture.)
        r.error = u"not a request the model can answer"_s;
        return r;
    }
    if (r.promptTokens > r.promptLimit) {
        r.status = TooLong;
        r.error = u"the request is too long for the model on this device (%1 tokens; at most %2)"_s.arg(r.promptTokens)
                          .arg(r.promptLimit);
        return r;
    }

    // Its lane, with room: the other lane goes when both do not fit.
    Lane &lane = chooseLane(cells);
    for (Lane &other : m_lanes) {
        if (&other != &lane && int(other.cells.size()) + r.promptTokens + reserve > m_contextTokens)
            clearLane(other);
    }
    size_t others = 0;
    for (const Lane &other : m_lanes) {
        if (&other != &lane)
            others += other.cells.size();
    }

    // Keep what the lane and the prompt share, never all of the prompt: its last token is read
    // again for the model's first word.
    size_t shared = 0;
    {
        const size_t n = std::min(lane.cells.size(), cells.size());
        while (shared < n && lane.cells[shared] == cells[shared])
            ++shared;
    }
    shared = std::min(shared, cells.size() - 1);
    // (Never inside a picture: all of it, or none.)
    size_t at = 0;
    for (const Segment &s : segments) {
        if (s.picture && shared > at && shared < at + size_t(s.cells))
            shared = at;
        at += size_t(s.cells);
    }
    rewind(lane, shared);
    r.reusedTokens = int(lane.cells.size());
    // The first request after the model was opened: its stable part from the disk, when kept (a
    // restore that fails leaves the lane empty).
    r.storedTokens = restoreStart(lane, cells, stableCells);
    r.reusedTokens = std::min(r.reusedTokens, int(lane.cells.size()) - r.storedTokens);

    // Read the rest. llama.cpp wants a free stretch of cells for every step: when it finds none
    // (two lanes leave gaps), the other lane goes; when that is not enough, this lane starts
    // again from nothing, its cells in one stretch.
    for (int attempt = 0;; ++attempt) {
        const Read read = readPrompt(lane, segments, stableCells);
        if (read == Read::Ok)
            break;
        if (read == Read::NoRoom && attempt < 2 && !stopped()) {
            if (attempt == 0) {
                for (Lane &other : m_lanes) {
                    if (&other != &lane)
                        clearLane(other);
                }
                others = 0;
            } else {
                clearLane(lane);
            }
            r.reusedTokens = std::min(r.reusedTokens, int(lane.cells.size()));
            r.storedTokens = std::min(r.storedTokens, int(lane.cells.size()) - r.reusedTokens);
            continue;
        }
        if (!stopped()) {
            r.status = Failed;
            r.error = u"the model could not read the request"_s;
        }
        if (read == Read::NoRoom)
            clearLane(lane);
        return r;
    }
    r.promptMs = m_timer.elapsed();

    // Write the answer, token by token.
    llama_sampler_chain_params chain = llama_sampler_chain_default_params();
    chain.no_perf = true;
    const std::unique_ptr<llama_sampler, SamplerFree> sampler(llama_sampler_chain_init(chain));
    llama_sampler_chain_add(sampler.get(), llama_sampler_init_penalties(llama_vocab_n_tokens(m_vocab), 64,
                                                                        params.repeatPenalty, 0.0f, 0.0f));
    llama_sampler_chain_add(sampler.get(), llama_sampler_init_top_k(params.topK));
    llama_sampler_chain_add(sampler.get(), llama_sampler_init_temp(params.temperature));
    llama_sampler_chain_add(sampler.get(), llama_sampler_init_dist(params.seed));
    // (The penalty for repeating looks at the prompt's last tokens too.)
    for (size_t i = cells.size() > 64 ? cells.size() - 64 : 0; i < cells.size(); ++i) {
        if (cells[i] >= 0)
            llama_sampler_accept(sampler.get(), cells[i]);
    }

    const int room = m_contextTokens - int(others) - int(lane.cells.size());
    const int most = params.maxTokens > 0 ? std::min(params.maxTokens, room) : room;
    QByteArray words; // the bytes of the text piece being written (a letter may span tokens)
    const auto flushWords = [&] {
        if (!words.isEmpty())
            r.pieces.append(local::Piece{QString::fromUtf8(words), false, {}});
        words.clear();
    };
    char buffer[256];
    r.status = Ok;
    while (r.outputTokens < most) {
        if (stopped())
            break;
        const llama_token token = llama_sampler_sample(sampler.get(), m_ctx.get(), -1);
        if (llama_vocab_is_eog(m_vocab, token)) {
            r.complete = true;
            break;
        }
        const int32_t length =
                llama_token_to_piece(m_vocab, token, buffer, int32_t(sizeof buffer), 0, /*special*/ true);
        const QByteArray bytes = length > 0 ? QByteArray(buffer, length) : QByteArray();
        if (isControl(token)) {
            const QString control = QString::fromUtf8(bytes);
            if (prompt.stops.contains(control)) {
                r.complete = true; // it began another turn: its own is over
                break;
            }
            flushWords();
            r.pieces.append(local::Piece{control, true, {}});
        } else {
            words += bytes;
        }
        ++r.outputTokens;
        Read read = readTokens(lane, &token, 1, true);
        if (read == Read::NoRoom && others > 0) {
            // No free cell beside the other lane's: it goes, and the token is read again.
            for (Lane &other : m_lanes) {
                if (&other != &lane)
                    clearLane(other);
            }
            others = 0;
            read = readTokens(lane, &token, 1, true);
        }
        if (read == Read::NoRoom)
            break; // the context is full: the answer ends here, as when out of tokens
        if (read == Read::Failed) {
            if (!stopped()) {
                r.status = Failed;
                r.error = u"the model stopped while writing its answer"_s;
            }
            break;
        }
    }
    flushWords();
    r.outputMs = m_timer.elapsed() - r.promptMs;
    return r;
}

} // namespace

std::unique_ptr<LocalRunner> openLlamaRunner(const LocalRunner::Options &options, QString *error)
{
    auto runner = std::make_unique<LlamaRunner>();
    if (!runner->open(options, error))
        return nullptr;
    return runner;
}

} // namespace brulion::ai
