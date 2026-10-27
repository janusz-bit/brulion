// One SyncEngine run, continued: moved content and folders.
//
//  * Moves: a file whose content arrives under a new path in the same run (uploaded or
//    downloaded by the first pass) was moved, not deleted — the guards do not count it. A folder
//    renamed in the vault (its synced files found below the new name, at least half of them
//    unchanged) is moved on the server with one MOVE instead of a deletion and an upload per
//    file; the sync states, merge bases and the listing follow it, so the files are then compared
//    at their new paths as usual: an edit made on the server meanwhile comes down into the renamed
//    folder, edits on both sides merge, a file deleted since the rename is deleted on the server.
//    Without MOVE (refused, or the new name taken meanwhile) the files travel one by one. A single
//    file renamed in the vault is moved the same way, one MOVE per file: proven by the rename
//    journal (SyncEngine::renamedFrom(): edits made since are uploaded after the MOVE), else by its
//    unchanged content in exactly one new file. A folder's evidence is counted by its parts (each
//    file in it, each subfolder as one): a subfolder moved elsewhere never takes its parent along.
//  * Renames on the server: a folder or file renamed or moved there (by another device's MOVE, the
//    Nextcloud web page or its desktop client) is known by the server's file ids (oc:fileid; else
//    by the ETag a move keeps, and the size) at the same places below the new name. It is renamed
//    here with Vault::rename — nothing downloaded, nothing trashed, the notes keep their version
//    history, favourites and folds — unless something here has the new name already or the vault
//    cannot rename it (a file held open): then the files travel one by one as before.
//  * Folders: an empty folder travels both ways (MKCOL / a new local folder); a folder deleted on
//    one side is deleted on the other once it holds nothing there — a local folder only when it is
//    empty on disk (no file at all: ignored and hidden ones count), a server folder only when the
//    run's view and a fresh listing show nothing in it. Which folders existed on both sides is
//    kept per folder (SyncEngine::folderKey); the folders of synced files count as known too.
#include "sync/syncrun_p.h"

#include "sync/pool_p.h"
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

/// Both absolute paths name one entry on disk (the same file system entry — case-insensitive
/// storage: "Projekty" opens "projekty"). False when either is missing.
bool sameEntryOnDisk(const QString &a, const QString &b)
{
    std::error_code error;
    const bool same = std::filesystem::equivalent(fsPath(a), fsPath(b), error);
    return !error && same;
}

/// The synced files of `folder` (sorted, `folder` excluded), the ones directly in it first: the new
/// name they point to is the folder's own; a subfolder may have been moved elsewhere on its own.
QStringList filesBelow(const QHash<VaultPath, SyncFileState> &states, const VaultPath &folder)
{
    QStringList files;
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        if (it.key() != folder && isWithin(it.key(), folder) && it.key() != vaultConfigPath())
            files.append(it.key());
    }
    std::sort(files.begin(), files.end(), [](const VaultPath &a, const VaultPath &b) {
        const qsizetype da = depthOf(a);
        const qsizetype db = depthOf(b);
        return da != db ? da < db : a < b;
    });
    return files;
}

