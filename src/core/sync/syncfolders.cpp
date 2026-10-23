// One SyncEngine run, continued: moved content and folders.
//
//  * Moves: a file whose content arrives under a new path in the same run (uploaded or
//    downloaded by the first pass) was moved, not deleted — the guards do not count it. A folder
//    renamed in the vault (its synced files found below the new name, at least half of them
//    unchanged) is moved on the server with one MOVE instead of a deletion and an upload per
//    file; the sync states, merge bases and the listing follow it, so the files are then compared
//    at their new paths as usual: an edit made on the server meanwhile comes down into the renamed
//    folder, edits on both sides merge, a file deleted since the rename is deleted on the server.
//    Without MOVE (refused, or the new name taken meanwhile) the files travel one by one.
//  * Folders: an empty folder travels both ways (MKCOL / a new local folder); a folder deleted on
//    one side is deleted on the other once it holds nothing there — a local folder only when it is
//    empty on disk (no file at all: ignored and hidden ones count), a server folder only when the
//    run's view and a fresh listing show nothing in it. Which folders existed on both sides is
//    kept per folder (SyncEngine::folderKey); the folders of synced files count as known too.
#include "sync/syncrun_p.h"

#include "sync/util_p.h"
#include "util/paths.h"
#include "vault/localstore.h"
#include "vault/vault.h"
#include "vault/vaultconfig.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

#include <algorithm>
#include <filesystem>
#include <system_error>

using namespace Qt::StringLiterals;

namespace brulion::sync {

using detail::lcSync;

namespace {

/// `path` is `folder` or lies below it.
bool isWithin(const VaultPath &path, const VaultPath &folder)
{
    return path == folder || (path.size() > folder.size() && path.startsWith(folder) && path.at(folder.size()) == u'/');
}

void addAncestors(const VaultPath &path, QSet<VaultPath> &out)
{
    for (qsizetype slash = path.indexOf(u'/'); slash > 0; slash = path.indexOf(u'/', slash + 1))
        out.insert(path.left(slash));
}

qsizetype depthOf(const VaultPath &path)
{
    return path.count(u'/');
}

std::filesystem::path fsPath(const QString &path)
{
    const QByteArray utf8 = path.toUtf8();
    return std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t *>(utf8.constData()), static_cast<size_t>(utf8.size())));
}

/// Both absolute paths name one folder on disk (the same file system entry — case-insensitive
/// storage: "Projekty" opens "projekty"). False when either is missing.
bool sameFolderOnDisk(const QString &a, const QString &b)
{
    std::error_code error;
    const bool same = std::filesystem::equivalent(fsPath(a), fsPath(b), error);
    return !error && same;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Moved content
// ---------------------------------------------------------------------------------------------

bool SyncRun::isDeletionCandidate(const VaultPath &path) const
{
    return m_states.contains(path) && m_local.contains(path) != m_remote.contains(path);
}

QSet<VaultPath> SyncRun::knownFolders() const
{
    // A synced file's folder existed on both sides when the file was synced.
    QSet<VaultPath> out = m_folderStates;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        if (it.key() != vaultConfigPath())
            addAncestors(it.key(), out);
    }
    return out;
}

QHash<QString, int> SyncRun::newLocalHashes(const QList<SyncFileState> &wanted)
{
    QSet<qint64> sizes;
    for (const SyncFileState &state : wanted)
        sizes.insert(state.size);
    QHash<QString, int> out;
    for (auto it = m_local.cbegin(); it != m_local.cend(); ++it) {
        if (m_states.contains(it.key()) || !sizes.contains(it->size) || it->size > m_engine->maxFileSize())
            continue;
        const ReadResult read = readLocal(it.key(), *it, std::nullopt);
        if (read.snap)
            ++out[read.snap->hash];
    }
    return out;
}

