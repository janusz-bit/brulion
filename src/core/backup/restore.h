// Restoring a backup into the open notebook as a reviewable, reversible operation: planRestore()
// says what would change, come back and go to the trash (the user sees the counts first), then
// RestoreApplier applies it through the Vault with ChangeSource::Import — what is replaced stays in
// History, what the backup does not hold goes to the trash, and the sync's mass-deletion guard sees
// ordinary deletions. Nothing is written before the whole backup was checked. Part of the backup
// module — see backup/backup.h.
#pragma once

#include "backup/archive.h"

#include <QHash>
#include <QObject>

#include <atomic>
#include <memory>
#include <optional>

class QTimer;

namespace brulion::backup {

namespace detail {
class TaskRunner;
class CheckTask;
} // namespace detail

/// One file of a restore's target (from the vault's cache).
struct NotebookFile {
    VaultPath path;
    QString absolutePath;
    qint64 size = 0;
};

/// An entry to write, and where: the notebook's spelling of a name that differs only in letter
/// case (case-insensitive storage holds one file for both).
struct PlannedWrite {
    ArchiveEntry entry;
    VaultPath target;
};

struct RestorePlan {
    QList<PlannedWrite> toChange;   ///< the file exists with other bytes
    QList<PlannedWrite> toRecreate; ///< the file is gone
    QList<VaultPath> toTrash;       ///< files of the notebook the backup does not hold
    /// Trashed before anything is written: a folder where the backup has a file, a file where the
    /// backup has a folder. (Their files are also in toTrash.)
    QList<VaultPath> removeFirst;
    QStringList foldersToCreate; ///< empty folders of the backup that are missing
    QStringList tooLarge;        ///< entries above the limit: left alone (the file stays as it is)
    int identical = 0;           ///< same bytes: not touched
    qint64 bytesToWrite = 0;
    /// The backup left files out, or does not say: what it lacks may just have been missed, so only
    /// blockers go to the trash (RestoreChecker). planRestore() itself does not decide this.
    bool incomplete = false;

    bool hasChanges() const
    {
        return !toChange.isEmpty() || !toRecreate.isEmpty() || !toTrash.isEmpty() || !foldersToCreate.isEmpty();
    }
};

/// The largest entry restored: Vault writes hold a file in memory.
constexpr qint64 MaxRestoredEntryBytes = qint64(1) << 30;

/// The plan (pure). `crcOf`: the CRC-32 of the notebook's files whose size equals their entry's
/// (a file without one counts as different). .brulion/config.json is compared and written like a
/// file, but never trashed: a backup without it leaves the settings alone. Names differing only in
/// letter case are different files unless `caseInsensitive` (the notebook's storage holds one file
/// for both); then an entry takes the notebook's spelling, unless another entry has that very name.
RestorePlan planRestore(const QList<ArchiveEntry> &entries, const QList<NotebookFile> &current,
                        const QStringList &currentFolders, const QHash<VaultPath, quint32> &crcOf,
                        bool caseInsensitive = false, qint64 maxEntryBytes = MaxRestoredEntryBytes);

/// The notebook's files as a restore compares them. On the vault's thread, from its cache.
QList<NotebookFile> notebookFiles(const Vault &vault);
QStringList notebookFolders(const Vault &vault);

/// What checking a backup found.
struct RestoreCheckOptions {
    QString notebookRoot;                ///< its storage is probed (isCaseInsensitiveFolder())
    std::optional<bool> caseInsensitive; ///< tests: instead of the probe
    ArchiveLimits limits;
};
struct RestoreCheck {
    ArchiveProblem problem = ArchiveProblem::None;
    bool cancelled = false;
    QString detail; ///< English, for the log
    RestorePlan plan;

    bool ok() const { return problem == ArchiveProblem::None && !cancelled; }
};

/// Checks a backup against the notebook without blocking: every entry validated, the notebook's
/// files compared (size, then CRC-32), the bytes to be written inflated and their checksums
/// compared. On a pool thread, or in slices without threads.
class RestoreChecker : public QObject {
    Q_OBJECT

public:
    explicit RestoreChecker(const notebook::WorkerPool &pool, QObject *parent = nullptr);
    ~RestoreChecker() override;

    void start(const QString &archivePath, const QList<NotebookFile> &current, const QStringList &currentFolders,
               const RestoreCheckOptions &options = {});
    void cancel();
    bool isRunning() const;

Q_SIGNALS:
    void finished(const brulion::backup::RestoreCheck &check);

private:
    std::unique_ptr<detail::TaskRunner> m_runner;
    std::shared_ptr<detail::CheckTask> m_task;
};

struct RestoreResult {
    int changed = 0;
    int recreated = 0;
    int trashed = 0;
    int foldersCreated = 0;
    QStringList failed;        ///< files that could not be written or trashed
    bool cancelled = false;    ///< stopped: what was done stays done
    bool trashSkipped = false; ///< something could not be written: nothing else was trashed
    QString detail;            ///< English, for the log

    bool ok() const { return failed.isEmpty() && !cancelled && !trashSkipped && detail.isEmpty(); }
};

/// Applies a plan: blockers to the trash, files written, folders made, the rest to the trash — the
/// last only when every write succeeded. Reads on a pool thread (or in slices), and writes through
/// `vault` on the owner's thread a slice at a time. The vault must outlive this object.
class RestoreApplier : public QObject {
    Q_OBJECT

public:
    RestoreApplier(Vault &vault, const notebook::WorkerPool &pool, QObject *parent = nullptr);
    ~RestoreApplier() override;

    void start(const RestorePlan &plan, const QString &archivePath);
    void cancel();
    bool isRunning() const { return m_running; }
    qreal progress() const;

Q_SIGNALS:
    void finished(const brulion::backup::RestoreResult &result);
    /// `path` (a file or a folder) goes to the trash now: an editor showing it must not save it back.
    void aboutToRemove(const QString &path);

private:
    struct ReadBatch;
    void removeBlockers();
    void readNextBatch();
    void writeSlice();
    void trashSlice();
    void createFolders();
    bool writeOne(const PlannedWrite &write, const QByteArray &bytes);
    void finish();

    Vault &m_vault;
    std::unique_ptr<detail::TaskRunner> m_runner;
    QTimer *m_timer = nullptr;
    RestorePlan m_plan;
    QList<PlannedWrite> m_writes; ///< toChange, then toRecreate
    qsizetype m_nextRead = 0;
    qsizetype m_nextWrite = 0;
    qsizetype m_nextTrash = 0;
    std::shared_ptr<ReadBatch> m_batch;
    std::shared_ptr<ArchiveReader> m_reader;
    bool m_readerChecked = false;
    bool m_running = false;
    std::shared_ptr<std::atomic_bool> m_cancel = std::make_shared<std::atomic_bool>(false);
    RestoreResult m_result;
};

} // namespace brulion::backup

Q_DECLARE_METATYPE(brulion::backup::RestoreCheck)
Q_DECLARE_METATYPE(brulion::backup::RestoreResult)
