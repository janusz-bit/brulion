// Vault: listing the folder, diffing it against the cache (rescan, watcher events, writes that
// find the disk changed) and the file system watcher.
//
// One engine serves every source of external changes: a ScanScope names what to look at
// (whole trees, the direct children of folders, single files); collect() lists exactly that
// and records which known paths the listing can vouch for; diff() compares those with the
// cache; pairRenames() turns "gone + appeared with the same content" into renames; apply()
// updates the cache (History snapshots, watches) and returns the changes.
#include "vault/vault.h"
#include "vault/vault_p.h"

#include "text/text.h"
#include "util/paths.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMultiHash>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion {
namespace {

/// More file watches than this are not requested (inotify limits); rescans cover the rest.
constexpr qsizetype kMaxFileWatches = 20000;

bool entryByPath(const DiskEntry &a, const DiskEntry &b)
{
    return a.stat.path < b.stat.path;
}

QList<DiskEntry> sortedEntries(QList<DiskEntry> entries)
{
    std::sort(entries.begin(), entries.end(), entryByPath);
    return entries;
}

/// How well `to` fits as the new name of `from` (both with the same content): two points per
/// trailing path segment they share — a moved folder keeps "Projekt/index.md", so identical
/// notes moved together pair with their own old paths — plus one for a rename in place.
int renameScore(const VaultPath &from, const VaultPath &to)
{
    const QStringList a = from.split(u'/');
    const QStringList b = to.split(u'/');
    qsizetype common = 0;
    while (common < a.size() && common < b.size() && a.at(a.size() - 1 - common) == b.at(b.size() - 1 - common))
        ++common;
    return static_cast<int>(2 * common) + (paths::dirname(from) == paths::dirname(to) ? 1 : 0);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// DiskListing coverage
// ---------------------------------------------------------------------------------------------

bool DiskListing::inListedTree(const VaultPath &path) const
{
    if (trees.isEmpty())
        return false;
    for (VaultPath cur = path;; cur = paths::dirname(cur)) {
        if (trees.contains(cur))
            return true;
        if (cur.isEmpty())
            return false;
    }
}

bool DiskListing::withinUnreadable(const VaultPath &path) const
{
    if (unreadable.isEmpty())
        return false;
    for (VaultPath cur = path;; cur = paths::dirname(cur)) {
        if (unreadable.contains(cur))
            return true;
        if (cur.isEmpty())
            return false;
    }
}

bool DiskListing::coversChildrenOf(const VaultPath &dir) const
{
    // What could not be listed is not known to be gone.
    if (withinUnreadable(dir))
        return false;
    // A folder inside a listed tree is covered; so is every folder below it, so the trees need
    // checking only once (a batch can hold thousands of new trees, diff() asks for every file).
    if (inListedTree(dir))
        return true;
    for (VaultPath cur = dir;;) {
        if (listedDirs.contains(cur))
            return true;
        // An existing folder that was not listed: nothing is known about its children.
        if (cur.isEmpty() || dirs.contains(cur))
            return false;
        // `cur` is gone (or was never seen): it is covered when its parent was listed.
        cur = paths::dirname(cur);
    }
}

bool DiskListing::coversFile(const VaultPath &path) const
{
    return checkedFiles.contains(path) || coversChildrenOf(paths::dirname(path));
}

bool DiskListing::coversDir(const VaultPath &dir) const
{
    return !dir.isEmpty() && !withinUnreadable(dir) && coversChildrenOf(paths::dirname(dir));
}

// ---------------------------------------------------------------------------------------------
// Listing
// ---------------------------------------------------------------------------------------------

void VaultPrivate::listChildren(const VaultPath &dir, const QString &dirRel, bool recursive, DiskListing &out) const
{
    if (out.listedDirs.contains(dir) || out.unreadable.contains(dir))
        return;
    const QString absDir = absOfDiskRel(dirRel);
    // Without read permission the listing comes back empty, without search permission the
    // entries cannot be stat-ed: neither means that everything inside was deleted.
    if (const QFileInfo self(absDir); !self.isReadable() || !self.isExecutable()) {
        out.unreadable.insert(dir);
        return;
    }
    out.listedDirs.insert(dir);
    QList<std::pair<VaultPath, QString>> descend; // vault path, on-disk path
    QDirIterator it(absDir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString name = info.fileName();
        if (name.contains(u'\\'))
            continue; // would be read as a separator by paths::normalize
        const VaultPath path = paths::join(dir, name);
        if (path.isEmpty())
            continue;
        QString rel = name;
        if (!dirRel.isEmpty())
            rel = dirRel + u'/' + name;
        if (info.isDir()) {
            if (info.isSymLink() && !followsLink(info, dirRel))
                continue; // a link into the vault (duplicates) or a loop
            if (path == vaultutil::configFolder()) {
                descend.append({path, rel}); // ignored, but its config.json is tracked
                continue;
            }
            if (!vaultutil::isTracked(path) || out.dirs.contains(path))
                continue;
            out.dirs.insert(path, DiskEntry{rel, statOf(info, path)});
            // A folder the vault does not know yet: everything inside it is new.
            if (recursive || !dirs.contains(path))
                descend.append({path, rel});
        } else if (info.isFile() && vaultutil::isTracked(path) && !out.files.contains(path)) {
            out.files.insert(path, DiskEntry{rel, statOf(info, path)});
        }
    }
    for (const auto &[sub, subRel] : std::as_const(descend)) {
        const bool configFolder = sub == vaultutil::configFolder();
        if (!recursive && !configFolder)
            out.trees.insert(sub); // a new folder found by a shallow listing
        listChildren(sub, subRel, !configFolder, out);
    }
}

DiskListing VaultPrivate::collect(const ScanScope &scope) const
{
    DiskListing out;
    QSet<VaultPath> parents = scope.dirs;
    // A failed direct stat is no proof that something is gone (the path may be spelled
    // differently on disk now): the listing of the nearest existing folder above decides.
    for (const VaultPath &tree : scope.trees) {
        if (!isDirOnDisk(tree)) {
            parents.insert(paths::dirname(tree));
            continue;
        }
        out.trees.insert(tree);
        if (!tree.isEmpty() && vaultutil::isTracked(tree)) {
            if (const std::optional<FileStat> st = diskStat(tree))
                out.dirs.insert(tree, DiskEntry{diskRel(tree), *st});
            out.checkedFiles.insert(tree); // a known *file* there is gone
        }
        listChildren(tree, diskRel(tree), true, out);
    }
    for (const VaultPath &file : scope.files) {
        const std::optional<FileStat> st = diskStat(file);
        if (st && !st->isDir) {
            out.checkedFiles.insert(file);
            if (vaultutil::isTracked(file) && !out.files.contains(file))
                out.files.insert(file, DiskEntry{diskRel(file), *st});
        } else {
            // Gone or replaced by a folder: the parent tells what happened (renames, new folder).
            parents.insert(paths::dirname(file));
        }
    }
    for (VaultPath dir : std::as_const(parents)) {
        while (!dir.isEmpty() && !isDirOnDisk(dir))
            dir = paths::dirname(dir);
        listChildren(dir, diskRel(dir), false, out);
    }
    return out;
}

void VaultPrivate::adoptListing(const DiskListing &listing)
{
    const QList<DiskEntry> folders = sortedEntries(listing.dirs.values());
    for (const DiskEntry &entry : folders) {
        rememberDiskName(entry.stat.path, entry.diskRel);
        trackDir(entry.stat);
    }
    for (const DiskEntry &entry : std::as_const(listing.files)) {
        rememberDiskName(entry.stat.path, entry.diskRel);
        trackFile(entry.stat);
    }
}

// ---------------------------------------------------------------------------------------------
// Diffing
// ---------------------------------------------------------------------------------------------

ScanDiff VaultPrivate::diff(const DiskListing &listing) const
{
    ScanDiff out;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (!listing.coversFile(it.key()))
            continue;
        const auto disk = listing.files.constFind(it.key());
        if (disk == listing.files.cend())
            out.vanishedFiles.append(it.value());
        else if (!sameStat(disk->stat, it.value()) || staleContent.contains(it.key()))
            out.modifiedFiles.append(*disk);
    }
    for (auto it = dirs.cbegin(); it != dirs.cend(); ++it) {
        if (listing.coversDir(it.key()) && !listing.dirs.contains(it.key()))
            out.vanishedDirs.append(it.key());
    }
    for (auto it = listing.files.cbegin(); it != listing.files.cend(); ++it) {
        if (!files.contains(it.key()))
            out.appearedFiles.append(it.value());
    }
    for (auto it = listing.dirs.cbegin(); it != listing.dirs.cend(); ++it) {
        if (!dirs.contains(it.key()))
            out.appearedDirs.append(it.value());
    }
    std::sort(out.vanishedFiles.begin(), out.vanishedFiles.end(), byPath);
    std::sort(out.vanishedDirs.begin(), out.vanishedDirs.end());
    out.modifiedFiles = sortedEntries(out.modifiedFiles);
    out.appearedFiles = sortedEntries(out.appearedFiles);
    out.appearedDirs = sortedEntries(out.appearedDirs);
    return out;
}

QByteArray VaultPrivate::readEntry(const DiskEntry &entry, bool *ok) const
{
    QFile file(absOfDiskRel(entry.diskRel));
    if (!file.open(QIODevice::ReadOnly)) {
        *ok = false;
        return {};
    }
    QByteArray bytes = file.readAll();
    *ok = file.error() == QFileDevice::NoError;
    return bytes;
}

QString VaultPrivate::entryHash(const DiskEntry &entry, QHash<VaultPath, QString> &hashCache, bool *ok) const
{
    if (const auto it = hashCache.constFind(entry.stat.path); it != hashCache.cend()) {
        *ok = true;
        return *it;
    }
    const QByteArray bytes = readEntry(entry, ok);
    if (!*ok)
        return {};
    const QString hash = text::fnv1a64Hex(bytes);
    hashCache.insert(entry.stat.path, hash);
    return hash;
}

bool VaultPrivate::sameContent(const FileStat &vanished, const DiskEntry &appeared,
                               QHash<VaultPath, QString> &hashCache) const
{
    const QString known = hashes.value(vanished.path);
    // Content never seen, or too big to read while scanning: a rename keeps the modification time.
    if (known.isEmpty() || appeared.stat.size > ContentCache::kMaxItemBytes)
        return vanished.mtime == appeared.stat.mtime;
    bool ok = false;
    const QString hash = entryHash(appeared, hashCache, &ok);
    return ok && hash == known;
}

QList<RenamePair> VaultPrivate::pairRenames(const ScanDiff &found, QHash<VaultPath, QString> &hashCache) const
{
    QList<RenamePair> pairs;
    const QList<FileStat> &gone = found.vanishedFiles;
    if (gone.isEmpty() || found.appearedFiles.isEmpty())
        return pairs;
    QMultiHash<qint64, qsizetype> bySize;
    for (qsizetype i = 0; i < gone.size(); ++i)
        bySize.insert(gone.at(i).size, i);
    QList<bool> used(gone.size(), false);
    for (const DiskEntry &appeared : found.appearedFiles) {
        qsizetype best = -1;
        int bestScore = -1;
        const QList<qsizetype> candidates = bySize.values(appeared.stat.size);
        for (const qsizetype i : candidates) {
            const FileStat &old = gone.at(i);
            if (used.at(i) || !sameContent(old, appeared, hashCache))
                continue;
            const int score = renameScore(old.path, appeared.stat.path);
            if (score > bestScore || (score == bestScore && old.path < gone.at(best).path)) {
                best = i;
                bestScore = score;
            }
        }
        if (best < 0)
            continue;
        used[best] = true;
        pairs.append(RenamePair{gone.at(best), appeared});
    }
    return pairs;
}

// ---------------------------------------------------------------------------------------------
// Applying a diff
// ---------------------------------------------------------------------------------------------

std::optional<VaultChange> VaultPrivate::applyModified(const DiskEntry &entry, ChangeSource source)
{
    const VaultPath &path = entry.stat.path;
    staleContent.remove(path);
    if (!comparesContent(entry.stat)) {
        // Binary files (and text too big to read while scanning) are compared by size and
        // modification time only; text the vault still holds from before goes to History.
        snapshotCached(path, source);
        hashes.remove(path);
        contents.remove(path);
        trackFile(entry.stat);
        watchFile(path);
        return VaultChange{ChangeType::Modified, path, {}, source};
    }
    bool ok = false;
    const QByteArray bytes = readEntry(entry, &ok);
    if (!ok) { // vanished between listing and reading
        snapshotCached(path, source);
        unwatch(path);
        untrackFile(path);
        return VaultChange{ChangeType::Deleted, path, {}, source};
    }
    const bool same = hashes.value(path) == text::fnv1a64Hex(bytes);
    if (!same)
        snapshotCached(path, source);
    trackFile(entry.stat);
    rememberContent(path, bytes);
    watchFile(path);
    if (same)
        return std::nullopt; // touched, or the echo of our own write
    return VaultChange{ChangeType::Modified, path, {}, source};
}

bool VaultPrivate::adoptFile(const DiskEntry &entry, const QHash<VaultPath, QString> &hashCache)
{
    const VaultPath &path = entry.stat.path;
    const bool isText = vaultutil::isTextPath(path) && entry.stat.size <= ContentCache::kMaxItemBytes;
    QByteArray bytes;
    if (isText) {
        bool ok = false;
        bytes = readEntry(entry, &ok);
        if (!ok)
            return false; // gone again
    }
    rememberDiskName(path, entry.diskRel);
    trackFile(entry.stat);
    if (isText)
        rememberContent(path, bytes);
    else if (const auto it = hashCache.constFind(path); it != hashCache.cend())
        hashes.insert(path, *it); // read while looking for renames
    watchFile(path);
    return true;
}

QList<VaultChange> VaultPrivate::apply(const ScanDiff &found, const QList<RenamePair> &renames,
                                       const QHash<VaultPath, QString> &hashCache, ChangeSource source)
{
    QSet<VaultPath> renamedFrom;
    QSet<VaultPath> renamedTo;
    for (const RenamePair &pair : renames) {
        renamedFrom.insert(pair.from.path);
        renamedTo.insert(pair.to.stat.path);
    }
    QList<VaultChange> deleted;
    QList<VaultChange> renamed;
    QList<VaultChange> modified;
    QList<VaultChange> created;

    for (const FileStat &gone : found.vanishedFiles) {
        if (renamedFrom.contains(gone.path))
            continue;
        snapshotCached(gone.path, source); // keep content other programs deleted
        unwatch(gone.path);
        untrackFile(gone.path);
        deleted.append(VaultChange{ChangeType::Deleted, gone.path, {}, source});
    }
    for (const VaultPath &dir : found.vanishedDirs) {
        QList<VaultPath> doomed;
        for (auto it = dirs.cbegin(); it != dirs.cend(); ++it) {
            if (vaultutil::isWithin(it.key(), dir))
                doomed.append(it.key());
        }
        for (const VaultPath &folder : std::as_const(doomed)) {
            unwatch(folder);
            untrackDir(folder);
        }
    }
    for (const DiskEntry &entry : found.appearedDirs) {
        rememberDiskName(entry.stat.path, entry.diskRel);
        trackDir(entry.stat);
        watchDir(entry.stat.path);
    }
    for (const RenamePair &pair : renames) {
        unwatch(pair.from.path);
        rememberDiskName(pair.to.stat.path, pair.to.diskRel);
        moveFileState(pair.from.path, pair.to.stat);
        renamed.append(VaultChange{ChangeType::Renamed, pair.to.stat.path, pair.from.path, source});
    }
    for (const DiskEntry &entry : found.modifiedFiles) {
        if (const std::optional<VaultChange> change = applyModified(entry, source))
            (change->type == ChangeType::Deleted ? deleted : modified).append(*change);
    }
    for (const DiskEntry &entry : found.appearedFiles) {
        if (renamedTo.contains(entry.stat.path) || !adoptFile(entry, hashCache))
            continue;
        created.append(VaultChange{ChangeType::Created, entry.stat.path, {}, source});
    }
    return deleted + renamed + modified + created;
}

QList<VaultChange> VaultPrivate::reconcile(const ScanScope &scope, ChangeSource source)
{
    if (scope.isEmpty() || !isOpen)
        return {};
    // An unmounted SD card or a removed folder must not look like "every note was deleted".
    if (const std::optional<VaultScanStatus> problem = rootProblem(false)) {
        if (*problem != scanStatus)
            qCWarning(lcVault).noquote() << "The vault folder is missing; changes are not reconciled:" << root;
        setScanStatus(*problem);
        return {};
    }
    DiskListing listing = collect(scope);
    verifyListing(listing);
    const ScanDiff found = diff(listing);
    // Before anything known disappears: an empty mount point (the card or share was unmounted
    // under a vault folder that is itself the mount point) still is a folder, but not ours.
    if (!found.vanishedFiles.isEmpty() || !found.vanishedDirs.isEmpty()) {
        if (const std::optional<VaultScanStatus> problem = rootProblem(true)) {
            if (*problem != scanStatus)
                qCWarning(lcVault).noquote() << problem->message << "- changes are not reconciled";
            setScanStatus(*problem);
            return {};
        }
    }
    QHash<VaultPath, QString> hashCache; // content hashes of appeared files (rename detection)
    const QList<RenamePair> renames = pairRenames(found, hashCache);
    const QList<VaultChange> changes = apply(found, renames, hashCache, source);
    adoptSpellings(listing);
    syncConfigFolderWatch();
    noteListing(listing, scope.trees.contains(QString()));
    return changes;
}

// ---------------------------------------------------------------------------------------------
// Scan safety
// ---------------------------------------------------------------------------------------------

QString VaultPrivate::probeStorage() const
{
    return storageProbe ? storageProbe(root) : QString();
}

std::optional<VaultScanStatus> VaultPrivate::rootProblem(bool checkStorage) const
{
    if (!isDirOnDisk(QString())) {
        return VaultScanStatus{VaultScanStatus::State::RootMissing, {}, u"The vault folder is missing: %1"_s.arg(root)};
    }
    if (!checkStorage || storageId.isEmpty())
        return std::nullopt;
    const QString current = probeStorage();
    if (current.isEmpty() || current == storageId)
        return std::nullopt;
    return VaultScanStatus{VaultScanStatus::State::StorageChanged,
                           {},
                           u"The vault folder %1 is on another storage than when it was opened (%2, was %3)"_s.arg(
                                   root, current, storageId)};
}

bool VaultPrivate::requireStorage()
{
    constexpr qint64 kRecheckMs = 1000;
    if (storageChecked.isValid() && storageChecked.elapsed() < kRecheckMs && isDirOnDisk(QString()))
        return true;
    if (const std::optional<VaultScanStatus> problem = rootProblem(true)) {
        storageChecked.invalidate();
        return fail(
                problem->state == VaultScanStatus::State::RootMissing
                        ? Vault::tr("The vault folder is missing (unmounted?): %1").arg(root)
                        : Vault::tr("The vault folder is on another storage than when it was opened (unmounted?): %1")
                                  .arg(root));
    }
    storageChecked.start();
    return true;
}

void VaultPrivate::verifyListing(DiskListing &listing) const
{
    // A listing that misses a known entry which a direct stat still finds is incomplete (an I/O
    // error while reading the folder, a flaky network mount): nothing below that folder counts
    // as gone. On case-insensitive storage the stat also finds a case variant that the listing
    // has — that is a rename, not a gap.
    QSet<QString> listedLower;
    bool lowerReady = false;
    const auto caseVariantListed = [&](const VaultPath &path) {
        if (!lowerReady) {
            for (auto it = listing.files.cbegin(); it != listing.files.cend(); ++it)
                listedLower.insert(it.key().toLower());
            for (auto it = listing.dirs.cbegin(); it != listing.dirs.cend(); ++it)
                listedLower.insert(it.key().toLower());
            lowerReady = true;
        }
        return listedLower.contains(path.toLower());
    };
    for (auto it = dirs.cbegin(); it != dirs.cend(); ++it) {
        const VaultPath &dir = it.key();
        if (listing.dirs.contains(dir) || !listing.coversDir(dir))
            continue;
        const QFileInfo info(absPath(dir));
        if (info.isSymLink() && !info.exists())
            listing.unreadable.insert(dir); // a linked folder whose target is unmounted
        else if (isDirOnDisk(dir) && !caseVariantListed(dir))
            listing.unreadable.insert(paths::dirname(dir));
    }
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const VaultPath &path = it.key();
        if (listing.files.contains(path) || !listing.coversFile(path))
            continue;
        if (QFileInfo(absPath(path)).isFile() && !caseVariantListed(path))
            listing.unreadable.insert(paths::dirname(path));
    }
}

