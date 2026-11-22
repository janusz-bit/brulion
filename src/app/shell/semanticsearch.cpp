#include "semanticsearch.h"

#include "index/vaultindex.h"
#include "semantic/models.h"
#include "semantic/notes.h"
#include "util/paths.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QSaveFile>

#include <algorithm>
#include <mutex>

using namespace Qt::StringLiterals;

namespace brulion::shell {

Q_LOGGING_CATEGORY(lcSemantic, "brulion.semantic")

namespace {

/// A call of the model embeds at most this many notes, of at most this many characters together
/// (some 2,000 tokens: half a second on a laptop's processor, a second or two on a phone) — the
/// notebook closing or the setting turned off waits for no more than that.
constexpr int MaxBatchNotes = 16;
constexpr qsizetype MaxBatchChars = 8000;
/// Query vectors kept (the search field's last queries, typed again or refined).
constexpr int KeptQueries = 32;
/// While a long build runs: its vectors are written this often (an app killed meanwhile starts
/// from there).
constexpr int SaveEveryMs = 60 * 1000;
/// While a notebook is indexed, results on screen follow at most this often.
constexpr int ContentEveryMs = 3000;

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString seconds(qint64 ms)
{
    return QString::number(double(ms) / 1000.0, 'f', 1);
}

/// The key a query vector is kept and asked for under.
QString queryKey(const QString &text)
{
    return text.simplified();
}

} // namespace

/// Notes to embed, and what came out (the index worker fills it).
struct SemanticBatch {
    struct Note {
        QString id;
        QString title;
        QString content;
        quint64 key = 0;
    };
    QList<Note> notes;
    QList<QList<semantic::Chunk>> chunks;   ///< per note
    QList<QList<semantic::Vector>> vectors; ///< per note
    bool ok = false;
    QString error;
    qint64 ms = 0;

    /// The note `path` of `index` as it is now (nullopt: no such note).
    static std::optional<Note> noteFor(const VaultIndex &index, const QString &path)
    {
        const ParsedNote *note = index.note(path);
        if (!note)
            return std::nullopt;
        Note n;
        n.id = path;
        // (brulion-cli's search --semantic takes the same title: the keys must agree.)
        n.title = note->title.isEmpty() ? paths::pageTitle(path) : note->title;
        n.content = index.content(path);
        n.key = semantic::noteKey(n.title, n.content);
        return n;
    }
};

/// The model in memory, shared by the two threads that use it (each with a context of its own):
/// opened by whichever needs it first, let go after a while without work.
class SemanticRunnerHolder {
public:
    explicit SemanticRunnerHolder(SemanticSearch::Opener open) : m_open(std::move(open)) {}

    std::shared_ptr<semantic::SemanticRunner> get(const QString &modelPath, QString *error)
    {
        std::lock_guard lock(m_mutex);
        if (m_runner && m_path == modelPath)
            return m_runner;
        m_runner.reset();
        m_path.clear();
        semantic::SemanticRunner::Options options;
        options.modelPath = modelPath;
        QElapsedTimer clock;
        clock.start();
        std::unique_ptr<semantic::SemanticRunner> runner = m_open(options, error);
        if (!runner) {
            qCWarning(lcSemantic).noquote() << u"the embedding model cannot be opened: %1"_s.arg(error ? *error : QString());
            return nullptr;
        }
        qCInfo(lcSemantic).noquote() << u"embedding model opened: %1, in %2 s"_s.arg(runner->device(),
                                                                                   seconds(clock.elapsed()));
        m_runner = std::move(runner);
        m_path = modelPath;
        return m_runner;
    }

    /// Lets the model go (once the jobs using it now end).
    bool release()
    {
        std::lock_guard lock(m_mutex);
        const bool had = m_runner != nullptr;
        m_runner.reset();
        m_path.clear();
        return had;
    }

private:
    std::mutex m_mutex;
    SemanticSearch::Opener m_open;
    std::shared_ptr<semantic::SemanticRunner> m_runner;
    QString m_path;
};

/// Reading and writing the kept index and embedding notes, on the index thread.
class SemanticIndexWorker : public QObject {
public:
    explicit SemanticIndexWorker(std::shared_ptr<SemanticRunnerHolder> runner) : m_runner(std::move(runner)) {}

