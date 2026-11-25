// Transcriber — speech recognition on the device for voice notes and any recording in a note
// (`AppController.speech`; Settings → AI → Speech; docs/UI.md "Voice notes"): which Whisper models
// are downloaded (core speech::speechModels(), ai::LocalModelFiles in <app data>/models/speech),
// downloading and removing them, the language it listens for and whether a new voice note is
// transcribed by itself — and the transcriptions, one after another on a worker thread: the
// recording decoded to 16 kHz mono (mediaengine.h decodeAudio()), heard by whisper.cpp (core
// speech::SpeechRunner: the plugin brulion_speech, loaded the first time), and the text written
// right under the block that embeds the recording (core media::placeTranscript, through
// Options::place: the shell writes the note). The model stays in memory five minutes after the last
// recording. A build without the plugin (`available` false) offers no transcription.
#pragma once

#include "ai/localfiles.h"
#include "media/transcript.h"
#include "speech/speechrunner.h"

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QVariantList>

#include <atomic>
#include <functional>
#include <memory>

namespace brulion::media {

class TranscriberWorker;

class Transcriber : public QObject {
    Q_OBJECT

    /// This build recognizes speech on the device (it has the plugin).
    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    /// The models to choose from, for a segmented control: {value: id, text: its label}.
    Q_PROPERTY(QVariantList choices READ choices CONSTANT FINAL)
    /// The model chosen (its id; "" never happens: the default's id).
    Q_PROPERTY(QString model READ chosen WRITE setChosen NOTIFY modelChanged FINAL)
    Q_PROPERTY(QString modelName READ modelName NOTIFY modelChanged FINAL)
    /// Its file's size as a text ("264 MB").
    Q_PROPERTY(QString modelSize READ modelSize NOTIFY modelChanged FINAL)
    Q_PROPERTY(QString modelLicense READ modelLicense NOTIFY modelChanged FINAL)
    Q_PROPERTY(QString modelLicenseUrl READ modelLicenseUrl NOTIFY modelChanged FINAL)
    Q_PROPERTY(QString modelPageUrl READ modelPageUrl NOTIFY modelChanged FINAL)
    /// It is downloaded (whole and checked).
    Q_PROPERTY(bool modelHere READ modelHere NOTIFY modelsChanged FINAL)
    /// Any model is downloaded: a recording can be transcribed (with the chosen one, else the
    /// first one here).
    Q_PROPERTY(bool ready READ ready NOTIFY modelsChanged FINAL)
    Q_PROPERTY(bool downloading READ downloadingChosen NOTIFY downloadChanged FINAL)
    Q_PROPERTY(bool downloadingOther READ downloadingOther NOTIFY downloadChanged FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QString progressText READ progressText NOTIFY downloadChanged FINAL)
    Q_PROPERTY(bool checking READ checking NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QString downloadError READ downloadError NOTIFY downloadChanged FINAL)
    /// "auto", "pl" or "en" (speech::speechLanguages()).
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY settingsChanged FINAL)
    /// A voice note just recorded is transcribed by itself (when a model is here).
    Q_PROPERTY(bool automatic READ automatic WRITE setAutomatic NOTIFY settingsChanged FINAL)
    /// Text while recording (livetranscript.h: the recorder's dialog, dictation) — "speech/live",
    /// on by default.
    Q_PROPERTY(bool live READ live WRITE setLive NOTIFY settingsChanged FINAL)
    /// The model text while recording would use heard its windows too late on this device (two of
    /// them more than LiveTranscript::LateMs behind): no text while recording with it until
    /// retryLive().
    Q_PROPERTY(bool liveSlow READ liveSlow NOTIFY settingsChanged FINAL)
    /// Text while recording can be heard: `live` on, a model here (`ready`), not `liveSlow`.
    Q_PROPERTY(bool liveReady READ liveReady NOTIFY liveReadyChanged FINAL)
    /// Something is being transcribed (or waits to be).
    Q_PROPERTY(bool busy READ busy NOTIFY jobsChanged FINAL)
    /// The attachments being transcribed or waiting (vault paths), the first one being heard.
    Q_PROPERTY(QStringList queue READ queue NOTIFY jobsChanged FINAL)
    /// How far the first one is (0–1) and what it does: "decoding" | "loading" | "hearing".
    Q_PROPERTY(double jobProgress READ jobProgress NOTIFY jobsChanged FINAL)
    Q_PROPERTY(QString jobStage READ jobStage NOTIFY jobsChanged FINAL)

public:
    /// Opens a recognizer (the worker thread calls it); nullptr with `error`.
    using Opener =
            std::function<std::unique_ptr<speech::SpeechRunner>(const speech::SpeechRunner::Options &, QString *error)>;
    /// Decodes a recording into 16 kHz mono (the worker thread calls it; default decodeAudio()).
    using Decoder =
            std::function<std::optional<QList<float>>(const QString &file, const std::atomic<bool> *cancel,
                                                      QString *error, const std::function<void(int)> &progress)>;
    /// Writes the transcript — `paragraphs` (media::transcriptParagraphs()) of a recording
    /// `audioMs` long — under the block of `page` that embeds `target` nearest to `line`, each
    /// paragraph of a longer one with a link to its moment; false when the note no longer has it
    /// (the shell: Workspace, media::placeTranscript).
    using Placer = std::function<bool(const QString &page, const QString &target, int line,
                                      const QList<TimedText> &paragraphs, qint64 audioMs)>;

