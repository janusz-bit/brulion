#include "transcriber.h"

#include "mediaengine.h"

#include "media/transcript.h"
#include "speech/speech.h"

#include <QFileInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::media {

Q_LOGGING_CATEGORY(lcSpeech, "brulion.speech")

namespace {

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString seconds(qint64 ms)
{
    return QString::number(double(ms) / 1000.0, 'f', 1);
}

} // namespace

/// The recognizer in memory, on the worker thread: opened for the first recording, kept for the
/// next, let go after a while without one.
class TranscriberWorker : public QObject {
public:
    struct Job {
        QString attachment;
        QString file;
        QString modelPath;
        QString modelId;
        QString language;
        std::shared_ptr<std::atomic<bool>> cancel;
    };
    struct Result {
        bool ok = false;
        QString text;
        QString error;
    };

    TranscriberWorker(Transcriber::Opener open, Transcriber::Decoder decode, int idleMs)
        : m_open(std::move(open)), m_decode(std::move(decode))
    {
        m_idle.setParent(this);
        m_idle.setSingleShot(true);
        m_idle.setInterval(idleMs);
        connect(&m_idle, &QTimer::timeout, this, [this] { close(); });
    }

    Result run(const Job &job, const std::function<void(const QString &, double)> &progress)
    {
        Result r;
        m_idle.stop();
        QElapsedTimer clock;
        clock.start();
        progress(u"decoding"_s, 0);
        QString error;
        const std::optional<QList<float>> samples = m_decode(
                job.file, job.cancel.get(), &error, [&](int percent) { progress(u"decoding"_s, percent / 100.0); });
        if (job.cancel->load()) {
            m_idle.start();
            return r; // (cancelled: no error)
        }
        if (!samples) {
            r.error = error;
            m_idle.start();
            return r;
        }
        const qint64 decodedMs = clock.elapsed();
        if (!m_runner || m_openPath != job.modelPath) {
            progress(u"loading"_s, 0);
            close();
            speech::SpeechRunner::Options options;
            options.modelPath = job.modelPath;
            QElapsedTimer opening;
            opening.start();
            m_runner = m_open(options, &error);
            if (!m_runner) {
                qCWarning(lcSpeech).noquote() << u"cannot open %1: %2"_s.arg(job.modelId, error);
                r.error = error.isEmpty() ? u"the speech model cannot be opened"_s : error;
                m_idle.start();
                return r;
            }
            m_openPath = job.modelPath;
            qCInfo(lcSpeech).noquote() << u"%1 opened: %2, in %3 s"_s.arg(job.modelId, m_runner->device(),
                                                                          seconds(opening.elapsed()));
        }
        speech::SpeechRunner::Params params;
        params.language = job.language;
        params.cancel = job.cancel.get();
        params.progress = [&](int percent) { progress(u"hearing"_s, percent / 100.0); };
        progress(u"hearing"_s, 0);
        const speech::SpeechRunner::Result heard = m_runner->transcribe(*samples, params);
        m_idle.start();
        if (heard.status == speech::SpeechRunner::Cancelled || job.cancel->load())
            return r;
        if (heard.status != speech::SpeechRunner::Ok) {
            r.error = heard.error;
            return r;
        }
        r.text = transcriptText(speech::spokenTexts(heard.segments));
        r.ok = true;
        qCInfo(lcSpeech).noquote() << u"%1 heard %2 s of sound (%3) in %4 s (decoded in %5 s): %6 characters"_s
                                              .arg(job.modelId, seconds(heard.audioMs), heard.language,
                                                   seconds(heard.elapsedMs), seconds(decodedMs))
                                              .arg(r.text.size());
        return r;
    }

    void close()
    {
        if (!m_runner)
            return;
        m_idle.stop();
        qCInfo(lcSpeech) << "the speech model left memory";
        m_runner.reset();
        m_openPath.clear();
    }

private:
    Transcriber::Opener m_open;
    Transcriber::Decoder m_decode;
    std::unique_ptr<speech::SpeechRunner> m_runner;
    QString m_openPath;
    QTimer m_idle;
};

