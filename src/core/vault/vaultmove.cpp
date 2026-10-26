// Vault: rename/move of files and folders, trash and restore.
#include "vault/vault.h"
#include "vault/vault_p.h"

#include "util/paths.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRandomGenerator>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion {

// ---------------------------------------------------------------------------------------------
// VaultPrivate
// ---------------------------------------------------------------------------------------------

bool VaultPrivate::renameTargetFree(const VaultPath &from, const VaultPath &to)
{
    if (files.contains(to) || dirs.contains(to))
        return fail(Vault::tr("%1 already exists").arg(to));
    // Another entry spelled differently only in letter case ("dom.md" next to "Dom.md"): one
    // entry on case-insensitive storage, and links do not tell them apart.
    if (from.toLower() != to.toLower() && isTakenCaseInsensitive(to))
        return fail(Vault::tr("%1 already exists").arg(existingCase(to, false)));
    if (!existsOnDisk(to))
        return true;
    // On case-insensitive storage the source itself answers to the new spelling: allowed.
    if (from.toLower() == to.toLower()) {
        const std::optional<FileStat> source = diskStat(from);
        const std::optional<FileStat> target = diskStat(to);
        if (source && target && source->isDir == target->isDir && source->size == target->size
            && source->mtime == target->mtime)
            return true;
    }
    return fail(Vault::tr("%1 already exists").arg(to));
}

bool VaultPrivate::moveOnDisk(const VaultPath &from, const VaultPath &to)
{
    const QString source = absPath(from);
    const QString target = absPath(to);
    // A symlink is moved itself, never what it points to.
    const QFileInfo info(source);
    const bool realDir = info.isDir() && !info.isSymLink();
    const auto move = [realDir](const QString &a, const QString &b) {
        return realDir ? QDir().rename(a, b) : QFile::rename(a, b);
    };
    // A folder's watches go first: Windows refuses to move a folder while a handle is open inside
    // it, and the watcher holds one for every folder it watches (and for the folder of every file).
    // relocate() watches the new locations; a failed move watches the old ones again.
    const QStringList released = realDir ? releaseWatches(source) : QStringList();
    const auto moved = [this, &released](bool ok) {
        if (!ok && watcher && !released.isEmpty())
            watcher->addPaths(released);
        return ok;
    };
    if (from.toLower() != to.toLower())
        return moved(move(source, target));
    // Case-only rename: through a temporary name, as some file systems ignore "a" -> "A".
    const QString temp = QFileInfo(target).absolutePath() + u"/.brulion-tmp-"_s
                         + QString::number(QRandomGenerator::global()->generate64(), 36);
    if (!move(source, temp))
        return moved(false);
    if (move(temp, target))
        return moved(true);
    move(temp, source);
    return moved(false);
}

QStringList VaultPrivate::releaseWatches(const QString &absoluteDir)
{
    if (!watcher)
        return {};
    const QString inside = absoluteDir + u'/';
    QStringList held;
    const QStringList watched = watcher->directories() + watcher->files();
    for (const QString &path : watched) {
        if (path == absoluteDir || path.startsWith(inside))
            held.append(path);
    }
    if (!held.isEmpty())
        watcher->removePaths(held);
    return held;
}

void VaultPrivate::moveFileState(const VaultPath &from, const FileStat &to)
{
    const QString hash = hashes.value(from);
    const std::optional<QByteArray> content = contents.take(from);
    const bool bom = bomPaths.contains(from);
    const bool stale = staleContent.contains(from);
    untrackFile(from);
    moveFileWatch(from, to.path);
    if (history && !history->rename(from, to.path))
        qCWarning(lcVault).noquote() << "Cannot move the history of" << from << ":" << history->errorString();
    if (!vaultutil::isTracked(to.path))
        return;
    trackFile(to);
    if (!hash.isEmpty())
        hashes.insert(to.path, hash);
    if (content)
        contents.put(to.path, *content);
    if (bom)
        bomPaths.insert(to.path);
    if (stale) {
        staleContent.insert(to.path);
        scheduleCheck(to.path);
    }
    watchFile(to.path);
}

