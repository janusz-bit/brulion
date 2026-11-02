// brulion::backup — automatic backups of a notebook, GUI-free. Include the module's headers for the
// whole API; the app's side is shell/workspacebackups.h, docs/ARCHITECTURE.md "Data safety".
//
// API overview (namespace brulion::backup; never reads the clock — callers pass `now`; no threads
// of its own — a notebook::WorkerPool is passed, without one the work runs in slices):
//
//   backup.h    names: backupFileName(notebook, now) "Brulion-<notebook>-yyyy-MM-dd-HHmmss.zip",
//               backupFileTime(file, notebook) (no time: not ours), freeBackupPath; whose it is:
//               backupManifest / readBackupManifest (the ZIP's comment: notebook id, complete or
//               not); the folder: listBackups(folder, notebook) newest first, backupsToPrune /
//               pruneBackups (oldest beyond the limit, only this notebook's by its id, never one
//               protected), removeStalePartials, isInsideFolder (a folder inside the notebook is
//               refused), checkFolderWritable, isCaseInsensitiveFolder; what goes in:
//               collectSources(vault) -> Sources, fingerprint();
//               the schedule: isDue(interval, last, now), decide(settings, state, fingerprint, now)
//   archive.h   BackupWriter (one ZIP: streamed, files changed during the run read again, written
//               to a temporary file and renamed on success; finished(WriteResult)); ArchiveReader
//               (a backup read with strict checks: readEntries() -> ArchiveInfo, verify, read);
//               checkEntryPath (what a backup may contain)
//   restore.h   planRestore (pure: what would change, come back, go to the trash), RestoreChecker
//               (a backup checked against the notebook, off the owner's thread), RestoreApplier
//               (the plan applied through the Vault: History snapshots, the trash)
//
// Backups are plain ZIP archives of the notebook's files exactly as bytes: everything Vault::files()
// lists, .brulion/config.json and the empty folders (directory entries); never .trash, .git or
// other ignored and temporary files. The list recognises a file by its whole name; whose it is and
// whether it is complete the archive's comment says (a small JSON object any ZIP tool ignores).
#pragma once

#include "types.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace brulion {
class Vault;
} // namespace brulion