    static std::optional<semantic::VectorIndex> load(const QString &file, const QString &model)
    {
        QFile f(file);
        if (file.isEmpty() || !f.open(QIODevice::ReadOnly))
            return std::nullopt;
        const QByteArray bytes = f.readAll();
        std::optional<semantic::VectorIndex> index = semantic::VectorIndex::load(bytes, model);
        if (!index)
            qCInfo(lcSemantic).noquote() << u"%1: not this version's or damaged, built again"_s.arg(file);
        return index;
    }

    static bool write(const QString &file, const QByteArray &bytes)
    {
        QDir().mkpath(QFileInfo(file).absolutePath());
        QSaveFile f(file);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
            qCWarning(lcSemantic).noquote() << u"%1 not written: %2"_s.arg(file, f.errorString());
            return false;
        }
        return true;
    }

    void embed(SemanticBatch &batch, const QString &modelPath)
    {
        QElapsedTimer clock;
        clock.start();
        QString error;
        const std::shared_ptr<semantic::SemanticRunner> runner = m_runner->get(modelPath, &error);
        if (!runner) {
            batch.error = error.isEmpty() ? u"the embedding model cannot be opened"_s : error;
            return;
        }
        QStringList texts;
        for (const SemanticBatch::Note &note : std::as_const(batch.notes)) {
            QList<semantic::Chunk> chunks = semantic::noteChunks(note.title, note.content);
            for (const semantic::Chunk &c : std::as_const(chunks))
                texts.append(c.text);
            batch.chunks.append(std::move(chunks));
        }
        const QList<semantic::Vector> vectors = runner->embedPassages(texts);
        if (vectors.size() != texts.size()) {
            batch.error = u"the embedding model gave no vectors"_s;
            return;
        }
        qsizetype at = 0;
        for (const QList<semantic::Chunk> &chunks : std::as_const(batch.chunks)) {
            batch.vectors.append(vectors.mid(at, chunks.size()));
            at += chunks.size();
        }
        batch.ok = true;
        batch.ms = clock.elapsed();
    }

private:
    std::shared_ptr<SemanticRunnerHolder> m_runner;
};

/// Embedding queries, on the query thread.
class SemanticQueryWorker : public QObject {
public:
    explicit SemanticQueryWorker(std::shared_ptr<SemanticRunnerHolder> runner) : m_runner(std::move(runner)) {}

    semantic::Vector embed(const QString &text, const QString &modelPath, QString *error)
    {
        const std::shared_ptr<semantic::SemanticRunner> runner = m_runner->get(modelPath, error);
        if (!runner)
            return {};
        QElapsedTimer clock;
        clock.start();
        semantic::Vector v = runner->embedQuery(text);
        qCDebug(lcSemantic).noquote() << u"query embedded in %1 ms"_s.arg(clock.elapsed());
        if (v.isEmpty() && error)
            *error = u"the embedding model gave no vector"_s;
        return v;
    }

private:
    std::shared_ptr<SemanticRunnerHolder> m_runner;
};

SemanticSearch::SemanticSearch(QObject *parent) : QObject(parent)
{
    m_startTimer.setSingleShot(true);
    connect(&m_startTimer, &QTimer::timeout, this, &SemanticSearch::begin);
    m_changeTimer.setSingleShot(true);
    connect(&m_changeTimer, &QTimer::timeout, this, [this] { reconcile(false); });
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { save(false); });
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        if (!m_inFlight.isEmpty() || !m_asked.isEmpty()) {
            touchRunner();
            return;
        }
        if (m_runner && m_runner->release())
            qCInfo(lcSemantic) << "the embedding model left memory";
    });
    m_contentTimer.setSingleShot(true);
    connect(&m_contentTimer, &QTimer::timeout, this, &SemanticSearch::contentChanged);
}