int SyncRun::countMoved(const QList<VaultPath> &paths, bool here, QHash<QString, int> &arrived) const
{
    // `here`: the paths are gone locally and their content may have been renamed locally (and
    // uploaded); otherwise they are gone on the server and may have been moved there.
    QSet<QString> renamedCase; // new names on the side the file vanished from, lower-cased
    QHash<QString, int> versions;
    if (here) {
        for (auto it = m_local.cbegin(); it != m_local.cend(); ++it) {
            if (!m_states.contains(it.key()))
                renamedCase.insert(it.key().toLower());
        }
    } else {
        for (auto it = m_remote.cbegin(); it != m_remote.cend(); ++it) {
            if (m_states.contains(it.key()))
                continue;
            renamedCase.insert(it.key().toLower());
            // A move on the server keeps the file's ETag (Nextcloud); the listing-based version of
            // servers without ETags (mtime and size) proves nothing.
            if (!it->etag.isEmpty())
                ++versions[it->etag];
        }
    }
    int moved = 0;
    for (const VaultPath &path : paths) {
        const SyncFileState state = m_states.value(path);
        if (renamedCase.contains(path.toLower())) {
            ++moved; // the same name in another letter case: a case-only rename
            continue;
        }
        if (const auto it = arrived.find(state.localHash); it != arrived.end() && *it > 0) {
            --*it;
            ++moved;
            continue;
        }
        if (!here && !state.remoteEtag.isEmpty()) {
            if (const auto it = versions.find(state.remoteEtag); it != versions.end() && *it > 0) {
                --*it;
                ++moved;
            }
        }
    }
    return moved;
}

QList<SyncRun::FolderMove> SyncRun::findFolderMoves()
{
    if (m_states.isEmpty())
        return {};
    // New local files (never synced, not on the server) by name.
    QHash<QString, QList<VaultPath>> newByName;
    for (auto it = m_local.cbegin(); it != m_local.cend(); ++it) {
        if (!m_states.contains(it.key()) && !m_remote.contains(it.key()))
            newByName[paths::basename(it.key())].append(it.key());
    }
    if (newByName.isEmpty())
        return {};
    QSet<VaultPath> here = localFolders();
    for (auto it = m_local.cbegin(); it != m_local.cend(); ++it)
        addAncestors(it.key(), here);
    const QSet<VaultPath> known = knownFolders();
    // Synced folders missing here that the server still has.
    const auto vanished = [&](const VaultPath &folder) {
        if (folder.isEmpty() || !known.contains(folder) || here.contains(folder) || !isSyncablePath(folder))
            return false;
        const auto remote = m_remoteAll.constFind(folder);
        return remote != m_remoteAll.cend() && remote->isDir;
    };
    QStringList sortedKnown(known.cbegin(), known.cend());
    std::sort(sortedKnown.begin(), sortedKnown.end());
    QStringList queue;
    for (const VaultPath &folder : std::as_const(sortedKnown)) {
        if (vanished(folder) && !vanished(paths::dirname(folder)))
            queue.append(folder);
    }
    QList<FolderMove> moves;
    QSet<VaultPath> taken;
    while (!queue.isEmpty()) {
        const VaultPath from = queue.takeFirst();
        if (const std::optional<VaultPath> to = renameTarget(from, known, here, newByName, taken)) {
            moves.append(FolderMove{from, *to});
            taken.insert(*to);
            continue;
        }
        // Not renamed as a whole: maybe a folder inside it was.
        for (const VaultPath &folder : std::as_const(sortedKnown)) {
            if (paths::dirname(folder) == from && vanished(folder))
                queue.append(folder);
        }
    }
    // A move into a folder another move creates comes after it.
    std::sort(moves.begin(), moves.end(), [](const FolderMove &a, const FolderMove &b) { return a.to < b.to; });
    for (const FolderMove &move : std::as_const(moves))
        qCDebug(lcSync).noquote() << u"%1 folder renamed here: %2 -> %3"_s.arg(tag(), move.from, move.to);
    return moves;
}

