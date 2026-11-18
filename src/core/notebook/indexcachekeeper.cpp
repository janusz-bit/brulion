#include "notebook/indexcachekeeper.h"

#include "index/indexcache.h"
#include "index/vaultindex.h"
#include "log/categories.h"
#include "log/slowscope.h"
#include "vault/vault.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <mutex>
#include <utility>

namespace brulion::notebook {

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

QString writeIndexCacheFile(const QString &path, const QByteArray &bytes)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return u"cannot create %1"_s.arg(info.absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    if (file.write(bytes) != bytes.size()) {
        const QString error = file.errorString();
        file.cancelWriting();
        return error;
    }
    if (!file.commit())
        return file.errorString();
    return {};
}

qint64 indexCacheBudget(qint64 usage, qint64 quota, qint64 replaced, qint64 cap)
{
    if (usage < 0 || quota <= 0)
        return cap;
    const qint64 free = std::max<qint64>(0, quota - usage);
    return std::min(cap, std::max<qint64>(0, replaced) + free / 2);
}

namespace {

/// What a write job shares with its keeper: the receiver of its report, null once the keeper is gone.
struct KeeperShared {
    std::mutex mutex;
    IndexCacheKeeper *receiver = nullptr;
};

/// How often, and how long each time, a save waits for the full-text index while more than a
/// few of its documents are pending (a bulk build after a load). A few pending documents (an
/// edit a moment ago) do not hold it up: the file carries the others, a load builds those.
constexpr int SearchWaits = 5;
constexpr std::chrono::milliseconds SearchWait = 1s;
constexpr qsizetype FewPendingDocuments = 16;
/// A snapshot the index's changes interrupted is taken again after this.
constexpr std::chrono::milliseconds SnapshotRetry = 2s;

/// Snapshots are numbered (every keeper, every file): a write never replaces the file of a newer
/// snapshot — two saves may run at once (a periodic one still writing when the notebook closes).
std::atomic<quint64> snapshotNumbers{0};

struct WrittenFiles {
    std::mutex mutex;               ///< also serialises the writes
    QHash<QString, quint64> newest; ///< path -> number of the snapshot in the file
};

WrittenFiles &writtenFiles()
{
    static WrittenFiles files;
    return files;
}

/// writeIndexCacheFile() unless a newer snapshot's file is there (then `superseded`, no error).
QString writeSnapshotFile(const QString &path, const QByteArray &bytes, quint64 number, bool *superseded)
{
    WrittenFiles &files = writtenFiles();
    std::lock_guard lock(files.mutex);
    if (files.newest.value(path) > number) {
        *superseded = true;
        return {};
    }
    *superseded = false;
    QString error = writeIndexCacheFile(path, bytes);
    if (error.isEmpty())
        files.newest.insert(path, number);
    return error;
}

/// What a save reports back to the keeper.
struct SaveResult {
    QString error;
    qint64 bytes = 0;
    qint64 ms = 0;
    bool superseded = false;
    bool fromPool = false;
};

} // namespace

struct IndexCacheKeeper::Private {
    IndexCacheKeeper *q;
    Vault &vault;
    VaultIndex &index;
    WorkerPool pool;
    QString path;
    Policy policy;
    std::shared_ptr<KeeperShared> shared = std::make_shared<KeeperShared>();
    QTimer due;    ///< the next save: after the first load, the longest wait of changes, retries
    QTimer slices; ///< steps of the snapshot (inline: of the encoding)
    std::unique_ptr<indexcache::SnapshotTaker> taker;
    std::unique_ptr<indexcache::Writer> writer; ///< inline: encoding in slices
    quint64 writerNumber = 0;
    QByteArray buffer;
    QElapsedTimer clock; ///< since the snapshot's start
    int poolJobs = 0;    ///< encodings and writes on the pool
    bool loaded = false;
    bool dirty = false;
    bool failureLogged = false;
    bool savedOnce = false;
    int searchWaits = 0;

    Private(IndexCacheKeeper *owner, Vault &v, VaultIndex &i, WorkerPool p, QString file, const Policy &pol)
        : q(owner), vault(v), index(i), pool(std::move(p)), path(std::move(file)), policy(pol)
    { }

    indexcache::Identity identity() const { return indexcache::Identity::current(index.config(), vault.id()); }

    indexcache::StatFn statFn() const
    {
        return [v = &vault](const VaultPath &p) { return v->stat(p); };
    }

