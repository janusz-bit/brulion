// Private implementation of Vault (d-pointer). Split over vault.cpp (state, reading, writing,
// config), vaultmove.cpp (rename, trash) and vaultscan.cpp (listing, diffing, watching).
#pragma once

#include "vault/obsidiansettings.h"
#include "vault/vault.h"
#include "vault/vaultutil_p.h"

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QSet>
#include <QTimer>
#include <QWaitCondition>

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
    QSet<VaultPath> trees = {}; ///< folders listed recursively ("" = the whole vault)
    QSet<VaultPath> dirs = {};  ///< folders whose direct children are listed
    QSet<VaultPath> files = {}; ///< single files to stat
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
    bool outsideLinkSkipped = false; ///< a folder link to outside was left out (followOutsideLinks off)
    /// Listed folders' modification times (ns, vaultutil::fineMtime()) taken before listing them;
    /// folders without a fine-grained one are left out.
    QHash<VaultPath, qint64> stamps;
    /// Regular files with the name of a leftover of an own atomic write
    /// (vaultutil::isLeftoverTempName()), relative to the root as spelled on disk.
    QStringList leftovers;

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

/// A tracked file's content as the caller just read it, with the stat taken before the read:
/// writeFile() uses it instead of reading the file again when the stat still matches.
struct DiskRead {
    FileStat stat;
    QByteArray bytes;
};

/// A file's next content written beforehand into a staging file (Vault::newStagingFile()), as
/// writeFile() takes it instead of bytes: never held in memory.
struct StagedContent {
    QString file; ///< absolute path, in the folder of the file it is for
    qint64 size = 0;
    QString hash;     ///< text::fnv1a64Hex() of its bytes
    bool bom = false; ///< it starts with a UTF-8 byte order mark
};

/// readText(), readBinary(), readDetached() and the writer's reads fail for files larger than
/// this: they hold the whole file in memory, as writes do. Notes are kilobytes, the app stores
/// attachments up to 200 MB and backups restore files up to 1 GiB (MaxRestoredEntryBytes) — all
/// of them read back.
inline constexpr qint64 kVaultMaxReadBytes = qint64(1) << 30;

/// One Vault::writeTextIfUnchangedAsync(): what the vault's thread knew at the start, the work
/// on the writer (run(): no vault state, only these fields) and its result for mergeWrite().
struct WriteJob {
    enum class Result {
        Written,
        Unchanged, ///< the file holds the new bytes already
        Changed,   ///< other text than `expected`: nothing written
        Fallback,  ///< not the plain case: writeTextIfUnchanged() on the vault's thread decides
        Failed,
    };
    // --- set on the vault's thread before run(), read-only afterwards
    quint64 ticket = 0;
    quint64 generation = 0;
    VaultPath path;
    VaultPath dir;
    QString absolute;
    QStringList ancestorsAbsolute; ///< the folders above it (a symlinked one: Fallback)
    QString content;
    QString expected;
    ChangeSource source = ChangeSource::User;
    QString historyReason;
    QDateTime at; ///< the snapshot's time (the vault's clock, on its thread)
    History *history = nullptr;
    FileStat known;             ///< the file's stat as the vault knows it
    QString knownHash;          ///< "" when unknown
    bool pendingBefore = false; ///< a watcher batch for `dir` (not an own echo) was queued
    bool probeClock = false;    ///< probe the folder's clock first (VaultPrivate::probesFolderClock())
    std::function<void(const QString &)> afterWrite;
    std::function<void(const VaultPath &)> beforeRename; ///< tests (Vault::setBeforeRenameHook)
    std::function<void(WriteOutcome)> done;              ///< vault's thread only
    // --- results of run()
    Result result = Result::Failed;
    QByteArray data; ///< what was written (a BOM kept)
    QString writtenText; ///< `data` decoded, as afterWrite got it (null without afterWrite)
    QString dataHash;
    QString readHash; ///< Changed: the hash of what the file holds
    std::optional<FileStat> written;
    vaultutil::StampedSave save;
    QString error;
    QString historyError;
    // --- hand-over
    QMutex mutex;
    QWaitCondition finishedCondition;
    bool finished = false; ///< run() is over (under `mutex`)
    bool merged = false;   ///< vault's thread only

    void run();
    void markFinished();
    void wait();
};

struct RenamePair {
    FileStat from;
    DiskEntry to;
};

