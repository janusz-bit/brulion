// The plugin the app loads the first time it transcribes a recording (speech/speechrunner.h):
// the whisper.cpp recognizer behind one exported function.

#include "speech/whisperrunner.h"

#include <QtCore/qglobal.h>

extern "C" Q_DECL_EXPORT brulion::speech::SpeechRunner *
brulion_speech_open(const brulion::speech::SpeechRunner::Options *options, QString *error)
{
    return brulion::speech::openWhisperRunner(*options, error).release();
}
