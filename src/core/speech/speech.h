#pragma once
// speech — speech recognition on the device (docs/UI.md "Voice notes", docs/EXTENSIONS.md "AI"):
// OpenAI's Whisper models (MIT) run by whisper.cpp over the same ggml that runs the local language
// models (3rdparty/whisper, cmake/BrulionSpeech.cmake) — no audio ever leaves the device. This
// header is the part without the recognizer, built and tested everywhere: which models there are
// (their files, sizes, licence), the languages one is asked to hear, and what a transcription
// gives back. The recognizer (speech/speechrunner.h: whisper.cpp in the plugin brulion_speech,
// only in builds that have it) turns 16 kHz mono samples (media/pcm.h) into text.
//
// A model is an ai::LocalModel of the format "whisper" (only `file` set): the app downloads and
// removes it with ai::LocalModelFiles, as it does the language models, in a folder of its own.

#include "ai/local.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace brulion::speech {

/// The models the app knows, the default first: multilingual Whisper models (they hear Polish),
/// from small and fast to large and accurate.
const QList<ai::LocalModel> &speechModels();
/// The model `id` names ("": the default); nullptr for an unknown one.
const ai::LocalModel *speechModel(const QString &id);

/// What the recognizer is asked to hear: "auto" (it decides from the first 30 seconds), "pl",
/// "en". The first is the default.
const QStringList &speechLanguages();
/// `language` when it is one of speechLanguages(), else "auto".
QString knownLanguage(const QString &language);

/// One stretch of speech the recognizer heard.
struct Segment {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
    friend bool operator==(const Segment &, const Segment &) = default;
};

/// The texts of `segments`, in order, without what Whisper writes for no speech at all — its
/// bracketed or parenthesised notes ("[BLANK_AUDIO]", "(muzyka)", "[Music]", "*laughs*") and
/// subtitle credits it learnt from films ("Napisy wykonane przez …", "Subtitles by …").
QStringList spokenTexts(const QList<Segment> &segments);

} // namespace brulion::speech
