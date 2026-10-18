// Private implementation of Vault (d-pointer). Split over vault.cpp (state, reading, writing,
// config), vaultmove.cpp (rename, trash) and vaultscan.cpp (listing, diffing, watching).
#pragma once

#include "vault/vault.h"
#include "vault/vaultutil_p.h"

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>

class QFileInfo;

namespace brulion {

/// An entry found on disk while listing the vault.
struct DiskEntry {
    QString diskRel; ///< path relative to the root as spelled on disk (may be NFD)
    FileStat stat;   ///< stat.path is the normalised vault path
};

/// Part of the vault to compare with the disk (see VaultPrivate::reconcile).
struct ScanScope {
    QSet<VaultPath> trees; ///< folders listed recursively ("" = the whole vault)
    QSet<VaultPath> dirs;  ///< folders whose direct children are listed
    QSet<VaultPath> files; ///< single files to stat
    bool isEmpty() const { return trees.isEmpty() && dirs.isEmpty() && files.isEmpty(); }
};

/// What a scan found on disk, and which known paths it can vouch for.
struct DiskListing {
    QHash<VaultPath, DiskEntry> files;
    QHash<VaultPath, DiskEntry> dirs;
    QSet<VaultPath> trees;        ///< roots of recursive listings
    QSet<VaultPath> listedDirs;   ///< folders whose direct children were listed
    QSet<VaultPath> checkedFiles; ///< files stat-ed individually
    /// Folders that could not be listed (completely): nothing at or below them is covered.
    QSet<VaultPath> unreadable;

    /// True when the listing shows every child of `dir`: the folder was listed, lies in a
    /// listed tree, or is gone from a folder that was listed — and neither it nor a folder
    /// above it is unreadable.
    bool coversChildrenOf(const VaultPath &dir) const;
    /// True when `path` is an unreadable folder or lies inside one.
    bool withinUnreadable(const VaultPath &path) const;
    /// True when `path` lies in one of `trees` (O(depth) set lookups, not O(trees)).
    bool inListedTree(const VaultPath &path) const;
    bool coversFile(const VaultPath &path) const;
    bool coversDir(const VaultPath &dir) const;
};

/// Known state versus disk for the covered part of the vault (each list sorted by path).
struct ScanDiff {
    QList<FileStat> vanishedFiles; ///< known stats of files that are gone
    QList<VaultPath> vanishedDirs;
    QList<DiskEntry> modifiedFiles; ///< known files whose stat changed
    QList<DiskEntry> appearedFiles;
    QList<DiskEntry> appearedDirs;
};

struct RenamePair {
    FileStat from;
    DiskEntry to;
};

/// Raw bytes of text files the vault read or wrote, so content that other programs replace or
/// delete can still be snapshotted into History. Bounded: the least recently stored entries
/// are evicted beyond `limit` bytes.
class ContentCache {
public:
    static constexpr qint64 kDefaultLimit = 64LL * 1024 * 1024;
    /// Larger files are never cached — and never read while scanning (see comparesContent).
    static constexpr qint64 kMaxItemBytes = kDefaultLimit / 4;
    std::optional<QByteArray> get(const VaultPath &path) const;
    bool contains(const VaultPath &path) const { return m_items.contains(path); }
    void put(const VaultPath &path, const QByteArray &bytes);
    std::optional<QByteArray> take(const VaultPath &path);
    void remove(const VaultPath &path);
    void clear();

private:
    struct Item {
        QByteArray bytes;
        quint64 tick = 0;
    };
    void evict();
    QHash<VaultPath, Item> m_items;
    qint64 m_bytes = 0;
    qint64 m_limit = kDefaultLimit;
    quint64 m_tick = 0;
};

class VaultPrivate {
public:
    VaultPrivate(Vault *owner, const QString &rootPath, const QString &deviceStatePath);
    ~VaultPrivate();
    Q_DISABLE_COPY_MOVE(VaultPrivate)

    // --- errors (vault.cpp)
    bool fail(const QString &message);
    bool requireOpen();

    // --- paths on disk (vault.cpp)
    QString diskRel(const VaultPath &path) const;
    QString absPath(const VaultPath &path) const;
    QString absOfDiskRel(const QString &diskRel) const;
    std::optional<VaultPath> vaultPathOfAbs(const QString &absolute) const;
    void rememberDiskName(const VaultPath &path, const QString &rel);
    void forgetDiskNames(const VaultPath &path);
    void rebaseDiskNames(const VaultPath &from, const VaultPath &to);
    void respell(const VaultPath &path, const QString &rel);
    void adoptSpellings(const DiskListing &listing);
    std::optional<FileStat> diskStat(const VaultPath &path) const;
    void resolveSpelling(const VaultPath &path);
    bool isDirOnDisk(const VaultPath &path) const;
    bool followsLink(const QFileInfo &link, const QString &parentRel) const;
    bool existsOnDisk(const VaultPath &path) const;

