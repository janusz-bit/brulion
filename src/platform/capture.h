// A photo from the camera and dictation for the AI chat (docs/UI.md "AI chat", docs/ANDROID.md
// "Camera and dictation"): what the device can give the chat's field besides typing.
//
//   if (platform::PhotoCamera *camera = platform::photoCamera()) {   // nullptr: no camera app
//       connect(camera, &PhotoCamera::photosWaiting, …);   // then camera->takePhotos(notebook)
//       camera->take(notebook, u"what asked for it"_s);
//   }
//   platform::SpeechRecognizer *speech = platform::speechRecognizer();   // nullptr: no dictation
//   connect(speech, &SpeechRecognizer::recognized, …); speech->start(u"pl-PL"_s);
//
// One implementation per build (src/platform/CMakeLists.txt): Android — the system's camera app
// (ACTION_IMAGE_CAPTURE into a file of the app's cache) and its speech recognizer
// (RecognizerIntent: Google's, or the one the device has), through AndroidBridge and
// MediaCapture.kt, with no CAMERA or RECORD_AUDIO permission of Brulion's own — capture_android.cpp;
// elsewhere none — capture_none.cpp (the chat picks a picture file instead; no dictation).
//
// A photo outlives the app's process: Android may end it while the camera app is in front (it
// needs the memory), and the camera's answer then starts the app anew. So the camera keeps what
// it was asked for (`notebook`, `purpose`) with the photo — before the camera app starts — and
// hands the photo out once, to whoever takes the photos of that notebook: the same process's
// caller, or the next one's once its notebook is open (WorkspaceChats: the conversation's draft).
//
// The hook for speech recognition of the app's own: setSpeechRecognizer() — a recognizer registered
// there is used instead of the system's, on every platform; the chat shows its microphone button
// whenever speechRecognizer() is not null. The desktop app registers one that records and hears
// on the device with Whisper (media/dictation.h: the voice notes' speech model); it tells how long
// it has listened, its limit, that it is making the recording into text (recognizing()), and
// whether it can listen at all (ready(): false without a speech model).
#pragma once

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QString>

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
    /// It can listen now; false: it needs something first (the app's own recognizer a speech
    /// model, which the chat offers to download).
    virtual bool ready() const { return true; }
    /// How long it has listened, in milliseconds (-1: it does not tell — the system's dialog).
    virtual qint64 elapsedMs() const { return -1; }
    /// The longest it listens, in milliseconds (0: no limit it tells).
    virtual qint64 maxMs() const { return 0; }
    /// Listening ended and what was heard is being made into text (recognized() follows).
    virtual bool recognizing() const { return false; }

Q_SIGNALS:
    /// What it has heard so far (a recognizer that tells; the system's dialog shows its own).
    void partial(const QString &text);
    /// The text heard ("" nothing); listening ended.
    void recognized(const QString &text);
    /// Listening ended without a text: cancelled (`why` "") or failed (`why` says what, in English).
    void failed(const QString &why);
    /// listening() or recognizing() changed.
    void listeningChanged();
    void readyChanged();
    /// elapsedMs() changed (about ten times a second while it listens).
    void elapsedChanged();
};

/// The recognizer dictation uses: one the app registered (setSpeechRecognizer), else the
/// system's (Android), else nullptr — no dictation on this device.
SpeechRecognizer *speechRecognizer();
/// Uses `recognizer` for dictation from now on (taken over: deleted with the application, or when
/// another one replaces it); nullptr: the system's again.
void setSpeechRecognizer(SpeechRecognizer *recognizer);

/// A photo the camera took, with what asked for it.
struct CapturedPhoto {
    QString file;     ///< a JPEG in the app's cache: the taker's now (it copies it and removes it)
    QString notebook; ///< PhotoCamera::take()'s `notebook`
    QString purpose;  ///< PhotoCamera::take()'s `purpose`
    bool operator==(const CapturedPhoto &) const = default;
};

/// The photos of `array` as the Android camera hands them out ({file, notebook, purpose} each), in
/// order; an entry without a file is left out. Pure.
QList<CapturedPhoto> capturedPhotos(const QJsonArray &array);

/// The device's camera app, asked for photos. Used from the GUI thread; its signals too.
class PhotoCamera : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    /// Asks the camera app for a photo for `purpose` (the caller's own words: what the photo is
    /// for) in the notebook `notebook` (Vault::id()); both are kept with the photo. False when it
    /// could not be asked.
    virtual bool take(const QString &notebook, const QString &purpose) = 0;
    /// The photos taken for `notebook` — and for none — that nobody took yet, also those taken
    /// while an earlier process of the app was gone; oldest first, each handed out once. The others
    /// wait for their notebook.
    virtual QList<CapturedPhoto> takePhotos(const QString &notebook) = 0;

Q_SIGNALS:
    /// A photo was taken: takePhotos() hands it out.
    void photosWaiting();
    /// No photo for `purpose`: the camera could not be used (`why`, in English) or the user took
    /// none (`why` "").
    void failed(const QString &purpose, const QString &why);
};

/// The camera photos are asked of: one the app registered (setPhotoCamera), else the system's
/// camera app (Android with one), else nullptr — no camera on this device.
PhotoCamera *photoCamera();
/// Uses `camera` for photos from now on (taken over: deleted with the application, or when another
/// one replaces it); nullptr: the system's again.
void setPhotoCamera(PhotoCamera *camera);

} // namespace brulion::platform
