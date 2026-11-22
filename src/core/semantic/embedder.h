// Embedder — text to vectors with a GGUF embedding model through llama.cpp: the SemanticRunner
// (semantic/semanticrunner.h) of the plugin brulion_semantic. Library brulion_semantic_llama, built
// only with BRULION_LOCAL_AI over the pinned llama.cpp (3rdparty/llama); part of the semantic
// module (semantic/semantic.h).
//
// Models: multilingual-e5-small or mmlw-e5-small converted by scripts/semantic/convert_e5_gguf.py
// (f16 242 MB, q8_0 132 MB). e5 models expect the prefixes "query: " and "passage: ": embedQueries()
// and embedPassages() add them.
//
// Two model contexts over one model in memory: one for passages (several threads, several texts in
// one call) and one for queries (one thread) — embedPassages() and embedQueries() may run at the
// same time on two threads (each of them on one thread at a time); embed() shares the passages'.
#pragma once

#include "semantic/semanticrunner.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace brulion::semantic {

class Embedder : public SemanticRunner {
public:
    enum class Device {
        /// A discrete GPU when llama.cpp has one (Vulkan, CUDA, Metal…), else the CPU. Integrated
        /// GPUs are not picked: through Vulkan an Intel iGPU embedded 15x slower than the CPU of
        /// the same laptop (docs/SEMANTIC.md) — phones should measure first (embedbench). The
        /// app's llama.cpp is built for the CPU alone (3rdparty/llama).
        Auto,
        Cpu,
        AnyGpu, ///< the first GPU, discrete or integrated (CPU when there is none)
    };
    struct Options {
        Device device = Device::Auto;
        /// CPU threads for passages; 0 = one fewer than the fast physical cores (ai/cpus.h), else
        /// half the logical cores — at most 4 either way.
        int threads = 0;
        int queryThreads = 0;    ///< CPU threads for queries; 0 = 1 (a query is a dozen tokens: one fast
                                 ///< core beats a barrier across big and little cores — 6 ms vs 21 ms on a phone)
        int maxTokens = 512;     ///< longer texts are cut (the e5 models' context)
        int batchTokens = 1024;  ///< tokens encoded in one call (several texts; fastest in the benchmark)
        int maxSequences = 32;   ///< texts encoded in one call
        /// The folder of llama.cpp's processor plugins this build packages ("": by name alone —
        /// Android); BRULION_GGML_BACKEND_DIR in the environment overrides.
        QString backendDir;
    };

    /// Load the model; nullptr with `*error` set on failure.
    static std::unique_ptr<Embedder> open(const QString &ggufPath, const Options &options, QString *error = nullptr);
    static std::unique_ptr<Embedder> open(const QString &ggufPath) { return open(ggufPath, Options{}); }
    ~Embedder() override;
    Embedder(const Embedder &) = delete;
    Embedder &operator=(const Embedder &) = delete;

    int dimension() const override;
    /// What runs the model: "ggml-cpu-haswell, 4 threads" or the GPU's backend name and
    /// description ("Vulkan0 (NVIDIA …)").
    QString device() const override;
    /// Unit-length embeddings of `texts` as they are (no prefix), in order (the passages'
    /// context). An empty list on a llama.cpp failure.
    QList<Vector> embed(const QStringList &texts);
    /// With "query: " in front, in the queries' context.
    QList<Vector> embedQueries(const QStringList &queries);
    /// With "passage: " in front, in the passages' context.
    QList<Vector> embedPassages(const QStringList &passages) override;
    Vector embedQuery(const QString &query) override;
    /// Tokens of the texts embedded so far (statistics).
    qint64 tokenCount() const;

private:
    struct Impl;
    explicit Embedder(std::unique_ptr<Impl> d);
    std::unique_ptr<Impl> d;
};

} // namespace brulion::semantic
