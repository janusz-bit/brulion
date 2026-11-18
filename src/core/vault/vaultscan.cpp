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

#include "log/slowscope.h"

#include "text/text.h"
#include "util/paths.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QMultiHash>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion {
namespace {

bool entryByPath(const DiskEntry &a, const DiskEntry &b)
{
    return a.stat.path < b.stat.path;
}

/// How well `to` fits as the new name of `from` (both with the same content): two points per
/// trailing path segment they share — a moved folder keeps "Projekt/index.md", so identical
/// notes moved together pair with their own old paths — plus one for a rename in place.
int renameScore(const VaultPath &from, const VaultPath &to)
{
    const QStringList a = from.split(u'/');
    const QStringList b = to.split(u'/');
    const auto common = std::mismatch(a.crbegin(), a.crend(), b.crbegin(), b.crend()).first - a.crbegin();
    return static_cast<int>(2 * common) + (paths::dirname(from) == paths::dirname(to) ? 1 : 0);
}

/// True when `path` or a folder above it (the vault folder "" included) is one of `folders`.
bool atOrBelow(const QSet<VaultPath> &folders, const VaultPath &path)
{
    if (folders.isEmpty())
        return false;
    for (VaultPath cur = path;; cur = paths::dirname(cur)) {
        if (folders.contains(cur))
            return true;
        if (cur.isEmpty())
            return false;
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
// DiskListing coverage
// ---------------------------------------------------------------------------------------------

bool DiskListing::inListedTree(const VaultPath &path) const
{
    return atOrBelow(trees, path);
}

bool DiskListing::withinUnreadable(const VaultPath &path) const
{
    return atOrBelow(unreadable, path);
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
    // A stack of the folders still to list instead of recursion: how deep folders nest is up to
    // what was synced or imported, while the call stack is small on some platforms (1 MB) and
    // each level of recursion kept a directory open.
    struct Folder {
        VaultPath path;
        QString rel;            ///< relative to the root as spelled on disk
        bool recursive = false; ///< also list the subfolders the vault knows already
    };
    QList<Folder> todo{Folder{dir, dirRel, recursive}};
    while (!todo.isEmpty()) {
        const Folder folder = todo.takeLast();
        if (out.listedDirs.contains(folder.path) || out.unreadable.contains(folder.path))
            continue;
        const QString absDir = absOfDiskRel(folder.rel);
        // Without read permission the listing comes back empty, without search permission the
        // entries cannot be stat-ed: neither means that everything inside was deleted.
        if (const QFileInfo self(absDir); !self.isReadable() || !self.isExecutable()) {
            out.unreadable.insert(folder.path);
            continue;
        }
        out.listedDirs.insert(folder.path);
        ++listedFolderCount;
        // Taken before the listing: a change while listing leaves the folder at another stamp.
        if (const std::optional<qint64> stamp = vaultutil::fineMtime(absDir))
            out.stamps.insert(folder.path, *stamp);
        QDirIterator it(absDir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        while (it.hasNext()) {
            const QFileInfo info = it.nextFileInfo();
            const QString name = info.fileName();
            if (name.contains(u'\\'))
                continue; // would be read as a separator by paths::normalize
            const VaultPath path = paths::join(folder.path, name);
            if (path.isEmpty())
                continue;
            const QString rel = folder.rel.isEmpty() ? name : folder.rel + u'/' + name;
            if (info.isDir()) {
                if (info.isSymLink() && !followsLink(info, folder.rel)) {
                    // A link into the vault (duplicates), a loop, or one to outside the device does
                    // not follow (noted for the app's one-time check: Vault::skippedOutsideLinks()).
                    out.outsideLinkSkipped =
                            out.outsideLinkSkipped || (!followOutsideLinks && leadsOutside(info, folder.rel));
                    continue;
                }
                if (path == vaultutil::configFolder()) {
                    todo.append(Folder{path, rel, false}); // ignored, but its config.json is tracked
                    continue;
                }
                if (!vaultutil::isTracked(path) || out.dirs.contains(path))
                    continue;
                out.dirs.insert(path, DiskEntry{rel, statOf(info, path)});
                // A folder the vault does not know yet: everything inside it is new.
                if (folder.recursive || !dirs.contains(path)) {
                    if (!folder.recursive)
                        out.trees.insert(path); // a new folder found by a shallow listing
                    todo.append(Folder{path, rel, true});
                }
            } else if (info.isFile() && !info.isSymLink()) {
                // Links to files are never followed, wherever they point (vault.h, "Semantics").
                if (vaultutil::isTracked(path)) {
                    if (!out.files.contains(path))
                        out.files.insert(path, DiskEntry{rel, statOf(info, path)});
                } else if (vaultutil::isLeftoverTempName(name)) {
                    out.leftovers.append(rel); // (removed by open() once old enough)
                }
            }
        }
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
    QList<DiskEntry> folders = listing.dirs.values();
    std::ranges::sort(folders, entryByPath); // parents before children
    for (const DiskEntry &entry : std::as_const(folders)) {
        rememberDiskName(entry.stat.path, entry.diskRel);
        trackDir(entry.stat);
    }
    for (const DiskEntry &entry : listing.files) {
        rememberDiskName(entry.stat.path, entry.diskRel);
        trackFile(entry.stat);
    }
    adoptStamps(listing);
}

void VaultPrivate::adoptStamps(const DiskListing &listing)
{
    for (const VaultPath &dir : listing.listedDirs) {
        const auto stamp = listing.stamps.constFind(dir);
        if (stamp != listing.stamps.cend() && !listing.withinUnreadable(dir) && (dir.isEmpty() || dirs.contains(dir)))
            folderStamps.insert(dir, *stamp);
        else
            folderStamps.remove(dir);
    }
}

// ---------------------------------------------------------------------------------------------
// Diffing
// ---------------------------------------------------------------------------------------------

void VaultPrivate::coverCandidates(const DiskListing &listing, QList<VaultPath> &outFiles,
                                   QList<VaultPath> &outDirs) const
{
    if (listing.trees.contains(QString())) { // the whole vault
        outFiles = files.keys();
        outDirs = dirs.keys();
        return;
    }
    // A file is covered when stat-ed itself, or when its folder was listed, lies in a listed tree
    // (whose folders were all listed) or is gone from a listed folder — so the children of the
    // listed folders, and everything below their children that are gone, are all there is.
    for (const VaultPath &file : listing.checkedFiles) {
        if (files.contains(file))
            outFiles.append(file);
    }
    const VaultPath configFolder = vaultutil::configFolder(); // untracked, but holds config.json
    QSet<VaultPath> visited;
    QList<VaultPath> todo(listing.listedDirs.cbegin(), listing.listedDirs.cend());
    todo.append(QList<VaultPath>(listing.trees.cbegin(), listing.trees.cend()));
    while (!todo.isEmpty()) {
        const VaultPath dir = todo.takeLast();
        if (visited.contains(dir))
            continue;
        visited.insert(dir);
        if (const auto it = childFiles.constFind(dir); it != childFiles.cend()) {
            for (const VaultPath &file : *it) {
                if (!listing.checkedFiles.contains(file))
                    outFiles.append(file);
            }
        }
        if (const auto it = childDirs.constFind(dir); it != childDirs.cend()) {
            for (const VaultPath &child : *it) {
                outDirs.append(child);
                if (!listing.dirs.contains(child))
                    todo.append(child); // gone (or listed itself): its children are covered too
            }
        }
        if (dir.isEmpty())
            todo.append(configFolder); // never in listing.dirs (see listChildren())
    }
}

ScanDiff VaultPrivate::diff(const DiskListing &listing) const
{
    ScanDiff out;
    QList<VaultPath> candidateFiles;
    QList<VaultPath> candidateDirs;
    coverCandidates(listing, candidateFiles, candidateDirs);
    for (const VaultPath &path : std::as_const(candidateFiles)) {
        if (!listing.coversFile(path))
            continue;
        const FileStat &known = *files.constFind(path);
        if (const auto disk = listing.files.constFind(path); disk == listing.files.cend())
            out.vanishedFiles.append(known);
        else if (!sameStat(disk->stat, known) || staleContent.contains(path)
                 || (!unsettled.isEmpty() && unsettled.contains(path)))
            out.modifiedFiles.append(*disk);
    }
    for (const VaultPath &dir : std::as_const(candidateDirs)) {
        if (listing.coversDir(dir) && !listing.dirs.contains(dir))
            out.vanishedDirs.append(dir);
    }
    for (const auto &[path, entry] : listing.files.asKeyValueRange()) {
        if (!files.contains(path))
            out.appearedFiles.append(entry);
    }
    for (const auto &[path, entry] : listing.dirs.asKeyValueRange()) {
        if (!dirs.contains(path))
            out.appearedDirs.append(entry);
    }
#ifndef QT_NO_DEBUG
    // Every known entry the listing covers must have been a candidate.
    const QSet<VaultPath> fileSet(candidateFiles.cbegin(), candidateFiles.cend());
    const QSet<VaultPath> dirSet(candidateDirs.cbegin(), candidateDirs.cend());
    for (auto it = files.cbegin(); it != files.cend(); ++it)
        Q_ASSERT_X(fileSet.contains(it.key()) || !listing.coversFile(it.key()), "VaultPrivate::diff",
                   qPrintable(it.key()));
    for (auto it = dirs.cbegin(); it != dirs.cend(); ++it)
        Q_ASSERT_X(dirSet.contains(it.key()) || !listing.coversDir(it.key()), "VaultPrivate::diff",
                   qPrintable(it.key()));
#endif
    std::ranges::sort(out.vanishedFiles, byPath);
    std::ranges::sort(out.vanishedDirs);
    std::ranges::sort(out.modifiedFiles, entryByPath);
    std::ranges::sort(out.appearedFiles, entryByPath);
    std::ranges::sort(out.appearedDirs, entryByPath);
    return out;
}

std::optional<QByteArray> VaultPrivate::readEntry(const DiskEntry &entry, bool *tooBig) const
{
    vaultutil::ContentRead read = vaultutil::readContent(absOfDiskRel(entry.diskRel), ContentCache::kMaxItemBytes);
    if (tooBig)
        *tooBig = read.status == vaultutil::ContentRead::Status::TooBig;
    if (!read.ok())
        return std::nullopt;
    return std::move(read.bytes);
}

std::optional<QString> VaultPrivate::entryHash(const DiskEntry &entry, QHash<VaultPath, QString> &hashCache) const
{
    if (const auto it = hashCache.constFind(entry.stat.path); it != hashCache.cend())
        return *it;
    bool tooBig = false;
    const std::optional<QByteArray> bytes = readEntry(entry, &tooBig);
    if (!bytes && !tooBig)
        return std::nullopt;
    return *hashCache.insert(entry.stat.path, bytes ? text::fnv1a64Hex(*bytes) : QString());
}

bool VaultPrivate::sameContent(const FileStat &vanished, const DiskEntry &appeared,
                               QHash<VaultPath, QString> &hashCache) const
{
    const QString known = hashes.value(vanished.path);
    // Content never seen, or too big to read while scanning: a rename keeps the modification time.
    if (known.isEmpty() || appeared.stat.size > ContentCache::kMaxItemBytes)
        return vanished.mtime == appeared.stat.mtime;
    const std::optional<QString> hash = entryHash(appeared, hashCache);
    if (hash && hash->isEmpty()) // too big after all (more than its stat said)
        return vanished.mtime == appeared.stat.mtime;
    return hash == known;
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
    bool byStat = !comparesContent(entry.stat);
    const std::optional<QByteArray> bytes = byStat ? std::nullopt : readEntry(entry, &byStat);
    if (byStat) {
        // Binary files (and text too big to read while scanning, by its size or its content) are
        // compared by size and modification time only; text the vault still holds from before
        // goes to History.
        snapshotCached(path, source);
        hashes.remove(path);
        contents.remove(path);
        trackFile(entry.stat);
        watchFile(path);
        return VaultChange{ChangeType::Modified, path, {}, source};
    }
    if (!bytes) { // vanished between listing and reading
        snapshotCached(path, source);
        unwatch(path);
        untrackFile(path);
        return VaultChange{ChangeType::Deleted, path, {}, source};
    }
    const QString hash = text::fnv1a64Hex(*bytes);
    const bool same = hashes.value(path) == hash;
    if (!same)
        snapshotCached(path, source);
    trackFile(entry.stat);
    rememberContent(path, *bytes, /*asText=*/false, hash);
    watchFile(path);
    if (same) {
        settleIfDue(path);   // (an unsettled file compared: diff())
        return std::nullopt; // touched, or the echo of our own write
    }
    noteUnsettled(entry.stat, false);
    return VaultChange{ChangeType::Modified, path, {}, source};
}

bool VaultPrivate::adoptFile(const DiskEntry &entry, const QHash<VaultPath, QString> &hashCache)
{
    const VaultPath &path = entry.stat.path;
    bool byStat = !vaultutil::isTextPath(path) || entry.stat.size > ContentCache::kMaxItemBytes;
    const std::optional<QByteArray> bytes = byStat ? std::nullopt : readEntry(entry, &byStat);
    if (!byStat && !bytes)
        return false; // gone again
    rememberDiskName(path, entry.diskRel);
    trackFile(entry.stat);
    if (bytes) {
        rememberContent(path, *bytes);
        noteUnsettled(entry.stat, false);
    } else if (const auto it = hashCache.constFind(path); it != hashCache.cend() && !it->isEmpty()) {
        hashes.insert(path, *it); // read while looking for renames
    }
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
    adoptStamps(listing);
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
    if (!isDirOnDisk(QString()))
        return VaultScanStatus{VaultScanStatus::State::RootMissing, {}, u"The vault folder is missing: %1"_s.arg(root)};
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
    QList<VaultPath> candidateFiles;
    QList<VaultPath> candidateDirs;
    coverCandidates(listing, candidateFiles, candidateDirs);
    for (const VaultPath &dir : std::as_const(candidateDirs)) {
        if (listing.dirs.contains(dir) || !listing.coversDir(dir))
            continue;
        const QFileInfo info(absPath(dir));
        if (followOutsideLinks && info.isSymLink() && !info.exists())
            listing.unreadable.insert(dir); // a linked folder whose target is unmounted
        else if (isDirOnDisk(dir) && !caseVariantListed(dir))
            listing.unreadable.insert(paths::dirname(dir));
    }
    for (const VaultPath &path : std::as_const(candidateFiles)) {
        if (listing.files.contains(path) || !listing.coversFile(path))
            continue;
        // Gone for the vault, not a gap: a file replaced by a link, or one in a linked folder no
        // longer followed (diskStat() sees neither, nor does the listing).
        if (const std::optional<FileStat> st = diskStat(path); st && !st->isDir && !caseVariantListed(path))
            listing.unreadable.insert(paths::dirname(path));
    }
}

void VaultPrivate::noteListing(const DiskListing &listing, bool full)
{
    if (full)
        outsideLinksSkipped = listing.outsideLinkSkipped;
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
        status.unreadableFolders = folders.values();
        std::ranges::sort(status.unreadableFolders);
        QStringList shown = status.unreadableFolders;
        std::ranges::replace(shown, QString(), u"(vault folder)"_s);
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
    // Folders, not files: a folder's events cover what most programs do to the files in it
    // (Syncthing, Nextcloud and most editors write a new file and rename it over the old one;
    // creations, deletions, renames and touches). Watching every file instead costs an inotify
    // watch each (Android allows a few thousand per app) and Qt's addPaths() is quadratic in
    // the number of watched paths. In-place rewrites of the files no editor has open are found
    // by rescan() (the app rescans when it comes back to the foreground).
    QStringList toWatch{root};
    toWatch.reserve(dirs.size() + fileWatches.size() + 2);
    for (auto it = dirs.cbegin(); it != dirs.cend(); ++it)
        toWatch.append(absPath(it.key()));
    if (files.contains(vaultConfigPath()))
        toWatch.append(absPath(vaultConfigPath()));
    for (const VaultPath &file : std::as_const(fileWatches)) {
        if (files.contains(file))
            toWatch.append(absPath(file));
    }
    const QStringList failed = watcher->addPaths(toWatch);
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

bool VaultPrivate::watchesFile(const VaultPath &path) const
{
    return path == vaultConfigPath() || fileWatches.contains(path);
}

void VaultPrivate::watchFile(const VaultPath &path)
{
    if (!watcher || !vaultutil::isTracked(path) || !watchesFile(path))
        return;
    const QString abs = absPath(path);
    // A file replaced by renaming another over it (atomic saves of editors, QSaveFile,
    // Syncthing) leaves a dead watch on the old inode: always watch afresh.
    watcher->removePath(abs);
    if (files.contains(path))
        watcher->addPath(abs);
}

void VaultPrivate::keepWatching(const VaultPath &path)
{
    if (path.isEmpty())
        return;
    if (const qsizetype at = fileWatches.indexOf(path); at >= 0) {
        fileWatches.move(at, fileWatches.size() - 1); // most recently named last
        return;
    }
    fileWatches.append(path);
    if (fileWatches.size() > kMaxFileWatches) {
        const VaultPath oldest = fileWatches.takeFirst();
        if (watcher && oldest != vaultConfigPath())
            watcher->removePath(absPath(oldest));
    }
    watchFile(path);
}

void VaultPrivate::stopWatchingFile(const VaultPath &path)
{
    if (!fileWatches.removeOne(path) || path == vaultConfigPath())
        return;
    if (watcher)
        watcher->removePath(absPath(path));
}

void VaultPrivate::moveFileWatch(const VaultPath &from, const VaultPath &to)
{
    const qsizetype at = fileWatches.indexOf(from);
    if (at < 0 || from == to)
        return;
    if (fileWatches.contains(to))
        fileWatches.removeAt(at); // already watched under the new name
    else
        fileWatches[at] = to;
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

bool VaultPrivate::foreignBatchPending(const VaultPath &dir) const
{
    // A batch already queued for the folder may hold another program's change — unless it is the
    // echo of an own write that left the folder at its known stamp.
    return pendingDirs.contains(dir) && !ownEchoes.contains(dir);
}

void VaultPrivate::noteOwnWrite(const VaultPath &dir, const vaultutil::StampedSave &save, bool pendingBefore)
{
    // Only from the stamp at which the vault knew the folder: what other programs did to its
    // entries since is still to be seen. Between `before` and `after` the write itself changed
    // them twice (the temporary file, the rename), each a single system call; while the data was
    // written and synced the folder must have kept its stamp (`afterTemp` == `beforeRename`), or
    // another program changed it meanwhile.
    // Kernels that stamp files with their clock tick (Linux before 6.13, Android's) let two own
    // steps right after a query of the stamp keep it: then another program's change in the same
    // tick would keep it too, and no own write is trusted again (fine-grained times — multigrain
    // since Linux 6.13 — always move a stamp that was looked at).
    if ((save.before && save.afterTemp && *save.before == *save.afterTemp)
        || (save.beforeRename && save.after && *save.beforeRename == *save.after))
        coarseFolderTimes = true;
    const auto known = folderStamps.constFind(dir);
    const bool trusted = !coarseFolderTimes && !pendingBefore && save.before && save.afterTemp && save.beforeRename
                         && save.after && known != folderStamps.cend() && *known == *save.before
                         && *save.afterTemp == *save.beforeRename;
    if (!trusted) {
        folderStamps.remove(dir);
        ownEchoes.remove(dir);
        return;
    }
    if (!echoClock.isValid())
        echoClock.start();
    folderStamps.insert(dir, *save.after);
    // The echo comes with the next batch; one that waits longer is not trusted.
    ownEchoes.insert(dir, echoClock.elapsed() + std::max<qint64>(2000, 2LL * batchTimer.interval()));
}

void VaultPrivate::processPendingEvents()
{
    if (!writes.isEmpty()) {
        // The batch may hold the echo of a write in flight: it waits for the write's result
        // (mergeWrite() starts it again) — at most kMaxEchoWaitMs, then for the write itself.
        if (!batchDeferred.isValid()) {
            batchDeferred.start();
            writeWaitTimer.start(kMaxEchoWaitMs);
        }
        if (batchDeferred.elapsed() < kMaxEchoWaitMs)
            return;
        waitForWrites();
    }
    batchDeferred.invalidate();
    writeWaitTimer.stop();
    batchTimer.stop();
    ScanScope scope{.dirs = std::exchange(pendingDirs, {}), .files = std::exchange(pendingFiles, {})};
    if (!ownEchoes.isEmpty()) {
        // The echo of own writes: the folder still at the stamp the write left it at has the
        // entries the vault knows (the file's own stat was recorded by the write). Attribute
        // changes of other files there in the same batch (touch, chmod) wait for the next scan,
        // like in-place rewrites of files that are not watched.
        const qint64 now = echoClock.elapsed();
        for (auto it = ownEchoes.begin(); it != ownEchoes.end();) {
            const VaultPath dir = it.key();
            const bool expected = *it >= now;
            if (!scope.dirs.contains(dir)) {
                it = expected ? std::next(it) : ownEchoes.erase(it);
                continue;
            }
            it = ownEchoes.erase(it);
            const auto known = folderStamps.constFind(dir);
            if (expected && known != folderStamps.cend() && vaultutil::fineMtime(absPath(dir)) == *known)
                scope.dirs.remove(dir);
        }
    }
    if (scope.isEmpty() || !isOpen)
        return;
    const QList<VaultChange> changes = reconcile(scope, ChangeSource::External);
    // Re-arm watches: other programs replace files and folders by renaming new ones over them.
    for (const VaultPath &file : scope.files) {
        if (files.contains(file))
            watchFile(file);
    }
    for (const VaultPath &dir : scope.dirs) {
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

void Vault::watchFile(const VaultPath &path)
{
    d->keepWatching(paths::normalize(path));
}

void Vault::unwatchFile(const VaultPath &path)
{
    d->stopWatchingFile(paths::normalize(path));
}

qint64 Vault::listedFolders() const
{
    return d->listedFolderCount;
}

int Vault::maxWatchedFiles()
{
    return int(VaultPrivate::kMaxFileWatches);
}

QStringList Vault::watchedPaths() const
{
    if (!d->watcher)
        return {};
    QStringList folders = d->watcher->directories();
    QStringList files = d->watcher->files();
    folders.sort();
    files.sort();
    return folders + files;
}

void Vault::setWatchDelay(int msec)
{
    d->batchTimer.setInterval(std::max(0, msec));
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
    d->waitForWrites();
    if (!d->isOpen)
        return {};
    // A full rescan covers everything queued by the watcher.
    d->pendingDirs.clear();
    d->pendingFiles.clear();
    d->batchTimer.stop();
    logging::SlowScope slow(lcVault(), "Rescan", 1000);
    const QList<VaultChange> changes = d->reconcile(ScanScope{.trees = {QString()}}, ChangeSource::External);
    d->finish(changes);
    // Without config.json the settings come from .obsidian/ (ignored, so unwatched) and depend
    // on the tracked files (the guards of adoptObsidianSettings): taken in again.
    if (d->scanStatus.isOk() && !d->files.contains(vaultConfigPath()) && d->loadConfig(true))
        Q_EMIT configChanged();
    slow.setDetail(u"%1 changes, %2 files"_s.arg(changes.size()).arg(d->files.size()));
    return changes;
}

void Vault::processPendingEvents()
{
    d->waitForWrites();
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