QList<VaultChange> VaultPrivate::relocate(const VaultPath &from, const VaultPath &to, bool isDir, ChangeSource source)
{
    QList<FileStat> movedFiles;
    QList<FileStat> movedDirs;
    if (isDir) {
        for (const FileStat &st : std::as_const(files)) {
            if (vaultutil::isStrictlyWithin(st.path, from))
                movedFiles.append(st);
        }
        for (const FileStat &st : std::as_const(dirs)) {
            if (vaultutil::isWithin(st.path, from))
                movedDirs.append(st);
        }
    } else if (const auto it = files.constFind(from); it != files.cend()) {
        movedFiles.append(*it);
    }
    std::sort(movedFiles.begin(), movedFiles.end(), byPath);
    std::sort(movedDirs.begin(), movedDirs.end(), byPath);

    // Stop watching the old locations while their on-disk names are still known.
    for (const FileStat &st : std::as_const(movedFiles))
        unwatch(st.path);
    for (const FileStat &st : std::as_const(movedDirs))
        unwatch(st.path);
    rebaseDiskNames(from, to);

    for (const FileStat &st : std::as_const(movedDirs))
        untrackDir(st.path);
    for (FileStat st : std::as_const(movedDirs)) {
        st.path = vaultutil::rebase(st.path, from, to);
        if (!vaultutil::isTracked(st.path))
            continue;
        trackDir(st);
        watchDir(st.path);
    }

    QList<VaultChange> out;
    for (const FileStat &old : std::as_const(movedFiles)) {
        FileStat moved = old;
        moved.path = vaultutil::rebase(old.path, from, to);
        moveFileState(old.path, moved);
        if (files.contains(moved.path))
            out.append(VaultChange{ChangeType::Renamed, moved.path, old.path, source});
        else
            out.append(VaultChange{ChangeType::Deleted, old.path, {}, source}); // moved out of view
    }

    // Files the vault did not know at the source: moved in from an ignored folder or the trash.
    if (vaultutil::isTracked(to)) {
        ScanScope scope;
        if (isDir)
            scope.trees.insert(to);
        else if (!files.contains(to))
            scope.files.insert(to);
        out += reconcile(scope, source);
    }
    return out;
}

VaultPath VaultPrivate::trashTarget(const VaultPath &path, bool isDir) const
{
    const QString stamp = u" ("_s + vaultutil::trashStamp(now()) + u')';
    // ".trash/Zakupy" may be a trashed *file* while "Zakupy/x.md" is deleted: use "Zakupy (stamp)/".
    const VaultPath base = avoidFileAncestors(paths::join(vaultTrashFolder(), path), 1, [&stamp](int n) -> QString {
        if (n == 1)
            return stamp;
        return stamp + u' ' + QString::number(n);
    });
    if (!existsOnDisk(base))
        return base;
    const VaultPath stamped = vaultutil::insertBeforeExtension(base, stamp, isDir);
    VaultPath candidate = stamped;
    for (int i = 2; existsOnDisk(candidate); ++i)
        candidate = vaultutil::insertBeforeExtension(stamped, u' ' + QString::number(i), isDir);
    return candidate;
}

VaultPath VaultPrivate::avoidFileAncestors(const VaultPath &path, qsizetype keep,
                                           const std::function<QString(int)> &suffix) const
{
    QStringList segments = path.split(u'/');
    for (qsizetype i = keep; i + 1 < segments.size(); ++i) {
        const QString name = segments.at(i);
        const auto prefix = [&segments, i] { return segments.mid(0, i + 1).join(u'/'); };
        QFileInfo info(absPath(prefix()));
        // Moving below an existing *file* would fail: pick another folder name.
        for (int n = 1; (info.exists() || info.isSymLink()) && !info.isDir(); ++n) {
            segments[i] = name + suffix(n);
            info = QFileInfo(absPath(prefix()));
        }
        if (!info.exists())
            break; // nothing exists below a missing folder
    }
    return segments.join(u'/');
}

VaultPath VaultPrivate::freePath(const VaultPath &base, bool isDir) const
{
    return vaultutil::uniqueName(
            base,
            [this](const VaultPath &candidate) { return isTakenCaseInsensitive(candidate) || existsOnDisk(candidate); },
            isDir);
}

bool VaultPrivate::throughTrashLink(const VaultPath &path) const
{
    // Trash operations never move or delete files through a symlinked folder inside the trash
    // (".trash" itself included): a planted link would carry deleted notes out of the vault and
    // offer files from elsewhere for restoring.
    const QStringList chain = vaultutil::ancestors(path);
    for (const VaultPath &dir : chain) {
        if (vaultutil::isWithin(dir, vaultTrashFolder()) && QFileInfo(absPath(dir)).isSymLink())
            return true;
    }
    return false;
}

QString VaultPrivate::trashKey(const VaultPath &trashPath)
{
    return u"trash/"_s + trashPath;
}