SemanticSearch::~SemanticSearch()
{
    ++m_generation;
    for (QThread *thread : {&m_indexThread, &m_queryThread}) {
        QObject *worker = thread == &m_indexThread ? static_cast<QObject *>(m_indexWorker) : m_queryWorker;
        if (thread->isRunning()) {
            // The worker goes on its own thread, after the call it is in ends.
            QMetaObject::invokeMethod(worker, [worker] { delete worker; }, Qt::BlockingQueuedConnection);
            thread->quit();
            thread->wait();
        } else {
            delete worker;
        }
    }
}

void SemanticSearch::configure(const Options &options)
{
    m_options = options;
    m_models = options.models.isEmpty() ? semantic::semanticModels() : options.models;
    m_files = std::make_unique<ai::LocalModelFiles>(options.folder);
    m_files->adopt(m_models);
    connect(m_files.get(), &ai::LocalModelFiles::progress, this, &SemanticSearch::downloadChanged);
    connect(m_files.get(), &ai::LocalModelFiles::changed, this, [this] {
        Q_EMIT modelChanged();
        refreshState();
    });
    connect(m_files.get(), &ai::LocalModelFiles::finished, this,
            [this](const QString &id, bool ok, const QString &error) {
                if (ok)
                    qCInfo(lcSemantic).noquote() << u"%1 downloaded"_s.arg(id);
                else if (!error.isEmpty())
                    qCWarning(lcSemantic).noquote() << u"%1 download failed: %2"_s.arg(id, error);
                m_downloadError = ok ? QString() : error;
                Q_EMIT downloadChanged();
                Q_EMIT modelChanged();
                if (ok) {
                    m_error.clear();
                    if (canRun())
                        begin();
                }
                refreshState();
            });

    Opener open = options.open;
    if (open) {
        m_available = true;
    } else {
        const QStringList dirs = options.pluginDirs;
        m_available = semantic::hasSemanticRunner(dirs);
        open = [dirs](const semantic::SemanticRunner::Options &o, QString *error) {
            return semantic::loadSemanticRunner(dirs, o, error);
        };
    }
    m_runner = std::make_shared<SemanticRunnerHolder>(std::move(open));
    if (m_available && !m_indexWorker) {
        m_indexWorker = new SemanticIndexWorker(m_runner);
        m_indexWorker->moveToThread(&m_indexThread);
        m_indexThread.setObjectName(u"brulion-semantic"_s);
        // Notes are embedded only when nothing else wants the processor (Linux: SCHED_IDLE; the
        // model's own threads, started from this one, inherit it).
        m_indexThread.start(QThread::IdlePriority);
        m_queryWorker = new SemanticQueryWorker(m_runner);
        m_queryWorker->moveToThread(&m_queryThread);
        m_queryThread.setObjectName(u"brulion-semantic-query"_s);
        m_queryThread.start();
    }
    Q_EMIT configured();
    Q_EMIT modelChanged();
    Q_EMIT enabledChanged();
    refreshState();
}

// ---- The model ----------------------------------------------------------------------------------

const ai::LocalModel *SemanticSearch::model() const
{
    return m_models.isEmpty() ? nullptr : &m_models.first();
}

QString SemanticSearch::modelPath() const
{
    const ai::LocalModel *m = model();
    return m && m_files ? m_files->pathOf(m->file) : QString();
}

QString SemanticSearch::modelName() const
{
    const ai::LocalModel *m = model();
    return m ? m->name : QString();
}

QString SemanticSearch::modelMaker() const
{
    const ai::LocalModel *m = model();
    return m ? m->maker : QString();
}

QString SemanticSearch::modelSize() const
{
    const ai::LocalModel *m = model();
    return m ? sizeText(m->bytes()) : QString();
}

QString SemanticSearch::modelLicense() const
{
    const ai::LocalModel *m = model();
    return m ? m->license : QString();
}

QString SemanticSearch::modelLicenseUrl() const
{
    const ai::LocalModel *m = model();
    return m ? m->licenseUrl.toString() : QString();
}

QString SemanticSearch::modelPageUrl() const
{
    const ai::LocalModel *m = model();
    return m ? m->pageUrl.toString() : QString();
}

bool SemanticSearch::modelHere() const
{
    const ai::LocalModel *m = model();
    return m && m_files && m_files->has(*m);
}