    // --- known entries (vault.cpp)
    void trackFile(const FileStat &stat);
    void trackDir(const FileStat &stat);
    void insertDir(const FileStat &stat);
    void trackAncestors(const VaultPath &path);
    bool untrackFile(const VaultPath &path);
    void untrackDir(const VaultPath &path);
    void addLower(const VaultPath &path);
    void removeLower(const VaultPath &path);
    bool isTakenCaseInsensitive(const VaultPath &path) const;
    /// Vault::existingCase(); `folders` only: the last segment is kept as given.
    VaultPath existingCase(const VaultPath &path, bool foldersOnly) const;
    std::optional<FileStat> knownStat(const VaultPath &path) const;
    std::optional<VaultPath> storedCaseVariant(const VaultPath &path, const FileStat &disk) const;
    QList<VaultChange> forgetTree(const VaultPath &path, ChangeSource source);
    QList<VaultChange> takeInKindChange(const VaultPath &path, bool isDirNow);
    void reset();

    // --- content (vault.cpp)
    bool isTextual(const VaultPath &path) const;
    bool comparesContent(const FileStat &stat) const;
    void rememberContent(const VaultPath &path, const QByteArray &bytes, bool asText = false);
    QByteArray readDisk(const VaultPath &path, bool *ok) const;
    QByteArray readFile(const VaultPath &path, bool asText, bool *ok);
    void snapshotCached(const VaultPath &path, ChangeSource reason);
    bool usesBom(const VaultPath &path) const;

    // --- writing (vault.cpp)
    bool ensureDirs(const VaultPath &dir, QList<VaultChange> &changes);
    void refreshKnownContent(const VaultPath &path, bool asText, qint64 newSize, QList<VaultChange> &changes);
    bool writeFile(const VaultPath &path, const QByteArray &bytes, ChangeSource source, bool asText,
                   QList<VaultChange> &changes);
    bool write(const VaultPath &path, const QByteArray &bytes, ChangeSource source, bool asText);
    VaultPath create(const VaultPath &path, const QByteArray &bytes, ChangeSource source, bool asText);
    QDateTime now() const;

    // --- config (vault.cpp)
    bool loadConfig();
    void finish(const QList<VaultChange> &changes);

    // --- moving and trash (vaultmove.cpp)
    bool renameTargetFree(const VaultPath &from, const VaultPath &to);
    bool moveOnDisk(const VaultPath &from, const VaultPath &to);
    void moveFileState(const VaultPath &from, const FileStat &to);
    QList<VaultChange> relocate(const VaultPath &from, const VaultPath &to, bool isDir, ChangeSource source);
    VaultPath trashTarget(const VaultPath &path, bool isDir) const;
    VaultPath avoidFileAncestors(const VaultPath &path, qsizetype keep,
                                 const std::function<QString(int)> &suffix) const;
    VaultPath freePath(const VaultPath &base, bool isDir) const;
    bool throughTrashLink(const VaultPath &path) const;
    static QString trashKey(const VaultPath &trashPath);
    void dropTrashMeta(const VaultPath &trashPath);
    void pruneEmptyTrashFolders(VaultPath dir);

    // --- scanning (vaultscan.cpp)
    void listChildren(const VaultPath &dir, const QString &dirRel, bool recursive, DiskListing &out) const;
    DiskListing collect(const ScanScope &scope) const;
    ScanDiff diff(const DiskListing &listing) const;
    // Content read while scanning is never kept beyond the file at hand: `hashCache` holds only
    // the content hashes of appeared files computed while looking for renames.
    QByteArray readEntry(const DiskEntry &entry, bool *ok) const;
    QString entryHash(const DiskEntry &entry, QHash<VaultPath, QString> &hashCache, bool *ok) const;
    bool sameContent(const FileStat &vanished, const DiskEntry &appeared, QHash<VaultPath, QString> &hashCache) const;
    QList<RenamePair> pairRenames(const ScanDiff &diff, QHash<VaultPath, QString> &hashCache) const;
    std::optional<VaultChange> applyModified(const DiskEntry &entry, ChangeSource source);
    bool adoptFile(const DiskEntry &entry, const QHash<VaultPath, QString> &hashCache);
    QList<VaultChange> apply(const ScanDiff &diff, const QList<RenamePair> &renames,
                             const QHash<VaultPath, QString> &hashCache, ChangeSource source);
    QList<VaultChange> reconcile(const ScanScope &scope, ChangeSource source);
    void adoptListing(const DiskListing &listing);

