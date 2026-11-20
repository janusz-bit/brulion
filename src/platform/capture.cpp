#include "capture.h"

#include <QCoreApplication>
#include <QPointer>

namespace brulion::platform {

namespace detail {
/// The system's recognizer of this build (capture_android.cpp / capture_none.cpp), or nullptr.
SpeechRecognizer *systemSpeechRecognizer();
} // namespace detail

namespace {
QPointer<SpeechRecognizer> &registered()
{
    static QPointer<SpeechRecognizer> recognizer;
    return recognizer;
}
} // namespace

SpeechRecognizer *speechRecognizer()
{
    if (SpeechRecognizer *own = registered())
        return own;
    return detail::systemSpeechRecognizer();
}

void setSpeechRecognizer(SpeechRecognizer *recognizer)
{
    QPointer<SpeechRecognizer> &current = registered();
    if (current == recognizer)
        return;
    if (current)
        current->deleteLater();
    current = recognizer;
    if (recognizer && QCoreApplication::instance() && !recognizer->parent())
        recognizer->setParent(QCoreApplication::instance());
}

} // namespace brulion::platform
