// Dictation — the app's own speech recognizer for the AI chat's microphone on the desktop
// (docs/UI.md "AI chat" → Dictation; platform/capture.h's SpeechRecognizer, registered by
// AppController::setUpSpeech() where Android's system recognizer is not there): it records with a
// VoiceRecorder (the system's default microphone, WAV into the cache's "dictation" folder) up to
// MaxMs, then hears the recording on the device with the voice notes' speech model (Transcriber:
// whisper.cpp, in the language the dictation started with unless the speech settings name one),
// and gives the text (recognized()). The recording is deleted when it has been heard, when
// dictation is cancelled, and — left over by an app that ended meanwhile — when the next dictation
// starts. Without a speech model it is not ready(): the chat offers the download.
//
// States: idle → recording (listening(), elapsedMs()) → saving → recognizing (recognizing()) → idle
// with recognized(text) or failed(why); cancel() from any of them: failed(""), the file gone.
#pragma once

#include "capture.h"

#include <QPointer>
#include <QString>
#include <QTimer>

namespace brulion::media {

class Transcriber;
class VoiceRecorder;

class Dictation : public platform::SpeechRecognizer {
    Q_OBJECT
public:
    /// The longest dictation (five minutes): it stops by itself then, and is heard.
    static constexpr qint64 MaxMs = 5 * 60 * 1000;

    /// `transcriber` is not owned (it may go first: dictation is then not ready()).
    explicit Dictation(Transcriber *transcriber, QObject *parent = nullptr);
    ~Dictation() override;

    bool start(const QString &language) override;
    void stop() override;
    void cancel() override;
    bool listening() const override;
    bool ready() const override;
    qint64 elapsedMs() const override;
    qint64 maxMs() const override { return m_maxMs; }
    bool recognizing() const override { return m_state == State::Recognizing || m_state == State::Saving; }

    /// Where the recordings go (default: the app's cache + "/dictation"); its files are removed
    /// when a dictation starts (none of them is in use then).
    void setFolder(const QString &folder);
    QString folder() const { return m_folder; }
    /// The recorder (tests: VoiceRecorder::useFeed(), feed()).
    VoiceRecorder *recorder() const { return m_recorder; }
    /// Tests: a shorter limit.
    void setMaxMs(qint64 ms) { m_maxMs = ms; }

private:
    enum class State { Idle, Recording, Saving, Recognizing };
    void setState(State state);
    void onRecorderState();
    void onHeard(const QString &file, bool ok, const QString &text, const QString &error);
    void removeLeftovers() const;
    void end();

    QPointer<Transcriber> m_transcriber;
    VoiceRecorder *m_recorder = nullptr;
    QString m_folder;
    QString m_language;
    QString m_file; // the recording being heard
    State m_state = State::Idle;
    qint64 m_maxMs = MaxMs;
};

} // namespace brulion::media
