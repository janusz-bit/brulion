// Internal helpers of the vault module: path arithmetic used by the Vault (pure functions,
// unit-tested in tst_vault_paths) and the module's logging category. Not part of the public API.
#pragma once

#include "log/categories.h"
#include "types.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QStringList>

#include <functional>
#include <optional>

namespace brulion {

/// "brulion.vault" (the log module's category).
using logging::lcVault;

namespace vaultutil {

/// ".brulion" — the folder of the shared vault settings (ignored except for config.json).
QString configFolder();

/// Ancestor folders, outermost first: "a/b/c.md" -> {"a", "a/b"}; "c.md" -> {}.
QStringList ancestors(const VaultPath &path);

/// True if `path` equals `dir` or lies inside it. The root ("") contains everything.
bool isWithin(const VaultPath &path, const VaultPath &dir);

/// True if `path` lies strictly inside `dir`.
bool isStrictlyWithin(const VaultPath &path, const VaultPath &dir);

/// Re-root `path` from `from` to `to`: rebase("a/x/y.md", "a", "b") == "b/x/y.md".
/// `path` must be within `from`.
VaultPath rebase(const VaultPath &path, const VaultPath &from, const VaultPath &to);

/// Insert `suffix` before the extension of the last segment: ("a/b.md", " 2") -> "a/b 2.md".
/// Folders (`isDir`), names without an extension and dot files (".env") get it appended.
VaultPath insertBeforeExtension(const VaultPath &path, const QString &suffix, bool isDir = false);

/// `base`, or the first of "Name 2.ext", "Name 3.ext", ... for which `isTaken` is false.
/// Unlike paths::uniquePath() the extension keeps its case ("Raport.PDF" -> "Raport 2.PDF").
VaultPath uniqueName(const VaultPath &base, const std::function<bool(const VaultPath &)> &isTaken, bool isDir = false);

/// Local wall-clock trash stamp "yyyyMMdd-HHmmss".
QString trashStamp(const QDateTime &time);

/// Remove a " (yyyyMMdd-HHmmss)" or " (yyyyMMdd-HHmmss) n" trash suffix from one path segment
/// ("a (20261014-101112) 2.md" -> "a.md").
QString stripTrashStamp(const QString &segment);

/// Files Brulion handles as UTF-8 text (hashed, cached and snapshotted into History);
/// everything else is binary and compared by size and modification time only.
bool isTextPath(const VaultPath &path);

/// Paths the vault tracks: everything paths::isIgnored() lets through, plus the vault config.
bool isTracked(const VaultPath &path);

/// "\xEF\xBB\xBF" — the UTF-8 byte order mark.
QByteArray utf8Bom();

/// Writes `bytes` to `absolutePath` atomically (QSaveFile: a temporary file renamed over it). A
/// rename refused for a moment — on Windows while another program has the file open for a look
/// after each write (a virus scanner, an indexer) — is tried again a few times within about a
/// tenth of a second, as Syncthing and editors do; any other failure is final. `error`: why not.
bool saveAtomically(const QString &absolutePath, QByteArrayView bytes, QString *error);

/// What saveAtomicallyStamped() saw of the folder around its own changes of the folder's entries.
struct StampedSave {
    bool ok = false;
    QString error; ///< why not, when !ok
    /// The folder's fineMtime(): right before the temporary file was created, right after it was
    /// created, right before the rename over the file and right after it. Equal `afterTemp` and
    /// `beforeRename`: nothing changed the folder's entries while the data was written and synced,
    /// so between `before` and `after` only the creation of the temporary file and the rename
    /// happened there (each one system call) — unless another program changed it in the same
    /// clock tick. nullopt where unknown (coarse times, the fallback without a sync of its own).
    std::optional<qint64> before, afterTemp, beforeRename, after;
    /// The written file's fineMtime() and size, taken from the temporary file before the rename
    /// (another program's file renamed over it later has another stamp).
    std::optional<qint64> fileMtime;
    qint64 fileSize = -1;
};

/// saveAtomically() that writes the temporary file itself (`.brulion-tmp-…` in the same folder,
/// the existing file's permissions), syncs it to the disk and only then renames it over the file,
/// taking the folder's stamps around each step (StampedSave). `beforeRename` (tests) runs between
/// the sync and the rename. Without a file sync of its own (filesync.h: none set) it is
/// saveAtomically() without stamps.
StampedSave saveAtomicallyStamped(const QString &absolutePath, QByteArrayView bytes,
                                  const std::function<void()> &beforeRename = {});

/// The modification time of the folder (or file) at `absolutePath` in nanoseconds (of the file
/// system's clock, not the Unix epoch), when the file system keeps it finer than milliseconds:
/// nullopt when it cannot be read or is a whole millisecond (coarse timestamps — FAT, exFAT,
/// HFS+, the browser's file system — where a change by another program right after one of ours
/// could keep it).
std::optional<qint64> fineMtime(const QString &absolutePath);

/// UTF-8 bytes to text with a leading byte order mark removed.
QString decodeUtf8(const QByteArray &bytes);

/// A file's content as readContent() found it.
struct ContentRead {
    enum class Status {
        Ok,
        TooBig, ///< more than the limit, by its size or by what reading it gave
        Link,   ///< a symlink: the vault never follows links to files (vault.h, "Semantics")
        Failed, ///< cannot be opened or read (`error`)
    };
    Status status = Status::Failed;
    QByteArray bytes; ///< the content, when Ok
    QString error;    ///< QFile::errorString(), when Failed
    bool ok() const { return status == Status::Ok; }
};

/// The content of the file at `absolutePath`, at most `limit` bytes: every read in the vault goes
/// through here. A file that reports a size is read up to it in one go (as QIODevice::readAll());
/// one that reports none — empty, or a file of /proc or /sys, some of which never end — in pieces
/// until it ends or would hold more than `limit` bytes (never more than that in memory).
ContentRead readContent(const QString &absolutePath, qint64 limit);

} // namespace vaultutil
} // namespace brulion