void VaultPrivate::noteListing(const DiskListing &listing, bool full)
{
    QSet<VaultPath> folders = listing.unreadable;
    if (!full) {
        // A partial scan vouches only for the folders it listed.
        for (const VaultPath &folder : std::as_const(scanStatus.unreadableFolders)) {
            if (!listing.listedDirs.contains(folder) && !listing.inListedTree(folder))
                folders.insert(folder);
        }
    }
    VaultScanStatus status;
    if (!folders.isEmpty()) {
        status.state = VaultScanStatus::State::Unreadable;
        status.unreadableFolders = QList<VaultPath>(folders.cbegin(), folders.cend());
        std::sort(status.unreadableFolders.begin(), status.unreadableFolders.end());
        QStringList shown;
        for (const VaultPath &folder : std::as_const(status.unreadableFolders))
            shown.append(folder.isEmpty() ? u"(vault folder)"_s : folder);
        status.message = u"%1 folder(s) could not be read; their content is kept as it was: %2"_s.arg(
                QString::number(shown.size()), shown.join(u", "_s));
        if (status != scanStatus)
            qCWarning(lcVault).noquote() << status.message;
    }
    setScanStatus(std::move(status));
}

void VaultPrivate::setScanStatus(VaultScanStatus status)
{
    if (status == scanStatus)
        return;
    scanStatus = std::move(status);
    Q_EMIT q->scanStatusChanged();
}

