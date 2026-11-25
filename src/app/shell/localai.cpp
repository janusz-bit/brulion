#include "localai.h"

#include "log/slowscope.h"
#include "memorypolicy.h"

#include "ai/gguf.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStorageInfo>
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

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

/// A file name as a copy in the models folder takes it: letters, digits, '.', '-', '_'.
QString safeName(const QString &name)
{
    QString out;
    for (const QChar c : name)
        out += c.isLetterOrNumber() && c.unicode() < 128 ? c : (c == u'.' || c == u'-' || c == u'_') ? c : u'_';
    if (!out.endsWith(u".gguf"_s, Qt::CaseInsensitive))
        out += u".gguf"_s;
    return out.right(120);
}
} // namespace

/// A file of the user's own being added (LocalAi::addModelFile): read and copied on a thread of its
/// own, then loaded once by the runner on the worker thread.
struct LocalAiAdding {
    bool projector = false;
    QString source; ///< what was picked: a path, or a content: URL
    QString name;   ///< its file's name, as shown
    QString folder; ///< where a copy goes ("": the file is used where it is)
    QString target; ///< a projector's model
    qint64 memory = 0;
    int contextTokens = LocalAi::ContextTokens;
    std::atomic<bool> cancel{false};
    std::atomic<bool> probing{false};
    std::atomic<qint64> done{0};
    std::atomic<qint64> total{0};
    // What it gave (written by its thread before it ends).
    QString error;
    ai::OwnModel model; ///< a model's
    QString path;       ///< where the file is now
    qint64 bytes = 0;
    bool copied = false;
};

namespace {

/// Reads, checks and (where the app copies files) copies `job`'s file; on its own thread.
/// `owner` hears of the copy's progress.
void runAdding(LocalAiAdding &job, LocalAi *owner)
{
    QFile in(job.source);
    if (!in.open(QIODevice::ReadOnly)) {
        job.error = LocalAi::tr("The file cannot be read (%1).").arg(in.errorString());
        return;
    }
    const ai::GgufInfo info = ai::readGguf(&in, in.size());
    QString why;
    if (job.projector) {
        if (!ai::isProjector(info, &why)) {
            job.error = why;
            return;
        }
    } else {
        std::optional<ai::OwnModel> model = ai::ownModelFrom(job.source, info, &why);
        if (!model) {
            job.error = why;
            return;
        }
        // Its memory: the weights, the context's cells it holds here, some buffers — refused
        // where it would leave the device too little.
        const int context = model->contextLength > 0 ? std::clamp(model->contextLength, 2048, job.contextTokens)
                                                     : job.contextTokens;
        model->memory = ai::memoryNeeded(info, context);
        if (job.memory > 0 && model->memory > job.memory * 85 / 100) {
            job.error = LocalAi::tr("It needs some %1 of memory, and this device has %2: too much for it.")
                                .arg(sizeText(model->memory), sizeText(job.memory));
            return;
        }
        // (A document's own name, not its URL's.)
        if (model->name == QFileInfo(job.source).completeBaseName() && !job.name.isEmpty())
            model->name = QFileInfo(job.name).completeBaseName().left(80);
        job.model = *model;
    }
    job.bytes = info.fileBytes;
    job.path = QFileInfo(job.source).absoluteFilePath();
    if (job.folder.isEmpty())
        return;

    // A copy in the models folder: as a download, the free space checked first, written as a part
    // and given its name when whole.
    if (!QDir().mkpath(job.folder)) {
        job.error = LocalAi::tr("The models folder cannot be made.");
        return;
    }
    constexpr qint64 Spare = 64LL * 1024 * 1024;
    const qint64 free = QStorageInfo(job.folder).bytesAvailable();
    if (free >= 0 && free < info.fileBytes + Spare) {
        job.error = LocalAi::tr("Not enough free space: it takes %1, and %2 is free.")
                            .arg(sizeText(info.fileBytes), sizeText(std::max<qint64>(0, free - Spare)));
        return;
    }
    const QString id = job.projector ? job.target : job.model.id;
    const QString target = job.folder + u'/' + id + u'-' + safeName(job.name.isEmpty() ? u"model"_s : job.name);
    QFile out(target + u".part"_s);
    in.close();
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        job.error = LocalAi::tr("The file cannot be copied (%1).").arg(out.errorString());
        return;
    }
    job.total = info.fileBytes;
    QByteArray buffer(4 * 1024 * 1024, Qt::Uninitialized);
    QElapsedTimer told;
    told.start();
    while (!job.cancel.load()) {
        const qint64 n = in.read(buffer.data(), buffer.size());
        if (n < 0 || (n > 0 && out.write(buffer.constData(), n) != n)) {
            job.error = LocalAi::tr("The copy broke off (%1).").arg(n < 0 ? in.errorString() : out.errorString());
            break;
        }
        if (n == 0)
            break;
        job.done += n;
        if (told.elapsed() > 200) {
            told.restart();
            QMetaObject::invokeMethod(owner, [owner] { Q_EMIT owner->addingChanged(); }, Qt::QueuedConnection);
        }
    }
    out.close();
    if (job.cancel.load() || !job.error.isEmpty() || job.done.load() != info.fileBytes) {
        if (job.error.isEmpty() && !job.cancel.load())
            job.error = LocalAi::tr("The copy broke off: the file is shorter than it says.");
        QFile::remove(out.fileName());
        return;
    }
    QFile::remove(target);
    if (!QFile::rename(out.fileName(), target)) {
        job.error = LocalAi::tr("The copy cannot be given its name.");
        QFile::remove(out.fileName());
        return;
    }
    job.path = target;
    job.copied = true;
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

