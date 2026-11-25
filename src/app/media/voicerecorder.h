// VoiceRecorder — a voice note recorded in the app (QML type `VoiceRecorder`, the recorder dialog
// qml/media/VoiceRecorderDialog.qml; docs/UI.md "Voice notes"). It asks for the microphone
// (QMicrophonePermission: Android's runtime request, the browser's getUserMedia, always granted on
// the desktop), records from the system's default input with a level meter, pauses and resumes,
// and writes the recording into a file of the cache folder named like the attachment it becomes
// (core media::recordingName: "Nagranie 2026-11-20 18.30.m4a"), which the editor then stores in the
// notebook's attachments folder and embeds (PageController::insertRecording()).
//
// The format is the best the device encodes (Qt Multimedia's encoders behind QAudioBufferInput):
// AAC in MPEG-4 (".m4a": plays on every phone, browser and computer, Obsidian on an iPhone too),
// else Opus in Ogg (".ogg"), else WAV of 16-bit samples written here (".wav": any device, no
// encoder needed — also when the encoder fails to start). The samples come from QAudioSource, so the
// meter and the duration are the recorder's own on every backend.
//
// States: "idle" → "asking" (the microphone permission) → "recording" ⇄ "paused" → "saving" →
// "done" (`filePath`; finished()) — or "denied" (no permission), "failed" (`error`; failed()),
// and back to "idle" with cancel() (the file deleted). It stops by itself after
// media::MaxRecordingMs.
#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

namespace brulion::media {

class VoiceRecorder : public QObject {
    Q_OBJECT

    /// This build records at all (Qt Multimedia).
    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)
    /// The microphone's level now (0–1, core media::meterLevel) and the last ones, oldest first
    /// (LevelCount of them, about 20 a second: the meter's bars).
    Q_PROPERTY(double level READ level NOTIFY levelChanged FINAL)
    Q_PROPERTY(QList<double> levels READ levels NOTIFY levelChanged FINAL)
    /// What is recorded so far, in milliseconds (pauses left out).
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged FINAL)
    /// The recording once "done" (a file of the cache folder: the editor imports it).
    Q_PROPERTY(QString filePath READ filePath NOTIFY stateChanged FINAL)
    /// "m4a", "ogg" or "wav": what it records into (known once recording).
    Q_PROPERTY(QString format READ format NOTIFY stateChanged FINAL)
    /// The microphone's name ("" before it records).
    Q_PROPERTY(QString device READ device NOTIFY stateChanged FINAL)
    /// `levels` follow the microphone (false: the system's recorder writes the file itself — Qt's
    /// native Android backend — and no sample passes here: the dialog shows that it records, not
    /// how loud).
    Q_PROPERTY(bool metered READ metered NOTIFY stateChanged FINAL)
    /// The file name's first word (the app's translated "Recording").
    Q_PROPERTY(QString namePrefix READ namePrefix WRITE setNamePrefix NOTIFY namePrefixChanged FINAL)

public:
    static constexpr int LevelCount = 48;

    explicit VoiceRecorder(QObject *parent = nullptr);
    ~VoiceRecorder() override;

    bool available() const;
    QString state() const { return m_state; }
    double level() const { return m_levels.isEmpty() ? 0.0 : m_levels.last(); }
    QList<double> levels() const { return m_levels; }
    double duration() const { return double(m_durationMs); }
    QString error() const { return m_error; }
    QString filePath() const { return m_filePath; }
    QString format() const { return m_format; }
    QString device() const { return m_device; }
    bool metered() const { return m_metered; }
    QString namePrefix() const { return m_namePrefix; }
    void setNamePrefix(const QString &prefix);

    /// Asks for the microphone and records (from "idle", "done", "failed", "denied").
    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    /// Ends the recording: "saving", then "done" with the file.
    Q_INVOKABLE void stop();
    /// Throws the recording away (its file deleted): "idle".
    Q_INVOKABLE void cancel();

    /// The time a recording is named by (the app's clock: `--now` in screenshots).
    static void setClock(std::function<QDateTime()> clock);
    /// Where recordings are written (default: the app's cache folder + "/recordings"; tests).
    void setFolder(const QString &folder) { m_folder = folder; }
    /// Tests: record what feed() gives instead of a microphone (no permission asked), as 16-bit
    /// samples of `channels` at `rate`.
    void useFeed(int rate, int channels);
    /// Tests: samples "from the microphone" (16-bit little-endian, the format useFeed() named).
    void feed(const QByteArray &pcm16);
    /// Tests: the encoders are not tried (WAV).
    void setWavOnly(bool wav) { m_wavOnly = wav; }
    /// Gets the samples as they are recorded: one channel (the channels' mean), floats in [-1, 1],
    /// `rate` a second, on the GUI thread — text while recording (livetranscript.h). None come
    /// where the system records by itself (`metered` false) or while paused. One listener at a
    /// time; an empty one: none.
    using SampleListener = std::function<void(const float *samples, qsizetype count, int rate)>;
    void setSampleListener(SampleListener listener) { m_listener = std::move(listener); }
    /// The environment variable that makes every recorder hear a made-up voice instead of a
    /// microphone (a tone that swells and fades, 48 kHz, ten pieces a second): the UI self-test and
    /// screenshots of a machine without one.
    static constexpr char TestMicrophoneVariable[] = "BRULION_TEST_MICROPHONE";

Q_SIGNALS:
    void stateChanged();
    void levelChanged();
    void durationChanged();
    void namePrefixChanged();
    void finished(const QString &filePath);
    void failed(const QString &error);

public:
    /// What records (voicerecorder_p.h).
    struct Backend;

private:
    friend struct Backend;

    void setState(const QString &state, const QString &error = {});
    void begin();
    /// The file the recording goes into, with `extension`.
    QString newFile(const QString &extension);
    void pushLevel(double level);
    void addFrames(qint64 frames, int rate);
    /// Samples recorded (the backend's, one channel) for the listener.
    void hearSamples(const float *samples, qsizetype count, int rate);
    /// The system's recorder says how long the recording is (no samples pass here).
    void setDuration(qint64 ms);
    void saved(bool ok, const QString &error);

    std::unique_ptr<Backend> m_backend;
    QString m_state = QStringLiteral("idle");
    QList<double> m_levels;
    qint64 m_durationMs = 0;
    qint64 m_frames = 0;
    QString m_error;
    QString m_filePath;
    QString m_format;
    QString m_device;
    bool m_metered = true;
    QString m_namePrefix = QStringLiteral("Recording");
    QString m_folder;
    bool m_wavOnly = false;
    SampleListener m_listener;
    int m_feedRate = 0;
    int m_feedChannels = 0;
    /// The made-up voice (TestMicrophoneVariable): a timer and how far it is.
    QTimer *m_testVoice = nullptr;
    qint64 m_testFrames = 0;
    void feedTestVoice();
};

} // namespace brulion::media
