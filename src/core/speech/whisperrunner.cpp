#include "speech/whisperrunner.h"

#include "ai/cpus.h"
#include "ai/localrunner.h"
#include "speech/load.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QThread>

#include <ggml-backend.h>
#include <whisper.h>

#include <algorithm>
#include <mutex>

using namespace Qt::StringLiterals;

namespace brulion::speech {

Q_LOGGING_CATEGORY(lcWhisper, "brulion.speech.whisper", QtWarningMsg)

namespace {

/// ggml's processor plugins of this build, the best first ("libggml-cpu-haswell.so|…": CMake,
/// the same list as the language models' runner).
#ifndef BRULION_LLAMA_CPU_VARIANTS
#define BRULION_LLAMA_CPU_VARIANTS ""
#endif

/// A look at the processor shorter than this tells little (the system counts in hundredths of a
/// second); one older than this says nothing of now.
constexpr qint64 ShortestLookMs = 100;
constexpr qint64 OldestLookMs = 10000;

void logCallback(ggml_log_level level, const char *text, void *)
{
    const QByteArray line = QByteArray(text).trimmed();
    if (line.isEmpty())
        return;
    if (level == GGML_LOG_LEVEL_ERROR)
        qCWarning(lcWhisper, "%s", line.constData());
    else
        qCDebug(lcWhisper, "%s", line.constData());
}

/// The file name of a plugin without "lib" and its extension ("ggml-cpu-haswell").
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
    QString name;
    QString error;
};

/// ggml's processor plugin: the one the language models loaded already in this process (ggml's
/// registry is one for both), else the first of this build's that the processor can run.
const Backend &backend(const QString &dir)
{
    static Backend loaded;
    static std::once_flag once;
    std::call_once(once, [&] {
        whisper_log_set(logCallback, nullptr);
        if (ggml_backend_dev_t cpu = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU)) {
            loaded.name = QString::fromUtf8(ggml_backend_reg_name(ggml_backend_dev_backend_reg(cpu)));
            loaded.ok = true;
            return;
        }
        const QStringList files = QString::fromLatin1(BRULION_LLAMA_CPU_VARIANTS).split(u'|', Qt::SkipEmptyParts);
        for (const QString &file : files) {
            if (ggml_backend_load(ai::backendPluginPath(dir, file).constData())) {
                loaded.name = pluginBaseName(file);
                loaded.ok = true;
                return;
            }
        }
        loaded.error = files.isEmpty() ? u"this build has no processor plugin of ggml"_s
                                       : u"this device's processor is too old to recognize speech (it lacks the "
                                         u"instructions ggml is built for here)"_s;
    });
    return loaded;
}

struct ContextFree {
    void operator()(whisper_context *c) const { whisper_free(c); }
};

class WhisperRunner final : public SpeechRunner {
public:
    WhisperRunner(std::unique_ptr<whisper_context, ContextFree> ctx, QString backendName, int asked)
        : m_ctx(std::move(ctx)), m_backend(std::move(backendName)), m_asked(asked)
    {
        m_cores = ai::computeCores(ai::cpuCores());
        m_cpus = std::max(1, QThread::idealThreadCount());
        m_last = sampleCpu();
    }

    QString device() const override
    {
        return m_asked > 0 ? u"%1, %2 threads"_s.arg(m_backend).arg(m_asked)
                           : u"%1, up to %2 threads"_s.arg(m_backend).arg(maxThreads());
    }

