#include "localai.h"

#include "log/slowscope.h"

#include <QDir>
#include <QElapsedTimer>
#include <QLocale>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

Q_LOGGING_CATEGORY(lcLocalAi, "brulion.ai.local")

namespace {
/// "1.42": seconds as the log writes them.
QString seconds(qint64 ms)
{
    return QString::number(double(ms) / 1000.0, 'f', 2);
}
} // namespace

/// The model in memory, on the worker thread: opened for the first request, kept for the next,
/// let go after a while without one.
class LocalAiWorker : public QObject {
public:
    struct Job {
        ai::LocalModel model;
        ai::LocalRunner::Options options;
        ai::Request request;
        std::shared_ptr<std::atomic<bool>> stopped; ///< set: it stops (never null)
        ai::PartialCallback partial;                ///< on this thread (none: nobody watches)
    };

    LocalAiWorker(LocalAi::Opener open, int idleMs) : m_open(std::move(open))
    {
        m_idle.setParent(this); // moves to the worker thread with this object
        m_idle.setSingleShot(true);
        m_idle.setInterval(idleMs);
        connect(&m_idle, &QTimer::timeout, this, [this] { close(QString()); });
    }

    ai::Reply answer(const Job &job)
    {
        if (job.stopped->load()) {
            ai::Reply stopped; // (stopped while it waited: the model is not even opened)
            stopped.error = u"stopped"_s;
            return stopped;
        }
        m_idle.stop();
        QElapsedTimer asked;
        asked.start();
        if (!m_runner || m_openId != job.model.id) {
            close(QString());
            QString error;
            QElapsedTimer opening;
            opening.start();
            m_runner = m_open(job.options, &error);
            if (!m_runner) {
                qCWarning(lcLocalAi).noquote() << u"cannot open %1: %2"_s.arg(job.model.id, error);
                ai::Reply failed;
                failed.error = error.isEmpty() ? u"the model cannot be opened"_s : error;
                return failed;
            }
            m_openId = job.model.id;
            qCInfo(lcLocalAi).noquote() << u"%1 opened: %2 tokens at once, %3, in %4 s"_s.arg(job.model.id)
                                                   .arg(m_runner->contextTokens())
                                                   .arg(m_runner->device(), seconds(opening.elapsed()));
        }
        // The times are what tells a slow device from a long prompt: of the tokens read, those
        // kept from the request before and those of the state on disk cost (almost) nothing.
        // (And how long the first word took to show: what a user waits for.)
        ai::LocalRunner::Result run;
        qint64 firstMs = -1;
        ai::PartialCallback partial;
        if (job.partial)
            partial = [&](const ai::Partial &p) {
                if (firstMs < 0)
                    firstMs = asked.elapsed();
                job.partial(p);
            };
        const ai::Reply reply =
                ai::answerLocally(m_runner.get(), job.model, job.request, job.stopped.get(), &run, partial);
        // (The tools it called by their names: an agent's turns are told apart by them.)
        QStringList called;
        for (const ai::ToolCall &call : reply.toolCalls)
            called << call.name;
        if (reply.ok)
            qCInfo(lcLocalAi).noquote()
                    << u"%1 answered: %2 tokens read in %3 s (%4 kept from before, %5 from the disk), %6 written in %7 s%8%9"_s
                               .arg(job.model.id)
                               .arg(run.promptTokens)
                               .arg(seconds(run.promptMs))
                               .arg(run.reusedTokens)
                               .arg(run.storedTokens)
                               .arg(run.outputTokens)
                               .arg(seconds(run.outputMs))
                               .arg(firstMs >= 0 ? u", shown from %1 s"_s.arg(seconds(firstMs)) : QString(),
                                    called.isEmpty() ? QString() : u", called "_s + called.join(u", "_s));
        else
            qCInfo(lcLocalAi).noquote() << u"%1 gave no answer: %2"_s.arg(job.model.id, reply.error);
        m_idle.start();
        return reply;
    }

    /// Lets the model `id` go ("": whichever is open).
    void close(const QString &id)
    {
        if (!m_runner || (!id.isEmpty() && id != m_openId))
            return;
        m_idle.stop();
        qCInfo(lcLocalAi).noquote() << u"%1 left memory"_s.arg(m_openId);
        m_runner.reset();
        m_openId.clear();
    }

private:
    LocalAi::Opener m_open;
    std::unique_ptr<ai::LocalRunner> m_runner;
    QString m_openId;
    QTimer m_idle;
};