/// Raw bytes of text files the vault read or wrote, so content that other programs replace or
/// delete can still be snapshotted into History. Bounded: the least recently stored entries
/// are evicted beyond `limit` bytes (of the files). Content read or written as text is kept as
/// that text — the very QString the reader got, shared with whoever keeps it (the index keeps
/// every note's text): no second copy of every note — when it gives the bytes back exactly
/// (valid UTF-8; checked again against the bytes' hash when they are asked for), else as bytes.
class ContentCache {
public:
    static constexpr qint64 kDefaultLimit = 64LL * 1024 * 1024;
    /// Larger files are never cached — and never read while scanning (see comparesContent).
    static constexpr qint64 kMaxItemBytes = kDefaultLimit / 4;
    std::optional<QByteArray> get(const VaultPath &path) const;
    bool contains(const VaultPath &path) const { return m_items.contains(path); }
    void put(const VaultPath &path, const QByteArray &bytes);
    /// `bytes` and the text they decode to (vaultutil::decodeUtf8()): kept as the text when
    /// encoding it gives the bytes back. `key`: text::fnv1a64() of the bytes.
    void put(const VaultPath &path, const QByteArray &bytes, const QString &text, quint64 key);
    struct Item;
    /// Take the entry of `path` out (a rename puts it back under the new path with restore()).
    std::optional<Item> take(const VaultPath &path);
    void restore(const VaultPath &path, Item item) { insert(path, std::move(item)); }
    void remove(const VaultPath &path);
    void clear();
    /// Is the entry of `path` kept as text (tests)?
    bool holdsText(const VaultPath &path) const
    {
        const auto it = m_items.constFind(path);
        return it != m_items.cend() && it->isText;
    }

    struct Item {
        QByteArray bytes; ///< empty for an entry kept as text
        QString text;     ///< the decoded text (isText)
        quint64 key = 0;  ///< text::fnv1a64() of the bytes (isText)
        qint64 size = 0;  ///< of the bytes
        quint64 tick = 0;
        bool bom = false; ///< the bytes start with a UTF-8 byte order mark (isText)
        bool isText = false;
    };

private:
    void insert(const VaultPath &path, Item item);
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
    /// A symlinked folder the vault follows: leadsOutside() and followOutsideLinks.
    bool followsLink(const QFileInfo &link, const QString &parentRel) const;
    /// The link points to a folder outside the vault, neither into it nor above itself (a loop).
    bool leadsOutside(const QFileInfo &link, const QString &parentRel) const;
    /// True when a folder above `path` is a symlinked folder the vault does not follow: what lies
    /// there is not the vault's (never listed, stat-ed, read, written or deleted through it).
    bool behindUnfollowedLink(const VaultPath &path) const;
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
    /// `hash`: text::fnv1a64Hex(bytes) when the caller has it already.
    /// `text`: what the bytes decode to (vaultutil::decodeUtf8()), as the reader got it — kept
    /// instead of the bytes (ContentCache).
    void rememberContent(const VaultPath &path, const QByteArray &bytes, bool asText = false, const QString &hash = {},
                         const QString *text = nullptr);
    /// At most `limit` bytes (vaultutil::readContent()); `ok` is false for anything else.
    QByteArray readDisk(const VaultPath &path, qint64 limit, bool *ok) const;
    /// `text` (optional): set to the bytes decoded (vaultutil::decodeUtf8()) when the read succeeds.
    QByteArray readFile(const VaultPath &path, bool asText, bool *ok, QString *text = nullptr);
    /// Why a read of readText(), readBinary() or readDetached() failed: the "%2" of "Cannot read %1: %2".
    static QString readFailure(const vaultutil::ContentRead &read);
    /// The bookkeeping after reading a tracked file (readFile(), Vault::adoptRead()).
    void takeRead(const VaultPath &path, const std::optional<FileStat> &before, const QByteArray &bytes, bool asText,
                  const QString &hash, const QString *text = nullptr);
    /// `stat` (the file's, just recorded with its content) as written by the vault itself
    /// (`ownWrite`: its stamp is now) or found (its stamp may be old): unsettled while its time is
    /// coarse and recent — `fineStamp` when the caller knows, else fineMtime() asked.
    void noteUnsettled(const FileStat &stat, bool ownWrite, std::optional<bool> fineStamp = std::nullopt);
    /// The content of an unsettled file was just compared: settled once the tick has passed.
    void settleIfDue(const VaultPath &path);
    void snapshotCached(const VaultPath &path, ChangeSource reason);
    void snapshotCached(const VaultPath &path, const QString &reason);
    bool usesBom(const VaultPath &path) const;

