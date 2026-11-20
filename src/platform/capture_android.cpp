// capture.h on Android: the system's camera app and speech recognizer through AndroidBridge
// (MediaCapture.kt): an intent each, its result as AndroidBridge::captured().
#include "capture.h"

#include "android/androidbridge.h"

#include <QJsonObject>
#include <QPointer>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

constexpr int kPhoto = 0;
constexpr int kSpeech = 1;

/// The system's recognizer: its dialog listens, the text comes back at once (no partial text).
class AndroidSpeechRecognizer : public SpeechRecognizer {
public:
    explicit AndroidSpeechRecognizer(QObject *parent) : SpeechRecognizer(parent)
    {
        connect(AndroidBridge::instance(), &AndroidBridge::captured, this, [this](int kind, const QJsonObject &result) {
            if (kind != kSpeech || !m_listening)
                return;
            m_listening = false;
            Q_EMIT listeningChanged();
            if (result.contains(u"text"_s))
                Q_EMIT recognized(result.value(u"text"_s).toString());
            else
                Q_EMIT failed(result.value(u"error"_s).toString());
        });
    }
    bool start(const QString &language) override
    {
        if (m_listening || !AndroidBridge::instance()->canCapture(kSpeech))
            return false;
        if (!AndroidBridge::instance()->capture(kSpeech, language))
            return false;
        m_listening = true;
        Q_EMIT listeningChanged();
        return true;
    }
    void stop() override { } // (the system's dialog ends listening itself)
    void cancel() override
    {
        if (!m_listening)
            return;
        m_listening = false; // (its answer, when it comes, is dropped)
        Q_EMIT listeningChanged();
    }
    bool listening() const override { return m_listening; }

private:
    bool m_listening = false;
};

} // namespace

namespace detail {
SpeechRecognizer *systemSpeechRecognizer()
{
    static QPointer<SpeechRecognizer> recognizer;
    if (!recognizer && AndroidBridge::instance()->canCapture(kSpeech))
        recognizer = new AndroidSpeechRecognizer(AndroidBridge::instance());
    return recognizer;
}
} // namespace detail

bool cameraAvailable()
{
    return AndroidBridge::instance()->canCapture(kPhoto);
}

bool takePhoto(std::function<void(const QString &, const QString &)> done)
{
    AndroidBridge *bridge = AndroidBridge::instance();
    if (!bridge->canCapture(kPhoto))
        return false;
    // One answer for this photo: the connection goes with it.
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(
            bridge, &AndroidBridge::captured, bridge, [connection, done](int kind, const QJsonObject &result) {
                if (kind != kPhoto)
                    return;
                QObject::disconnect(*connection);
                if (done)
                    done(result.value(u"file"_s).toString(), result.value(u"error"_s).toString());
            });
    if (!bridge->capture(kPhoto, QString())) {
        QObject::disconnect(*connection);
        return false;
    }
    return true;
}

} // namespace brulion::platform
