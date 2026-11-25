#pragma once
// What recognizes speech on the device (speech/speech.h): 16 kHz mono samples in, the segments
// heard out. The one implementation is whisper.cpp (speech/whisperrunner.h), which the app does
// not link: it is a plugin — brulion_speech, next to llama.cpp's ggml libraries it computes with —
// loaded with loadSpeechRunner() the first time something is transcribed, so an app that never
// transcribes loads none of it, and a build without it simply has no such plugin
// (cmake/BrulionSpeech.cmake).
//
//   QString error;
//   std::unique_ptr<speech::SpeechRunner> runner = speech::loadSpeechRunner(pluginDirs, options, &error);
//   speech::SpeechRunner::Result r = runner->transcribe(samples16k, params);
//   const QString text = media::transcriptText(speech::spokenTexts(r.segments));
//
// A runner is used from one thread at a time (the app: a worker thread; transcribe() blocks until
// the whole recording is heard, `cancel` ends it early).

#include "speech/speech.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

namespace brulion::speech {

class SpeechRunner {
public:
    struct Options {
        QString modelPath; ///< the model's file (whisper.cpp's ggml .bin)
        /// 0: chosen for each recording (speech/load.h: one per fast physical core, at most four,
        /// no more than the processor has free from other programs then); more: that many.
        int threads = 0;
        /// The folder of ggml's processor plugins ("": found by name alone — Android).
        QString backendDir;
    };

    struct Params {
        QString language = QStringLiteral("auto"); ///< speechLanguages(): "auto", "pl", "en"
        const std::atomic<bool> *cancel = nullptr; ///< set from another thread: stop now
        /// Called now and then with how much of the recording is heard (0–100), on the thread
        /// that transcribes.
        std::function<void(int percent)> progress;
        /// Called with each segment as it is heard, on the thread that transcribes.
        std::function<void(const Segment &)> heard;
        /// A window of a recording still going (text while recording, speech/live.h): heard fast
        /// rather than best — one greedy pass without fallbacks, no text of earlier windows.
        bool live = false;
    };

    enum Status {
        Ok,
        Cancelled, ///< `cancel` was set
        Failed,    ///< `error` says why
    };

    struct Result {
        Status status = Failed;
        QString error;
        QList<Segment> segments;
        QString language;     ///< what it heard ("pl"; the asked one unless "auto")
        qint64 audioMs = 0;   ///< the recording's length
        qint64 elapsedMs = 0; ///< the time it took
        int threads = 0;      ///< the threads it computed with
    };

    virtual ~SpeechRunner() = default;
    /// What computes ("ggml-cpu-haswell, up to 4 threads").
    virtual QString device() const = 0;
    /// Hears `samples` (16 kHz mono, media::SpeechSampleRate). Blocks until done.
    virtual Result transcribe(const QList<float> &samples, const Params &params) = 0;
};

/// The plugin's file name without "lib" and its extension, and the function it exports:
///   extern "C" brulion::speech::SpeechRunner *brulion_speech_open(const brulion::speech::SpeechRunner::Options *,
///                                                                 QString *error)
inline constexpr char SpeechRunnerPlugin[] = "brulion_speech";
inline constexpr char SpeechRunnerOpen[] = "brulion_speech_open";

/// Is the plugin there: in one of `pluginDirs`, or (with a "" among them) wherever the system
/// finds a library by its name — Android. Does not load it.
bool hasSpeechRunner(const QStringList &pluginDirs);

/// Loads the plugin from the first of `pluginDirs` that has it (a "": by its name) and opens a
/// model with it; nullptr with `error` when there is no plugin, it cannot be loaded, or the model
/// cannot be opened. `options.backendDir` is set to the plugin's folder when it is empty. The
/// plugin stays loaded until the app ends.
std::unique_ptr<SpeechRunner> loadSpeechRunner(const QStringList &pluginDirs, SpeechRunner::Options options,
                                               QString *error);

} // namespace brulion::speech