    // --- writing (vault.cpp)
    bool ensureDirs(const VaultPath &dir, QList<VaultChange> &changes);
    /// `read`: the file's content the caller read a moment ago (its stat still matches the disk).
    void refreshKnownContent(const VaultPath &path, bool asText, qint64 newSize, QList<VaultChange> &changes,
                             const DiskRead *read);
    /// `expect`: write only over the file the caller read (vaultutil::ReadMark; a compare-and-swap),
    /// checked here and right before the rename — else the file is taken in and false returned
    /// with `changedMeanwhile` set.
    /// `staged`: the content is that file's (renamed over the file), `bytes` unused.
    bool writeFile(const VaultPath &path, const QByteArray &bytes, ChangeSource source, bool asText,
                   QList<VaultChange> &changes, const DiskRead *read = nullptr,
                   const vaultutil::ReadMark *expect = nullptr, bool *changedMeanwhile = nullptr,
                   const StagedContent *staged = nullptr);
    bool write(const VaultPath &path, const QByteArray &bytes, ChangeSource source, bool asText,
               const DiskRead *read = nullptr, const vaultutil::ReadMark *expect = nullptr,
               bool *changedMeanwhile = nullptr, const StagedContent *staged = nullptr);
    /// The size and hash of a staging file, read piece by piece; std::nullopt (failed) when it
    /// cannot be read.
    std::optional<StagedContent> readStaged(const QString &file);
    /// After an own atomic write into `dir` (`save`: the folder's stamps around its steps; called
    /// once the written file is tracked): when the vault knew the folder's entries at `save.before`
    /// and nothing but the write's own steps changed them (StampedSave; only where the folder's
    /// clock was probed Fine — never where file times follow a clock's tick), it knows them after
    /// the write too, and the watcher's echo of the write needs no listing (see
    /// processPendingEvents()). `pendingBefore`: a batch that is not an own echo was queued for
    /// `dir` before the write.
    void noteOwnWrite(const VaultPath &dir, const vaultutil::StampedSave &save, bool pendingBefore);
    /// True when an own write into `dir` is to probe the folder's clock first
    /// (vaultutil::probeFolderClock(), once per folder and open()): the vault watches,
    /// has not found `dir`'s clock Fine yet and no folder's coarse.
    bool probesFolderClock(const VaultPath &dir) const;
    /// pendingBefore for a write into `dir` starting now.
    bool foreignBatchPending(const VaultPath &dir) const;

    // --- writes on the writer (vaultwrite.cpp)
    quint64 writeAsync(AsyncTextWrite write);
    /// Wait for every write in flight and merge them, oldest first, reporting them (and the
    /// reports settleWrites() left for the event loop, first) before this returns. Every call
    /// that changes files starts with it.
    void waitForWrites();
    /// Reads and queries (read*, adoptRead, stat(), exists()): wait for the writes in flight and
    /// merge them into what the vault knows, but leave their reports (`done`, changed()) to the
    /// event loop — a caller in the middle of its own work (a dataview query holding the index's
    /// notes, a model building a row) never sees the index or an editor change under it.
    void settleWrites();
    /// settleWrites() when a write of `path` is in flight (stat(), exists(): a query of another
    /// path never waits for the writer's fsync).
    void settleWritesTo(const VaultPath &path);
    /// Merge a finished job (no-op when merged already): Vault state now, then report() its
    /// `done` and changed().
    void mergeWrite(const std::shared_ptr<WriteJob> &job);
    /// Run `report` now (after the reports left for the event loop), or — inside settleWrites()
    /// — leave it for the event loop, in order.
    void report(std::function<void()> what);
    /// reportDeferred() from the event loop (once queued).
    void postDeferred();
    /// The reports settleWrites() left, in order (re-entrant: a report may start a vault call).
    void reportDeferred();
    /// The destructor: wait for the writer, report nothing.
    void abandonWrites();
    VaultPath create(const VaultPath &path, const QByteArray &bytes, ChangeSource source, bool asText,
                     const StagedContent *staged = nullptr);
    QDateTime now() const;
    /// open(): removes the leftovers of interrupted own writes (DiskListing::leftovers, History's).
    void removeLeftovers(const QStringList &rels);

