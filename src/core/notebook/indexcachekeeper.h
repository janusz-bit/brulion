// IndexCacheKeeper — keeps a notebook's index cache file (index/indexcache.h, L2 of
// docs/PERFORMANCE.md) up to date: written soon after the first load when anything had to be
// parsed, then when the app asks for it (saveIfDirty(): the app goes to the background, the
// notebook closes) and, while the app stays in front, at the latest Policy::maxDirtyMs after the
// first change not saved yet — never again and again while someone types. The owner's thread
// only takes a snapshot of the index (indexcache::SnapshotTaker: reference counts, a few
// milliseconds per event-loop turn); encoding it and writing the file (QSaveFile: the commit's
// fsync) run on a pool thread. Without threads (WebAssembly) the encoding runs on the owner's
// thread too, in slices — or at once in saveIfDirty(), when nobody is looking. No file for small
// notebooks (Policy::minNotes) nor above Policy::maxBytes. Part of the notebook module — see
// notebook/notebook.h.
#pragma once

#include "notebook/indexloader.h"

#include <QObject>
#include <QString>

#include <memory>

namespace brulion::notebook {

class IndexCacheKeeper : public QObject {
    Q_OBJECT

public:
    struct Policy {
        int firstSaveQuietMs = 5000; ///< after a load that parsed anything
        /// Unsaved changes are written at the latest this long after the first of them (while the
        /// app stays in front: going to the background writes them at once, saveIfDirty()).
        int maxDirtyMs = 10 * 60 * 1000;
        qsizetype minNotes = 200; ///< no cache for smaller notebooks (the guide, samples)
        qint64 maxBytes = 256LL << 20;
        int sliceMs = 4; ///< the owner's thread's work per event-loop turn (snapshot; inline: encoding)
    };

    /// `path`: the cache file (<device state folder>/index-cache.bin). The vault and the index
    /// must outlive the keeper.
    IndexCacheKeeper(Vault &vault, VaultIndex &index, WorkerPool pool, QString path, Policy policy,
                     QObject *parent = nullptr);
    ~IndexCacheKeeper() override;

    /// The index was loaded (IndexLoader::stats()): save soon when anything was parsed or the
    /// cache had no full-text index; from now on follow the changes (see the file comment).
    void loaded(const IndexLoader::Stats &stats);
    /// Write what is not saved yet, now: the app went to the background or loses the notebook
    /// (Workspace: an inactive application state and close()). Only the snapshot is taken on this
    /// thread (at once, with the few full-text documents still pending built first); the encoding
    /// and the file follow on a pool thread — whose owner must let its jobs finish before the
    /// process ends (QThreadPool's destructor waits) — or, without threads, here and now. A
    /// no-op when nothing changed since the last snapshot and for small notebooks.
    void saveIfDirty();
    /// Encode and write now, on this thread (brulion-cli at exit, tests). True when written.
    bool saveNow();
    /// Changes not saved yet.
    bool isDirty() const;
    /// The file was written by this keeper (since it was created).
    bool hasSaved() const;
    /// An encoding or a write is under way.
    bool isSaving() const;
    QString path() const;

Q_SIGNALS:
    /// The file was written (`bytes`, `ms` from the start of the encoding).
    void saved(qint64 bytes, qint64 ms);
    /// It could not be written (English, for the log); the old file stays.
    void saveFailed(const QString &error);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

/// Write `bytes` to `path` atomically (QSaveFile; the folder is created). "" or the error.
QString writeIndexCacheFile(const QString &path, const QByteArray &bytes);

/// The largest index cache worth writing where storage has a quota (the browser's IndexedDB,
/// docs/WEB.md): the cache file it replaces (`replaced` bytes) plus half of what is still free, so
/// notes, History and settings keep the other half — never more than `cap`. Unknown `usage` or
/// `quota` (< 0, quota 0): `cap`. For Policy::maxBytes.
qint64 indexCacheBudget(qint64 usage, qint64 quota, qint64 replaced, qint64 cap);

} // namespace brulion::notebook