bool SemanticSearch::downloading() const
{
    const ai::LocalModel *m = model();
    return m && m_files && m_files->downloading() == m->id;
}

double SemanticSearch::progress() const
{
    if (!downloading())
        return 0.0;
    return m_files->total() > 0 ? double(m_files->received()) / double(m_files->total()) : 0.0;
}

QString SemanticSearch::progressText() const
{
    if (!downloading())
        return QString();
    return tr("%1 of %2").arg(sizeText(m_files->received()), sizeText(m_files->total()));
}

bool SemanticSearch::checking() const
{
    return m_files && m_files->checking();
}

void SemanticSearch::download()
{
    const ai::LocalModel *m = model();
    if (!m || !m_files || !m_files->downloading().isEmpty() || m_files->has(*m))
        return;
    m_downloadError.clear();
    m_files->download(*m);
    Q_EMIT downloadChanged();
    refreshState();
}

void SemanticSearch::cancelDownload()
{
    if (m_files)
        m_files->cancel();
}

void SemanticSearch::remove()
{
    const ai::LocalModel *m = model();
    if (!m || !m_files)
        return;
    cancelDownload();
    setEnabled(false);
    m_runner->release();
    m_queries.clear();
    m_queryOrder.clear();
    m_files->remove(*m, m_models);
    // Every notebook's vectors of this model: of no use without it.
    if (!m_options.cacheDir.isEmpty()) {
        QDir dir(m_options.cacheDir);
        for (const QString &file : dir.entryList({semantic::indexFileName(u"*"_s, m->id)}, QDir::Files))
            dir.remove(file);
    }
    Q_EMIT modelChanged();
    refreshState();
}

// ---- The setting and the state ------------------------------------------------------------------

bool SemanticSearch::enabled() const
{
    return m_options.setting ? m_options.setting(u"search/semantic"_s, false).toBool() : m_enabled;
}

void SemanticSearch::setEnabled(bool on)
{
    if (on == enabled())
        return;
    if (m_options.setSetting)
        m_options.setSetting(u"search/semantic"_s, on);
    m_enabled = on;
    Q_EMIT enabledChanged();
    if (on) {
        m_error.clear();
        if (!modelHere())
            download();
        else if (m_started && !m_loaded)
            begin();
    } else {
        // Its memory goes too; the vectors stay on the device for the next time.
        save(true);
        stop();
        m_index = semantic::VectorIndex();
        m_loaded = false;
        m_queue.clear();
        m_queued.clear();
        m_total = 0;
        Q_EMIT progressChanged();
        Q_EMIT contentChanged();
    }
    refreshState();
}

bool SemanticSearch::canRun() const
{
    return m_available && enabled() && modelHere() && m_vaultIndex && m_started && m_error.isEmpty();
}

QString SemanticSearch::state() const
{
    if (!m_available)
        return u"unavailable"_s;
    if (!enabled())
        return u"off"_s;
    if (!modelHere())
        return u"noModel"_s;
    if (!m_vaultIndex)
        return u"idle"_s;
    if (!m_error.isEmpty())
        return u"failed"_s;
    if (!m_loaded)
        return u"loading"_s;
    if (!m_queue.isEmpty() || !m_inFlight.isEmpty())
        return u"indexing"_s;
    return u"ready"_s;
}

bool SemanticSearch::active() const
{
    return m_available && enabled() && modelHere() && m_vaultIndex && m_loaded && m_error.isEmpty()
           && m_index.size() > 0;
}

int SemanticSearch::notesDone() const
{
    return std::max(0, m_total - int(m_queue.size()) - int(m_inFlight.size()));
}

bool SemanticSearch::idle() const
{
    return m_inFlight.isEmpty() && (m_queue.isEmpty() || !canRun()) && !m_startTimer.isActive()
           && !m_changeTimer.isActive() && (m_loaded || !canRun());
}

QString SemanticSearch::indexFile() const
{
    const ai::LocalModel *m = model();
    if (!m || m_notebookId.isEmpty() || m_options.cacheDir.isEmpty())
        return QString();
    return QDir(m_options.cacheDir).filePath(semantic::indexFileName(m_notebookId, m->id));
}