    // --- config (vault.cpp)
    /// Loads .brulion/config.json — or, without it, the settings of `.obsidian/` — into
    /// `config`; true when config(), configAdoptedFromObsidian() or configWarnings() changed.
    /// `quiet`: warnings already reported by the previous load are not logged again (periodic
    /// rescans). `rereadObsidian` false: Obsidian's settings as last read (`obsidianFiles`),
    /// for the guards after files came or went.
    bool loadConfig(bool quiet = false, bool rereadObsidian = true);
    std::optional<QByteArray> readObsidianSetting(const QString &path, QStringList &warnings) const;
    /// Emit changed() (and configChanged()); `ticket`: the async write whose changes these are
    /// (Vault::reportingWrite() while they are reported), 0 for any other operation.
    void finish(const QList<VaultChange> &changes, quint64 ticket = 0);

    // --- moving and trash (vaultmove.cpp)
    bool renameTargetFree(const VaultPath &from, const VaultPath &to);
    bool moveOnDisk(const VaultPath &from, const VaultPath &to);
    /// Stops watching an absolute folder and everything in it; returns the paths it watched.
    QStringList releaseWatches(const QString &absoluteDir);
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
    /// Known files and folders the listing may cover: a superset of those coversFile() /
    /// coversDir() accept, found through the children of the listed folders (not every file).
    void coverCandidates(const DiskListing &listing, QList<VaultPath> &outFiles, QList<VaultPath> &outDirs) const;
    ScanDiff diff(const DiskListing &listing) const;
    // Content read while scanning is never kept beyond the file at hand: `hashCache` holds only
    // the content hashes of appeared files computed while looking for renames ("" for a file
    // that turned out too big to read).
    /// The content of a listed file; nullopt when it cannot be read — `tooBig` when it holds more
    /// than ContentCache::kMaxItemBytes after all (files in /proc report no size): such a file is
    /// compared by its stat, like one whose size says so.
    std::optional<QByteArray> readEntry(const DiskEntry &entry, bool *tooBig = nullptr) const;
    std::optional<QString> entryHash(const DiskEntry &entry, QHash<VaultPath, QString> &hashCache) const;
    bool sameContent(const FileStat &vanished, const DiskEntry &appeared, QHash<VaultPath, QString> &hashCache) const;
    QList<RenamePair> pairRenames(const ScanDiff &diff, QHash<VaultPath, QString> &hashCache) const;
    std::optional<VaultChange> applyModified(const DiskEntry &entry, ChangeSource source);
    bool adoptFile(const DiskEntry &entry, const QHash<VaultPath, QString> &hashCache);
    QList<VaultChange> apply(const ScanDiff &diff, const QList<RenamePair> &renames,
                             const QHash<VaultPath, QString> &hashCache, ChangeSource source);
    QList<VaultChange> reconcile(const ScanScope &scope, ChangeSource source);
    void adoptListing(const DiskListing &listing);
    /// Remember the stamps of the folders `listing` listed completely (see folderStamps).
    void adoptStamps(const DiskListing &listing);

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
    QString canonicalRoot;            ///< root with symlinks resolved (set by open())
    QString storageId;                ///< storage identity of the root at open() ("" = unknown)
    bool followOutsideLinks = false;  ///< Vault::setFollowsOutsideLinks()
    bool outsideLinksSkipped = false; ///< Vault::skippedOutsideLinks()
    VaultScanStatus scanStatus;
    QElapsedTimer storageChecked; ///< last successful requireStorage() check
    bool isOpen = false;
    QString error;
    std::function<QDateTime()> clock;
    QString historyReason; ///< Vault::setHistoryReason(): reason of own writes' snapshots ("" = source)
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
    /// Text files whose stat cannot vouch for their content yet (noteUnsettled()): written — by
    /// the vault or another program — on storage with coarse file times less than
    /// vaultutil::kSettleMs ago, so another write within the same tick, of the same size, would
    /// keep the stat. Listings compare their content (diff(): as if modified, applyModified():
    /// this one file read and hashed) until one after the tick has passed (settleIfDue()). Value:
    /// when it was marked (the vault's clock, ms since the epoch).
    QHash<VaultPath, qint64> unsettled;
    static constexpr qsizetype kMaxUnsettled = 4096;
    /// Children of each folder ("" = the vault folder) among `files` and `dirs`: diffs of a
    /// listing look at the listed folders' children only, not at every known file.
    QHash<VaultPath, QSet<VaultPath>> childFiles;
    QHash<VaultPath, QSet<VaultPath>> childDirs;
    /// files() / markdownFiles() / folders(), sorted by path: kept up to date in place when a
    /// known entry's stat changes (a save), rebuilt on the next call after entries come or go.
    mutable QList<FileStat> sortedFiles;
    mutable bool sortedFilesValid = false;
    mutable QList<FileStat> sortedMarkdown;
    mutable bool sortedMarkdownValid = false;
    mutable QList<FileStat> sortedDirs;
    mutable bool sortedDirsValid = false;
    /// Folder -> its stamp (vaultutil::fineMtime()) at which the vault knew its entries
    /// completely: taken before a complete listing, or right after an own write into it that
    /// started from the known stamp.
    QHash<VaultPath, qint64> folderStamps;
    /// Folders an own write changed since their last watcher batch, with when the echo is no
    /// longer expected (ms of `echoClock`): a batch that finds such a folder at its known stamp
    /// is the write's echo and lists nothing.
    QHash<VaultPath, qint64> ownEchoes;
    QElapsedTimer echoClock;
    /// Folders whose clock an own write probed Fine (vaultutil::probeFolderClock()): only their
    /// stamps can vouch for the echo of an own write.
    QSet<VaultPath> fineClockFolders;
    /// A probe found a folder's clock Coarse or could not tell, or an own write's step left the
    /// folder's stamp as it was right before (noteOwnWrite()): the file times follow a clock's
    /// tick, and no own echo is trusted any more.
    bool coarseFolderTimes = false;
    mutable qint64 listedFolderCount = 0; ///< Vault::listedFolders()