    /// Lets the open model go when nothing is being asked of it (the app is away: what it read
    /// of the conversation is on the disk, the next request opens it again).
    void closeIdle()
    {
        if (m_idle.isActive())
            close(QString());
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
    const QStringList dirs = options.pluginDirs;
    if (open) {
        m_available = true;
    } else {
        m_available = ai::hasLocalRunner(dirs);
        open = [dirs](const ai::LocalRunner::Options &o, QString *error) {
            return ai::loadLocalRunner(dirs, o, error);
        };
    }
    m_probe = options.probe;
    if (!m_probe)
        m_probe = [dirs](const QString &path, QString *error) { return ai::probeLocalModel(dirs, path, error); };
    m_copyFiles = options.copyFiles;
    m_memory = options.memory < 0 ? ai::deviceMemory() : options.memory;
    m_contextTokens = ContextTokens;
    m_formerDefault = options.formerDefault;
    if (!m_models.isEmpty())
        m_chosen = m_models.first().id;
    m_files.adopt(m_models); // (models downloaded before they were marked so)
    loadOwn();

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
    connect(this, &LocalAi::addingChanged, this, &LocalAi::updateWorking);
    connect(this, &LocalAi::busyChanged, this, &LocalAi::noticeChanged);
    connect(this, &LocalAi::downloadChanged, this, &LocalAi::noticeChanged);
    connect(this, &LocalAi::addingChanged, this, &LocalAi::noticeChanged);

    // The worker thread only where a model can run at all (never in a build without the plugin —
    // the browser's Qt has no threads either).
    m_worker = new LocalAiWorker(std::move(open), options.idleMs);
    if (m_available) {
        m_worker->moveToThread(&m_thread);
        m_thread.setObjectName(u"brulion-local-ai"_s);
        m_thread.start();
        // Away — another program in front for a while, no window shown —, an open model that is
        // not answering leaves memory at once instead of after its idle time (gigabytes;
        // docs/PERFORMANCE.md "Memory: only what is in use").
        if (ui::MemoryPolicy *policy = ui::MemoryPolicy::instance()) {
            connect(policy, &ui::MemoryPolicy::letGo, this, [this] {
                LocalAiWorker *worker = m_worker;
                QMetaObject::invokeMethod(worker, [worker] { worker->closeIdle(); }, Qt::QueuedConnection);
            });
        }
    }
}

LocalAi::~LocalAi()
{
    // A file being added stops (its part removed); a request being answered stops, the ones still
    // waiting are dropped with the thread.
    if (m_adding)
        m_adding->cancel.store(true);
    if (m_addThread) {
        m_addThread->wait();
        delete m_addThread.data();
    }
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

const ai::LocalModel *LocalAi::find(const QString &id) const
{
    if (id.isEmpty())
        return m_models.isEmpty() ? nullptr : &m_models.first();
    for (const ai::LocalModel &m : m_models) {
        if (m.id == id)
            return &m;
    }
    for (const ai::LocalModel &m : m_ownModels) {
        if (m.id == id)
            return &m;
    }
    return nullptr;
}

const ai::OwnModel *LocalAi::own(const QString &id) const
{
    for (const ai::OwnModel &m : m_own) {
        if (m.id == id)
            return &m;
    }
    return nullptr;
}

const ai::LocalModel *LocalAi::seer(bool here) const
{
    for (const QList<ai::LocalModel> *list : {&m_models, &m_ownModels}) {
        for (const ai::LocalModel &m : *list) {
            if (m.sees() && (!here || has(m.id)))
                return &m;
        }
    }
    return nullptr;
}

std::optional<ai::LocalModel> LocalAi::model(const QString &id) const
{
    const ai::LocalModel *m = find(id);
    return m ? std::optional<ai::LocalModel>(*m) : std::nullopt;
}

int LocalAi::contextTokens(const ai::LocalModel &model) const
{
    if (model.trainedContext > 0)
        return std::min(m_contextTokens, std::max(2048, model.trainedContext));
    return m_contextTokens;
}

QVariantList LocalAi::choices() const
{
    QVariantList out;
    for (const ai::LocalModel &m : m_models)
        out.append(QVariantMap{{u"value"_s, m.id}, {u"text"_s, m.label.isEmpty() ? m.name : m.label}});
    return out;
}

QString LocalAi::keptChoice() const
{
    if (m_formerDefault.isEmpty() || !find(m_formerDefault) || find(m_formerDefault) == find(QString()))
        return QString();
    return has(m_formerDefault) && !has(QString()) ? m_formerDefault : QString();
}

void LocalAi::setChosen(const QString &id)
{
    // The default model has an id of its own: "" and it are the same choice — and so is a model
    // the app does not know (a setting left by another version).
    const ai::LocalModel *m = find(id);
    if (!m)
        m = find(QString());
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
    const ai::LocalModel *m = find(m_chosen);
    return m ? m->name : QString();
}

QString LocalAi::modelMaker() const
{
    const ai::LocalModel *m = find(m_chosen);
    return m ? m->maker : QString();
}

QString LocalAi::modelSize() const
{
    const ai::LocalModel *m = find(m_chosen);
    return m ? sizeText(m->bytes()) : QString();
}

bool LocalAi::modelSees() const
{
    const ai::LocalModel *m = find(m_chosen);
    return m && m->sees();
}

QString LocalAi::pictureModel() const
{
    const ai::LocalModel *m = find(m_chosen);
    const ai::LocalModel *other = m && !m->sees() ? seer(false) : nullptr;
    return other ? other->name : QString();
}

bool LocalAi::pictureModelHere() const
{
    const ai::LocalModel *m = find(m_chosen);
    return m && !m->sees() && seer(true);
}

bool LocalAi::modelAdapted() const
{
    const ai::LocalModel *m = find(m_chosen);
    return m && m->adapted();
}

QString LocalAi::modelBase() const
{
    const ai::LocalModel *m = find(m_chosen);
    return m ? m->baseName : QString();
}

QString LocalAi::modelBaseMaker() const
{
    const ai::LocalModel *m = find(m_chosen);
    return m ? m->baseMaker : QString();
}

QString LocalAi::modelLicense() const
{
    const ai::LocalModel *m = find(m_chosen);
    return m ? m->license : QString();
}

QString LocalAi::modelLicenseUrl() const
{
    const ai::LocalModel *m = find(m_chosen);
    return m ? m->licenseUrl.toString() : QString();
}

QString LocalAi::modelPageUrl() const
{
    const ai::LocalModel *m = find(m_chosen);
    return m ? m->pageUrl.toString() : QString();
}

QString LocalAi::modelLeft() const
{
    const ai::LocalModel *m = find(m_chosen);
    if (!m || m->own)
        return QString();
    const qint64 left = m->bytes() - m_files.bytesHere(*m);
    return left > 0 ? sizeText(left) : QString();
}

QString LocalAi::modelSharesWith() const
{
    const ai::LocalModel *m = find(m_chosen);
    if (!m || m->own)
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
    const ai::LocalModel *m = find(m_chosen);
    return m && m_files.downloading() == m->id;
}

QString LocalAi::downloadError() const
{
    const ai::LocalModel *m = find(m_chosen);
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
    const ai::LocalModel *m = find(id);
    if (m && m->own)
        return QFileInfo(m->file.path).isFile();
    return m && m_files.has(*m);
}

void LocalAi::download(const QString &id)
{
    const ai::LocalModel *m = find(id);
    if (!m || m->own || !m_files.downloading().isEmpty())
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
    const ai::LocalModel *m = find(id);
    if (!m)
        return;
    const ai::LocalModel removed = *m;
    if (removed.own) {
        // Forgotten — a file used where it is stays there, a copy goes —, after it left memory.
        const auto forget = [this, id = removed.id] {
            const ai::OwnModel *o = own(id);
            if (!o)
                return;
            if (o->copied) {
                QFile::remove(o->path);
                if (!o->projector.isEmpty())
                    QFile::remove(o->projector);
            }
            m_own.removeIf([&](const ai::OwnModel &other) { return other.id == id; });
            saveOwn();
            if (m_chosen == id)
                setChosen(QString());
            ownChanged();
            qCInfo(lcLocalAi).noquote() << u"%1 removed"_s.arg(id);
            Q_EMIT ownModelRemoved(id);
        };
        if (!m_thread.isRunning())
            return forget();
        stopAll();
        QMetaObject::invokeMethod(m_worker, [this, worker = m_worker, id = removed.id, forget] {
            worker->close(id);
            QMetaObject::invokeMethod(this, forget, Qt::QueuedConnection);
        });
        return;
    }
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
    const bool working = m_pending > 0 || !m_files.downloading().isEmpty() || m_adding;
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
    const ai::LocalModel *m = find(settings.model);
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
    if (m->own && !has(m->id))
        return refuse(u"the file of the model %1 is not there any more (%2): add it again in Settings → Extensions → "
                      u"AI"_s.arg(m->name, m->file.path));
    if (!has(m->id))
        return refuse(
                u"the model %1 is not on this device yet: download it in Settings → Extensions → AI"_s.arg(m->name));

    LocalAiWorker::Job job;
    job.model = *m;
    job.options.modelPath = m_files.pathOf(m->file);
    if (m->adapted())
        job.options.adapterPath = m_files.pathOf(m->adapter);
    if (m->sees() && (!m->own || QFileInfo(m->projector.path).isFile()))
        job.options.projectorPath = m_files.pathOf(m->projector);
    job.options.contextTokens = contextTokens(*m);
    job.options.imageTokens = imageTokensFor(job.options.contextTokens);
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

// ---- Models of the user's own ------------------------------------------------------------------

QString LocalAi::ownFolder() const
{
    return m_files.folder() + u"/own"_s;
}

void LocalAi::loadOwn()
{
    QFile file(m_files.folder() + u'/' + QLatin1String(ai::OwnModelsFile));
    m_own = file.open(QIODevice::ReadOnly) ? ai::readOwnModels(file.read(4 * 1024 * 1024)) : QList<ai::OwnModel>();
    m_ownModels.clear();
    for (const ai::OwnModel &o : std::as_const(m_own))
        m_ownModels << ai::localModelOf(o);
}

void LocalAi::saveOwn()
{
    QDir().mkpath(m_files.folder());
    QSaveFile file(m_files.folder() + u'/' + QLatin1String(ai::OwnModelsFile));
    if (!file.open(QIODevice::WriteOnly) || file.write(ai::writeOwnModels(m_own)) < 0 || !file.commit())
        qCWarning(lcLocalAi).noquote() << u"the list of own models cannot be written: %1"_s.arg(file.errorString());
}

void LocalAi::ownChanged()
{
    m_ownModels.clear();
    for (const ai::OwnModel &o : std::as_const(m_own))
        m_ownModels << ai::localModelOf(o);
    Q_EMIT ownModelsChanged();
    Q_EMIT modelChanged();
    Q_EMIT modelsChanged();
}

namespace {
/// "Qwen3 4B · Q4_K_M · 2.5 GB · context 32 768 · ChatML".
QString detailsOf(const ai::OwnModel &o)
{
    QString name = o.name;
    if (!o.sizeLabel.isEmpty() && !name.contains(o.sizeLabel, Qt::CaseInsensitive))
        name += u' ' + o.sizeLabel;
    QStringList parts{name};
    if (!o.quantization.isEmpty())
        parts << o.quantization;
    parts << sizeText(o.bytes + o.projectorBytes);
    if (o.contextLength > 0)
        parts << LocalAi::tr("context %1").arg(QLocale().toString(o.contextLength));
    const QString family = ai::local::formatName(o.format);
    if (!family.isEmpty())
        parts << family;
    return parts.join(u" · "_s);
}
} // namespace

QVariantList LocalAi::ownModelsList() const
{
    QVariantList out;
    for (const ai::OwnModel &o : m_own)
        out.append(QVariantMap{{u"id"_s, o.id},
                               {u"name"_s, o.name},
                               {u"details"_s, detailsOf(o)},
                               {u"path"_s, o.path},
                               {u"sees"_s, !o.projector.isEmpty()},
                               {u"here"_s, QFileInfo(o.path).isFile()},
                               {u"chosen"_s, o.id == m_chosen}});
    return out;
}

bool LocalAi::modelOwn() const
{
    return own(m_chosen) != nullptr;
}

QString LocalAi::modelDetails() const
{
    const ai::OwnModel *o = own(m_chosen);
    return o ? detailsOf(*o) : QString();
}

QString LocalAi::modelPath() const
{
    const ai::OwnModel *o = own(m_chosen);
    return o ? o->path : QString();
}

QString LocalAi::modelProjector() const
{
    const ai::OwnModel *o = own(m_chosen);
    return o ? o->projector : QString();
}

QString LocalAi::memoryNote() const
{
    const ai::OwnModel *o = own(m_chosen);
    if (!o || o->memory <= 0 || m_memory <= 0 || o->memory * 2 <= m_memory)
        return QString();
    return tr("It takes some %1 of this device's %2 of memory while it answers: other apps may be closed "
              "meanwhile, and it may answer slowly.")
            .arg(sizeText(o->memory), sizeText(m_memory));
}

double LocalAi::addProgress() const
{
    if (!m_adding || m_adding->probing.load())
        return -1.0;
    const qint64 total = m_adding->total.load();
    return total > 0 ? double(m_adding->done.load()) / double(total) : -1.0;
}

QString LocalAi::addText() const
{
    if (!m_adding)
        return QString();
    if (m_adding->probing.load())
        return tr("Checking that %1 loads…").arg(m_adding->name);
    const qint64 total = m_adding->total.load();
    if (total > 0)
        return tr("Copying %1: %2 of %3").arg(m_adding->name, sizeText(m_adding->done.load()), sizeText(total));
    return tr("Reading %1…").arg(m_adding->name);
}

void LocalAi::addModelFile(const QUrl &file)
{
    startAdding(file.isLocalFile()        ? QDir::cleanPath(file.toLocalFile())
                : file.scheme().isEmpty() ? file.toString()
                                          : file.toString(QUrl::FullyEncoded),
                false);
}

void LocalAi::addProjector(const QUrl &file)
{
    if (!own(m_chosen))
        return;
    startAdding(file.isLocalFile()        ? QDir::cleanPath(file.toLocalFile())
                : file.scheme().isEmpty() ? file.toString()
                                          : file.toString(QUrl::FullyEncoded),
                true);
}

void LocalAi::startAdding(const QString &source, bool projector)
{
    if (m_adding || source.isEmpty())
        return;
    if (!m_available) {
        endAdding(u"this build of Brulion runs no model on the device"_s);
        return;
    }
    auto job = std::make_shared<LocalAiAdding>();
    job->projector = projector;
    job->source = source;
    job->name = QFileInfo(source).fileName();
    if (job->name.isEmpty() || job->name.contains(u':'))
        job->name = projector ? u"mmproj.gguf"_s : u"model.gguf"_s; // (a document's id, not its name)
    job->folder = m_copyFiles ? ownFolder() : QString();
    job->target = projector ? m_chosen : QString();
    job->memory = m_memory;
    job->contextTokens = m_contextTokens;
    m_adding = job;
    m_addError.clear();
    qCInfo(lcLocalAi).noquote() << u"adding %1 %2%3"_s.arg(projector ? u"the projector"_s : u"the model"_s, job->name,
                                                           m_copyFiles ? u" (a copy)"_s : QString());
    Q_EMIT addingChanged();
#if QT_CONFIG(thread) && !defined(Q_OS_WASM)
    // (A file of gigabytes copied: never on the GUI thread, an "app not responding" on Android.)
    QThread *thread = QThread::create([job, owner = this] { runAdding(*job, owner); });
    thread->setObjectName(u"brulion-add-model"_s);
    m_addThread = thread;
    connect(thread, &QThread::finished, this, [this, thread, job] {
        if (m_addThread == thread)
            m_addThread.clear();
        thread->deleteLater();
        added(job);
    });
    thread->start(QThread::LowPriority);
#else
    runAdding(*job, this);
    added(job);
#endif
}

void LocalAi::added(const std::shared_ptr<LocalAiAdding> &job)
{
    if (job != m_adding)
        return;
    if (job->cancel.load())
        return endAdding(QString());
    if (!job->error.isEmpty())
        return endAdding(job->error);
    if (job->projector) {
        auto it = std::ranges::find_if(m_own, [&](const ai::OwnModel &o) { return o.id == job->target; });
        if (it == m_own.end()) {
            if (job->copied)
                QFile::remove(job->path);
            return endAdding(QString());
        }
        if (it->copied && !it->projector.isEmpty() && it->projector != job->path)
            QFile::remove(it->projector);
        it->projector = job->path;
        it->projectorBytes = job->bytes;
        saveOwn();
        // (Opened again with it for the next request.)
        if (m_thread.isRunning())
            QMetaObject::invokeMethod(m_worker, [worker = m_worker, id = job->target] { worker->close(id); });
        ownChanged();
        return endAdding(QString());
    }
    // Loaded once by the runner (its vocabulary, not its weights): an architecture this llama.cpp
    // knows, a file whole — on the worker thread, where the model's library lives.
    job->probing.store(true);
    Q_EMIT addingChanged();
    QMetaObject::invokeMethod(m_worker, [this, job, probe = m_probe] {
        QString error;
        const bool ok = probe(job->path, &error);
        QMetaObject::invokeMethod(this, [this, job, ok, error] { probed(job, ok, error); }, Qt::QueuedConnection);
    });
}

void LocalAi::probed(const std::shared_ptr<LocalAiAdding> &job, bool ok, const QString &error)
{
    if (job != m_adding)
        return;
    if (!ok || job->cancel.load()) {
        if (job->copied)
            QFile::remove(job->path);
        return endAdding(job->cancel.load() ? QString() : error.isEmpty() ? tr("The model cannot be loaded.") : error);
    }
    ai::OwnModel model = job->model;
    model.path = job->path;
    model.copied = job->copied;
    model.bytes = job->bytes;
    if (const ai::OwnModel *before = own(model.id)) {
        // (The same file again: its name and projector stay.)
        model.name = before->name;
        model.projector = before->projector;
        model.projectorBytes = before->projectorBytes;
    }
    m_own.removeIf([&](const ai::OwnModel &o) { return o.id == model.id; });
    m_own.append(model);
    saveOwn();
    ownChanged();
    qCInfo(lcLocalAi).noquote() << u"%1 added: %2, %3, %4 parameters, context %5, %6"_s
                                           .arg(model.id, model.architecture, model.quantization)
                                           .arg(model.parameters)
                                           .arg(model.contextLength)
                                           .arg(model.format);
    endAdding(QString());
    Q_EMIT ownModelAdded(model.id);
}

void LocalAi::endAdding(const QString &error)
{
    if (!error.isEmpty())
        qCInfo(lcLocalAi).noquote() << u"a model file was not added: %1"_s.arg(error);
    m_adding.reset();
    m_addError = error;
    Q_EMIT addingChanged();
}

void LocalAi::cancelAdding()
{
    if (m_adding)
        m_adding->cancel.store(true);
}

void LocalAi::removeProjector()
{
    auto it = std::ranges::find_if(m_own, [&](const ai::OwnModel &o) { return o.id == m_chosen; });
    if (it == m_own.end() || it->projector.isEmpty())
        return;
    if (it->copied)
        QFile::remove(it->projector);
    it->projector.clear();
    it->projectorBytes = 0;
    saveOwn();
    if (m_thread.isRunning())
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, id = m_chosen] { worker->close(id); });
    ownChanged();
}

void LocalAi::renameModel(const QString &name)
{
    auto it = std::ranges::find_if(m_own, [&](const ai::OwnModel &o) { return o.id == m_chosen; });
    if (it == m_own.end())
        return;
    const QString named = name.simplified().left(80);
    it->name = named.isEmpty() ? QFileInfo(it->path).completeBaseName() : named;
    saveOwn();
    ownChanged();
}

// ---- What Android's notification says ----------------------------------------------------------

LocalAi::Notice LocalAi::notice() const
{
    if (m_pending > 0) {
        const ai::LocalModel *m = find(m_chosen);
        return Notice{tr("The model on this device is answering…"), m ? m->name : QString(), -1};
    }
    if (!m_files.downloading().isEmpty()) {
        const ai::LocalModel *m = find(m_files.downloading());
        const QString name = m ? m->name : m_files.downloading();
        if (m_files.checking())
            return Notice{tr("Checking %1…").arg(name), QString(), -1};
        const qint64 total = m_files.total();
        const int percent = total > 0 ? int(m_files.received() * 100 / total) : 0;
        return Notice{tr("Downloading %1… %2 %").arg(name).arg(percent),
                      tr("%1 of %2").arg(sizeText(m_files.received()), sizeText(total)), percent};
    }
    if (m_adding) {
        const qint64 total = m_adding->total.load();
        if (total > 0 && !m_adding->probing.load()) {
            const int percent = int(m_adding->done.load() * 100 / total);
            return Notice{tr("Copying %1… %2 %").arg(m_adding->name).arg(percent),
                          tr("%1 of %2").arg(sizeText(m_adding->done.load()), sizeText(total)), percent};
        }
        return Notice{tr("Adding %1…").arg(m_adding->name), QString(), -1};
    }
    return Notice{};
}

void LocalAi::stopWork()
{
    stopAll();
    cancelDownload();
    cancelAdding();
}

} // namespace brulion::shell