std::optional<VaultPath> SyncRun::renameTarget(const VaultPath &from, const QSet<VaultPath> &known,
                                               const QSet<VaultPath> &localFolders,
                                               const QHash<QString, QList<VaultPath>> &newByName,
                                               const QSet<VaultPath> &taken)
{
    QStringList files;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        if (it.key() != from && isWithin(it.key(), from) && it.key() != vaultConfigPath())
            files.append(it.key());
    }
    if (files.isEmpty())
        return std::nullopt;
    std::sort(files.begin(), files.end());
    const auto renamedTo = [&](const VaultPath &to) {
        if (taken.contains(to) || isWithin(to, from) || isWithin(from, to) || !isSyncablePath(to)
            || !localFolders.contains(to)) {
            return false;
        }
        // A folder never synced: nothing known at or below it (its files are all new).
        if (std::any_of(known.cbegin(), known.cend(), [&to](const VaultPath &k) { return isWithin(k, to); })
            || std::any_of(m_states.keyBegin(), m_states.keyEnd(),
                           [&to](const VaultPath &k) { return isWithin(k, to); })) {
            return false;
        }
        // Nothing on the server at or below the new name, in any letter case, but the old folder
        // itself (a case-only rename).
        const QString lower = to.toLower();
        const bool clash = std::any_of(m_remoteAll.keyBegin(), m_remoteAll.keyEnd(), [&](const VaultPath &path) {
            if (isWithin(path, from))
                return false;
            const QString l = path.toLower();
            return l == lower || (l.startsWith(lower) && l.at(lower.size()) == u'/');
        });
        if (clash)
            return false;
        // At least half of the synced files of the old folder are at their places in the new one,
        // unchanged; the others may have been edited (compared as usual at the new path) or
        // deleted (deleted on the server as usual) since the rename. A file the server no longer
        // has stays out of it: moved, it would be taken for deleted there, while the rename of a
        // file wins over its deletion on the other side (it is uploaded again). When this device
        // had none of its synced files under their names (m_localLookedEmpty), every file found
        // must be unchanged: an older copy put back after the storage was lost would otherwise
        // overwrite the newer notes on the server as if they had been edited here.
        int unchanged = 0;
        int found = 0;
        for (const VaultPath &file : std::as_const(files)) {
            const VaultPath target = to + file.mid(from.size());
            if (!m_local.contains(target))
                continue;
            if (!m_remote.contains(file))
                return false;
            ++found;
            if (sameContent(target, m_states.value(file)))
                ++unchanged;
        }
        if (m_localLookedEmpty && unchanged != found)
            return false;
        return unchanged > 0 && unchanged * 2 >= files.size();
    };
    QSet<VaultPath> tried;
    for (const VaultPath &file : std::as_const(files)) {
        const QString rel = file.mid(from.size()); // "/sub/name.md"
        for (const VaultPath &candidate : newByName.value(paths::basename(file))) {
            if (!candidate.endsWith(rel) || candidate.size() == rel.size())
                continue;
            const VaultPath to = candidate.left(candidate.size() - rel.size());
            if (tried.contains(to))
                continue;
            tried.insert(to);
            if (renamedTo(to))
                return to;
        }
    }
    return std::nullopt;
}

bool SyncRun::sameContent(const VaultPath &localPath, const SyncFileState &state)
{
    const FileStat stat = m_local.value(localPath);
    // A rename keeps size and modification time: the engine's quick check for unchanged binary files.
    if (!isSyncTextPath(localPath) && state.mtime && stat.size == state.size && stat.mtime == *state.mtime)
        return true;
    if (stat.size != state.size || stat.size > m_engine->maxFileSize())
        return false;
    const ReadResult read = readLocal(localPath, stat, std::nullopt);
    return read.snap && read.snap->hash == state.localHash;
}

void SyncRun::moveFolders(qsizetype index, std::function<void()> next)
{
    if (index >= m_moves.size() || m_cancelled) {
        next();
        return;
    }
    const FolderMove move = m_moves.at(index);
    MoveOptions options;
    options.folder = true;
    client()->move(move.from, move.to, options,
                   guarded([this, index, move, next = std::move(next)](const WebDavError &error) mutable {
                       if (!error.isError()) {
                           applyFolderMove(move);
                           qCInfo(lcSync).noquote() << u"%1 folder %2 renamed here: moved to %3 on the server"_s.arg(
                                   tag(), move.from, move.to);
                       } else if (error.isFatal()) {
                           m_report.remoteError = error;
                           finish();
                           return;
                       } else {
                           // Not moved (no MOVE on this server, the new name taken meanwhile, …): the
                           // files are deleted and uploaded one by one instead.
                           qCInfo(lcSync).noquote()
                                   << u"%1 folder %2 renamed here to %3: not moved on the server (%4, HTTP %5); "
                                      u"its files are uploaded and deleted instead"_s.arg(
                                              tag(), move.from, move.to, errorCodeName(error.code),
                                              QString::number(error.status));
                       }
                       moveFolders(index + 1, std::move(next));
                   }));
}

