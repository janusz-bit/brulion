// IndexCacheKeeper — keeps a notebook's index cache file (index/indexcache.h, L2 of
// docs/PERFORMANCE.md) up to date: written after the first load when anything had to be parsed,
// then after changes once the index has been quiet for a while; encoded in slices on the owner's
// thread (indexcache::Writer), written on a pool thread (QSaveFile: the commit's fsync never
// stalls the GUI; inline without threads). No file for small notebooks (Policy::minNotes) nor
// above Policy::maxBytes. Part of the notebook module — see notebook/notebook.h.
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
        int quietMs = 60000;         ///< after the last change of the index
        qsizetype minNotes = 200;    ///< no cache for smaller notebooks (the guide, samples)
        qint64 maxBytes = 256LL << 20;
        int sliceMs = 8;             ///< encoding time per event-loop turn
    };

    /// `path`: the cache file (<device state folder>/index-cache.bin). The vault and the index
    /// must outlive the keeper.
    IndexCacheKeeper(Vault &vault, VaultIndex &index, WorkerPool pool, QString path, Policy policy,
                     QObject *parent = nullptr);
    ~IndexCacheKeeper() override;

    /// The index was loaded (IndexLoader::stats()): save soon when anything was parsed or the
    /// cache had no full-text index; from now on save after quiet changes.
    void loaded(const IndexLoader::Stats &stats);
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

} // namespace brulion::notebook