    Result transcribe(const QList<float> &samples, const Params &params) override
    {
        Result result;
        result.audioMs = samples.size() * 1000 / 16000;
        if (samples.isEmpty()) {
            result.status = Ok;
            result.language = params.language;
            return result;
        }
        const int threads = threadsNow();
        QElapsedTimer clock;
        clock.start();
        result.threads = threads;

        struct Callbacks {
            const Params *params;
            WhisperRunner *runner;
            int lastPercent = -1;
        } callbacks{&params, this};

        whisper_full_params wp =
                whisper_full_default_params(params.live ? WHISPER_SAMPLING_GREEDY : WHISPER_SAMPLING_BEAM_SEARCH);
        wp.n_threads = threads;
        wp.translate = false;
        wp.no_context = true;
        wp.print_special = false;
        wp.print_progress = false;
        wp.print_realtime = false;
        wp.print_timestamps = false;
        wp.suppress_nst = true;
        if (params.live) {
            // A window of text while recording: once, greedily, no fallbacks at higher
            // temperatures. The encoder still hears Whisper's whole 30 s (audio_ctx 0): over the
            // window alone — whisper.cpp's stream example's --audio-ctx — the small models lose
            // the words and repeat a phrase for the rest of the window (docs/PERFORMANCE.md
            // "Speech recognition on a busy processor").
            wp.greedy.best_of = 1;
            wp.temperature_inc = 0.0f;
        } else {
            wp.beam_search.beam_size = 3;
        }
        const QByteArray language = params.language.toLatin1();
        wp.language = params.language.isEmpty() || params.language == u"auto" ? "auto" : language.constData();
        wp.detect_language = false;
        wp.progress_callback = [](whisper_context *, whisper_state *, int progress, void *user) {
            auto *cb = static_cast<Callbacks *>(user);
            if (cb->params->progress && progress != cb->lastPercent) {
                cb->lastPercent = progress;
                cb->params->progress(std::clamp(progress, 0, 100));
            }
        };
        wp.progress_callback_user_data = &callbacks;
        wp.new_segment_callback = [](whisper_context *ctx, whisper_state *, int newSegments, void *user) {
            auto *cb = static_cast<Callbacks *>(user);
            if (!cb->params->heard)
                return;
            const int n = whisper_full_n_segments(ctx);
            for (int i = std::max(0, n - newSegments); i < n; ++i)
                cb->params->heard(segmentAt(ctx, i));
        };
        wp.new_segment_callback_user_data = &callbacks;
        wp.abort_callback = [](void *user) {
            auto *cb = static_cast<Callbacks *>(user);
            return cb->params->cancel && cb->params->cancel->load();
        };
        wp.abort_callback_user_data = &callbacks;
        // (Also checked before each window of 30 s: the encoder's start.)
        wp.encoder_begin_callback = [](whisper_context *, whisper_state *, void *user) {
            auto *cb = static_cast<Callbacks *>(user);
            return !(cb->params->cancel && cb->params->cancel->load());
        };
        wp.encoder_begin_callback_user_data = &callbacks;

        const int rc = whisper_full(m_ctx.get(), wp, samples.constData(), int(samples.size()));
        result.elapsedMs = clock.elapsed();
        // The next recording's look at the processor counts from here (what others compute
        // meanwhile).
        m_last = sampleCpu();
        if (params.cancel && params.cancel->load()) {
            result.status = Cancelled;
            return result;
        }
        if (rc != 0) {
            result.error = u"the speech recognizer failed (%1)"_s.arg(rc);
            return result;
        }
        const int n = whisper_full_n_segments(m_ctx.get());
        for (int i = 0; i < n; ++i)
            result.segments.append(segmentAt(m_ctx.get(), i));
        const int lang = whisper_full_lang_id(m_ctx.get());
        result.language = lang >= 0 ? QString::fromLatin1(whisper_lang_str(lang)) : params.language;
        if (params.progress)
            params.progress(100);
        result.status = Ok;
        return result;
    }

private:
    int maxThreads() const { return speechThreads(int(m_cores.size()), m_cpus, -1); }

    /// The threads for this recording (speech/load.h): what the fast cores allow, less what other
    /// programs keep busy now — measured since the last recording ended, or over a tenth of a
    /// second now when that was too short or too long ago.
    int threadsNow()
    {
        int threads = m_asked > 0 ? std::min(m_asked, MaxSpeechThreads) : 0;
        if (threads <= 0) {
            CpuSample now = sampleCpu();
            const qint64 since = now.atMs - m_last.atMs;
            CpuSample before = m_last;
            if (!m_last.isValid() || since < ShortestLookMs || since > OldestLookMs) {
                before = now;
                QThread::msleep(ShortestLookMs);
                now = sampleCpu();
            }
            const double busy = othersBusy(before, now, m_cpus);
            threads = speechThreads(int(m_cores.size()), m_cpus, busy);
            qCDebug(lcWhisper, "%d threads (others keep %.1f of %d processors busy)", threads, busy, m_cpus);
        }
        return threads;
    }

    static Segment segmentAt(whisper_context *ctx, int i)
    {
        Segment s;
        // (Timestamps in hundredths of a second.)
        s.startMs = whisper_full_get_segment_t0(ctx, i) * 10;
        s.endMs = whisper_full_get_segment_t1(ctx, i) * 10;
        s.text = QString::fromUtf8(whisper_full_get_segment_text(ctx, i)).trimmed();
        return s;
    }

    std::unique_ptr<whisper_context, ContextFree> m_ctx;
    QString m_backend;
    int m_asked = 0;    ///< Options::threads (0: chosen for each recording)
    QList<int> m_cores; ///< the fast cores (ai::computeCores; empty: not told)
    int m_cpus = 1;     ///< the logical processors
    CpuSample m_last;   ///< the processor when the last recording ended
};

} // namespace

std::unique_ptr<SpeechRunner> openWhisperRunner(const SpeechRunner::Options &options, QString *error)
{
    const Backend &b = backend(options.backendDir);
    if (!b.ok || options.modelPath.isEmpty()) {
        if (error)
            *error = b.error; // (no model: only the check that ggml loads, speechrunner.h)
        return nullptr;
    }
    if (!QFileInfo(options.modelPath).isFile()) {
        if (error)
            *error = u"the speech model is not on this device"_s;
        return nullptr;
    }
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = false;
    cp.flash_attn = true;
    std::unique_ptr<whisper_context, ContextFree> ctx(
            // UTF-8 everywhere: whisper.cpp widens it itself on Windows (an ANSI code page's
            // bytes there are no UTF-8 — its converter throws on them).
            whisper_init_from_file_with_params(options.modelPath.toUtf8().constData(), cp));
    if (!ctx) {
        if (error)
            *error = u"the speech model cannot be read (%1)"_s.arg(QFileInfo(options.modelPath).fileName());
        return nullptr;
    }
    return std::make_unique<WhisperRunner>(std::move(ctx), b.name, std::max(0, options.threads));
}

} // namespace brulion::speech