    /// What a window of text while recording was heard as (hearLive()).
    struct LiveHeard {
        bool ok = false;
        QList<speech::Segment> segments; ///< times from the window's start
        QString language;                ///< what it heard ("pl")
        qint64 elapsedMs = 0;            ///< the time it took
        QString modelId;                 ///< the model that heard it
        QString error;
    };

    struct Options {
        QString folder;               ///< where the models' files are kept
        QStringList pluginDirs;       ///< where the plugin may be ("": by its name)
        QList<ai::LocalModel> models; ///< empty: speech::speechModels() (tests: small ones)
        Opener open;                  ///< empty: the plugin (tests: a fake recognizer)
        Decoder decode;               ///< empty: decodeAudio()
        int idleMs = 5 * 60 * 1000;   ///< the model leaves memory after this long without work
        /// The settings (the shell: AppSettings' "speech/…" keys).
        std::function<QVariant(const QString &key, const QVariant &fallback)> setting;
        std::function<void(const QString &key, const QVariant &value)> setSetting;
        /// The file of an attachment (its vault path; the shell: Workspace.assetUrl's local file).
        std::function<QString(const QString &attachment)> filePath;
    };

    explicit Transcriber(const Options &options, QObject *parent = nullptr);
    ~Transcriber() override;

    bool available() const { return m_available; }
    QVariantList choices() const;
    QString chosen() const { return m_chosen; }
    void setChosen(const QString &id);
    QString modelName() const;
    QString modelSize() const;
    QString modelLicense() const;
    QString modelLicenseUrl() const;
    QString modelPageUrl() const;
    bool modelHere() const;
    bool ready() const;
    bool downloadingChosen() const;
    bool downloadingOther() const;
    double progress() const;
    QString progressText() const;
    bool checking() const { return m_files.checking(); }
    QString downloadError() const;
    QString language() const;
    void setLanguage(const QString &language);
    bool automatic() const;
    void setAutomatic(bool on);
    bool live() const;
    void setLive(bool on);
    bool liveSlow() const;
    bool liveReady() const;
    bool busy() const { return !m_jobs.isEmpty(); }
    QStringList queue() const;
    double jobProgress() const { return m_jobProgress; }
    QString jobStage() const { return m_jobStage; }

    Q_INVOKABLE void download();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void remove();