LocalAi::LocalAi(const Options &options, QObject *parent)
    : QObject(parent)
    , m_models(options.models.isEmpty() ? ai::localModels() : options.models)
    , m_files(options.folder)
    , m_stateFolder(options.stateFolder)
    , m_spinWait(options.spinWait)
{
    Opener open = options.open;
    if (open) {
        m_available = true;
    } else {
        const QStringList dirs = options.pluginDirs;
        m_available = ai::hasLocalRunner(dirs);
        open = [dirs](const ai::LocalRunner::Options &o, QString *error) {
            return ai::loadLocalRunner(dirs, o, error);
        };
    }
    m_contextTokens = ContextTokens;
    m_formerDefault = options.formerDefault;
    if (!m_models.isEmpty())
        m_chosen = m_models.first().id;
    m_files.adopt(m_models); // (models downloaded before they were marked so)

    connect(&m_files, &ai::LocalModelFiles::progress, this, &LocalAi::downloadChanged);
    connect(&m_files, &ai::LocalModelFiles::changed, this, &LocalAi::modelsChanged);
    connect(&m_files, &ai::LocalModelFiles::finished, this, [this](const QString &id, bool ok, const QString &error) {
        const QString took = logging::formatMs(m_downloadClock.elapsed());
        if (ok)
            qCInfo(lcLocalAi).noquote() << u"%1 downloaded: %2 in %3"_s.arg(id, logging::formatBytes(m_downloadBytes),
                                                                            took);
        else if (error.isEmpty()) // stopped by the user, or removed meanwhile
            qCInfo(lcLocalAi).noquote() << u"%1 download stopped after %2"_s.arg(id, took);
        else
            qCWarning(lcLocalAi).noquote() << u"%1 download failed after %2: %3"_s.arg(id, took, error);
        m_downloadError = ok ? QString() : error;
        m_downloadErrorId = id;
        Q_EMIT downloadChanged();
        Q_EMIT modelsChanged(); // what is here of it changed, also when it broke off
        Q_EMIT downloadFinished(id, ok, error);
    });

    connect(this, &LocalAi::busyChanged, this, &LocalAi::updateWorking);
    connect(this, &LocalAi::downloadChanged, this, &LocalAi::updateWorking);

    // The worker thread only where a model can run at all (never in a build without the plugin —
    // the browser's Qt has no threads either).
    m_worker = new LocalAiWorker(std::move(open), options.idleMs);
    if (m_available) {
        m_worker->moveToThread(&m_thread);
        m_thread.setObjectName(u"brulion-local-ai"_s);
        m_thread.start();
    }
}

LocalAi::~LocalAi()
{
    // A request being answered stops; the ones still waiting are dropped with the thread.
    stopAll();
    if (m_thread.isRunning()) {
        m_thread.quit();
        m_thread.wait();
    }
    delete m_worker;
}

int LocalAi::imageTokensFor(int contextTokens)
{
    return std::clamp(contextTokens / 8, 256, 1024);
}

const ai::LocalModel *LocalAi::model(const QString &id) const
{
    if (id.isEmpty())
        return m_models.isEmpty() ? nullptr : &m_models.first();
    for (const ai::LocalModel &m : m_models) {
        if (m.id == id)
            return &m;
    }
    return nullptr;
}

const ai::LocalModel *LocalAi::seer(bool here) const
{
    for (const ai::LocalModel &m : m_models) {
        if (m.sees() && (!here || m_files.has(m)))
            return &m;
    }
    return nullptr;
}

namespace {
QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}
} // namespace

QVariantList LocalAi::choices() const
{
    QVariantList out;
    for (const ai::LocalModel &m : m_models)
        out.append(QVariantMap{{u"value"_s, m.id}, {u"text"_s, m.label.isEmpty() ? m.name : m.label}});
    return out;
}

QString LocalAi::keptChoice() const
{
    if (m_formerDefault.isEmpty() || !model(m_formerDefault) || model(m_formerDefault) == model(QString()))
        return QString();
    return has(m_formerDefault) && !has(QString()) ? m_formerDefault : QString();
}

void LocalAi::setChosen(const QString &id)
{
    // The default model has an id of its own: "" and it are the same choice — and so is a model
    // the app does not know (a setting left by another version).
    const ai::LocalModel *m = model(id);
    if (!m)
        m = model(QString());
    const QString chosen = m ? m->id : QString();
    if (chosen == m_chosen)
        return;
    m_chosen = chosen;
    Q_EMIT modelChanged();
    Q_EMIT modelsChanged();
    Q_EMIT downloadChanged();
}