// ---------------------------------------------------------------------------------------------
// Watching
// ---------------------------------------------------------------------------------------------

void VaultPrivate::startWatching()
{
    watchRequested = true;
    if (watcher || !isOpen)
        return; // open() starts it
    watcher = std::make_unique<QFileSystemWatcher>(q);
    QObject::connect(watcher.get(), &QFileSystemWatcher::directoryChanged, q,
                     [this](const QString &path) { onWatcherEvent(path, true); });
    QObject::connect(watcher.get(), &QFileSystemWatcher::fileChanged, q,
                     [this](const QString &path) { onWatcherEvent(path, false); });
    QStringList paths{root};
    for (auto it = dirs.cbegin(); it != dirs.cend(); ++it)
        paths.append(absPath(it.key()));
    qsizetype budget = kMaxFileWatches;
    for (auto it = files.cbegin(); it != files.cend() && budget > 0; ++it, --budget)
        paths.append(absPath(it.key()));
    const QStringList failed = watcher->addPaths(paths);
    if (!failed.isEmpty()) {
        qCWarning(lcVault).noquote() << failed.size()
                                     << "paths cannot be watched (inotify limit?); rescan() finds changes there";
    }
    syncConfigFolderWatch();
}

void VaultPrivate::stopWatching()
{
    watchRequested = false;
    watcher.reset();
}

