// One backup as a ZIP archive, written and read with streamed miniz: a notebook's attachments may
// be hundreds of megabytes and phones are small, so neither side ever holds the whole archive in
// memory (restoring holds one file at a time). Part of the backup module — see backup/backup.h.
#pragma once

#include "backup/backup.h"

#include <QMetaType>
#include <QObject>

#include <atomic>
#include <functional>
#include <memory>

namespace brulion::notebook {
struct WorkerPool;
} // namespace brulion::notebook

namespace brulion::backup {

namespace detail {
class TaskRunner;
class WriteTask;
} // namespace detail

/// Why a backup could not be written (the app turns these into its messages).
enum class WriteError {
    None,
    Cancelled,
    FolderMissing,    ///< the folder is gone: an unplugged drive, a removed folder
    FolderUnwritable, ///< it refuses the temporary file
    WriteFailed,      ///< writing failed under way: a full disk, a drive pulled out
    TooLarge,         ///< above WriteOptions::maxBytes
    NothingArchived,  ///< no file of the notebook could be read: such an archive is no backup
};

struct WriteOptions {
    QString targetPath;        ///< the backup's absolute path (freeBackupPath())
    QString notebookId;        ///< Vault::id(), for the manifest
    bool createFolder = false; ///< make a missing folder (the device's own; a chosen one is not made)
    /// Write the temporary file here and move it to `targetPath` when finished ("": beside the
    /// target). The browser: outside the storage's mounts, which would save it again and again
    /// while it grows.
    QString workFolder;
    qint64 maxBytes = 0; ///< the archive may not grow above this; 0: no limit
    int compressionLevel = 6;
    /// In slices on the calling thread (no pool) files above this are stored, not deflated: a
    /// single file is the smallest slice, and storing a big one takes a moment only.
    qint64 inlineDeflateLimit = 4 << 20;
    /// Tests: called before each read of a file (its path, the offset), on the writing thread.
    std::function<void(const VaultPath &, qint64)> onRead;
};

struct WriteResult {
    WriteError error = WriteError::None;
    QString detail; ///< English, for the log
    QString path;   ///< the finished backup; "" unless error is None
    qint64 bytes = 0;
    int files = 0;
    /// Files left out: they could not be read, kept changing, or have names a backup may not hold.
    QStringList skipped;
    QStringList vanished; ///< deleted or renamed after the run started, or the storage went

    bool ok() const { return error == WriteError::None; }
    /// Nothing was left out. Only then the run stands for the notebook: its fingerprint is stored
    /// and older complete backups may go. Otherwise the archive is kept as an incomplete copy.
    bool complete() const { return ok() && skipped.isEmpty() && vanished.isEmpty(); }
};

/// Writes one backup (see the file comment). On a pool thread, or in slices without threads. A
/// file that changes while being read is read again, so the archive never holds a torn entry. A
/// failed or cancelled run leaves nothing behind; earlier backups are never touched.
class BackupWriter : public QObject {
    Q_OBJECT

public:
    explicit BackupWriter(const notebook::WorkerPool &pool, QObject *parent = nullptr);
    /// Cancels; a pool job still running stops at its next read and removes its temporary file.
    ~BackupWriter() override;

    /// Start writing `sources`; finished() follows on this thread. One run at a time.
    void start(const Sources &sources, const WriteOptions &options);
    /// finished() with WriteError::Cancelled follows soon.
    void cancel();
    bool isRunning() const;
    /// 0…1 of the bytes to read.
    qreal progress() const;

Q_SIGNALS:
    void finished(const brulion::backup::WriteResult &result);

private:
    std::unique_ptr<detail::TaskRunner> m_runner;
    std::shared_ptr<detail::WriteTask> m_task;
};

/// Short file operations off the owner's thread: a folder may be on a slow or unplugged drive. With
/// a pool on one of its threads, without one on a later turn of the owner's event loop.
class FolderWork : public QObject {
public:
    explicit FolderWork(const notebook::WorkerPool &pool, QObject *parent = nullptr);
    /// `done` is never called after this; `work` holds no reference to the owner.
    ~FolderWork() override;
    /// `work` touches only what it captures; `done` (may be empty) follows on the owner's thread.
    void run(std::function<void()> work, std::function<void()> done);

private:
    struct Shared;
    std::function<void(std::function<void()>)> m_start;
    std::shared_ptr<Shared> m_shared;
};

/// What a backup's entry name may be. Strict: a backup may come from anywhere, and Brulion's own
/// never holds anything else (the writer leaves such files out and says so).
enum class EntryPath {
    Ok,
    Unsafe,  ///< absolute, "..", a backslash, a drive letter, control characters, not NFC, …
    Ignored, ///< .trash, .git, temporary files, … (.brulion/config.json is allowed)
};
EntryPath checkEntryPath(const QString &path);

/// Why a file is not accepted as a backup.
enum class ArchiveProblem {
    None,
    Unreadable,    ///< the file cannot be read
    NotAZip,       ///< no ZIP archive, or a damaged one
    UnsafePath,    ///< an entry escapes the notebook (checkEntryPath)
    IgnoredPath,   ///< an entry the notebook's folder may not hold
    Duplicate,     ///< two entries of one path, or a file used as a folder
    Unsupported,   ///< encrypted, or an unknown compression method
    TooLarge,      ///< above ArchiveLimits, or sizes no compression can give (a ZIP bomb)
    Damaged,       ///< an entry's bytes do not match its checksum
    NoFiles,       ///< no file of a notebook in it: restoring it would only empty the notebook
    CaseCollision, ///< names one case-insensitive storage cannot hold apart ("Plan.md", "plan.md")
};

struct ArchiveLimits {
    int maxEntries = 1'000'000;
    qint64 maxEntryBytes = qint64(64) << 30;
    qint64 maxTotalBytes = qint64(256) << 30;
};

struct ArchiveEntry {
    VaultPath path; ///< without the trailing "/" of a folder
    int index = -1; ///< in the archive
    bool folder = false;
    qint64 size = 0;
    qint64 compressedSize = 0;
    quint32 crc32 = 0;
};

struct ArchiveInfo {
    ArchiveProblem problem = ArchiveProblem::None;
    QString detail; ///< English, for the log (an entry's name with its control characters replaced)
    QList<ArchiveEntry> entries;
    qint64 totalBytes = 0;

    bool ok() const { return problem == ArchiveProblem::None; }
};

/// A backup read entry by entry. Used by one thread at a time. Malformed or hostile input gives
/// problems, never a crash: names and sizes are the archive's claims until checked.
class ArchiveReader {
public:
    explicit ArchiveReader(const QString &path);
    ~ArchiveReader();
    Q_DISABLE_COPY_MOVE(ArchiveReader)

    /// The entries, every one checked (central directory only: no entry's bytes are read).
    ArchiveInfo readEntries(const ArchiveLimits &limits = {});
    /// Inflate the entry and compare its checksum, keeping nothing.
    bool verify(const ArchiveEntry &entry, const std::atomic_bool *cancel = nullptr);
    /// The entry's bytes; nothing when it is damaged or larger than it claimed.
    std::optional<QByteArray> read(const ArchiveEntry &entry);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

/// CRC-32 (as ZIP records it) of a file on disk; nothing when it cannot be read or `cancel` is set.
std::optional<quint32> fileCrc32(const QString &absolutePath, const std::atomic_bool *cancel = nullptr);

} // namespace brulion::backup

Q_DECLARE_METATYPE(brulion::backup::WriteResult)