QString LocalAi::modelName() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m ? m->name : QString();
}

QString LocalAi::modelMaker() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m ? m->maker : QString();
}

QString LocalAi::modelSize() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m ? sizeText(m->bytes()) : QString();
}

bool LocalAi::modelSees() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m && m->sees();
}

QString LocalAi::pictureModel() const
{
    const ai::LocalModel *m = model(m_chosen);
    const ai::LocalModel *other = m && !m->sees() ? seer(false) : nullptr;
    return other ? other->name : QString();
}

bool LocalAi::pictureModelHere() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m && !m->sees() && seer(true);
}

bool LocalAi::modelAdapted() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m && m->adapted();
}

QString LocalAi::modelBase() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m ? m->baseName : QString();
}

QString LocalAi::modelBaseMaker() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m ? m->baseMaker : QString();
}

QString LocalAi::modelLicense() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m ? m->license : QString();
}

QString LocalAi::modelLicenseUrl() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m ? m->licenseUrl.toString() : QString();
}

QString LocalAi::modelPageUrl() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m ? m->pageUrl.toString() : QString();
}

QString LocalAi::modelLeft() const
{
    const ai::LocalModel *m = model(m_chosen);
    if (!m)
        return QString();
    const qint64 left = m->bytes() - m_files.bytesHere(*m);
    return left > 0 ? sizeText(left) : QString();
}

QString LocalAi::modelSharesWith() const
{
    const ai::LocalModel *m = model(m_chosen);
    if (!m)
        return QString();
    QStringList names;
    const QList<ai::LocalFile> files = m->files();
    for (const ai::LocalModel &other : m_models) {
        if (other.id == m->id)
            continue;
        const QList<ai::LocalFile> its = other.files();
        const bool shares = std::ranges::any_of(files, [&](const ai::LocalFile &file) {
            return std::ranges::any_of(its, [&](const ai::LocalFile &f) { return f.name == file.name; });
        });
        if (shares)
            names << other.name;
    }
    return QLocale().createSeparatedList(names);
}

bool LocalAi::downloadingChosen() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m && m_files.downloading() == m->id;
}

QString LocalAi::downloadError() const
{
    const ai::LocalModel *m = model(m_chosen);
    return m && m->id == m_downloadErrorId ? m_downloadError : QString();
}

bool LocalAi::downloadingOther() const
{
    return !m_files.downloading().isEmpty() && !downloadingChosen();
}

double LocalAi::progress() const
{
    if (!downloadingChosen())
        return 0.0;
    return m_files.total() > 0 ? double(m_files.received()) / double(m_files.total()) : 0.0;
}

QString LocalAi::progressText() const
{
    if (!downloadingChosen())
        return QString();
    return tr("%1 of %2").arg(sizeText(m_files.received()), sizeText(m_files.total()));
}

bool LocalAi::has(const QString &id) const
{
    const ai::LocalModel *m = model(id);
    return m && m_files.has(*m);
}

void LocalAi::download(const QString &id)
{
    const ai::LocalModel *m = model(id);
    if (!m || !m_files.downloading().isEmpty())
        return;
    m_downloadError.clear();
    m_downloadErrorId.clear();
    // Said before it starts: a download that cannot begin (no room) ends at once, and says so.
    m_downloadBytes = m->bytes() - m_files.bytesHere(*m);
    m_downloadClock.start();
    qCInfo(lcLocalAi).noquote() << u"%1 download: %2 to fetch of %3"_s.arg(m->id, logging::formatBytes(m_downloadBytes),
                                                                           logging::formatBytes(m->bytes()));
    m_files.download(*m);
    Q_EMIT downloadChanged();
}

void LocalAi::cancelDownload()
{
    m_files.cancel();
}

void LocalAi::remove(const QString &id)
{
    const ai::LocalModel *m = model(id);
    if (!m)
        return;
    const ai::LocalModel removed = *m;
    if (!m_thread.isRunning()) {
        m_files.remove(removed, m_models); // no model ever runs here: only its files
        return;
    }
    // The model leaves memory first (the requests not answered yet end: "stopped"), then its
    // files go — and the states kept on disk (of any model: the next request keeps them again).
    stopAll();
    QMetaObject::invokeMethod(m_worker, [this, worker = m_worker, removed, states = m_stateFolder] {
        worker->close(removed.id);
        if (!states.isEmpty())
            QDir(states).removeRecursively();
        QMetaObject::invokeMethod(this, [this, removed] { m_files.remove(removed, m_models); }, Qt::QueuedConnection);
    });
}

