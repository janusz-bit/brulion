// Vault — Brulion's view of a folder of notes (module "vault"; deps: util, text; QtCore only).
//
// API overview (namespace brulion)
//
//   Vault : QObject — one vault folder; everything is synchronous on the owner's thread.
//     Vault(rootPath, deviceStatePath, parent)  deviceStatePath = this vault's AppData folder:
//         History in <dir>/history, LocalStore in <dir>/store.json. Empty = no History and an
//         in-memory LocalStore. Vault::idForPath(root) gives the id to name that folder by
//         (the same before and after open() creates a missing vault folder).
//     open(&error)            create the folder if missing, scan it, load .brulion/config.json
//                             (can be called again to reload; does not emit changed())
//     id(), rootPath(), absolutePath(p), errorString()
//     config(), configWarnings(), saveConfig(cfg)            + signal configChanged()
//     configAdoptedFromObsidian()  keys taken from .obsidian/ while config.json is missing
//     files() (sorted, non-ignored), markdownFiles(), folders(), exists(p), stat(p)
//                             — answered from the cache, no disk access
//     readText(p, &ok) (UTF-8, BOM stripped), readBinary(p, &ok)
//     writeText(p, text, source) / writeBinary(p, bytes, source)
//     writeTextIfUnchanged(p, text, expected, source) -> WriteOutcome (compare-and-swap)
//     create(p, text) -> unique path ("Nowa notatka 2.md"), createFolder(p)
//     rename(from, to, source)  files and folders (one Renamed change per file)
//     remove(p, source) -> moves to .trash/, removePermanently(p, source)
//     trash(), trashInfo(trashPath), restore(trashPath), emptyTrash()
//     startWatching() / stopWatching() (also before open()), rescan(), processPendingEvents(),
//     setWatchDelay(ms), setRescanInterval(ms), watchFile(p) / unwatchFile(p) (open files),
//     watchedPaths()
//     scanStatus() -> VaultScanStatus{state Ok|RootMissing|StorageChanged|Unreadable,
//     unreadableFolders, message} + signal scanStatusChanged(); isStorageAvailable() (live
//     check), setStorageProbe(fn) (storage identity; default QStorageInfo device + mount root)
//     history(), store(), setClock(fn), setHistoryReason(reason)
//     signal changed(QList<VaultChange>) — once per operation / batch of external changes
//
//   History (vault/history.h), LocalStore (vault/localstore.h),
//   parseVaultConfig()/serializeVaultConfig()/vaultConfigPath() (vault/vaultconfig.h),
//   adoptObsidianSettings()/qtDateFormatFromMoment() (vault/obsidiansettings.h),
//   changeSourceName(source) — "user", "external", "sync", "import", "system".
//
// Semantics
//  * Tracked paths: everything paths::isIgnored() lets through plus ".brulion/config.json"
//    (which exists()/stat() know but files() does not list). Ignored paths can be read and
//    written but produce no changes. Symlinked folders are followed when they point outside
//    the vault (an "assets" link to a photo folder); links into the vault (duplicates) and
//    loops are not. Deleting a symlink removes the link, never what it points to.
//  * Paths are normalised with paths::normalize (NFC); files stored with other Unicode
//    normalisation (NFD names copied from macOS) are found and written under their real name,
//    also after another program re-spells a known entry and in the (untracked) trash.
//  * writeText/writeBinary write atomically (QSaveFile; missing folders are created) and are a
//    no-op when the bytes equal the current content (FNV-1a hash). Before overwriting a text
//    file its previous bytes are snapshotted into History (reason = source name). If the file
//    changed on disk behind the vault's back — by its stat, or for text files by content (a
//    same-size change hidden by coarse timestamps) — that change is taken in first (reported
//    as External and kept in History), so nothing is overwritten without a trace. Text written
//    with writeText to a file that has a UTF-8 BOM keeps it. Line endings and every other byte
//    are written exactly as given. A path that is a symlink leading nowhere is refused
//    (QSaveFile would create the link's target, wherever it points).
//  * writeTextIfUnchanged() writes only when the file still holds the text the caller last saw
//    (read from the disk, not the cache; a missing file always qualifies): an editor's debounced
//    save or an agent's edit never overwrites what another program wrote a moment ago. A file
//    holding other text is taken in instead (reported as External, kept in History) and Changed
//    is returned, so the caller merges and tries again.
//  * remove() moves to ".trash/<path>"; when taken, " (yyyyMMdd-HHmmss)" (then " … 2", …) is
//    inserted before the extension. The original location is remembered in the LocalStore,
//    otherwise derived from the trash path. restore() picks a free path ("Name 2.md") when the
//    original is taken. Paths inside ".trash/" are deleted permanently. Trash operations never
//    go through a symlinked folder inside the trash (".trash" itself included): such a link
//    would carry notes out of the vault or offer foreign files for restoring.
//  * rename()/remove() of an entry that changed kind behind the vault's back (a folder
//    replaced by a file) first report that change (External), then their own.
//  * External changes (Syncthing, Nextcloud, other editors) are found by the file system
//    watcher and by rescan() — call it on start-up/resume, or let setRescanInterval() do it.
//    The watcher watches every tracked folder (files created, deleted, renamed or replaced by
//    renaming a new version over them — how Syncthing, Nextcloud and most editors save), the
//    config file and the files named by watchFile() (the files open in editors: their in-place
//    rewrites are seen at once; atomic replace-by-rename re-arms the watch). In-place rewrites
//    of other files are found by the next rescan(), or when the vault reads or writes them —
//    one watch per folder instead of per file keeps within inotify limits (Android) and keeps
//    start-up fast with thousands of notes. Events are
//    coalesced for setWatchDelay() ms, then the affected folders are diffed against the
//    cache: stat (size, mtime) first, content hash for text files, so touches and the echoes
//    of the vault's own writes produce nothing. A file that disappears while another with the
//    same content appears is reported as Renamed (folder moves: one Renamed per file; files
//    with identical content pair by the trailing folders they keep).
//    Content replaced or deleted by other programs is snapshotted into History when the vault
//    has it cached (files it read or wrote). A read that finds other content than the vault
//    knows behind an unchanged stat schedules a check that reports it.
//  * Memory stays bounded: scans read one file at a time and never read files over 16 MB —
//    those are compared by size and mtime like binary files (a touch counts as a
//    modification) and are not kept in History; the content cache holds at most 64 MB, and a
//    .brulion/config.json over 1 MB is ignored (defaults + warning).
//  * Without .brulion/config.json, a vault with an `.obsidian/` folder takes its journal folder,
//    file name format and template, templates folder and attachments folder from Obsidian's
//    settings (obsidiansettings.h; files over 1 MB are skipped with a warning) — in memory, never
//    written: saveConfig() stores them with the rest. `.obsidian/` is not watched; open() and
//    rescan() read it again (configChanged() when the result differs).
//  * Scans never turn what they cannot see into deletions (scanStatus() says why):
//    - the vault folder is missing (unmounted SD card, removed folder): nothing is reconciled
//      (RootMissing);
//    - the folder now lies on another storage than at open() — an empty mount point left by an
//      unmounted card or network share (QStorageInfo device / mount root compared): nothing is
//      reconciled while known files would vanish (StorageChanged; open() again to adopt it);
//    - a folder cannot be listed (no read/search permission, or the listing misses entries that
//      a direct stat still finds — I/O errors, flaky network mounts): nothing below it is
//      reported as deleted (Unreadable, with the folders); the rest of the vault is reconciled.
//    Full scans (open(), rescan()) replace the list of unreadable folders; watcher batches add to
//    it and drop the folders they could list. Writes (writeText, writeBinary, create, createFolder,
//    saveConfig)
//    fail while the folder is missing or on another storage, so nothing lands on an empty mount
//    point to be hidden when the card is mounted again.
//
// vault.h declares the metatype of brulion::VaultChange (QList<VaultChange> follows), so
// changed() works with queued connections, QVariant and QSignalSpy.
#pragma once

