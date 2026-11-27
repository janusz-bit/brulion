// capture.h without a camera app or a speech recognizer of the system (desktop, the browser): the
// chat picks a picture file instead of a photo; dictation only through a recognizer the app
// registers (setSpeechRecognizer), a photo only through a camera it registers (setPhotoCamera).
#include "capture.h"

namespace brulion::platform::detail {

SpeechRecognizer *systemSpeechRecognizer()
{
    return nullptr;
}

PhotoCamera *systemPhotoCamera()
{
    return nullptr;
}

} // namespace brulion::platform::detail
