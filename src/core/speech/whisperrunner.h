#pragma once
// The speech recognizer behind speech/speechrunner.h: whisper.cpp (3rdparty/whisper, the release
// pinned there) over llama.cpp's ggml — the same libraries and processor plugins the local language
// models compute with, never a second copy. Only in builds with BRULION_SPEECH
// (cmake/BrulionSpeech.cmake), linked into the plugin brulion_speech the app loads on first use.

#include "speech/speechrunner.h"

#include <memory>

namespace brulion::speech {

/// Opens the model `options.modelPath`; nullptr with `error` when it cannot be read or no processor
/// plugin of ggml runs on this processor. Without a model (`options.modelPath` ""): nullptr,
/// `error` empty once the processor plugin loaded.
std::unique_ptr<SpeechRunner> openWhisperRunner(const SpeechRunner::Options &options, QString *error);

} // namespace brulion::speech