void SyncRun::applyFolderMove(const FolderMove &move)
{
    const auto rebased = [&move](const VaultPath &path) { return move.to + path.mid(move.from.size()); };
    // Sync states and merge bases: the files are compared at their new paths.
    QStringList statePaths;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        if (isWithin(it.key(), move.from))
            statePaths.append(it.key());
    }
    for (const VaultPath &path : std::as_const(statePaths)) {
        const VaultPath target = rebased(path);
        m_states.insert(target, m_states.take(path));
        const QJsonValue state = store()->get(SyncEngine::stateKey(path));
        const QJsonValue base = store()->get(SyncEngine::baseKey(path));
        if (!state.isUndefined())
            store()->set(SyncEngine::stateKey(target), state);
        if (!base.isUndefined())
            store()->set(SyncEngine::baseKey(target), base);
        store()->remove(SyncEngine::stateKey(path));
        store()->remove(SyncEngine::baseKey(path));
    }
    const QSet<VaultPath> folders = m_folderStates;
    for (const VaultPath &folder : folders) {
        if (!isWithin(folder, move.from))
            continue;
        m_folderStates.remove(folder);
        m_folderStates.insert(rebased(folder));
        store()->remove(SyncEngine::folderKey(folder));
        store()->set(SyncEngine::folderKey(rebased(folder)), QJsonObject{{u"syncedAt"_s, now().toMSecsSinceEpoch()}});
    }
    // The listing as the server has it now.
    QHash<VaultPath, RemoteEntry> remote;
    for (auto it = m_remote.cbegin(); it != m_remote.cend(); ++it) {
        RemoteEntry entry = *it;
        if (isWithin(entry.path, move.from))
            entry.path = rebased(entry.path);
        remote.insert(entry.path, entry);
    }
    m_remote = remote;
    QHash<VaultPath, RemoteEntry> all;
    for (auto it = m_remoteAll.cbegin(); it != m_remoteAll.cend(); ++it) {
        RemoteEntry entry = *it;
        if (isWithin(entry.path, move.from))
            entry.path = rebased(entry.path);
        all.insert(entry.path, entry);
    }
    m_remoteAll = all;
    QSet<VaultPath> parents;
    addAncestors(move.to, parents);
    for (const VaultPath &parent : std::as_const(parents)) {
        if (!m_remoteAll.contains(parent))
            m_remoteAll.insert(parent, RemoteEntry{parent, true, {}, 0, 0});
    }
    m_report.folderMoves.append(SyncFolderMove{move.from, move.to});
    record(move.to, SyncAction::MoveRemote);
}

void SyncRun::noteRemoteWritten(const VaultPath &path, bool isDir)
{
    if (const auto it = m_remoteAll.constFind(path); it == m_remoteAll.cend() || it->isDir != isDir)
        m_remoteAll.insert(path, RemoteEntry{path, isDir, {}, 0, 0});
    QSet<VaultPath> parents;
    addAncestors(path, parents);
    for (const VaultPath &parent : std::as_const(parents)) {
        if (!m_remoteAll.contains(parent))
            m_remoteAll.insert(parent, RemoteEntry{parent, true, {}, 0, 0});
    }
}

void SyncRun::noteRemoteDeleted(const VaultPath &path)
{
    m_remoteAll.removeIf(
            [&path](const QHash<VaultPath, RemoteEntry>::iterator it) { return isWithin(it.key(), path); });
}

// ---------------------------------------------------------------------------------------------
// Folders
// ---------------------------------------------------------------------------------------------

QSet<VaultPath> SyncRun::localFolders() const
{
    QSet<VaultPath> out;
    for (const FileStat &folder : vault()->folders()) {
        if (!folder.path.isEmpty() && isSyncablePath(folder.path))
            out.insert(folder.path);
    }
    return out;
}

QSet<VaultPath> SyncRun::remoteFolders() const
{
    QSet<VaultPath> out;
    for (auto it = m_remoteAll.cbegin(); it != m_remoteAll.cend(); ++it) {
        if (it->isDir && isSyncablePath(it.key()))
            out.insert(it.key());
    }
    return out;
}