Transcriber::Transcriber(const Options &options, QObject *parent)
    : QObject(parent)
    , m_models(options.models.isEmpty() ? speech::speechModels() : options.models)
    , m_files(options.folder)
    , m_options(options)
{
    Opener open = options.open;
    if (open) {
        m_available = true;
    } else {
        const QStringList dirs = options.pluginDirs;
        m_available = speech::hasSpeechRunner(dirs);
        open = [dirs](const speech::SpeechRunner::Options &o, QString *error) {
            return speech::loadSpeechRunner(dirs, o, error);
        };
    }
    Decoder decode = options.decode;
    if (!decode)
        decode = [](const QString &file, const std::atomic<bool> *cancel, QString *error,
                    const std::function<void(int)> &progress) { return decodeAudio(file, cancel, error, progress); };
    m_chosen = m_models.isEmpty() ? QString() : m_models.first().id;
    if (m_options.setting) {
        const QString saved = m_options.setting(u"speech/model"_s, QString()).toString();
        if (!saved.isEmpty() && model(saved))
            m_chosen = saved;
    }
    m_files.adopt(m_models);

    connect(&m_files, &ai::LocalModelFiles::progress, this, &Transcriber::downloadChanged);
    connect(&m_files, &ai::LocalModelFiles::changed, this, &Transcriber::modelsChanged);
    connect(&m_files, &ai::LocalModelFiles::finished, this, [this](const QString &id, bool ok, const QString &error) {
        if (ok)
            qCInfo(lcSpeech).noquote() << u"%1 downloaded"_s.arg(id);
        else if (!error.isEmpty())
            qCWarning(lcSpeech).noquote() << u"%1 download failed: %2"_s.arg(id, error);
        m_downloadError = ok ? QString() : error;
        m_downloadErrorId = id;
        Q_EMIT downloadChanged();
        Q_EMIT modelsChanged();
    });

    m_worker = new TranscriberWorker(std::move(open), std::move(decode), options.idleMs);
    if (m_available) {
        m_worker->moveToThread(&m_thread);
        m_thread.setObjectName(u"brulion-speech"_s);
        m_thread.start();
    }
}

Transcriber::~Transcriber()
{
    for (const Job &job : std::as_const(m_jobs))
        job.cancel->store(true);
    if (m_thread.isRunning()) {
        // The worker goes on its own thread (its timer is that thread's), after the recording
        // being heard ends — it was told to stop above.
        QMetaObject::invokeMethod(m_worker, [worker = m_worker] { delete worker; }, Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
    } else {
        delete m_worker;
    }
}

const ai::LocalModel *Transcriber::model(const QString &id) const
{
    if (id.isEmpty())
        return m_models.isEmpty() ? nullptr : &m_models.first();
    for (const ai::LocalModel &m : m_models) {
        if (m.id == id)
            return &m;
    }
    return nullptr;
}

const ai::LocalModel *Transcriber::usable() const
{
    if (const ai::LocalModel *m = model(m_chosen); m && m_files.has(*m))
        return m;
    for (const ai::LocalModel &m : m_models) {
        if (m_files.has(m))
            return &m;
    }
    return nullptr;
}

QVariantList Transcriber::choices() const
{
    QVariantList out;
    for (const ai::LocalModel &m : m_models)
        out.append(QVariantMap{{u"value"_s, m.id}, {u"text"_s, m.label.isEmpty() ? m.name : m.label}});
    return out;
}

void Transcriber::setChosen(const QString &id)
{
    const ai::LocalModel *m = model(id);
    if (!m)
        m = model(QString());
    const QString chosen = m ? m->id : QString();
    if (chosen == m_chosen)
        return;
    m_chosen = chosen;
    if (m_options.setSetting)
        m_options.setSetting(u"speech/model"_s, chosen);
    Q_EMIT modelChanged();
    Q_EMIT modelsChanged();
    Q_EMIT downloadChanged();
}

QString Transcriber::modelName() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m ? m->name : QString();
}

QString Transcriber::modelSize() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m ? sizeText(m->bytes()) : QString();
}

QString Transcriber::modelLicense() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m ? m->license : QString();
}