void VaultPrivate::dropTrashMeta(const VaultPath &trashPath)
{
    store->remove(trashKey(trashPath));
    store->removePrefix(trashKey(trashPath) + u'/');
}

void VaultPrivate::pruneEmptyTrashFolders(VaultPath dir)
{
    while (vaultutil::isStrictlyWithin(dir, vaultTrashFolder())) {
        if (!QDir().rmdir(absPath(dir)))
            return; // not empty
        forgetDiskNames(dir);
        store->remove(trashKey(dir)); // a trashed folder whose last item was restored
        dir = paths::dirname(dir);
    }
}

// ---------------------------------------------------------------------------------------------
// Vault
// ---------------------------------------------------------------------------------------------

bool Vault::rename(const VaultPath &from, const VaultPath &to, ChangeSource source)
{
    if (!d->requireOpen())
        return false;
    const VaultPath a = paths::normalize(from);
    const VaultPath b = paths::normalize(to);
    if (a.isEmpty() || b.isEmpty())
        return d->fail(tr("Cannot rename the vault folder itself"));
    if (a == b)
        return true;
    if (vaultutil::isStrictlyWithin(b, a))
        return d->fail(tr("Cannot move %1 into itself").arg(a));
    d->resolveSpelling(a);
    const QFileInfo info(d->absPath(a));
    if (!info.exists() && !info.isSymLink()) {
        if (d->knownStat(a))
            d->scheduleCheck(a); // the cache is stale: report the deletion soon
        return d->fail(tr("%1 does not exist").arg(a));
    }
    if (!d->renameTargetFree(a, b))
        return false;
    const bool isDir = d->isDirOnDisk(a);
    QList<VaultChange> changes = d->takeInKindChange(a, isDir);
    bool ok = d->ensureDirs(paths::dirname(b), changes);
    if (ok && !d->moveOnDisk(a, b))
        ok = d->fail(tr("Cannot move %1 to %2").arg(a, b));
    if (ok && vaultutil::isStrictlyWithin(a, vaultTrashFolder()))
        d->dropTrashMeta(a); // moved out of the trash by hand
    if (ok)
        changes += d->relocate(a, b, isDir, source);
    d->finish(changes);
    return ok;
}

bool Vault::remove(const VaultPath &path, ChangeSource source)
{
    if (!d->requireOpen())
        return false;
    const VaultPath p = paths::normalize(path);
    if (p.isEmpty())
        return d->fail(tr("Cannot delete the vault folder itself"));
    if (vaultutil::isWithin(p, vaultTrashFolder()))
        return removePermanently(p, source);
    d->resolveSpelling(p);
    const QFileInfo info(d->absPath(p));
    if (!info.exists() && !info.isSymLink()) {
        // Already gone on disk (stale cache): just forget it.
        d->finish(d->forgetTree(p, source));
        return true;
    }
    const bool isDir = d->isDirOnDisk(p);
    const VaultPath target = d->trashTarget(p, isDir);
    if (d->throughTrashLink(target))
        return d->fail(tr("Cannot move %1 to the trash: %2 leads through a link").arg(p, paths::dirname(target)));
    QList<VaultChange> changes = d->takeInKindChange(p, isDir);
    bool ok = d->ensureDirs(paths::dirname(target), changes);
    if (ok && !d->moveOnDisk(p, target))
        ok = d->fail(tr("Cannot move %1 to the trash").arg(p));
    if (ok) {
        d->store->set(VaultPrivate::trashKey(target),
                      QJsonObject{{u"original"_s, p}, {u"deletedAt"_s, d->now().toMSecsSinceEpoch()}});
        changes += d->forgetTree(p, source);
    }
    d->finish(changes);
    return ok;
}

bool Vault::removePermanently(const VaultPath &path, ChangeSource source)
{
    if (!d->requireOpen())
        return false;
    const VaultPath p = paths::normalize(path);
    if (p.isEmpty())
        return d->fail(tr("Cannot delete the vault folder itself"));
    if (d->throughTrashLink(p))
        return d->fail(tr("Cannot delete %1: it lies behind a link in the trash").arg(p));
    d->resolveSpelling(p); // e.g. NFD names in the trash
    const QFileInfo info(d->absPath(p));
    bool ok = true;
    if (info.isDir() && !info.isSymLink())
        ok = QDir(info.absoluteFilePath()).removeRecursively();
    else if (info.exists() || info.isSymLink())
        ok = QFile::remove(info.absoluteFilePath());
    if (vaultutil::isWithin(p, vaultTrashFolder())) {
        d->dropTrashMeta(p);
        if (ok)
            d->pruneEmptyTrashFolders(paths::dirname(p));
    }
    // After a partial failure, report exactly what is gone.
    d->finish(ok ? d->forgetTree(p, source) : d->reconcile(ScanScope{{p}, {}, {}}, source));
    return ok || d->fail(tr("Cannot delete %1").arg(p));
}