void LocalAi::updateWorking()
{
    const bool working = m_pending > 0 || !m_files.downloading().isEmpty();
    if (working == m_working)
        return;
    m_working = working;
    Q_EMIT workingChanged();
}

void LocalAi::finished(std::function<void(const ai::Reply &)> done, const ai::Reply &reply)
{
    m_pending = std::max(0, m_pending - 1);
    Q_EMIT busyChanged();
    done(reply);
}

void LocalAi::stopAll()
{
    for (const std::weak_ptr<std::atomic<bool>> &stop : std::as_const(m_stops)) {
        if (const std::shared_ptr<std::atomic<bool>> flag = stop.lock())
            flag->store(true);
    }
    m_stops.clear();
}

void LocalAi::send(const ai::Settings &settings, const ai::Request &request, std::function<void(const ai::Reply &)> done,
                   ai::PartialCallback partial, std::shared_ptr<std::atomic<bool>> stopped)
{
    const auto refuse = [&](const QString &why) {
        ai::Reply reply;
        reply.error = why;
        QTimer::singleShot(0, this, [done = std::move(done), reply] { done(reply); });
    };
    const ai::LocalModel *m = model(settings.model);
    if (!m)
        return refuse(u"no such model on this device: \"%1\" (Settings → Extensions → AI)"_s.arg(settings.model));
    if (!m_available)
        return refuse(u"this build of Brulion runs no model on the device"_s);
    if (ai::local::hasImages(request) && !m->sees()) {
        // A picture: the model that sees looks at it in this one's place (a phone holds one).
        const ai::LocalModel *other = seer(true);
        if (!other) {
            const ai::LocalModel *any = seer(false);
            return refuse(any ? u"the model on this device (%1) does not see pictures: download %2 too in Settings → "
                                 u"Extensions → AI, and it looks at them in its place"_s.arg(m->name, any->name)
                              : u"the model on this device (%1) does not see pictures"_s.arg(m->name));
        }
        qCInfo(lcLocalAi).noquote() << u"%1 does not see pictures: %2 looks at them"_s.arg(m->id, other->id);
        m = other;
    }
    if (!m_files.has(*m))
        return refuse(
                u"the model %1 is not on this device yet: download it in Settings → Extensions → AI"_s.arg(m->name));

    LocalAiWorker::Job job;
    job.model = *m;
    job.options.modelPath = m_files.pathOf(m->file);
    if (m->adapted())
        job.options.adapterPath = m_files.pathOf(m->adapter);
    if (m->sees())
        job.options.projectorPath = m_files.pathOf(m->projector);
    job.options.contextTokens = m_contextTokens;
    job.options.imageTokens = imageTokensFor(m_contextTokens);
    job.options.stateDir = m_stateFolder;
    job.options.spinWait = m_spinWait;
    job.request = request;
    if (!m->thinks)
        job.request.effort = u"low"_s; // (a model trained to answer at once)
    job.stopped = stopped ? std::move(stopped) : std::make_shared<std::atomic<bool>>(false);
    m_stops.removeIf([](const std::weak_ptr<std::atomic<bool>> &stop) { return stop.expired(); });
    m_stops.append(job.stopped);
    if (partial) {
        // From the worker to the event loop: the latest only, when they come faster than they are
        // read (a busy screen), never one after the answer (that comes after them).
        struct Latest {
            QMutex mutex;
            ai::Partial value;
            bool posted = false;
        };
        const auto latest = std::make_shared<Latest>();
        job.partial = [this, latest, partial = std::move(partial)](const ai::Partial &p) {
            {
                const QMutexLocker lock(&latest->mutex);
                latest->value = p;
                if (std::exchange(latest->posted, true))
                    return;
            }
            QMetaObject::invokeMethod(
                    this,
                    [latest, partial] {
                        ai::Partial value;
                        {
                            const QMutexLocker lock(&latest->mutex);
                            value = latest->value;
                            latest->posted = false;
                        }
                        partial(value);
                    },
                    Qt::QueuedConnection);
        };
    }

    ++m_pending;
    Q_EMIT busyChanged();
    QMetaObject::invokeMethod(m_worker, [this, worker = m_worker, job, done = std::move(done)] {
        const ai::Reply reply = worker->answer(job);
        QMetaObject::invokeMethod(this, [this, done, reply] { finished(done, reply); }, Qt::QueuedConnection);
    });
}

} // namespace brulion::shell