    /// Transcribes the recording `attachment` (its vault path: Options::filePath finds its file)
    /// into `page` under the block that embeds `target` nearest to `line`. False when it cannot (no
    /// plugin, no model here, no such file, already queued).
    Q_INVOKABLE bool transcribe(const QString &attachment, const QString &page, const QString &target, int line);
    /// Transcribes the recording `file` (an absolute path) of no note — dictation's (dictation.h):
    /// heard() when it is done, nothing placed anywhere, the file left for the caller. `language`
    /// ("pl", "en"; "" the setting's) is the language it is heard as. False when it cannot.
    bool transcribeFile(const QString &file, const QString &language = QString());
    /// `attachment` is queued or being heard.
    Q_INVOKABLE bool isQueued(const QString &attachment) const;
    /// Stops the transcription of `attachment` (waiting or being heard).
    Q_INVOKABLE void cancel(const QString &attachment);

    /// Where the placer writes (the shell sets it).
    void setPlacer(Placer place) { m_place = std::move(place); }

    /// Hears a window of a recording still going — `samples` of one channel at `rate` a second
    /// (resampled to 16 kHz on the worker), in `language` ("auto", "pl", "en") — fast rather than
    /// best (speech::SpeechRunner::Params::live), on the worker thread; `done` on the GUI thread
    /// with what it heard, unless `context` went meanwhile. False (and no `done`) when it cannot
    /// now: no `liveReady`, a transcription running or waiting (it goes first), a window being
    /// heard already.
    bool hearLive(const QList<float> &samples, int rate, const QString &language, QObject *context,
                  std::function<void(const LiveHeard &)> done);
    /// A window being heard is not needed any more (the recording ended: its transcript goes
    /// first); its `done` still comes, not ok.
    void cancelLive();
    /// A window is being heard.
    bool hearingLive() const { return m_liveBusy; }
    /// `modelId` heard text while recording too late on this device: `liveSlow` with it.
    void markLiveSlow(const QString &modelId);
    /// Text while recording with the chosen model is tried again (`liveSlow` no more).
    Q_INVOKABLE void retryLive();

Q_SIGNALS:
    void modelChanged();
    void modelsChanged();
    void downloadChanged();
    void settingsChanged();
    void jobsChanged();
    void liveReadyChanged();
    /// A transcription ended: `ok` with the text placed (`text`), else `error` ("" cancelled).
    void transcribed(const QString &attachment, const QString &page, bool ok, const QString &text,
                     const QString &error);
    /// A transcription of transcribeFile() ended: `ok` with its text, else `error` ("" cancelled).
    void heard(const QString &file, bool ok, const QString &text, const QString &error);

private:
    struct Job {
        QString attachment;
        QString file;
        QString page;
        QString target;
        int line = -1;
        std::shared_ptr<std::atomic<bool>> cancel;
        bool place = true;  ///< a voice note's (placed in its page); false: transcribeFile()'s
        QString language{}; ///< "" the setting's
    };
    /// The end of `job` said: transcribed() of a voice note, heard() of a file.
    void tell(const Job &job, bool ok, const QString &text, const QString &error);
    const ai::LocalModel *model(const QString &id) const;
    /// The model a transcription uses: the chosen one when here, else the first one here.
    const ai::LocalModel *usable() const;
    void next();
    void onProgress(const QString &stage, double progress);
    void onDone(const QString &attachment, bool ok, const QString &text, const QList<TimedText> &paragraphs,
                qint64 audioMs, const QString &error);

    QList<ai::LocalModel> m_models;
    ai::LocalModelFiles m_files;
    bool m_available = false;
    QString m_chosen;
    QString m_downloadError;
    QString m_downloadErrorId;
    Options m_options;
    Placer m_place;
    QList<Job> m_jobs;
    bool m_running = false;
    double m_jobProgress = 0;
    QString m_jobStage;
    QThread m_thread;
    TranscriberWorker *m_worker = nullptr;
    bool m_liveBusy = false;
    std::shared_ptr<std::atomic<bool>> m_liveCancel;
    /// The models text while recording was too slow with ("speech/liveSlow").
    QStringList slowModels() const;
};

} // namespace brulion::media
