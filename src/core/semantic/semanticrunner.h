#pragma once
// What turns text into vectors on the device for search by meaning (semantic/semantic.h). The one
// implementation is the Embedder (semantic/embedder.h: llama.cpp), which the app and brulion-cli do
// not link: it is a plugin — brulion_semantic, next to llama.cpp's libraries and the model runner's
// plugin — loaded with loadSemanticRunner() the first time a note or a query is embedded, so an app
// that never searches by meaning loads none of it, and a build without it (the browser, a build
// without BRULION_LOCAL_AI) simply has no such plugin: search stays full text.
//
//   QString error;
//   std::unique_ptr<semantic::SemanticRunner> runner = semantic::loadSemanticRunner(pluginDirs, options, &error);
//   const QList<semantic::Vector> passages = runner->embedPassages(chunkTexts);   // a worker thread
//   const semantic::Vector query = runner->embedQuery(u"ciasto z jabłkami"_s);    // another one
//
// Passages and queries have a model context each: embedPassages() is called from one thread at a
// time and embedQuery() from one thread at a time, and the two may run at once — a query is never
// held up by a notebook being indexed.

#include "semantic/calibration.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace brulion::semantic {

class SemanticRunner {
public:
    struct Options {
        QString modelPath; ///< the model's file (GGUF, semantic/models.h)
        /// Threads for passages (0: half the processor's threads, at most 4); a query takes one.
        int threads = 0;
        /// The folder of llama.cpp's processor plugins ("": found by name alone — Android).
        QString backendDir;
    };

    virtual ~SemanticRunner() = default;
    /// The vectors' size (384 for e5-small).
    virtual int dimension() const = 0;
    /// What computes ("ggml-cpu-haswell, 4 threads").
    virtual QString device() const = 0;
    /// Unit-length vectors of `passages` (the chunks' texts, semantic/chunker.h), in order; empty
    /// when the model fails. Blocks until done.
    virtual QList<Vector> embedPassages(const QStringList &passages) = 0;
    /// The unit-length vector of a search query; empty when the model fails.
    virtual Vector embedQuery(const QString &query) = 0;
};

/// The plugin's file name without "lib" and its extension, and the function it exports:
///   extern "C" brulion::semantic::SemanticRunner *brulion_semantic_open(
///           const brulion::semantic::SemanticRunner::Options *, QString *error)
inline constexpr char SemanticRunnerPlugin[] = "brulion_semantic";
inline constexpr char SemanticRunnerOpen[] = "brulion_semantic_open";

/// Is the plugin there: in one of `pluginDirs`, or (with a "" among them) wherever the system
/// finds a library by its name — Android. Does not load it.
bool hasSemanticRunner(const QStringList &pluginDirs);

/// Loads the plugin from the first of `pluginDirs` that has it (a "": by its name) and opens a
/// model with it; nullptr with `error` when there is no plugin, it cannot be loaded, or the model
/// cannot be opened. An empty `options.modelPath` opens nothing: nullptr with `error` empty when
/// the plugin and llama.cpp's processor plugin loaded (`brulion-cli version --plugins`).
/// `options.backendDir` is set to the plugin's folder when it is empty. The plugin stays loaded
/// until the process ends.
std::unique_ptr<SemanticRunner> loadSemanticRunner(const QStringList &pluginDirs, SemanticRunner::Options options,
                                                   QString *error);

} // namespace brulion::semantic