    QString tooLarge() const { return u"larger than %1"_s.arg(logging::formatBytes(policy.maxBytes)); }

    bool stepping() const { return taker || writer; }

    void onIndexChanged()
    {
        if (!loaded)
            return;
        dirty = true;
        // The longest wait runs from the first unsaved change: typing never postpones it. (A
        // snapshot being taken gives up at its next step: the index moved.)
        if (!due.isActive() && !stepping())
            due.start(policy.maxDirtyMs);
    }

    bool bigEnough() const { return index.paths().size() >= policy.minNotes; }

    /// The periodic save: a snapshot in slices, then the encoding.
    void startSave()
    {
        if (stepping() || !dirty)
            return;
        if (!bigEnough()) {
            dirty = false;
            return;
        }
        // Wait a little for the full-text index being built (the file is more useful with it).
        if (index.pendingSearchDocuments() > FewPendingDocuments && searchWaits < SearchWaits) {
            ++searchWaits;
            due.start(SearchWait);
            return;
        }
        searchWaits = 0;
        clock.start();
        taker = std::make_unique<indexcache::SnapshotTaker>(index, identity(), statFn());
        slices.start();
    }

    void slice()
    {
        if (taker) {
            const indexcache::SnapshotTaker::State state = taker->step(policy.sliceMs);
            if (state == indexcache::SnapshotTaker::State::More)
                return;
            std::shared_ptr<const indexcache::Snapshot> snapshot = taker->take();
            taker.reset();
            slices.stop();
            if (state == indexcache::SnapshotTaker::State::Aborted) {
                due.start(SnapshotRetry); // (still dirty)
                return;
            }
            dirty = !snapshot->searchComplete(); // (else written again once it is)
            encode(std::move(snapshot), true);
            return;
        }
        if (!writer) {
            slices.stop();
            return;
        }
        const indexcache::Writer::State state = writer->step(policy.sliceMs, &buffer);
        if (state == indexcache::Writer::State::More && buffer.size() <= policy.maxBytes)
            return;
        finishWriter();
    }

    /// Encode `snapshot` and write it: on the pool, else on this thread (in slices or at once).
    void encode(std::shared_ptr<const indexcache::Snapshot> snapshot, bool sliced)
    {
        const quint64 number = ++snapshotNumbers;
        if (!pool.isInline()) {
            ++poolJobs;
            pool.start([keeper = shared, file = path, snapshot = std::move(snapshot), number, clock = clock,
                        maxBytes = policy.maxBytes, tooLarge = tooLarge()] {
                SaveResult result;
                result.fromPool = true;
                const QByteArray bytes = indexcache::encode(*snapshot);
                if (bytes.size() > maxBytes)
                    result.error = tooLarge;
                else
                    result.error = writeSnapshotFile(file, bytes, number, &result.superseded);
                result.bytes = result.error.isEmpty() ? bytes.size() : 0;
                result.ms = clock.isValid() ? clock.elapsed() : 0;
                std::lock_guard lock(keeper->mutex);
                if (IndexCacheKeeper *receiver = keeper->receiver)
                    QMetaObject::invokeMethod(
                            receiver, [receiver, result] { receiver->d->report(result); }, Qt::QueuedConnection);
            });
            return;
        }
        writer = std::make_unique<indexcache::Writer>(std::move(snapshot));
        writerNumber = number;
        buffer.clear();
        if (sliced)
            slices.start();
        else
            finishWriter();
    }

    /// The inline encoding: the rest at once, then the file.
    void finishWriter()
    {
        slices.stop();
        while (buffer.size() <= policy.maxBytes
               && writer->step(std::numeric_limits<int>::max(), &buffer) == indexcache::Writer::State::More) { }
        writer.reset();
        QByteArray bytes = std::exchange(buffer, QByteArray());
        SaveResult result;
        if (bytes.size() > policy.maxBytes)
            result.error = tooLarge();
        else
            result.error = writeSnapshotFile(path, bytes, writerNumber, &result.superseded);
        result.bytes = result.error.isEmpty() ? bytes.size() : 0;
        result.ms = clock.isValid() ? clock.elapsed() : 0;
        report(result);
    }

