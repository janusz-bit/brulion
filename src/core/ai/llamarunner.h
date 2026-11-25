#pragma once
// The model runner over llama.cpp (ai/localrunner.h; library brulion_ai_llama, built only with
// BRULION_LOCAL_AI: cmake/BrulionLocalAi.cmake, 3rdparty/llama). The app reaches it through the
// plugin brulion_localai (localplugin.cpp); the tests link it.

#include "ai/localrunner.h"

namespace brulion::ai {

/// Opens the model of `options` (with its adapter and projector) with llama.cpp on the processor; nullptr with
/// `error` when the processor is too old for the processor plugins this build has, or a file
/// cannot be loaded.
std::unique_ptr<LocalRunner> openLlamaRunner(const LocalRunner::Options &options, QString *error);

/// Can this llama.cpp load the model at `path` — its architecture known, its vocabulary whole —?
/// Reads its header and vocabulary only (some hundred milliseconds), never its weights; `error`
/// says why not ("this build's llama.cpp cannot load it: unknown model architecture: 'xyz'").
bool probeLlamaModel(const QString &path, const QString &backendDir, QString *error);

} // namespace brulion::ai