QString Transcriber::modelLicenseUrl() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m ? m->licenseUrl.toString() : QString();
}

QString Transcriber::modelPageUrl() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m ? m->pageUrl.toString() : QString();
}

bool Transcriber::modelHere() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m && m_files.has(*m);
}

bool Transcriber::ready() const
{
    return m_available && usable();
}

bool Transcriber::downloadingChosen() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m && m_files.downloading() == m->id;
}

bool Transcriber::downloadingOther() const
{
    return !m_files.downloading().isEmpty() && !downloadingChosen();
}

double Transcriber::progress() const
{
    if (!downloadingChosen())
        return 0.0;
    return m_files.total() > 0 ? double(m_files.received()) / double(m_files.total()) : 0.0;
}

QString Transcriber::progressText() const
{
    if (!downloadingChosen())
        return QString();
    return tr("%1 of %2").arg(sizeText(m_files.received()), sizeText(m_files.total()));
}

QString Transcriber::downloadError() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m && m->id == m_downloadErrorId ? m_downloadError : QString();
}

QString Transcriber::language() const
{
    return speech::knownLanguage(m_options.setting ? m_options.setting(u"speech/language"_s, u"auto"_s).toString()
                                                   : u"auto"_s);
}

void Transcriber::setLanguage(const QString &language)
{
    const QString value = speech::knownLanguage(language);
    if (value == this->language())
        return;
    if (m_options.setSetting)
        m_options.setSetting(u"speech/language"_s, value);
    Q_EMIT settingsChanged();
}

bool Transcriber::automatic() const
{
    return m_options.setting ? m_options.setting(u"speech/automatic"_s, true).toBool() : true;
}

void Transcriber::setAutomatic(bool on)
{
    if (on == automatic())
        return;
    if (m_options.setSetting)
        m_options.setSetting(u"speech/automatic"_s, on);
    Q_EMIT settingsChanged();
}

QStringList Transcriber::queue() const
{
    QStringList out;
    for (const Job &job : m_jobs)
        out.append(job.attachment);
    return out;
}

void Transcriber::download()
{
    const ai::LocalModel *m = model(m_chosen);
    if (!m || !m_files.downloading().isEmpty())
        return;
    m_downloadError.clear();
    m_downloadErrorId.clear();
    m_files.download(*m);
    Q_EMIT downloadChanged();
}

void Transcriber::cancelDownload()
{
    m_files.cancel();
}

void Transcriber::remove()
{
    const ai::LocalModel *m = model(m_chosen);
    if (!m)
        return;
    const ai::LocalModel removed = *m;
    // Transcriptions with it stop; the recognizer leaves memory first, then its file goes.
    for (const Job &job : std::as_const(m_jobs))
        job.cancel->store(true);
    if (!m_thread.isRunning()) {
        m_files.remove(removed, m_models);
        return;
    }
    QMetaObject::invokeMethod(m_worker, [this, worker = m_worker, removed] {
        worker->close();
        QMetaObject::invokeMethod(this, [this, removed] { m_files.remove(removed, m_models); }, Qt::QueuedConnection);
    });
}

bool Transcriber::isQueued(const QString &attachment) const
{
    return std::ranges::any_of(m_jobs, [&](const Job &job) { return job.attachment == attachment; });
}

bool Transcriber::transcribe(const QString &attachment, const QString &page, const QString &target, int line)
{
    const QString file = m_options.filePath ? m_options.filePath(attachment) : attachment;
    if (!m_available || !usable() || attachment.isEmpty() || isQueued(attachment) || !QFileInfo(file).isFile())
        return false;
    m_jobs.append(Job{attachment, file, page, target, line, std::make_shared<std::atomic<bool>>(false)});
    Q_EMIT jobsChanged();
    next();
    return true;
}

bool Transcriber::transcribeFile(const QString &file, const QString &language)
{
    if (!m_available || !usable() || file.isEmpty() || isQueued(file) || !QFileInfo(file).isFile())
        return false;
    Job job{file, file, QString(), QString(), -1, std::make_shared<std::atomic<bool>>(false)};
    job.place = false;
    job.language = language;
    m_jobs.append(job);
    Q_EMIT jobsChanged();
    next();
    return true;
}