void VaultPrivate::watchFile(const VaultPath &path)
{
    if (!watcher || !vaultutil::isTracked(path))
        return;
    const QString abs = absPath(path);
    // A file replaced by renaming another over it (atomic saves of editors, QSaveFile,
    // Syncthing) leaves a dead watch on the old inode: always watch afresh.
    watcher->removePath(abs);
    if (watcher->files().size() < kMaxFileWatches)
        watcher->addPath(abs);
}

void VaultPrivate::watchDir(const VaultPath &path)
{
    if (watcher)
        watcher->addPath(absPath(path));
}

void VaultPrivate::unwatch(const VaultPath &path)
{
    if (watcher)
        watcher->removePath(absPath(path));
}

void VaultPrivate::syncConfigFolderWatch()
{
    if (!watcher)
        return;
    const QString folder = vaultutil::configFolder();
    if (isDirOnDisk(folder))
        watchDir(folder);
    else
        unwatch(folder);
}

void VaultPrivate::onWatcherEvent(const QString &absolute, bool isDir)
{
    const std::optional<VaultPath> path = vaultPathOfAbs(absolute);
    if (!path)
        return;
    (isDir ? pendingDirs : pendingFiles).insert(*path);
    if (!batchTimer.isActive())
        batchTimer.start();
}