void SemanticSearch::refreshState()
{
    const QString now = state();
    const bool isActive = active();
    if (now == m_state && isActive == m_wasActive)
        return;
    m_state = now;
    m_wasActive = isActive;
    Q_EMIT stateChanged();
}

void SemanticSearch::fail(const QString &error)
{
    qCWarning(lcSemantic).noquote() << u"search by meaning stopped: %1"_s.arg(error);
    m_error = error;
    stop();
    refreshState();
}

// ---- The notebook -------------------------------------------------------------------------------

void SemanticSearch::attach(VaultIndex *index, const QString &notebookId)
{
    detach();
    m_vaultIndex = index;
    m_notebookId = notebookId;
    if (index) {
        connect(index, &VaultIndex::changed, this, [this] {
            if (m_loaded && canRun())
                m_changeTimer.start(m_options.changeDelayMs);
        });
    }
    refreshState();
}

void SemanticSearch::start()
{
    if (!m_vaultIndex || m_started)
        return;
    m_started = true;
    if (canRun())
        m_startTimer.start(m_options.startDelayMs);
    refreshState();
}

void SemanticSearch::detach()
{
    if (!m_vaultIndex && m_notebookId.isEmpty())
        return;
    save(true);
    stop();
    if (m_vaultIndex)
        disconnect(m_vaultIndex, nullptr, this, nullptr);
    m_vaultIndex = nullptr;
    m_notebookId.clear();
    m_started = false;
    m_loaded = false;
    m_index = semantic::VectorIndex();
    m_queue.clear();
    m_queued.clear();
    m_total = 0;
    m_version = -1;
    m_error.clear();
    Q_EMIT progressChanged();
    Q_EMIT contentChanged();
    refreshState();
}

void SemanticSearch::stop()
{
    ++m_generation; // what is being embedded or read now is dropped when it arrives
    m_inFlight.clear();
    m_loading = false;
    m_startTimer.stop();
    m_changeTimer.stop();
    m_saveTimer.stop();
    m_contentTimer.stop();
    m_asked.clear();
    if (m_runner)
        m_runner->release();
}

void SemanticSearch::begin()
{
    if (!canRun() || m_loaded || m_loading)
        return;
    m_loading = true;
    const quint64 generation = ++m_generation;
    const QString file = indexFile();
    const QString modelId = model()->id;
    auto loaded = std::make_shared<std::optional<semantic::VectorIndex>>();
    refreshState();
    QMetaObject::invokeMethod(m_indexWorker, [this, generation, file, modelId, loaded] {
        QElapsedTimer clock;
        clock.start();
        *loaded = SemanticIndexWorker::load(file, modelId);
        if (*loaded)
            qCInfo(lcSemantic).noquote() << u"%1 notes' vectors read in %2 s"_s.arg((*loaded)->size()).arg(
                    seconds(clock.elapsed()));
        QMetaObject::invokeMethod(this, [this, generation, loaded] { onLoaded(generation, loaded); },
                                  Qt::QueuedConnection);
    });
}

void SemanticSearch::onLoaded(quint64 generation, std::shared_ptr<std::optional<semantic::VectorIndex>> loaded)
{
    if (generation != m_generation)
        return;
    m_loading = false;
    m_index = *loaded ? std::move(**loaded) : semantic::VectorIndex();
    m_loaded = true;
    m_calibrate = true;
    m_buildClock.start();
    m_builtNotes = 0;
    reconcile(true);
}