/// The evidence of a folder rename suffices: at least half of the folder's parts are proven at
/// the new name, where each file directly in `folder` is one part and each subfolder one (proven
/// when at least half of the files below it are). A big subfolder moved elsewhere on its own
/// thus never takes its parent along: the parent follows its own files.
bool mostlyProven(const VaultPath &folder, const QStringList &files, const QSet<VaultPath> &proven)
{
    int parts = 0;
    int provenParts = 0;
    QHash<QString, std::pair<int, int>> subfolders; // name -> proven files, files
    for (const VaultPath &file : files) {
        const QString rel = file.mid(folder.size() + 1);
        const qsizetype slash = rel.indexOf(u'/');
        if (slash < 0) {
            ++parts;
            if (proven.contains(file))
                ++provenParts;
            continue;
        }
        std::pair<int, int> &counts = subfolders[rel.left(slash)];
        ++counts.second;
        if (proven.contains(file))
            ++counts.first;
    }
    for (const std::pair<int, int> &counts : std::as_const(subfolders)) {
        ++parts;
        if (counts.first > 0 && counts.first * 2 >= counts.second)
            ++provenParts;
    }
    return provenParts > 0 && provenParts * 2 >= parts;
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
    QSet<QString> ids; // oc:fileid of the server's new files
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
            if (!it->fileId.isEmpty())
                ids.insert(it->fileId);
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
        if (!here && !state.remoteId.isEmpty() && ids.remove(state.remoteId)) {
            ++moved; // the server's very file under a new name
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
    const QStringList files = filesBelow(m_states, from);
    if (files.isEmpty())
        return std::nullopt;
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
        // At least half of the old folder (mostlyProven()) is at its place in the new one,
        // unchanged or renamed there by the vault (the rename journal); the other files may have
        // been edited (compared as usual at the new path) or deleted (deleted on the server as
        // usual) since the rename. A file the server no longer has stays out of it: moved, it
        // would be taken for deleted there, while the rename of a file wins over its deletion on
        // the other side (it is uploaded again). When this device had none of its synced files
        // under their names (m_localLookedEmpty), every file found must be proven: an older copy
        // put back after the storage was lost would otherwise overwrite the newer notes on the
        // server as if they had been edited here.
        QSet<VaultPath> unchanged;
        int found = 0;
        for (const VaultPath &file : std::as_const(files)) {
            const VaultPath target = to + file.mid(from.size());
            if (!m_local.contains(target))
                continue;
            if (!m_remote.contains(file))
                return false;
            ++found;
            if (m_renamedHere.value(target) == file || sameContent(target, m_states.value(file)))
                unchanged.insert(file);
        }
        if (m_localLookedEmpty && unchanged.size() != found)
            return false;
        return mostlyProven(from, files, unchanged);
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
                           applyRemoteMove(move, true);
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

QList<SyncRun::FolderMove> SyncRun::findFileMoves()
{
    // A side that lost every file under its name (an older copy put back under new names) moves
    // nothing: its files travel one by one, the deletions wait for the user.
    if (m_states.isEmpty() || m_localLookedEmpty)
        return {};
    const qint64 maxSize = m_engine->maxFileSize();
    const auto goneHere = [this](const VaultPath &path) {
        return path != vaultConfigPath() && m_states.contains(path) && !m_local.contains(path)
               && m_remote.contains(path);
    };
    const auto newHere = [this](const VaultPath &path) {
        return path != vaultConfigPath() && m_local.contains(path) && !m_states.contains(path)
               && !m_remote.contains(path);
    };
    // Nothing on the server at or below the new name, in any letter case, but the file itself (the
    // listing holds every folder: something below a name means the name itself is there).
    QHash<QString, int> remoteNames;
    for (auto it = m_remoteAll.keyBegin(); it != m_remoteAll.keyEnd(); ++it)
        ++remoteNames[it->toLower()];
    const auto free = [&remoteNames](const VaultPath &from, const VaultPath &to) {
        const QString lower = to.toLower();
        return remoteNames.value(lower) - (from.toLower() == lower ? 1 : 0) <= 0;
    };
    QList<FolderMove> moves;
    // Renamed by the vault (the rename journal): whatever was written to the file since (typing
    // on, its links rewritten) is uploaded after the MOVE as an edit.
    QHash<VaultPath, int> origins; // a synced file renamed twice (once as a new file of that name): neither
    for (auto it = m_renamedHere.cbegin(); it != m_renamedHere.cend(); ++it)
        ++origins[it.value()];
    QSet<VaultPath> journaled; // both ends of those moves
    for (auto it = m_renamedHere.cbegin(); it != m_renamedHere.cend(); ++it) {
        if (origins.value(it.value()) != 1 || !goneHere(it.value()) || !newHere(it.key()) || !free(it.value(), it.key()))
            continue;
        moves.append(FolderMove{it.value(), it.key()});
        journaled.insert(it.value());
        journaled.insert(it.key());
    }
    // Else synced files gone here, by their synced content.
    QHash<QString, QList<VaultPath>> goneByHash;
    QSet<qint64> sizes;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        if (!goneHere(it.key()) || journaled.contains(it.key()) || it->size <= 0 || it->size > maxSize)
            continue; // an empty file proves nothing
        goneByHash[it->localHash].append(it.key());
        sizes.insert(it->size);
    }
    // New local files of those sizes, by content.
    QHash<QString, QList<VaultPath>> newByHash;
    if (!goneByHash.isEmpty()) {
        for (auto it = m_local.cbegin(); it != m_local.cend(); ++it) {
            if (!newHere(it.key()) || journaled.contains(it.key()) || !sizes.contains(it->size))
                continue;
            const ReadResult read = readLocal(it.key(), *it, std::nullopt);
            if (read.snap)
                newByHash[read.snap->hash].append(it.key());
        }
    }
    for (auto it = goneByHash.cbegin(); it != goneByHash.cend(); ++it) {
        const QList<VaultPath> arrived = newByHash.value(it.key());
        if (arrived.isEmpty())
            continue;
        const QList<VaultPath> &gone = *it;
        if (gone.size() == 1 && arrived.size() == 1) {
            if (free(gone.first(), arrived.first()))
                moves.append(FolderMove{gone.first(), arrived.first()});
            continue;
        }
        // Look-alikes (copies, notes from one template): only a name that tells them apart.
        const auto named = [](const QList<VaultPath> &list, const QString &name) {
            QList<VaultPath> out;
            for (const VaultPath &path : list) {
                if (paths::basename(path) == name)
                    out.append(path);
            }
            return out;
        };
        for (const VaultPath &from : gone) {
            const QString name = paths::basename(from);
            const QList<VaultPath> same = named(arrived, name);
            if (same.size() == 1 && named(gone, name).size() == 1 && free(from, same.first()))
                moves.append(FolderMove{from, same.first()});
        }
    }
    std::sort(moves.begin(), moves.end(), [](const FolderMove &a, const FolderMove &b) { return a.to < b.to; });
    for (const FolderMove &move : std::as_const(moves))
        qCDebug(lcSync).noquote() << u"%1 file renamed here: %2 -> %3"_s.arg(tag(), move.from, move.to);
    return moves;
}

void SyncRun::moveFiles(std::function<void()> next)
{
    if (m_fileMoves.isEmpty() || m_cancelled) {
        next();
        return;
    }
    // Independent of each other (a new name is never a synced one): in parallel.
    m_pool = std::make_shared<detail::JobPool>(m_engine->concurrency(), m_engine);
    for (const FolderMove &move : std::as_const(m_fileMoves)) {
        m_pool->add([weak = weak_from_this(), move](detail::JobPool::Done done) {
            const std::shared_ptr<SyncRun> self = weak.lock();
            if (!self || self->m_finished || self->m_cancelled || !self->client()) {
                done();
                return;
            }
            self->client()->move(move.from, move.to, {}, self->guarded([self, move, done](const WebDavError &error) {
                if (!error.isError()) {
                    self->applyRemoteMove(move, false);
                    qCInfo(lcSync).noquote() << u"%1 %2 renamed here: moved to %3 on the server"_s.arg(
                            self->tag(), move.from, move.to);
                } else if (error.isFatal()) {
                    if (!self->m_report.remoteError.isError())
                        self->m_report.remoteError = error;
                    if (self->m_pool)
                        self->m_pool->stop();
                } else {
                    qCInfo(lcSync).noquote()
                            << u"%1 %2 renamed here to %3: not moved on the server (%4, HTTP %5); uploaded and "
                               u"deleted instead"_s.arg(self->tag(), move.from, move.to, errorCodeName(error.code),
                                                        QString::number(error.status));
                }
                done();
            }));
        });
    }
    m_pool->start(guarded([this, next = std::move(next)] {
        if (m_report.remoteError.isError()) {
            finish();
            return;
        }
        next();
    }));
}

void SyncRun::rebaseStates(const VaultPath &from, const VaultPath &to, bool isFolder)
{
    const auto rebased = [&from, &to](const VaultPath &path) { return to + path.mid(from.size()); };
    // Sync states and merge bases: the files are compared at their new paths. A file has only its
    // own (many single renames in one run must not walk every state each).
    QStringList statePaths;
    if (!isFolder) {
        if (m_states.contains(from))
            statePaths.append(from);
    } else {
        for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
            if (isWithin(it.key(), from))
                statePaths.append(it.key());
        }
    }
    for (const VaultPath &path : std::as_const(statePaths)) {
        const VaultPath target = rebased(path);
        m_states.insert(target, m_states.take(path));
        const QJsonValue state = store()->get(SyncEngine::stateKey(path));
        const QJsonValue base = store()->get(SyncEngine::baseKey(path));
        // Removed first: a case-only rename may map a key onto itself in another store.
        store()->remove(SyncEngine::stateKey(path));
        store()->remove(SyncEngine::baseKey(path));
        store()->remove(SyncEngine::renamedKey(target)); // the rename is synced
        if (!state.isUndefined())
            store()->set(SyncEngine::stateKey(target), state);
        if (!base.isUndefined())
            store()->set(SyncEngine::baseKey(target), base);
    }
    if (!isFolder)
        return;
    const QSet<VaultPath> folders = m_folderStates;
    for (const VaultPath &folder : folders) {
        if (!isWithin(folder, from))
            continue;
        m_folderStates.remove(folder);
        m_folderStates.insert(rebased(folder));
        store()->remove(SyncEngine::folderKey(folder));
        store()->set(SyncEngine::folderKey(rebased(folder)), QJsonObject{{u"syncedAt"_s, now().toMSecsSinceEpoch()}});
    }
}