    VaultConfig config;
    QJsonObject rawConfig;
    QStringList configWarnings;
    QStringList configFromObsidian; ///< config.json keys adopted from .obsidian/ (no config.json)
    /// Obsidian's settings files as last read by loadConfig() and the problems reading them;
    /// nullopt while config.json is there or the vault has no `.obsidian/`.
    struct ObsidianRead {
        ObsidianVaultFiles files;
        QStringList warnings;
    };
    std::optional<ObsidianRead> obsidianFiles;

    std::unique_ptr<History> history;
    LocalStore *store = nullptr; ///< child of q

    // Watching
    std::unique_ptr<QFileSystemWatcher> watcher;
    bool watchRequested = false; ///< startWatching() called (also before open())
    /// Files watched themselves (besides their folders), the most recently named last; kept
    /// across stopWatching() / open().
    QList<VaultPath> fileWatches;
    QSet<VaultPath> pendingDirs;
    QSet<VaultPath> pendingFiles;
    QTimer batchTimer;
    QTimer rescanTimer;

    // Writes on the writer (Vault::writeTextIfUnchangedAsync)
    /// A watcher batch waits at most this long for writes in flight, then for the writes.
    static constexpr int kMaxEchoWaitMs = 2000;
    std::function<void(std::function<void()>)> writeExecutor;
    QList<std::shared_ptr<WriteJob>> writes; ///< in flight, oldest first
    quint64 nextTicket = 0;
    quint64 generation = 0;                       ///< open() counts up: older results are dropped
    QList<quint64> reporting;                     ///< tickets whose changes finish() reports now (innermost last)
    int settling = 0;                             ///< inside settleWrites(): reports wait for the event loop
    QList<std::function<void()>> deferredReports; ///< settleWrites()'s reports, oldest first
    bool deferredPosted = false;                  ///< reportDeferred() is queued
    QElapsedTimer batchDeferred;                  ///< since when a watcher batch waits for writes
    QTimer writeWaitTimer;                        ///< the cap of that wait
    std::function<void(const VaultPath &)> beforeRenameHook;
    /// The text the last async writes of a few paths wrote, with the hash written
    /// (Vault::holdsText()); the most recent last.
    struct OwnText {
        VaultPath path;
        QString text;
        QString hash;
    };
    static constexpr qsizetype kMaxOwnTexts = 16;
    QList<OwnText> ownTexts;
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
