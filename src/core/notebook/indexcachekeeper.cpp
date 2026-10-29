#include "notebook/indexcachekeeper.h"

#include "index/indexcache.h"
#include "index/vaultindex.h"
#include "log/categories.h"
#include "log/slowscope.h"
#include "vault/vault.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>
#include <mutex>
#include <utility>

namespace brulion::notebook {

using namespace Qt::StringLiterals;

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

/// What a write job shares with its keeper (which may be gone when it ends).
struct KeeperShared {
    std::mutex mutex;
    bool cancelled = false;
    IndexCacheKeeper *receiver = nullptr;
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
    QTimer quiet;
    QTimer slices;
    std::unique_ptr<indexcache::Writer> writer;
    QByteArray buffer;
    QElapsedTimer clock;
    bool loaded = false;
    bool dirty = false;
    bool writing = false;
    bool failureLogged = false;
    bool savedOnce = false;
    int searchWaits = 0;

    Private(IndexCacheKeeper *owner, Vault &v, VaultIndex &i, WorkerPool p, QString file, Policy pol)
        : q(owner), vault(v), index(i), pool(std::move(p)), path(std::move(file)), policy(pol)
    { }

    indexcache::Identity identity() const { return indexcache::Identity::current(index.config(), vault.id()); }

    indexcache::StatFn statFn() const
    {
        Vault *v = &vault;
        return [v](const VaultPath &p) { return v->stat(p); };
    }

    void onIndexChanged()
    {
        if (!loaded)
            return;
        dirty = true;
        // A running encoding aborts at its next step (the index's version moved).
        quiet.start(policy.quietMs);
    }

    bool bigEnough() const { return index.paths().size() >= policy.minNotes; }

    void startSave()
    {
        if (writer || writing || !dirty)
            return;
        if (!bigEnough()) {
            dirty = false;
            return;
        }
        // Wait a little for the full-text index (the file is more useful with it).
        if (index.pendingSearchDocuments() > 0 && searchWaits < 5) {
            ++searchWaits;
            quiet.start(1000);
            return;
        }
        searchWaits = 0;
        dirty = false;
        writer = std::make_unique<indexcache::Writer>(index, identity(), statFn());
        buffer.clear();
        clock.start();
        slices.start();
    }

    void slice()
    {
        if (!writer) {
            slices.stop();
            return;
        }
        const indexcache::Writer::State state = writer->step(policy.sliceMs, &buffer);
        if (state == indexcache::Writer::State::More && buffer.size() <= policy.maxBytes)
            return;
        slices.stop();
        writer.reset();
        if (state == indexcache::Writer::State::Aborted) {
            buffer = QByteArray();
            dirty = true;
            quiet.start(policy.quietMs);
            return;
        }
        if (buffer.size() > policy.maxBytes) {
            buffer = QByteArray();
            report(u"larger than %1"_s.arg(logging::formatBytes(policy.maxBytes)), 0);
            return;
        }
        write(std::exchange(buffer, QByteArray()));
    }

    void write(QByteArray bytes)
    {
        writing = true;
        const qint64 size = bytes.size();
        if (pool.isInline()) {
            report(writeIndexCacheFile(path, bytes), size);
            return;
        }
        pool.start([keeper = shared, file = path, bytes = std::move(bytes), size] {
            const QString error = writeIndexCacheFile(file, bytes);
            std::lock_guard lock(keeper->mutex);
            if (keeper->cancelled || !keeper->receiver)
                return;
            IndexCacheKeeper *receiver = keeper->receiver;
            QMetaObject::invokeMethod(
                    receiver, [receiver, error, size] { receiver->d->report(error, size); }, Qt::QueuedConnection);
        });
    }

    void report(const QString &error, qint64 size)
    {
        writing = false;
        if (!error.isEmpty()) {
            if (!failureLogged) {
                failureLogged = true; // once per session (a read-only folder, a full disk)
                qCWarning(logging::lcIndex).noquote() << u"Index cache not saved: %1"_s.arg(error);
            }
            Q_EMIT q->saveFailed(error);
            return;
        }
        savedOnce = true;
        const qint64 ms = clock.isValid() ? clock.elapsed() : 0;
        qCInfo(logging::lcIndex).noquote() << u"Index cache saved: %1 notes, %2 in %3"_s.arg(index.paths().size())
                                                      .arg(logging::formatBytes(size), logging::formatMs(ms));
        Q_EMIT q->saved(size, ms);
    }
};

IndexCacheKeeper::IndexCacheKeeper(Vault &vault, VaultIndex &index, WorkerPool pool, QString path, Policy policy,
                                   QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(this, vault, index, std::move(pool), std::move(path), policy))
{
    d->shared->receiver = this;
    d->quiet.setSingleShot(true);
    connect(&d->quiet, &QTimer::timeout, this, [this] { d->startSave(); });
    // 1 ms, not 0: a repeating 0 ms timer stalls Qt's timers in the browser (docs/WEB.md).
    d->slices.setInterval(1);
    connect(&d->slices, &QTimer::timeout, this, [this] { d->slice(); });
    connect(&index, &VaultIndex::changed, this, [this] { d->onIndexChanged(); });
}

IndexCacheKeeper::~IndexCacheKeeper()
{
    std::lock_guard lock(d->shared->mutex);
    d->shared->cancelled = true;
    d->shared->receiver = nullptr;
}

void IndexCacheKeeper::loaded(const IndexLoader::Stats &stats)
{
    d->loaded = true;
    d->dirty = stats.cache != u"used" || stats.parsed > 0 || stats.recordsRejected > 0 || !stats.searchFromCache;
    if (d->dirty)
        d->quiet.start(d->policy.firstSaveQuietMs);
}

bool IndexCacheKeeper::saveNow()
{
    d->quiet.stop();
    d->slices.stop();
    d->writer.reset();
    d->buffer = QByteArray();
    if (!d->bigEnough())
        return false;
    d->dirty = false;
    d->clock.start();
    const QByteArray bytes = indexcache::encode(d->index, d->identity(), d->statFn());
    if (bytes.size() > d->policy.maxBytes) {
        d->report(u"larger than %1"_s.arg(logging::formatBytes(d->policy.maxBytes)), 0);
        return false;
    }
    const QString error = writeIndexCacheFile(d->path, bytes);
    d->report(error, bytes.size());
    return error.isEmpty();
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
    return d->writer != nullptr || d->writing;
}

QString IndexCacheKeeper::path() const
{
    return d->path;
}

} // namespace brulion::notebook