void SyncRun::syncFolders(std::function<void()> done)
{
    const QSet<VaultPath> local = localFolders();
    const QSet<VaultPath> remote = remoteFolders();
    const QSet<VaultPath> known = knownFolders();
    QSet<VaultPath> all = local;
    all.unite(remote);
    for (const VaultPath &folder : known) {
        if (isSyncablePath(folder))
            all.insert(folder);
    }
    QStringList sorted(all.cbegin(), all.cend());
    std::sort(sorted.begin(), sorted.end());

    QList<FolderOp> ops;
    // Deletions first, deepest first: a folder emptied by its subfolders' deletion goes too, and
    // on case-insensitive storage the old spelling goes before the new one is created.
    if (!m_holdDeletions) {
        QStringList deepest = sorted;
        std::stable_sort(deepest.begin(), deepest.end(),
                         [](const VaultPath &a, const VaultPath &b) { return depthOf(a) > depthOf(b); });
        for (const VaultPath &folder : std::as_const(deepest)) {
            if (!known.contains(folder))
                continue;
            const bool l = local.contains(folder);
            const bool r = remote.contains(folder);
            if (l && !r)
                ops.append(FolderOp{FolderOp::Kind::DeleteLocal, folder});
            else if (!l && r)
                ops.append(FolderOp{FolderOp::Kind::DeleteRemote, folder});
        }
    }
    // Then the new folders, parents first (the letter case is checked when they are created:
    // after the deletions).
    for (const VaultPath &folder : std::as_const(sorted)) {
        if (known.contains(folder))
            continue;
        const bool l = local.contains(folder);
        const bool r = remote.contains(folder);
        if (l && !r)
            ops.append(FolderOp{FolderOp::Kind::CreateRemote, folder});
        else if (!l && r)
            ops.append(FolderOp{FolderOp::Kind::CreateLocal, folder});
    }
    runFolderOps(std::make_shared<const QList<FolderOp>>(std::move(ops)), 0, [this, done = std::move(done)] {
        if (!m_finished && !m_cancelled && !m_report.remoteError.isError())
            recordFolders();
        done();
    });
}

void SyncRun::folderFailed(const VaultPath &path, const QString &code, const QString &message, int status)
{
    const SyncFileError error{path, logging::redact(message), code, status};
    m_report.errors.append(error);
    qCWarning(lcSync).noquote() << u"%1 folder %2 not synced: %3%4"_s.arg(
            tag(), path, code, status > 0 ? u" (HTTP %1)"_s.arg(status) : u" — "_s + error.message);
}