    void report(const SaveResult &result)
    {
        if (result.fromPool)
            --poolJobs;
        // Changes made meanwhile are saved at the latest after the longest wait.
        if (dirty && !due.isActive() && !stepping())
            due.start(policy.maxDirtyMs);
        if (result.superseded)
            return; // a newer snapshot's file is there already
        if (!result.error.isEmpty()) {
            if (!failureLogged) {
                failureLogged = true; // once per session (a read-only folder, a full disk)
                qCWarning(logging::lcIndex).noquote() << u"Index cache not saved: %1"_s.arg(result.error);
            }
            Q_EMIT q->saveFailed(result.error);
            return;
        }
        savedOnce = true;
        qCInfo(logging::lcIndex).noquote()
                << u"Index cache saved: %1 notes, %2 in %3"_s.arg(index.paths().size())
                           .arg(logging::formatBytes(result.bytes), logging::formatMs(result.ms));
        Q_EMIT q->saved(result.bytes, result.ms);
    }

    /// Drop the steps of a save under way (its snapshot is taken again).
    void cancelSteps()
    {
        due.stop();
        slices.stop();
        taker.reset();
        writer.reset();
        buffer = QByteArray();
    }
};

IndexCacheKeeper::IndexCacheKeeper(Vault &vault, VaultIndex &index, WorkerPool pool, QString path, Policy policy,
                                   QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(this, vault, index, std::move(pool), std::move(path), policy))
{
    d->shared->receiver = this;
    d->due.setSingleShot(true);
    connect(&d->due, &QTimer::timeout, this, [this] { d->startSave(); });
    // 1 ms, not 0: a repeating 0 ms timer stalls Qt's timers in the browser (docs/WEB.md).
    d->slices.setInterval(1ms);
    connect(&d->slices, &QTimer::timeout, this, [this] { d->slice(); });
    connect(&index, &VaultIndex::changed, this, [this] { d->onIndexChanged(); });
}

IndexCacheKeeper::~IndexCacheKeeper()
{
    std::lock_guard lock(d->shared->mutex);
    d->shared->receiver = nullptr; // (a write on the pool still finishes)
}

void IndexCacheKeeper::loaded(const IndexLoader::Stats &stats)
{
    d->loaded = true;
    // (Documents pending: the cache's full-text index lacked some, or had stale ones.)
    d->dirty = stats.cache != u"used" || stats.parsed > 0 || stats.recordsRejected > 0 || !stats.searchFromCache
               || d->index.pendingSearchDocuments() > 0;
    if (d->dirty)
        d->due.start(d->policy.firstSaveQuietMs);
}

void IndexCacheKeeper::saveIfDirty()
{
    if (d->writer && !d->dirty) {
        d->finishWriter(); // an inline encoding under way: its snapshot is the newest
        return;
    }
    if (!d->dirty)
        return;
    d->cancelSteps();
    if (!d->bigEnough()) {
        d->dirty = false;
        return;
    }
    // The few documents still pending (an edit a moment ago) go into the file; more (a bulk
    // build) are left to the next load.
    if (const qsizetype pending = d->index.pendingSearchDocuments(); pending > 0 && pending <= FewPendingDocuments)
        d->index.flushSearch();
    d->clock.start();
    std::shared_ptr<const indexcache::Snapshot> snapshot =
            indexcache::Snapshot::take(d->index, d->identity(), d->statFn());
    d->dirty = !snapshot->searchComplete();
    d->encode(std::move(snapshot), false);
}

bool IndexCacheKeeper::saveNow()
{
    d->cancelSteps();
    if (!d->bigEnough())
        return false;
    d->dirty = false;
    d->clock.start();
    const quint64 number = ++snapshotNumbers;
    const QByteArray bytes = indexcache::encode(d->index, d->identity(), d->statFn());
    SaveResult result;
    if (bytes.size() > d->policy.maxBytes)
        result.error = d->tooLarge();
    else
        result.error = writeSnapshotFile(d->path, bytes, number, &result.superseded);
    result.bytes = result.error.isEmpty() ? bytes.size() : 0;
    result.ms = d->clock.elapsed();
    d->report(result);
    return result.error.isEmpty();
}

bool IndexCacheKeeper::isDirty() const
{
    return d->dirty;
}

bool IndexCacheKeeper::hasSaved() const
{
    return d->savedOnce;
}

bool IndexCacheKeeper::isSaving() const
{
    return d->stepping() || d->poolJobs > 0;
}

QString IndexCacheKeeper::path() const
{
    return d->path;
}

} // namespace brulion::notebook