void SyncRun::adoptCaseOnlyFolderRenames()
{
    QHash<QString, VaultPath> remoteDirs; // lower-cased -> as the server spells it
    for (auto it = m_remoteAll.cbegin(); it != m_remoteAll.cend(); ++it) {
        if (it->isDir && isSyncablePath(it.key()))
            remoteDirs.insert(it.key().toLower(), it.key());
    }
    if (remoteDirs.isEmpty() || m_states.isEmpty())
        return;
    // Parents first; after a rename the folders below it are looked at under their new names.
    for (bool renamed = true; renamed;) {
        renamed = false;
        const QSet<VaultPath> known = knownFolders();
        QStringList folders(known.cbegin(), known.cend());
        std::sort(folders.begin(), folders.end());
        for (const VaultPath &from : std::as_const(folders)) {
            if (from.isEmpty() || !isSyncablePath(from) || m_remoteAll.contains(from))
                continue;
            const VaultPath to = remoteDirs.value(from.toLower());
            // Only the last name differs in case: a parent's case is the parent's rename.
            if (to.isEmpty() || paths::dirname(to) != paths::dirname(from))
                continue;
            // One folder on disk under both names; on case-sensitive storage "Projekty" is
            // another folder (or none) and the old one is deleted as usual.
            const QString absFrom = vault()->absolutePath(from);
            if (!QFileInfo(absFrom).isDir() || !sameEntryOnDisk(absFrom, vault()->absolutePath(to)))
                continue;
            // The folder as the vault spells it: this device may have renamed it in letter case
            // too since the last run ("projekty" -> "Projekty" here, -> "PROJEKTY" on the server).
            const VaultPath here = vault()->existingCase(from);
            if (paths::dirname(here) != paths::dirname(from))
                continue;
            if (!renameHere(here, from, to, true))
                continue;
            if (here != to) { // else renamed so here already: only the states followed
                m_report.localFolderMoves.append(SyncFolderMove{here, to});
                record(to, SyncAction::MoveLocal);
            }
            qCInfo(lcSync).noquote() << u"%1 folder %2 renamed on the server to %3 (letter case): renamed here too"_s
                                                .arg(tag(), from, to);
            renamed = true;
            break;
        }
    }
}

