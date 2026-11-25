#pragma once
// What runs a model on the device (the provider "local", ai/local.h): a prompt's pieces in, the
// answer's pieces out. The one implementation is llama.cpp (ai/llamarunner.h, library
// brulion_ai_llama), which the app does not link: it is a plugin — brulion_localai, next to
// llama.cpp's own libraries — loaded with loadLocalRunner() the first time a model is asked, so
// an app that never asks one loads none of it, and a build without it simply has no such plugin
// (cmake/BrulionLocalAi.cmake).
//
//   QString error;
//   std::unique_ptr<ai::LocalRunner> runner = ai::loadLocalRunner(pluginDirs, options, &error);
//   ai::LocalRunner::Result r = runner->generate(ai::local::prompt(format, request), params);
//   if (r.status == ai::LocalRunner::TooLong) …   // local::shorten() and again
//   ai::Reply reply = ai::local::reply(format, r.pieces, request, r.complete);
//
// A runner is used from one thread at a time (the app: a worker thread; generate() blocks until
// the answer is written, `cancel` and `timeoutMs` end it early).

#include "ai/cpus.h"
#include "ai/local.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

namespace brulion::ai {

class LocalRunner {
public:
    struct Options {
        /// The model's file (GGUF). "": no model is opened — only the plugin and llama.cpp's processor
        /// plugin are loaded (nullptr with `error` empty when both did): a check that this build's
        /// runner works on this machine (`brulion-cli version --plugins`).
        QString modelPath;
        QString adapterPath;   ///< a LoRA adapter over its weights (GGUF; "": the model as it is)
        QString projectorPath; ///< what lets it see pictures (GGUF; "": it reads text only)
        /// The tokens it holds at once — the prompt and the answer together; memory grows with it.
        int contextTokens = 16384;
        /// The most tokens a picture may take (it is scaled down to fit).
        int imageTokens = 1024;
        /// 0: one per fast physical core (ai/cpus.h: a phone's slow cores, a PC's SMT siblings
        /// and efficiency cores left out), the threads kept on those cores; where the cores do not
        /// tell their speed (an older kernel), half the processor's threads; at most
        /// MaxComputeThreads.
        int threads = 0;
        /// The folder of llama.cpp's processor plugins ("": found by name alone — Android, where
        /// libraries stay inside the APK).
        QString backendDir;
        /// Where the model's state after a prompt's stable part (the system text and the tools)
        /// is kept between runs, so that the first request after the model was opened again
        /// does not read them anew ("": nowhere). A cache: its files can go at any time.
        QString stateDir;
        /// Between two steps (two tokens of an answer) the threads spin a while instead of going
        /// to sleep at once: an answer is written some 7 % faster on an idle processor (no waking
        /// up, the cores' clocks stay up) — and many times slower on one other programs keep busy,
        /// where they spin while the thread they wait for waits for a core. For a system that
        /// keeps other work off the fast cores (Android: background work on the slow ones).
        bool spinWait = false;
    };

    struct Params {
        /// The most tokens of the answer (0: as many as the context has room for).
        int maxTokens = 0;
        /// The room a prompt must leave for the answer: one that leaves less is TooLong.
        int reserveTokens = 1024;
        float temperature = 0.1f;
        int topK = 50;
        float repeatPenalty = 1.1f;
        quint32 seed = 0xB2011017;
        /// The longest it may go without getting anywhere — a batch of the prompt read, a token
        /// written — (0: no limit): a slow device writes on, a stuck one ends.
        int timeoutMs = 0;
        const std::atomic<bool> *cancel = nullptr; ///< set from another thread: stop now
        /// Called after every token of the answer with what the model has written so far (its
        /// last letters only once they are whole), the tokens written and the time writing them
        /// took, on the thread that generates.
        std::function<void(const QList<local::Piece> &written, int tokens, qint64 writingMs)> written;
    };

    enum Status {
        Ok,
        TooLong,   ///< the prompt has `promptTokens`, more than `promptLimit`: shorten the request
        Cancelled, ///< `cancel` was set
        TimedOut,
        Failed, ///< `error` says why
    };