    // --- scan safety (vaultscan.cpp)
    /// RootMissing / StorageChanged when the vault folder cannot be trusted at all; storage
    /// identities are compared only with `checkStorage` (QStorageInfo reads the mount table).
    std::optional<VaultScanStatus> rootProblem(bool checkStorage) const;
    /// Mark folders whose listing misses known entries that a direct stat still finds.
    void verifyListing(DiskListing &listing) const;
    /// Status after a scan that could see the vault folder (`full`: the whole vault was listed).
    void noteListing(const DiskListing &listing, bool full);
    void setScanStatus(VaultScanStatus status);
    QString probeStorage() const;
    /// Writes need the vault folder on the storage it was opened on (an empty mount point
    /// would swallow them); the storage identity is re-read at most once a second.
    bool requireStorage();

    // --- watching (vaultscan.cpp)
    /// Files watched themselves (Vault::watchFile()); folders are always watched.
    static constexpr qsizetype kMaxFileWatches = 64;
    void startWatching();
    void stopWatching();
    /// True for files watched themselves: those named by Vault::watchFile() and the config file.
    bool watchesFile(const VaultPath &path) const;
    /// (Re-)arm the watch on `path` when it is watched itself (after it was written or replaced).
    void watchFile(const VaultPath &path);
    /// Vault::watchFile(): `path` becomes the most recently named watched file.
    void keepWatching(const VaultPath &path);
    void stopWatchingFile(const VaultPath &path);
    /// A watched file was renamed: watch it under its new name.
    void moveFileWatch(const VaultPath &from, const VaultPath &to);
    void watchDir(const VaultPath &path);
    void unwatch(const VaultPath &path);
    void syncConfigFolderWatch();
    void onWatcherEvent(const QString &absolute, bool isDir);
    void scheduleCheck(const VaultPath &path);
    void processPendingEvents();

    Vault *const q;
    const QString root;       ///< absolute, clean
    const QString rootPrefix; ///< root + "/" (just "/" for the file system root)
    const QString statePath;
    QString id;
    QString canonicalRoot; ///< root with symlinks resolved (set by open())
    QString storageId;     ///< storage identity of the root at open() ("" = unknown)
    VaultScanStatus scanStatus;
    QElapsedTimer storageChecked; ///< last successful requireStorage() check
    bool isOpen = false;
    QString error;
    std::function<QDateTime()> clock;
    std::function<QString(const QString &)> storageProbe; ///< see Vault::setStorageProbe()

    // Known entries. Every tracked ancestor of a known file is a known folder.
    QHash<VaultPath, FileStat> files;    ///< tracked files (incl. the config file)
    QHash<VaultPath, FileStat> dirs;     ///< tracked folders
    QHash<QString, int> lowerCounts;     ///< lower-cased paths of files + folders (multiset)
    QHash<VaultPath, QString> diskNames; ///< vault path -> on-disk relative path where they differ
    QHash<VaultPath, QString> hashes;    ///< FNV-1a of the content as last seen (read/written)
    ContentCache contents;
    QSet<VaultPath> bomPaths; ///< files whose content starts with a UTF-8 BOM
    /// Files whose content on disk is known to differ from `hashes` although their stat did not
    /// change (coarse timestamps): the next reconcile treats them as modified.
    QSet<VaultPath> staleContent;
    mutable QList<FileStat> sortedFiles;
    mutable bool sortedFilesValid = false;

    VaultConfig config;
    QJsonObject rawConfig;
    QStringList configWarnings;

    std::unique_ptr<History> history;
    LocalStore *store = nullptr; ///< child of q

    // Watching
    std::unique_ptr<QFileSystemWatcher> watcher;
    bool watchRequested = false; ///< startWatching() called (also before open())
    bool configFolderWatched = false;
    /// Files watched themselves (besides their folders), the most recently named last; kept
    /// across stopWatching() / open().
    QList<VaultPath> fileWatches;
    QSet<VaultPath> pendingDirs;
    QSet<VaultPath> pendingFiles;
    QTimer batchTimer;
    QTimer rescanTimer;
};

} // namespace brulion

namespace brulion {

/// Stat of a file system entry under the vault path `path` (mtime in ms since epoch).
FileStat statOf(const QFileInfo &info, const VaultPath &path);
/// Same kind, and for files the same size and modification time.
bool sameStat(const FileStat &a, const FileStat &b);
inline bool byPath(const FileStat &a, const FileStat &b)
{
    return a.path < b.path;
}

} // namespace brulion