void Transcriber::tell(const Job &job, bool ok, const QString &text, const QString &error)
{
    if (job.place)
        Q_EMIT transcribed(job.attachment, job.page, ok, text, error);
    else
        Q_EMIT heard(job.file, ok, text, error);
}

void Transcriber::cancel(const QString &attachment)
{
    for (qsizetype i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs.at(i).attachment != attachment)
            continue;
        m_jobs.at(i).cancel->store(true);
        // One waiting goes at once; the one being heard ends when the recognizer sees it.
        if (i > 0 || !m_running) {
            const Job job = m_jobs.takeAt(i);
            Q_EMIT jobsChanged();
            if (job.place)
                Q_EMIT transcribed(attachment, QString(), false, QString(), QString());
            else
                Q_EMIT heard(job.file, false, QString(), QString());
        }
        return;
    }
}

void Transcriber::next()
{
    if (m_running || m_jobs.isEmpty())
        return;
    const ai::LocalModel *m = usable();
    if (!m) {
        // The model went meanwhile: what waits cannot be heard.
        const QList<Job> jobs = std::exchange(m_jobs, {});
        Q_EMIT jobsChanged();
        for (const Job &job : jobs)
            tell(job, false, QString(), tr("No speech model is on this device."));
        return;
    }
    m_running = true;
    m_jobProgress = 0;
    m_jobStage = u"decoding"_s;
    Q_EMIT jobsChanged();
    const Job &job = m_jobs.first();
    TranscriberWorker::Job work{job.attachment, job.file, m_files.pathOf(m->file), m->id,
                                job.language.isEmpty() ? language() : speech::knownLanguage(job.language), job.cancel};
    QMetaObject::invokeMethod(m_worker, [this, worker = m_worker, work] {
        const TranscriberWorker::Result r = worker->run(work, [this](const QString &stage, double progress) {
            QMetaObject::invokeMethod(
                    this, [this, stage, progress] { onProgress(stage, progress); }, Qt::QueuedConnection);
        });
        QMetaObject::invokeMethod(
                this, [this, attachment = work.attachment, r] { onDone(attachment, r.ok, r.text, r.error); },
                Qt::QueuedConnection);
    });
}

void Transcriber::onProgress(const QString &stage, double progress)
{
    if (!m_running)
        return;
    // (At most a hundredth at a time: the bindings of a progress bar, not of every sample.)
    if (stage == m_jobStage && std::abs(progress - m_jobProgress) < 0.01 && progress < 1)
        return;
    m_jobStage = stage;
    m_jobProgress = progress;
    Q_EMIT jobsChanged();
}

void Transcriber::onDone(const QString &attachment, bool ok, const QString &text, const QString &error)
{
    m_running = false;
    Job job;
    if (!m_jobs.isEmpty() && m_jobs.first().attachment == attachment)
        job = m_jobs.takeFirst();
    m_jobProgress = 0;
    m_jobStage.clear();
    Q_EMIT jobsChanged();
    const bool cancelled = job.cancel && job.cancel->load();
    if (!job.place) {
        // A file of no note (dictation): its text, nothing placed.
        if (cancelled)
            tell(job, false, QString(), QString());
        else if (!ok)
            tell(job, false, QString(), error);
        else if (text.isEmpty())
            tell(job, false, QString(), tr("No speech was heard in the recording."));
        else
            tell(job, true, text, QString());
    } else if (cancelled) {
        Q_EMIT transcribed(attachment, job.page, false, QString(), QString());
    } else if (!ok) {
        Q_EMIT transcribed(attachment, job.page, false, QString(), error);
    } else if (text.isEmpty()) {
        Q_EMIT transcribed(attachment, job.page, false, QString(), tr("No speech was heard in the recording."));
    } else if (m_place && !m_place(job.page, job.target, job.line, text)) {
        Q_EMIT transcribed(attachment, job.page, false, text, tr("The note no longer has the recording."));
    } else {
        Q_EMIT transcribed(attachment, job.page, true, text, QString());
    }
    next();
}

} // namespace brulion::media