void SemanticSearch::reconcile(bool full)
{
    if (!m_vaultIndex || !m_loaded || !canRun())
        return;
    const VaultIndex &index = *m_vaultIndex;
    bool changed = false;
    if (!full) {
        const IndexChanges changes = index.changesSince(m_version);
        if (!changes.complete) {
            reconcile(true);
            return;
        }
        m_version = index.version();
        for (const VaultPath &path : changes.notes) {
            const std::optional<SemanticBatch::Note> note = SemanticBatch::noteFor(index, path);
            if (!note) {
                if (m_index.contains(path)) {
                    m_index.remove(path);
                    m_dirty = m_calibrate = changed = true;
                }
                if (m_queued.remove(path))
                    m_queue.removeAll(path);
            } else if (!m_index.contains(path) || m_index.key(path) != note->key) {
                // The note just edited first: it is the one most likely searched for next.
                enqueue(path, true);
            }
        }
    } else {
        m_version = index.version();
        // Every note with its key, the most recent journals first, then the pages by path.
        struct Entry {
            QString id;
            quint64 key = 0;
            std::optional<QDate> journal;
        };
        QList<Entry> entries;
        index.forEachNote([&entries, &index](const VaultPath &path, const ParsedNote &note) {
            const QString title = note.title.isEmpty() ? paths::pageTitle(path) : note.title;
            entries.append({path, semantic::noteKey(title, index.content(path)), note.journalDate});
        });
        std::ranges::stable_sort(entries, [](const Entry &a, const Entry &b) {
            if (a.journal.has_value() != b.journal.has_value())
                return a.journal.has_value();
            return a.journal && *a.journal > *b.journal;
        });
        QList<std::pair<QString, quint64>> notes;
        notes.reserve(entries.size());
        for (const Entry &e : std::as_const(entries))
            notes.append({e.id, e.key});
        const semantic::IndexDiff diff = semantic::diffIndex(notes, m_index);
        for (const QString &id : diff.remove)
            m_index.remove(id);
        if (!diff.remove.isEmpty())
            m_dirty = m_calibrate = changed = true;
        m_queue = diff.embed;
        m_queued = QSet<QString>(m_queue.cbegin(), m_queue.cend());
        if (!m_queue.isEmpty())
            qCInfo(lcSemantic).noquote() << u"%1 of %2 notes to embed"_s.arg(m_queue.size()).arg(notes.size());
    }
    m_total = int(index.noteCount());
    Q_EMIT progressChanged();
    if (changed)
        notifyContent(false);
    if (m_inFlight.isEmpty() && m_queue.isEmpty())
        onDrained();
    else
        dispatch();
    refreshState();
}

void SemanticSearch::enqueue(const QString &id, bool first)
{
    if (m_queued.contains(id)) {
        if (!first)
            return;
        m_queue.removeAll(id);
    }
    m_queued.insert(id);
    if (first)
        m_queue.prepend(id);
    else
        m_queue.append(id);
}

void SemanticSearch::dispatch()
{
    if (!canRun() || !m_loaded || !m_inFlight.isEmpty() || !m_vaultIndex)
        return;
    auto batch = std::make_shared<SemanticBatch>();
    qsizetype chars = 0;
    while (!m_queue.isEmpty() && batch->notes.size() < MaxBatchNotes && chars < MaxBatchChars) {
        const QString id = m_queue.takeFirst();
        m_queued.remove(id);
        std::optional<SemanticBatch::Note> note = SemanticBatch::noteFor(*m_vaultIndex, id);
        if (!note)
            continue;
        chars += note->content.size();
        m_inFlight.append(id);
        batch->notes.append(std::move(*note));
    }
    if (batch->notes.isEmpty()) {
        onDrained();
        return;
    }
    const quint64 generation = m_generation;
    const QString path = modelPath();
    m_idleTimer.stop();
    QMetaObject::invokeMethod(m_indexWorker, [this, worker = m_indexWorker, generation, batch, path] {
        worker->embed(*batch, path);
        QMetaObject::invokeMethod(this, [this, generation, batch] { onEmbedded(generation, batch); },
                                  Qt::QueuedConnection);
    });
    refreshState();
}