bool SyncRun::renameHere(const VaultPath &here, const VaultPath &from, const VaultPath &to, bool folder)
{
    if (here != to && !vault()->rename(here, to, ChangeSource::Sync)) {
        // Another program holds a file open (Windows), a read-only folder, …: the files travel one
        // by one instead (downloaded under the new name, the old ones trashed): nothing is lost.
        qCWarning(lcSync).noquote() << u"%1 %2 %3 renamed on the server to %4: not renamed here: %5"_s.arg(
                tag(), folder ? u"folder"_s : u"file"_s, here, to, vault()->errorString());
        return false;
    }
    rebaseStates(from, to, folder);
    if (!folder) {
        if (const auto it = m_local.find(here); it != m_local.end()) {
            FileStat stat = *it;
            m_local.erase(it);
            stat.path = to;
            m_local.insert(to, stat);
        }
        return true;
    }
    QHash<VaultPath, FileStat> local;
    for (auto it = m_local.cbegin(); it != m_local.cend(); ++it) {
        FileStat stat = *it;
        if (isWithin(stat.path, here))
            stat.path = to + stat.path.mid(here.size());
        local.insert(stat.path, stat);
    }
    m_local = local;
    return true;
}

bool SyncRun::sameRemoteFile(const RemoteEntry &entry, const SyncFileState &state)
{
    if (!state.remoteId.isEmpty() && !entry.fileId.isEmpty())
        return entry.fileId == state.remoteId;
    // No id: the ETag a move keeps (never the listing-based version of servers without ETags).
    return !entry.etag.isEmpty() && entry.etag == state.remoteEtag && entry.size == state.size;
}