namespace brulion::backup {

constexpr int DefaultKeep = 7;
/// The browser keeps its files in memory as well as in IndexedDB (docs/WEB.md): fewer copies.
constexpr int BrowserDefaultKeep = 2;
constexpr int MinKeep = 1;
constexpr int MaxKeep = 100;

enum class Interval { Daily, Weekly };

/// A notebook's name as a part of a file name ("Moje notatki/x" -> "Moje notatki x"): NFC, nothing
/// a file system or a link cannot take, never empty.
QString notebookFilePart(const QString &notebookName);
/// "Brulion-<notebook>-yyyy-MM-dd-HHmmss.zip".
QString backupFileName(const QString &notebookName, const QDateTime &now);
/// The time in the name of a backup of this notebook; nothing for any other file name, also for a
/// date that does not exist or another notebook's name.
std::optional<QDateTime> backupFileTime(const QString &fileName, const QString &notebookName);
/// `folder`/backupFileName() at `now`, or a second later while that name is taken.
QString freeBackupPath(const QString &folder, const QString &notebookName, const QDateTime &now);

/// What a backup says about itself, in the ZIP's comment.
struct BackupManifest {
    bool valid = false;    ///< Brulion wrote this comment (no comment, or another: not valid)
    QString notebookId;    ///< Vault::id() of the notebook it was made from
    bool complete = false; ///< no file was left out
    int files = 0;
};
/// The comment BackupWriter stores.
QByteArray backupManifest(const QString &notebookId, bool complete, int files);
/// The manifest of the archive at `path`, from its last bytes only. Never trusted for safety — an
/// archive may come from anywhere; it says which copies are this notebook's own. Any thread.
BackupManifest readBackupManifest(const QString &path);

struct BackupFile {
    QString fileName; ///< as it is on disk
    QString path;     ///< absolute
    QDateTime time;   ///< from the name
    qint64 size = 0;
    QString notebookId;    ///< from the manifest; "" when it has none
    bool complete = false; ///< from the manifest; without one: not known to be complete
};
/// The backups named after this notebook in `folder` (regular files, by whole name), newest first,
/// also those of another notebook of that name (their `notebookId` tells). Any thread.
QList<BackupFile> listBackups(const QString &folder, const QString &notebookName);

/// The file names to delete so that at most `keep` complete and `keep` incomplete backups of this
/// notebook stay (`backups` newest first, by the time in their names). Only backups with this
/// `notebookId` are ever given: another notebook's, or one without a manifest, is left alone and not
/// counted. Without `pruneComplete` (the new backup left files out) no complete one is given: an
/// incomplete copy never pushes a good one out. `protect` is never given and counts as kept. Pure.
QStringList backupsToPrune(const QList<BackupFile> &backups, int keep, const QString &notebookId,
                           const QSet<QString> &protect, bool pruneComplete = true);
/// backupsToPrune() on the folder's files, deleted. The names deleted; `failed` those that could
/// not be. Any thread.
QStringList pruneBackups(const QString &folder, const QString &notebookName, int keep, const QString &notebookId,
                         const QSet<QString> &protect, bool pruneComplete = true, QStringList *failed = nullptr);
/// Delete what an interrupted run of this notebook left (QSaveFile's "<backup name>.XXXXXX") when
/// older than a day. The number deleted. Any thread.
int removeStalePartials(const QString &folder, const QString &notebookName, const QDateTime &now);

/// `path` is `folder` or lies inside it, links resolved; for what does not exist yet the nearest
/// existing folder above it counts.
bool isInsideFolder(const QString &path, const QString &folder);
/// A file can be created in `folder`. False with `error` (English, for the log). Any thread.
bool checkFolderWritable(const QString &folder, QString *error = nullptr);
/// `folder` holds one file for "a" and "A" (Android's shared storage, Windows, macOS): a short-lived
/// probe file (a name the vault ignores). False when it cannot tell. Any thread.
bool isCaseInsensitiveFolder(const QString &folder);

/// One file of the notebook, as the vault knows it at the start of a run.
struct SourceFile {
    VaultPath path;
    QString absolutePath;
    qint64 size = 0;
    qint64 mtime = 0;
};
struct Sources {
    QList<SourceFile> files;  ///< sorted by path, .brulion/config.json included when it exists
    QStringList emptyFolders; ///< tracked folders with no tracked file or folder in them
    qint64 totalBytes = 0;

    /// The files that make a notebook not empty: without .brulion/config.json.
    qsizetype notebookFileCount() const;
};
/// What a backup of `vault` holds now. From the vault's cache (no disk access), on its thread.
Sources collectSources(const Vault &vault);
/// A hash of the paths, sizes and modification times: unchanged means nothing to back up.
QString fingerprint(const Sources &sources);

struct Settings {
    bool enabled = true;
    Interval interval = Interval::Daily;
    int keep = DefaultKeep;
};
struct ScheduleState {
    QDateTime lastBackup;        ///< of the last automatic or manual backup; invalid: none yet
    QString lastFingerprint;     ///< what it held
    bool lastFileExists = false; ///< the file is still in the folder in use
};
enum class Decision {
    Disabled,
    NotDue,    ///< the interval has not passed
    Unchanged, ///< due, but the last backup, still there, holds exactly this
    Empty,     ///< nothing to back up: an empty notebook never pushes good backups out
    Run,
};
/// The interval has passed: another calendar day (Daily), seven days (Weekly). No last backup, or a
/// last backup "in the future" (the clock was set back), is due: the schedule never wedges.
bool isDue(Interval interval, const QDateTime &last, const QDateTime &now);
/// What a scheduled check does. `fileCount`: Sources::notebookFileCount().
Decision decide(const Settings &settings, const ScheduleState &state, const QString &fingerprint, qsizetype fileCount,
                const QDateTime &now);

} // namespace brulion::backup
