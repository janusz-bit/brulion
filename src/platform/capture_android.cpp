// capture.h on Android: the system's camera app and speech recognizer through AndroidBridge
// (MediaCapture.kt): an intent each, its result as AndroidBridge::captured() — a photo's kept in
// the app's cache until AndroidBridge::takePhotos() hands it out.
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

/// The system's camera app: MediaCapture.kt keeps each photo with what asked for it in the app's
/// cache until takePhotos() hands it out — over the end of the app's process too.
class AndroidPhotoCamera : public PhotoCamera {
public:
    explicit AndroidPhotoCamera(QObject *parent) : PhotoCamera(parent)
    {
        connect(AndroidBridge::instance(), &AndroidBridge::captured, this, [this](int kind, const QJsonObject &result) {
            if (kind == kPhoto)
                answered(result);
        });
    }
    bool take(const QString &notebook, const QString &purpose) override
    {
        return AndroidBridge::instance()->takePhoto(notebook, purpose);
    }
    QList<CapturedPhoto> takePhotos(const QString &notebook) override
    {
        return capturedPhotos(AndroidBridge::instance()->takePhotos(notebook));
    }
};

/// The system's app of `kind` when the device has one, else nullptr.
template<typename App>
App *madeIfAvailable(int kind)
{
    return AndroidBridge::instance()->canCapture(kind) ? new App(AndroidBridge::instance()) : nullptr;
}

/// The system's app of `kind` (made once, while the device has one).
template<typename App>
App *systemApp(int kind)
{
    static QPointer<App> app;
    if (!app)
        app = madeIfAvailable<App>(kind);
    return app;
}

} // namespace

namespace detail {
SpeechRecognizer *systemSpeechRecognizer()
{
    return systemApp<AndroidSpeechRecognizer>(kSpeech);
}

PhotoCamera *systemPhotoCamera()
{
    return systemApp<AndroidPhotoCamera>(kPhoto);
}
} // namespace detail

} // namespace brulion::platform