bool SyncRun::takenHere(const VaultPath &to, const VaultPath &from) const
{
    const QString lower = to.toLower();
    const auto clashes = [&](const VaultPath &path) {
        if (isWithin(path, from))
            return false;
        const QString l = path.toLower();
        return l == lower || (l.startsWith(lower) && l.at(lower.size()) == u'/');
    };
    if (std::any_of(m_local.keyBegin(), m_local.keyEnd(), clashes))
        return true;
    const QList<FileStat> folders = vault()->folders();
    if (std::any_of(folders.cbegin(), folders.cend(), [&](const FileStat &f) { return clashes(f.path); }))
        return true;
    // Names the vault does not list (ignored or hidden ones), unless it is the old entry itself
    // under another spelling (case-insensitive storage).
    const QString absTo = vault()->absolutePath(to);
    return QFileInfo::exists(absTo) && !sameEntryOnDisk(vault()->absolutePath(from), absTo);
}

void SyncRun::adoptRemoteFolderMoves()
{
    if (m_states.isEmpty())
        return;
    QSet<VaultPath> taken;   // new names adopted in this run
    QSet<VaultPath> refused; // the vault could not rename them: as before (download and trash)
    for (bool renamed = true; renamed;) {
        renamed = false;
        // New server files (never synced) by identity; the states move with every rename.
        QHash<QString, VaultPath> byId;
        QHash<QString, QList<VaultPath>> byEtag;
        for (auto it = m_remote.cbegin(); it != m_remote.cend(); ++it) {
            if (m_states.contains(it.key()))
                continue;
            if (!it->fileId.isEmpty())
                byId.insert(it->fileId, it.key());
            if (!it->etag.isEmpty())
                byEtag[it->etag].append(it.key());
        }
        if (byId.isEmpty() && byEtag.isEmpty())
            return;
        const QSet<VaultPath> known = knownFolders();
        // Synced folders still here that the server no longer has.
        const auto vanished = [&](const VaultPath &folder) {
            if (folder.isEmpty() || !known.contains(folder) || !isSyncablePath(folder) || m_remoteAll.contains(folder)
                || refused.contains(folder)) {
                return false;
            }
            const std::optional<FileStat> here = vault()->stat(folder);
            return here && here->isDir;
        };
        QStringList sortedKnown(known.cbegin(), known.cend());
        std::sort(sortedKnown.begin(), sortedKnown.end());
        QStringList queue;
        for (const VaultPath &folder : std::as_const(sortedKnown)) {
            if (vanished(folder) && !vanished(paths::dirname(folder)))
                queue.append(folder);
        }
        while (!queue.isEmpty() && !renamed) {
            const VaultPath from = queue.takeFirst();
            if (const std::optional<VaultPath> to = remoteRenameTarget(from, known, byId, byEtag, taken)) {
                if (!renameHere(from, from, *to, true)) {
                    refused.insert(from);
                    continue;
                }
                taken.insert(*to);
                m_report.localFolderMoves.append(SyncFolderMove{from, *to});
                record(*to, SyncAction::MoveLocal);
                qCInfo(lcSync).noquote()
                        << u"%1 folder %2 renamed on the server to %3: renamed here too"_s.arg(tag(), from, *to);
                renamed = true; // look again: a folder moved out of this one is at its new path now
                continue;
            }
            // Not renamed as a whole: maybe a folder inside it was.
            for (const VaultPath &folder : std::as_const(sortedKnown)) {
                if (paths::dirname(folder) == from && vanished(folder))
                    queue.append(folder);
            }
        }
    }
}