void SyncRun::runFolderOps(std::shared_ptr<const QList<FolderOp>> ops, qsizetype index, std::function<void()> done)
{
    const auto fatal = [this](const WebDavError &error) {
        if (!error.isFatal())
            return false;
        if (!m_report.remoteError.isError())
            m_report.remoteError = error;
        finish();
        return true;
    };
    // Local steps run in this loop; a request continues from its answer (never nested).
    for (; index < ops->size(); ++index) {
        if (m_cancelled || m_finished)
            break;
        const FolderOp op = ops->at(index);
        const auto next = [this, ops, index, done] { runFolderOps(ops, index + 1, done); };
        switch (op.kind) {
        case FolderOp::Kind::DeleteLocal: {
            // Deleted on the server. Kept while it holds anything at all (a file not synced yet, an
            // ignored or hidden file); rmdir() never removes a folder that is not empty.
            const QString absolute = vault()->absolutePath(op.path);
            const QFileInfo info(absolute);
            if (!info.isDir() || info.isSymLink())
                continue;
            // On case-insensitive storage the server's "Projekty" is this very "projekty".
            const bool stillThere = std::any_of(m_remoteAll.cbegin(), m_remoteAll.cend(), [&](const RemoteEntry &e) {
                return e.isDir && e.path != op.path && e.path.compare(op.path, Qt::CaseInsensitive) == 0
                       && sameFolderOnDisk(absolute, vault()->absolutePath(e.path));
            });
            if (stillThere)
                continue;
            if (!QDir(absolute).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)
                || !QDir().rmdir(absolute)) {
                qCInfo(lcSync).noquote() << u"%1 folder %2 deleted on the server is kept here: it is not empty"_s.arg(
                        tag(), op.path);
                continue;
            }
            vault()->remove(op.path, ChangeSource::Sync); // gone on disk: the vault forgets it
            record(op.path, SyncAction::DeleteFolderLocal);
            continue;
        }
        case FolderOp::Kind::CreateLocal:
            // A folder (or file) here in another letter case is that folder: case-insensitive
            // storage holds one, and links do not tell them apart.
            if (vault()->exists(op.path) || vault()->existingCase(op.path) != op.path)
                continue;
            if (vault()->createFolder(op.path))
                record(op.path, SyncAction::CreateFolderLocal);
            else
                folderFailed(op.path, u"write-failed"_s, vault()->errorString());
            continue;
        case FolderOp::Kind::CreateRemote:
            if (std::any_of(m_remoteAll.keyBegin(), m_remoteAll.keyEnd(),
                            [&op](const VaultPath &path) { return path.compare(op.path, Qt::CaseInsensitive) == 0; })) {
                continue; // the server has it in another letter case
            }
            client()->mkdirs(op.path, guarded([this, op, next, fatal](const WebDavError &error) {
                                 if (fatal(error))
                                     return;
                                 if (error.isError()) {
                                     folderFailed(op.path, errorCodeName(error.code), error.message, error.status);
                                 } else {
                                     noteRemoteWritten(op.path, true);
                                     record(op.path, SyncAction::CreateFolderRemote);
                                 }
                                 next();
                             }));
            return;
        case FolderOp::Kind::DeleteRemote: {
            // Deleted here. The server deletes a folder with everything in it, so only one that
            // holds nothing: not in this run's view (files not synced, ignored names, new uploads)
            // and not in a fresh listing (written by another device meanwhile).
            const bool holds = std::any_of(m_remoteAll.keyBegin(), m_remoteAll.keyEnd(), [&op](const VaultPath &path) {
                return path != op.path && isWithin(path, op.path);
            });
            if (holds) {
                qCInfo(lcSync).noquote() << u"%1 folder %2 deleted here is kept on the server: it is not empty"_s.arg(
                        tag(), op.path);
                continue;
            }
            client()->listFolder(
                    op.path, guarded([this, op, next, fatal](const ListResult &listed) {
                        if (listed.error.is(WebDavErrorCode::NotFound)) {
                            noteRemoteDeleted(op.path); // gone already
                            next();
                            return;
                        }
                        if (fatal(listed.error))
                            return;
                        if (listed.error.isError()) {
                            folderFailed(op.path, errorCodeName(listed.error.code), listed.error.message,
                                         listed.error.status);
                            next();
                            return;
                        }
                        if (!listed.entries.isEmpty()) {
                            for (const RemoteEntry &entry : listed.entries)
                                m_remoteAll.insert(entry.path, entry);
                            qCInfo(lcSync).noquote()
                                    << u"%1 folder %2 deleted here is kept on the server: it holds %3 new item(s)"_s
                                               .arg(tag(), op.path, QString::number(listed.entries.size()));
                            next();
                            return;
                        }
                        client()->remove(op.path, {}, guarded([this, op, next, fatal](const WebDavError &error) {
                                             if (fatal(error))
                                                 return;
                                             if (error.isError()) {
                                                 folderFailed(op.path, errorCodeName(error.code), error.message,
                                                              error.status);
                                             } else {
                                                 noteRemoteDeleted(op.path);
                                                 record(op.path, SyncAction::DeleteFolderRemote);
                                             }
                                             next();
                                         }));
                    }));
            return;
        }
        }
    }
    done();
}

void SyncRun::recordFolders()
{
    // Folders on both sides are known from now on; a known folder still on one side (kept because
    // it is not empty there, or its deletion waits for confirmation) stays known, so it is not
    // taken for a new one; one gone from both sides is forgotten.
    const QSet<VaultPath> local = localFolders();
    const QSet<VaultPath> remote = remoteFolders();
    QSet<VaultPath> keep;
    for (const VaultPath &folder : local) {
        if (remote.contains(folder))
            keep.insert(folder);
    }
    for (const VaultPath &folder : knownFolders()) {
        if (isSyncablePath(folder) && (local.contains(folder) || remote.contains(folder)))
            keep.insert(folder);
    }
    const QJsonObject stamp{{u"syncedAt"_s, now().toMSecsSinceEpoch()}};
    for (const VaultPath &folder : std::as_const(keep)) {
        if (!m_folderStates.contains(folder))
            store()->set(SyncEngine::folderKey(folder), stamp);
    }
    for (const VaultPath &folder : std::as_const(m_folderStates)) {
        if (!keep.contains(folder))
            store()->remove(SyncEngine::folderKey(folder));
    }
    m_folderStates = keep;
}

} // namespace brulion::sync
