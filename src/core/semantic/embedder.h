// Embedder — text to vectors with a GGUF embedding model through llama.cpp. Library
// brulion_semantic_llama (built only when CMake finds llama.cpp); part of the semantic module's
// prototype (semantic/semantic.h).
//
// Models: multilingual-e5-small or mmlw-e5-small converted by scripts/semantic/convert_e5_gguf.py
// (f16 242 MB, q8_0 132 MB). e5 models expect the prefixes "query: " and "passage: ": embedQueries()
// and embedPassages() add them.
#pragma once

#include "semantic/calibration.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace brulion::semantic {

class Embedder {
public:
    enum class Device {
        /// A discrete GPU when llama.cpp has one (Vulkan, CUDA, Metal…), else the CPU. Integrated
        /// GPUs are not picked: through Vulkan an Intel iGPU embedded 15x slower than the CPU of
        /// the same laptop (docs/SEMANTIC.md) — phones should measure first (embedbench).
        Auto,
        Cpu,
        AnyGpu, ///< the first GPU, discrete or integrated (CPU when there is none)
    };
    struct Options {
        Device device = Device::Auto;
        int threads = 0;         ///< CPU threads for passages; 0 = half the logical cores, at most 4
        int queryThreads = 0;    ///< CPU threads for queries; 0 = 1 (a query is a dozen tokens: one fast
                                 ///< core beats a barrier across big and little cores — 6 ms vs 21 ms on a phone)
        int maxTokens = 512;     ///< longer texts are cut (the e5 models' context)
        int batchTokens = 1024;  ///< tokens encoded in one call (several texts; fastest in the benchmark)
        int maxSequences = 32;   ///< texts encoded in one call
    };

    /// Load the model; nullptr with `*error` set on failure.
    static std::unique_ptr<Embedder> open(const QString &ggufPath, const Options &options, QString *error = nullptr);
    static std::unique_ptr<Embedder> open(const QString &ggufPath) { return open(ggufPath, Options{}); }
    ~Embedder();
    Embedder(const Embedder &) = delete;
    Embedder &operator=(const Embedder &) = delete;

    int dimension() const;
    /// What runs the model: "CPU" or the GPU's backend name and description ("Vulkan0 (NVIDIA …)").
    QString device() const;
    /// Unit-length embeddings of `texts` as they are (no prefix), in order. An empty list on a
    /// llama.cpp failure.
    QList<Vector> embed(const QStringList &texts);
    QList<Vector> embedQueries(const QStringList &queries);
    QList<Vector> embedPassages(const QStringList &passages);
    /// Tokens of the texts embedded so far (statistics).
    qint64 tokenCount() const;

private:
    struct Impl;
    explicit Embedder(std::unique_ptr<Impl> d);
    std::unique_ptr<Impl> d;
};

} // namespace brulion::semantic