void VaultPrivate::scheduleCheck(const VaultPath &path)
{
    pendingFiles.insert(path);
    if (!batchTimer.isActive())
        batchTimer.start();
}

void VaultPrivate::processPendingEvents()
{
    batchTimer.stop();
    ScanScope scope;
    scope.dirs = std::exchange(pendingDirs, {});
    scope.files = std::exchange(pendingFiles, {});
    if (scope.isEmpty() || !isOpen)
        return;
    const QList<VaultChange> changes = reconcile(scope, ChangeSource::External);
    // Re-arm watches: other programs replace files and folders by renaming new ones over them.
    for (const VaultPath &file : std::as_const(scope.files)) {
        if (files.contains(file))
            watchFile(file);
    }
    for (const VaultPath &dir : std::as_const(scope.dirs)) {
        if ((dir.isEmpty() || dirs.contains(dir)) && isDirOnDisk(dir))
            watchDir(dir);
    }
    finish(changes);
}

// ---------------------------------------------------------------------------------------------
// Vault
// ---------------------------------------------------------------------------------------------

void Vault::startWatching()
{
    d->startWatching();
}

void Vault::stopWatching()
{
    d->stopWatching();
}

bool Vault::isWatching() const
{
    return d->watcher != nullptr;
}

void Vault::setWatchDelay(int msec)
{
    d->batchTimer.setInterval(qMax(0, msec));
}

void Vault::setRescanInterval(int msec)
{
    if (msec > 0)
        d->rescanTimer.start(msec);
    else
        d->rescanTimer.stop();
}

QList<VaultChange> Vault::rescan()
{
    if (!d->isOpen)
        return {};
    // A full rescan covers everything queued by the watcher.
    d->pendingDirs.clear();
    d->pendingFiles.clear();
    d->batchTimer.stop();
    ScanScope scope;
    scope.trees.insert(QString());
    const QList<VaultChange> changes = d->reconcile(scope, ChangeSource::External);
    d->finish(changes);
    return changes;
}

void Vault::processPendingEvents()
{
    d->processPendingEvents();
}

VaultScanStatus Vault::scanStatus() const
{
    return d->scanStatus;
}

bool Vault::isStorageAvailable() const
{
    return d->isOpen && !d->rootProblem(true);
}

void Vault::setStorageProbe(std::function<QString(const QString &)> probe)
{
    d->storageProbe = std::move(probe);
}

} // namespace brulion
