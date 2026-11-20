// capture.h without a camera app or a speech recognizer of the system (desktop, the browser): the
// chat picks a picture file instead of a photo; dictation only through a recognizer the app
// registers (setSpeechRecognizer).
#include "capture.h"

namespace brulion::platform {

namespace detail {
SpeechRecognizer *systemSpeechRecognizer() { return nullptr; }
} // namespace detail

bool cameraAvailable() { return false; }

bool takePhoto(std::function<void(const QString &, const QString &)>) { return false; }

} // namespace brulion::platform