void SemanticSearch::onEmbedded(quint64 generation, std::shared_ptr<SemanticBatch> batch)
{
    if (generation != m_generation)
        return;
    m_inFlight.clear();
    touchRunner();
    if (!batch->ok) {
        fail(batch->error);
        return;
    }
    qsizetype chunks = 0;
    for (qsizetype i = 0; i < batch->notes.size(); ++i) {
        const SemanticBatch::Note &note = batch->notes.at(i);
        const std::optional<SemanticBatch::Note> now =
                m_vaultIndex ? SemanticBatch::noteFor(*m_vaultIndex, note.id) : std::nullopt;
        if (!now)
            continue; // (gone meanwhile)
        if (now->key != note.key) {
            enqueue(note.id, true); // changed meanwhile: again
            continue;
        }
        m_index.set(note.id, batch->chunks.at(i), batch->vectors.at(i), note.key);
        chunks += batch->chunks.at(i).size();
        ++m_embeddedNotes;
        ++m_builtNotes;
    }
    qCDebug(lcSemantic).noquote() << u"%1 notes (%2 chunks) embedded in %3 ms"_s.arg(batch->notes.size())
                                             .arg(chunks)
                                             .arg(batch->ms);
    m_dirty = m_calibrate = true;
    if (!m_saveTimer.isActive())
        m_saveTimer.start(SaveEveryMs);
    Q_EMIT progressChanged();
    notifyContent(false);
    if (m_queue.isEmpty())
        onDrained();
    else
        dispatch();
    refreshState();
}

void SemanticSearch::onDrained()
{
    if (!m_inFlight.isEmpty() || !m_queue.isEmpty())
        return;
    if (m_builtNotes > 0) {
        qCInfo(lcSemantic).noquote() << u"%1 notes embedded in %2 s (%3 notes, %4 chunks in the index)"_s
                                                .arg(m_builtNotes)
                                                .arg(seconds(m_buildClock.elapsed()))
                                                .arg(m_index.size())
                                                .arg(m_index.chunkCount());
        m_builtNotes = 0;
    }
    if (m_calibrate) {
        m_calibrate = false;
        const semantic::Vector queryMean = model() ? semantic::queryMeanOf(model()->id) : semantic::Vector();
        m_index.setCalibration(queryMean.isEmpty() || m_index.chunkCount() == 0
                                       ? semantic::Calibration()
                                       : m_index.estimateCalibration(queryMean));
        notifyContent(true);
    }
    save(false);
    m_buildClock.start();
    refreshState();
}

void SemanticSearch::save(bool now)
{
    m_saveTimer.stop();
    const QString file = indexFile();
    if (!m_dirty || file.isEmpty() || !m_loaded || !model())
        return;
    m_dirty = false;
    QByteArray bytes = m_index.save(model()->id);
    if (now || !m_indexWorker) {
        SemanticIndexWorker::write(file, bytes);
        return;
    }
    QMetaObject::invokeMethod(m_indexWorker, [file, bytes = std::move(bytes)] { SemanticIndexWorker::write(file, bytes); });
}

void SemanticSearch::touchRunner()
{
    m_idleTimer.start(m_options.idleMs);
}

void SemanticSearch::notifyContent(bool now)
{
    if (now) {
        m_contentTimer.stop();
        Q_EMIT contentChanged();
    } else if (!m_contentTimer.isActive()) {
        m_contentTimer.start(ContentEveryMs);
    }
}

// ---- Queries ------------------------------------------------------------------------------------

std::optional<semantic::Vector> SemanticSearch::queryVector(const QString &text)
{
    const QString key = queryKey(text);
    if (key.isEmpty())
        return std::nullopt;
    if (const auto it = m_queries.constFind(key); it != m_queries.cend()) {
        m_queryOrder.removeOne(key);
        m_queryOrder.append(key);
        return it.value();
    }
    if (!active() || m_asked.contains(key) || !m_queryWorker)
        return std::nullopt;
    m_asked.insert(key);
    m_idleTimer.stop();
    const quint64 generation = m_generation;
    const QString path = modelPath();
    QMetaObject::invokeMethod(m_queryWorker, [this, worker = m_queryWorker, generation, key, path] {
        QString error;
        const semantic::Vector v = worker->embed(key, path, &error);
        QMetaObject::invokeMethod(
                this,
                [this, generation, key, v, error] {
                    m_asked.remove(key);
                    if (generation != m_generation)
                        return;
                    touchRunner();
                    if (v.isEmpty()) {
                        fail(error);
                        return;
                    }
                    m_queries.insert(key, v);
                    m_queryOrder.append(key);
                    while (m_queryOrder.size() > KeptQueries)
                        m_queries.remove(m_queryOrder.takeFirst());
                    Q_EMIT queryReady(key);
                },
                Qt::QueuedConnection);
    });
    return std::nullopt;
}

} // namespace brulion::shell