QList<FileStat> Vault::trash() const
{
    QList<FileStat> out;
    if (!d->isOpen || QFileInfo(d->absPath(vaultTrashFolder())).isSymLink())
        return out; // a ".trash" link is never used (see remove())
    const QString base = d->absPath(vaultTrashFolder());
    QDirIterator it(base, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        out.append(statOf(info, paths::join(vaultTrashFolder(), info.filePath().sliced(base.size() + 1))));
    }
    std::sort(out.begin(), out.end(), byPath);
    return out;
}

TrashInfo Vault::trashInfo(const VaultPath &trashPath) const
{
    TrashInfo info;
    const VaultPath t = paths::normalize(trashPath);
    const QString trashFolder = vaultTrashFolder();
    if (!vaultutil::isStrictlyWithin(t, trashFolder))
        return info;
    // Metadata of the item itself, else of a trashed folder that contains it.
    QStringList chain{t};
    const QStringList up = vaultutil::ancestors(t);
    for (auto it = up.crbegin(); it != up.crend(); ++it) {
        if (vaultutil::isStrictlyWithin(*it, trashFolder))
            chain.append(*it);
    }
    for (const VaultPath &item : std::as_const(chain)) {
        const QJsonObject meta = d->store->get(VaultPrivate::trashKey(item)).toObject();
        const VaultPath original = paths::normalize(meta.value("original"_L1).toString());
        if (original.isEmpty() || vaultutil::isWithin(original, trashFolder))
            continue; // unusable metadata
        info.original = vaultutil::rebase(t, item, original);
        if (meta.value("deletedAt"_L1).isDouble())
            info.deletedAt = QDateTime::fromMSecsSinceEpoch(meta.value("deletedAt"_L1).toInteger());
        return info;
    }
    // Deleted on another device (or metadata lost): the trash path minus the stamps.
    QStringList segments = t.sliced(trashFolder.size() + 1).split(u'/');
    for (QString &segment : segments)
        segment = vaultutil::stripTrashStamp(segment);
    info.original = paths::normalize(segments.join(u'/'));
    return info;
}

VaultPath Vault::restore(const VaultPath &trashPath, ChangeSource source)
{
    if (!d->requireOpen())
        return {};
    const VaultPath t = paths::normalize(trashPath);
    if (!vaultutil::isStrictlyWithin(t, vaultTrashFolder())) {
        d->fail(tr("%1 is not in the trash").arg(t));
        return {};
    }
    if (d->throughTrashLink(t)) {
        d->fail(tr("Cannot restore %1: it lies behind a link in the trash").arg(t));
        return {};
    }
    d->resolveSpelling(t); // items deleted on other devices may have NFD names
    const QFileInfo info(d->absPath(t));
    if (!info.exists() && !info.isSymLink()) {
        d->fail(tr("%1 does not exist").arg(t));
        return {};
    }
    const VaultPath original = trashInfo(t).original;
    if (original.isEmpty()) {
        d->fail(tr("Cannot tell where %1 came from").arg(t));
        return {};
    }
    const bool isDir = d->isDirOnDisk(t);
    // A folder of the original path may meanwhile be a file: restore into "Folder 2/" then.
    const VaultPath target = d->freePath(
            d->avoidFileAncestors(original, 0, [](int n) -> QString { return u' ' + QString::number(n + 1); }), isDir);
    QList<VaultChange> changes;
    bool ok = d->ensureDirs(paths::dirname(target), changes);
    if (ok && !d->moveOnDisk(t, target))
        ok = d->fail(tr("Cannot restore %1 to %2").arg(t, target));
    if (ok) {
        d->dropTrashMeta(t);
        d->pruneEmptyTrashFolders(paths::dirname(t));
        changes += d->relocate(t, target, isDir, source);
    }
    d->finish(changes);
    return ok ? target : VaultPath();
}

bool Vault::emptyTrash()
{
    return removePermanently(vaultTrashFolder(), ChangeSource::User);
}

} // namespace brulion
