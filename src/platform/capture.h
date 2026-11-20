// A photo from the camera and dictation for the AI chat (docs/UI.md "AI chat", docs/ANDROID.md
// "Camera and dictation"): what the device can give the chat's field besides typing.
//
//   if (platform::cameraAvailable())
//       platform::takePhoto([](const QString &file, const QString &error) { … });
//   platform::SpeechRecognizer *speech = platform::speechRecognizer();   // nullptr: no dictation
//   connect(speech, &SpeechRecognizer::recognized, …); speech->start(u"pl-PL"_s);
//
// One implementation per build (src/platform/CMakeLists.txt): Android — the system's camera app
// (ACTION_IMAGE_CAPTURE into a file of the app's cache) and its speech recognizer
// (RecognizerIntent: Google's, or the one the device has), through AndroidBridge and
// MediaCapture.kt, with no CAMERA or RECORD_AUDIO permission of Brulion's own — capture_android.cpp;
// elsewhere none — capture_none.cpp (the chat picks a picture file instead; no dictation).
//
// The hook for speech recognition of the app's own (on the device: whisper.cpp, TODO.md):
// setSpeechRecognizer() — a recognizer registered there is used instead of the system's, on every
// platform; the chat shows its microphone button whenever speechRecognizer() is not null.
#pragma once

#include <QObject>
#include <QString>

#include <functional>

namespace brulion::platform {

/// What turns speech into text for dictation. Used from the GUI thread; its signals too.
class SpeechRecognizer : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    /// Starts listening in `language` (BCP 47, "pl-PL", "en-GB"); false when it cannot (failed()
    /// may say why first).
    virtual bool start(const QString &language) = 0;
    /// Ends listening: what was heard is recognized (recognized() follows).
    virtual void stop() = 0;
    /// Ends it without a result.
    virtual void cancel() = 0;
    /// It listens now.
    virtual bool listening() const = 0;

Q_SIGNALS:
    /// What it has heard so far (a recognizer that tells; the system's dialog shows its own).
    void partial(const QString &text);
    /// The text heard ("" nothing); listening ended.
    void recognized(const QString &text);
    /// Listening ended without a text: cancelled (`why` "") or failed (`why` says what, in English).
    void failed(const QString &why);
    void listeningChanged();
};

/// The recognizer dictation uses: one the app registered (setSpeechRecognizer), else the
/// system's (Android), else nullptr — no dictation on this device.
SpeechRecognizer *speechRecognizer();
/// Uses `recognizer` for dictation from now on (taken over: deleted with the application, or when
/// another one replaces it); nullptr: the system's again.
void setSpeechRecognizer(SpeechRecognizer *recognizer);

/// The device has a camera app to ask for a photo (Android with one).
bool cameraAvailable();
/// Asks the camera app for a photo: `done` gets its file (a JPEG in the app's cache, the caller
/// copies it into the notebook) or "" — with `error` "" when the user took none. False when it
/// could not be asked (no camera here): `done` is not called.
bool takePhoto(std::function<void(const QString &file, const QString &error)> done);

} // namespace brulion::platform