#include "types.h"
#include "vault/history.h"
#include "vault/localstore.h"
#include "vault/vaultconfig.h"

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

namespace brulion {

class VaultPrivate;

/// Lower-case name of a change source: "user", "external", "sync", "import" or "system".
/// Used as the History reason of snapshots.
QString changeSourceName(ChangeSource source);

/// ".trash" — the folder that receives deleted files.
QString vaultTrashFolder();

/// Result of Vault::writeTextIfUnchanged().
enum class WriteOutcome {
    Written, ///< written (or nothing to write: the file already held the new text)
    Changed, ///< the file holds other text than expected: nothing written, the change taken in
    Failed,  ///< I/O error or no usable vault (errorString())
};

/// Whether the last scan could see the whole vault (see Vault::scanStatus()).
struct VaultScanStatus {
    enum class State {
        Ok,
        RootMissing,    ///< the vault folder is gone (unmounted SD card, removed folder)
        StorageChanged, ///< the folder lies on another storage than at open() (empty mount point)
        Unreadable,     ///< some folders could not be listed; their content was left alone
    };
    State state = State::Ok;
    QList<VaultPath> unreadableFolders; ///< Unreadable: sorted; "" = the vault folder itself
    QString message;                    ///< English details for logs ("" when Ok)

    bool isOk() const { return state == State::Ok; }
    friend bool operator==(const VaultScanStatus &, const VaultScanStatus &) = default;
};

/// Where a trashed file or folder came from.
struct TrashInfo {
    VaultPath original;  ///< empty when it cannot be determined
    QDateTime deletedAt; ///< invalid when unknown (deleted on another device)
};

class Vault : public QObject {
    Q_OBJECT
public:
    /// `rootPath`: the vault folder. `deviceStatePath`: AppData folder for this vault
    /// (History + LocalStore); empty for none.
    explicit Vault(const QString &rootPath, const QString &deviceStatePath = {}, QObject *parent = nullptr);
    ~Vault() override;
    Q_DISABLE_COPY_MOVE(Vault)