    struct Result {
        Status status = Failed;
        QString error;
        QList<local::Piece> pieces; ///< what the model wrote, without the token that ended its turn
        bool complete = false;      ///< it ended its turn itself (false: out of tokens)
        int promptTokens = 0;
        int promptLimit = 0;  ///< the most tokens a prompt may have (with the room for the answer left)
        int reusedTokens = 0; ///< of the prompt: already in the model's memory from the last requests
        int storedTokens = 0; ///< of the prompt: its stable part, from the state kept on disk (Options::stateDir)
        int outputTokens = 0;
        qint64 promptMs = 0; ///< reading the prompt
        qint64 outputMs = 0; ///< writing the answer
    };

    virtual ~LocalRunner() = default;
    virtual int contextTokens() const = 0;
    /// Can it take pictures (it was opened with a projector)?
    virtual bool sees() const = 0;
    /// What computes: llama.cpp's processor plugin ("ggml-cpu-haswell"), the threads used and the
    /// cores they are kept on ("ggml-cpu-android_armv8.6_1, 4 threads on cores 4-7").
    virtual QString device() const = 0;
    virtual Result generate(const local::Prompt &prompt, const Params &params) = 0;
};

/// A plugin file's name as the log gives it: without its folder, "lib" and its extension, its own
/// dots kept — "libggml-cpu-android_armv8.6_1.so" is "ggml-cpu-android_armv8.6_1".
QString pluginName(const QString &file);

/// The path of llama.cpp's processor plugin `file` in `dir` ("": by its name alone) as
/// ggml_backend_load() takes it — in the 8-bit encoding of the system's own file functions
/// (QFile::encodeName: UTF-8, on Windows the ANSI code page, which is how std::filesystem reads
/// ggml's narrow path there) with the system's separators. A folder that encoding cannot spell (on
/// Windows, a user's name in another script than the code page's) gives the file's name alone: the
/// system looks for it next to the program first, where the Windows package has it. Shared by the
/// three runners over ggml (the model on the device, search by meaning, speech recognition).
QByteArray backendPluginPath(const QString &dir, const QString &file);

/// Answers `request` with the model `model` open in `runner`: writes its prompt, cuts the request
/// (local::shorten) for as long as the runner finds it too long for its context, lets the model
/// write and reads the answer. A model that does not see refuses a request with pictures; no
/// answer within ai::TimeoutMs is an error, as for a provider on the network. `cancel` (set from
/// another thread) stops it. Blocks until the answer is there. `run`, when given, gets what the
/// runner said of its last try — the counts and the times the app's log tells an answer by.
/// `partial`, when given, gets the answer while it is written (local::partial), each time it
/// changes, on the runner's thread.
Reply answerLocally(LocalRunner *runner, const LocalModel &model, Request request,
                    const std::atomic<bool> *cancel = nullptr, LocalRunner::Result *run = nullptr,
                    const PartialCallback &partial = {});

/// The plugin's file name without "lib" and its extension, and the function it exports:
///   extern "C" brulion::ai::LocalRunner *brulion_localai_open(const brulion::ai::LocalRunner::Options *, QString
///   *error)
inline constexpr char LocalRunnerPlugin[] = "brulion_localai";
inline constexpr char LocalRunnerOpen[] = "brulion_localai_open";
/// …and the one that checks a file (probeLocalModel):
///   extern "C" bool brulion_localai_probe(const QString *path, const QString *backendDir, QString *error)
inline constexpr char LocalRunnerProbe[] = "brulion_localai_probe";

/// Is the plugin there: in one of `pluginDirs`, or (with a "" among them) wherever the system
/// finds a library by its name — Android. Does not load it.
bool hasLocalRunner(const QStringList &pluginDirs);

/// Loads the plugin from the first of `pluginDirs` that has it (a "": by its name) and opens a
/// model with it; nullptr with `error` when there is no plugin, it cannot be loaded, or the model
/// cannot be opened (an empty `options.modelPath`: nullptr with `error` empty when the plugin and
/// llama.cpp's processor plugin loaded). `options.backendDir` is set to the plugin's folder when
/// it is empty. The plugin stays loaded until the app ends.
std::unique_ptr<LocalRunner> loadLocalRunner(const QStringList &pluginDirs, LocalRunner::Options options,
                                             QString *error);

/// Can the plugin's llama.cpp load the model file at `path` (a model of the user's own: its
/// architecture one it knows, its vocabulary whole)? Loads the plugin as loadLocalRunner() does;
/// reads the file's header and vocabulary only. False with `error` (no plugin, or why not).
bool probeLocalModel(const QStringList &pluginDirs, const QString &path, QString *error);

} // namespace brulion::ai