std::optional<VaultPath> SyncRun::remoteRenameTarget(const VaultPath &from, const QSet<VaultPath> &known,
                                                     const QHash<QString, VaultPath> &byId,
                                                     const QHash<QString, QList<VaultPath>> &byEtag,
                                                     const QSet<VaultPath> &taken)
{
    const QStringList files = filesBelow(m_states, from);
    if (files.isEmpty())
        return std::nullopt;
    const auto renamedTo = [&](const VaultPath &to) {
        if (taken.contains(to) || isWithin(to, from) || isWithin(from, to) || !isSyncablePath(to))
            return false;
        // A folder never synced: nothing known at or below it (its files are all new).
        if (std::any_of(known.cbegin(), known.cend(), [&to](const VaultPath &k) { return isWithin(k, to); })
            || std::any_of(m_states.keyBegin(), m_states.keyEnd(),
                           [&to](const VaultPath &k) { return isWithin(k, to); })) {
            return false;
        }
        // Nothing here at or below the new name in any letter case: made here meanwhile, the
        // files travel one by one (and meet there as usual).
        if (takenHere(to, from))
            return false;
        // At least half of the old folder (mostlyProven()) is the server's very files at their
        // places in the new one; the other files may have been edited since (compared as usual at
        // the new path), deleted or moved elsewhere. When the server had none of the synced files
        // under their names (m_remoteLookedEmpty), every file found there must be proven.
        QSet<VaultPath> proven;
        int found = 0;
        for (const VaultPath &file : std::as_const(files)) {
            const auto remote = m_remote.constFind(to + file.mid(from.size()));
            if (remote == m_remote.cend())
                continue;
            ++found;
            if (sameRemoteFile(*remote, m_states.value(file)))
                proven.insert(file);
        }
        if (m_remoteLookedEmpty && proven.size() != found)
            return false;
        return mostlyProven(from, files, proven);
    };
    QSet<VaultPath> tried;
    for (const VaultPath &file : std::as_const(files)) {
        const SyncFileState state = m_states.value(file);
        const QString rel = file.mid(from.size()); // "/sub/name.md"
        QList<VaultPath> candidates;
        if (const auto id = byId.constFind(state.remoteId); !state.remoteId.isEmpty() && id != byId.cend())
            candidates.append(*id);
        else
            candidates = byEtag.value(state.remoteEtag);
        for (const VaultPath &candidate : std::as_const(candidates)) {
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

void SyncRun::adoptRemoteFileMoves()
{
    // A side that lost every file under its name: nothing is renamed on such evidence.
    if (m_states.isEmpty() || m_localLookedEmpty)
        return;
    QHash<QString, VaultPath> byId;
    QHash<QString, QList<VaultPath>> byEtag;
    for (auto it = m_remote.cbegin(); it != m_remote.cend(); ++it) {
        if (m_states.contains(it.key()) || it.key() == vaultConfigPath())
            continue;
        if (!it->fileId.isEmpty())
            byId.insert(it->fileId, it.key());
        if (!it->etag.isEmpty())
            byEtag[it->etag].append(it.key());
    }
    if (byId.isEmpty() && byEtag.isEmpty())
        return;
    // Synced files still here that the server no longer has.
    QStringList gone;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        if (it.key() != vaultConfigPath() && !m_remoteAll.contains(it.key()) && m_local.contains(it.key()))
            gone.append(it.key());
    }
    std::sort(gone.begin(), gone.end());
    // Synced versions by ETag: one that several synced files had (a server whose ETags are mtime
    // and size on coarse storage) tells neither apart.
    QHash<QString, int> syncedEtags;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        if (!it->remoteEtag.isEmpty())
            ++syncedEtags[it->remoteEtag];
    }
    // What this device has, lower-cased: its files, their folders and its folders (takenHere() for
    // many files at once).
    QSet<QString> here;
    const auto addHere = [&here](const VaultPath &path) {
        here.insert(path.toLower());
        QSet<VaultPath> parents;
        addAncestors(path, parents);
        for (const VaultPath &parent : std::as_const(parents))
            here.insert(parent.toLower());
    };
    for (auto it = m_local.keyBegin(); it != m_local.keyEnd(); ++it)
        addHere(*it);
    for (const FileStat &folder : vault()->folders())
        here.insert(folder.path.toLower());
    const auto takenHereAlready = [&](const VaultPath &to, const VaultPath &from) {
        const QString lower = to.toLower();
        if (lower != from.toLower() && here.contains(lower))
            return true;
        const QString absTo = vault()->absolutePath(to);
        return QFileInfo::exists(absTo) && !sameEntryOnDisk(vault()->absolutePath(from), absTo);
    };
    QSet<VaultPath> taken;
    for (const VaultPath &from : std::as_const(gone)) {
        const SyncFileState state = m_states.value(from);
        std::optional<VaultPath> to;
        if (const auto id = byId.constFind(state.remoteId); !state.remoteId.isEmpty() && id != byId.cend()) {
            to = *id;
        } else if (!m_remoteLookedEmpty && syncedEtags.value(state.remoteEtag) == 1) {
            // Without ids: the one new file with this version and size, the one synced file with
            // that version (a server that looked empty may hold an older copy put back: no rename
            // on such evidence).
            QList<VaultPath> same;
            for (const VaultPath &path : byEtag.value(state.remoteEtag)) {
                if (sameRemoteFile(m_remote.value(path), state))
                    same.append(path);
            }
            if (same.size() == 1)
                to = same.first();
        }
        if (!to || taken.contains(*to) || !sameRemoteFile(m_remote.value(*to), state) || takenHereAlready(*to, from))
            continue;
        if (!renameHere(from, from, *to, false))
            continue;
        taken.insert(*to);
        addHere(*to);
        m_report.localFileMoves.append(SyncFolderMove{from, *to});
        record(*to, SyncAction::MoveFileLocal);
        qCInfo(lcSync).noquote() << u"%1 %2 renamed on the server to %3: renamed here too"_s.arg(tag(), from, *to);
    }
}

void SyncRun::applyRemoteMove(const FolderMove &move, bool folder)
{
    rebaseStates(move.from, move.to, folder);
    // The listing as the server has it now (a file: just its own entry, a folder: everything at
    // or below it).
    const auto follow = [&move, folder](QHash<VaultPath, RemoteEntry> &listing) {
        if (!folder) {
            if (const auto it = listing.find(move.from); it != listing.end()) {
                RemoteEntry entry = *it;
                listing.erase(it);
                entry.path = move.to;
                listing.insert(move.to, entry);
            }
            return;
        }
        QHash<VaultPath, RemoteEntry> moved;
        for (auto it = listing.cbegin(); it != listing.cend(); ++it) {
            RemoteEntry entry = *it;
            if (isWithin(entry.path, move.from))
                entry.path = move.to + entry.path.mid(move.from.size());
            moved.insert(entry.path, entry);
        }
        listing = std::move(moved);
    };
    follow(m_remote);
    follow(m_remoteAll);
    QSet<VaultPath> parents;
    addAncestors(move.to, parents);
    for (const VaultPath &parent : std::as_const(parents)) {
        if (!m_remoteAll.contains(parent))
            m_remoteAll.insert(parent, RemoteEntry{parent, true, {}, 0, 0, {}});
    }
    if (folder) {
        m_report.folderMoves.append(SyncFolderMove{move.from, move.to});
        record(move.to, SyncAction::MoveRemote);
    } else {
        m_report.fileMoves.append(SyncFolderMove{move.from, move.to});
        record(move.to, SyncAction::MoveFileRemote);
    }
}

void SyncRun::noteRemoteWritten(const VaultPath &path, bool isDir)
{
    if (const auto it = m_remoteAll.constFind(path); it == m_remoteAll.cend() || it->isDir != isDir)
        m_remoteAll.insert(path, RemoteEntry{path, isDir, {}, 0, 0, {}});
    QSet<VaultPath> parents;
    addAncestors(path, parents);
    for (const VaultPath &parent : std::as_const(parents)) {
        if (!m_remoteAll.contains(parent))
            m_remoteAll.insert(parent, RemoteEntry{parent, true, {}, 0, 0, {}});
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
                       && sameEntryOnDisk(absolute, vault()->absolutePath(e.path));
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