    /// Stable id of a vault folder: FNV-1a 64 (hex) of its canonical path. For a folder that
    /// does not exist yet: the canonical path of its nearest existing ancestor plus the rest.
    static QString idForPath(const QString &rootPath);

    /// Create the folder if missing, scan it and load the config. Returns false (with `error`)
    /// when the folder cannot be created or is a file.
    bool open(QString *error = nullptr);
    bool isOpen() const;

    QString id() const;
    /// Absolute, clean path of the vault folder.
    QString rootPath() const;
    QString deviceStatePath() const;
    /// File system path of a vault path (respects the on-disk spelling of known entries).
    QString absolutePath(const VaultPath &path) const;
    /// Description of the last failed operation.
    QString errorString() const;

    // --- configuration -------------------------------------------------------------------
    const VaultConfig &config() const;
    /// Problems found in .brulion/config.json (or Obsidian's settings) when it was last loaded.
    QStringList configWarnings() const;
    /// The config.json keys whose values config() took from the Obsidian vault's `.obsidian/`
    /// folder ("journalsFolder", "journalFileFormat", "journalTemplate", "templatesFolder",
    /// "attachmentsFolder"); empty when .brulion/config.json exists or nothing was taken.
    QStringList configAdoptedFromObsidian() const;
    /// Write .brulion/config.json (unknown keys already in the file are preserved).
    bool saveConfig(const VaultConfig &config);

    // --- queries (cached, no disk access) ------------------------------------------------
    /// Tracked, non-ignored files (conflict copies included), sorted by path.
    QList<FileStat> files() const;
    /// Markdown files (.md, .markdown), sorted by path.
    QList<FileStat> markdownFiles() const;
    /// Tracked folders (also empty ones), sorted by path.
    QList<FileStat> folders() const;
    /// True if a tracked file or folder is known at `path` (also the config file).
    bool exists(const VaultPath &path) const;
    std::optional<FileStat> stat(const VaultPath &path) const;
    /// `path` spelled like the tracked files and folders it differs from only in letter case,
    /// segment by segment ("projekty/dom.md" -> "Projekty/Dom.md"; segments without such an
    /// entry stay as given). Case-insensitive storage (Android shared storage, FAT, macOS) holds
    /// one entry for both spellings, and links do not tell them apart either.
    VaultPath existingCase(const VaultPath &path) const;

    // --- reading and writing -------------------------------------------------------------
    /// Content of a text file (UTF-8, a leading BOM stripped). `ok` is false when it cannot
    /// be read. Reading a file that changed on disk schedules a check (changed() follows).
    QString readText(const VaultPath &path, bool *ok = nullptr);
    QByteArray readBinary(const VaultPath &path, bool *ok = nullptr);
    /// Atomic write; see the semantics above. Emits Created or Modified with `source`.
    bool writeText(const VaultPath &path, const QString &content, ChangeSource source = ChangeSource::User);
    bool writeBinary(const VaultPath &path, const QByteArray &data, ChangeSource source = ChangeSource::User);
    /// Compare-and-swap: write `content` like writeText() only when the file's text on disk (UTF-8,
    /// a BOM dropped, as readText() gives it) equals `expected` — or when the file does not exist.
    /// Otherwise nothing is written, the other program's change is reported (changed(), External,
    /// kept in History) and Changed is returned: the caller merges with what is there now (an
    /// editor's three-way merge, an agent's edit applied again) and writes again.
    WriteOutcome writeTextIfUnchanged(const VaultPath &path, const QString &content, const QString &expected,
                                      ChangeSource source = ChangeSource::User);
    /// Write a new file at `path`, or at "Name 2.ext", "Name 3.ext", … when `path` is taken
    /// (case-insensitively, by files or folders, known or on disk). Folders that exist in another
    /// letter case are used as they are spelled ("projekty/Dom.md" goes into "Projekty/"), never
    /// created a second time. Returns the path used, or an empty path on failure.
    VaultPath create(const VaultPath &path, const QString &content = {}, ChangeSource source = ChangeSource::User);
    VaultPath createBinary(const VaultPath &path, const QByteArray &data, ChangeSource source = ChangeSource::User);
    /// Create a folder (and missing parents).
    bool createFolder(const VaultPath &path);

