// LiveTranscript — text while recording (QML type `LiveTranscript`; docs/UI.md "Voice notes" →
// "Text while recording"): the samples of a VoiceRecorder (setSampleListener) cut into windows as
// whisper.cpp's stream example does (core speech::LiveWindows: a window heard again every three
// seconds as it grows, kept once ten seconds long) and heard by the speech model on the device
// (Transcriber::hearLive(): on its worker thread, never the GUI thread, fast rather than best) —
// `text` is what was kept (`committed`) and what the window heard last says after it (`tentative`,
// replaced as the window grows). It is a preview: the transcript placed in the note, and the
// dictation's text, come from one full pass over the whole recording when it ends.
//
// It hears only with the speech settings' "Text while recording" on and a model here
// (Transcriber::liveReady) and from a recorder whose samples pass through the app (the system's
// own recorder on Android gives none: `metered` false). The language of an "auto" setting is the
// one the first window with words was heard in — the next windows are heard in it. A window that
// comes back more than LateMs behind the recording twice ends it for this recording ("slow") and
// marks the model too slow on this device (Transcriber::liveSlow: off until "Try again").
//
//   LiveTranscript { recorder: recorder; transcriber: AppController.speech }
//   BText { text: live.tail }
#pragma once

#include "transcriber.h"
#include "voicerecorder.h"

#include "speech/live.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

namespace brulion::media {

class LiveTranscript : public QObject {
    Q_OBJECT

    /// The recorder it listens to (not owned).
    Q_PROPERTY(brulion::media::VoiceRecorder *recorder READ recorder WRITE setRecorder NOTIFY recorderChanged FINAL)
    /// What hears (AppController.speech; not owned).
    Q_PROPERTY(brulion::media::Transcriber *transcriber READ transcriber WRITE setTranscriber NOTIFY transcriberChanged
                       FINAL)
    /// Hear at all (the owner's switch: on).
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
    /// The text so far: committed and tentative.
    Q_PROPERTY(QString text READ text NOTIFY textChanged FINAL)
    Q_PROPERTY(QString committed READ committed NOTIFY textChanged FINAL)
    Q_PROPERTY(QString tentative READ tentative NOTIFY textChanged FINAL)
    /// The end of `text`, at most `tailChars` characters from the start of a word ("…" before
    /// it when cut: speech::textTail) — what a small field shows.
    Q_PROPERTY(QString tail READ tail NOTIFY textChanged FINAL)
    Q_PROPERTY(int tailChars READ tailChars WRITE setTailChars NOTIFY textChanged FINAL)
    /// "off" (it does not hear: see above), "waiting" (recording, the first window not heard
    /// yet), "hearing", "slow" (it gave up for this recording: too far behind).
    Q_PROPERTY(QString status READ status NOTIFY statusChanged FINAL)
    /// The language the windows are heard in ("" until the first one with words, for "auto").
    Q_PROPERTY(QString language READ language NOTIFY statusChanged FINAL)
    /// How many windows were heard (tests, the log).
    Q_PROPERTY(int windows READ windows NOTIFY textChanged FINAL)

public:
    /// A window coming back this much behind the recording (twice) is too late.
    static constexpr qint64 LateMs = 10000;

    explicit LiveTranscript(QObject *parent = nullptr);
    ~LiveTranscript() override;

    VoiceRecorder *recorder() const { return m_recorder; }
    void setRecorder(VoiceRecorder *recorder);
    Transcriber *transcriber() const { return m_transcriber; }
    void setTranscriber(Transcriber *transcriber);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QString text() const;
    QString committed() const;
    QString tentative() const;
    QString tail() const;
    int tailChars() const { return m_tailChars; }
    void setTailChars(int chars);
    QString status() const { return m_status; }
    QString language() const { return m_language; }
    int windows() const { return m_windows; }

    /// The language to hear in when the speech settings say "auto" ("pl"; "": the first window's)
    /// — dictation's, from the chat's language. Read when a recording starts.
    void setPreferredLanguage(const QString &language) { m_preferred = language; }
    /// Tests: the rules of the windows (before the recording starts).
    void setRules(const speech::LiveRules &rules) { m_rules = rules; }
    /// Tests: how late a window is, instead of the recording's length when it comes back.
    void setLateMs(qint64 ms) { m_lateMs = ms; }

Q_SIGNALS:
    void recorderChanged();
    void transcriberChanged();
    void enabledChanged();
    void textChanged();
    void statusChanged();

private:
    void listen();
    void onRecorderState();
    void onSamples(const float *samples, qsizetype count, int rate);
    void hearNext();
    void onHeard(quint64 generation, const speech::LiveWindows::Window &window, bool ok, const QString &modelId,
                 const QList<speech::Segment> &segments, const QString &language);
    void setStatus(const QString &status);
    /// Recording, and it may hear (enabled, the transcriber ready for it).
    bool canHear() const;
    void start();
    void end();

    QPointer<VoiceRecorder> m_recorder;
    QPointer<Transcriber> m_transcriber;
    bool m_enabled = true;
    std::unique_ptr<speech::LiveWindows> m_live;
    speech::LiveRules m_rules;
    QString m_status = QStringLiteral("off");
    QString m_language;
    QString m_preferred;
    int m_tailChars = 280;
    int m_windows = 0;
    int m_late = 0;
    qint64 m_lateMs = LateMs;
    bool m_inFlight = false;
    bool m_recording = false;
    int m_rate = 0;
    quint64 m_generation = 0; ///< bumped when a recording starts or ends: older windows are ignored
};

} // namespace brulion::media