    /// Rename or move a file or folder. Fails when `to` exists, also in another letter case
    /// ("dom.md" next to "Dom.md"; case-only renames such as "notatka.md" -> "Notatka.md" are
    /// allowed), or when moving a folder into itself.
    /// Emits one Renamed change per file (Deleted when moved into an ignored folder, Created
    /// for files arriving from one).
    bool rename(const VaultPath &from, const VaultPath &to, ChangeSource source = ChangeSource::User);

    // --- trash ---------------------------------------------------------------------------
    /// Move a file or folder to the trash (Deleted per file). Inside ".trash/" deletes
    /// permanently. A path that does not exist is not an error.
    bool remove(const VaultPath &path, ChangeSource source = ChangeSource::User);
    /// Delete a file or folder without the trash.
    bool removePermanently(const VaultPath &path, ChangeSource source = ChangeSource::User);
    /// Files inside ".trash/" (paths start with ".trash/"), sorted.
    QList<FileStat> trash() const;
    /// Original location of a trashed file or folder.
    TrashInfo trashInfo(const VaultPath &trashPath) const;
    /// Move a trashed file or folder back; returns the path it was restored to (a free
    /// variant of the original), or an empty path on failure.
    VaultPath restore(const VaultPath &trashPath, ChangeSource source = ChangeSource::User);
    /// Permanently delete everything in the trash.
    bool emptyTrash();

    // --- external changes ----------------------------------------------------------------
    /// Watch the folder for external changes; before open() it takes effect when the vault opens.
    void startWatching();
    void stopWatching();
    bool isWatching() const;
    /// Coalescing delay for watcher events (default 50 ms).
    void setWatchDelay(int msec);
    /// Periodic rescan() interval; 0 (default) disables it.
    void setRescanInterval(int msec);
    /// Also watch this file itself, not only its folder: a program that rewrites it in place
    /// (instead of saving a new file and renaming it over the old one) is then noticed at once,
    /// not at the next rescan(). Meant for the files open in an editor; the vault watches the
    /// maxWatchedFiles() most recently named ones, also across stopWatching() and open(); a file
    /// that does not exist yet is watched once it appears. Renames take the watch along.
    void watchFile(const VaultPath &path);
    /// Stop watching a file named by watchFile() itself (its folder stays watched).
    void unwatchFile(const VaultPath &path);
    /// How many files watchFile() keeps watched.
    static int maxWatchedFiles();
    /// Absolute paths the file system watcher watches now (folders, then files; each sorted),
    /// empty when not watching — diagnostics and tests.
    QStringList watchedPaths() const;
    /// Diff the whole folder against the cache; emits and returns the changes (source External).
    QList<VaultChange> rescan();
    /// Handle queued watcher events (and checks scheduled by reads) now instead of after the
    /// watch delay.
    void processPendingEvents();

    /// What the last scan (open(), rescan(), a watcher batch) could not see; see the semantics
    /// above. Changes are announced by scanStatusChanged().
    VaultScanStatus scanStatus() const;
    /// Live check (no scan): the vault is open, its folder exists and lies on the storage it was
    /// on at open(). Sync and other bulk operations should not run otherwise.
    bool isStorageAvailable() const;
    /// Identity of the storage holding an absolute folder path; an empty identity is never
    /// compared. Default: QStorageInfo device + mount root. Tests and platforms without mount
    /// information inject their own. Set it before open(): open() records the identity that
    /// later answers are compared with.
    void setStorageProbe(std::function<QString(const QString &absolutePath)> probe);

    // --- device-local state --------------------------------------------------------------
    /// Version history (nullptr without a device state folder).
    History *history() const;
    /// Device-local key/value store of this vault (never nullptr). Keys starting with
    /// "trash/" belong to the vault.
    LocalStore *store() const;
    /// Clock for trash stamps and History (default QDateTime::currentDateTime).
    void setClock(std::function<QDateTime()> clock);
    /// History reason of the snapshots taken before this vault's own writes ("" = the name of
    /// the write's ChangeSource, the default). brulion-cli writes with "cli": the app's history
    /// then says the text was replaced from the command line. Changes found on disk keep
    /// "external".
    void setHistoryReason(const QString &reason);

Q_SIGNALS:
    /// Tracked files changed (by this vault or by other programs).
    void changed(const QList<brulion::VaultChange> &changes);
    /// config() or configAdoptedFromObsidian() changed (saveConfig, external edit, deletion of
    /// the file, reload).
    void configChanged();
    /// scanStatus() changed.
    void scanStatusChanged();

private:
    friend class VaultPrivate;
    std::unique_ptr<VaultPrivate> d;
};

} // namespace brulion

Q_DECLARE_METATYPE(brulion::VaultChange)
