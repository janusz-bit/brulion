// One SyncEngine run: list both sides, decide per path (planSync), transfer, merge, record.
#include "sync/syncrun_p.h"

#include "drawing/drawingfile.h"
#include "drawing/excalidraw.h"
#include "drawing/merge.h"
#include "log/redact.h"
#include "log/slowscope.h"
#include "sync/conflicts.h"
#include "sync/merge.h"
#include "sync/pool_p.h"
#include "sync/util_p.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/localstore.h"
#include "vault/vault.h"
#include "vault/vaultconfig.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::sync {

using detail::lcSync;

namespace {

/// Tracked files (besides the vault config) before the "one side suddenly empty" guard applies.
constexpr int kEmptySideGuardMin = 2;
/// Attempts to merge when the remote file keeps changing under us (HTTP 412).
constexpr int kMaxMergeAttempts = 3;
/// Per-file error warnings logged per run; the rest is summed up.
constexpr int kMaxLoggedErrors = 10;
/// Slowest requests kept per run.
constexpr int kSlowestKept = 5;

using logging::formatBytes;
using logging::formatMs;

bool isJsonObject(const QString &text)
{
    QString json = text;
    if (json.startsWith(QChar(0xFEFF)))
        json.remove(0, 1);
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    return error.error == QJsonParseError::NoError && doc.isObject();
}

/// A drawing (docs/SYNTAX.md §2 "Drawings"): an SVG with an embedded Excalidraw scene, an Obsidian
/// Excalidraw plugin file (`*.excalidraw.md`) or a `.excalidraw` scene — by its name and start
/// (drawing::looksLikeDrawingFile). Never merged line by line: Brulion's SVG keeps its scene in one
/// payload line both sides change, and a line merge may glue two scenes into broken JSON. (A
/// `.excalidraw` file syncs as a binary file — isSyncTextPath() — so it never gets here; it would
/// merge the same way should it become text.)
bool isDrawingText(const VaultPath &path, const QString &text)
{
    const QByteArray head = QStringView(text).first(std::min<qsizetype>(text.size(), drawing::HeadBytes)).toUtf8();
    return drawing::looksLikeDrawingFile(path, head);
}

/// Remote folders not worth listing (ignored ones; ".brulion" holds the config).
bool skipRemoteDir(const VaultPath &path)
{
    return path != u".brulion"_s && !isSyncablePath(path);
}

/// The mass-deletion rule: deleting more than MassDeletionRatio of a side's `files` and at least
/// MassDeletionMinFiles, or every file of the side (at least kEmptySideGuardMin).
bool tripsMassDeletionGuard(int deletions, int files)
{
    if (deletions <= 0)
        return false;
    if (deletions >= files && deletions >= kEmptySideGuardMin)
        return true;
    return deletions >= SyncEngine::MassDeletionMinFiles && deletions > files * SyncEngine::MassDeletionRatio;
}

/// Synced files of one side, the vault config not counted.
template<typename Map>
int countWithoutConfig(const Map &files)
{
    return static_cast<int>(files.size()) - (files.contains(vaultConfigPath()) ? 1 : 0);
}

} // namespace

SyncRun::Outcome SyncRun::Outcome::fromError(const WebDavError &error)
{
    if (error.isFatal())
        return Outcome{Kind::Fatal, error.message, error, errorCodeName(error.code), error.status};
    return failed(errorCodeName(error.code), error.message, error.status);
}

SyncRun::SyncRun(SyncEngine *engine, SyncRunOptions options, int number)
    : m_engine(engine), m_options(std::move(options))
{
    m_report.runNumber = number;
    m_report.trigger = m_options.trigger;
    m_runTimer.start();
    m_activityTimer.start();
}

QString SyncRun::tag() const
{
    return u"#%1"_s.arg(m_report.runNumber);
}

SyncActivity SyncRun::activity() const
{
    SyncActivity a;
    a.running = !m_finished;
    a.runNumber = m_report.runNumber;
    a.idleMs = m_activityTimer.elapsed();
    if (const WebDavClient *c = client()) {
        a.requestsInFlight = c->requestsInFlight();
        a.waitingFor = c->inFlight(4);
        if (a.requestsInFlight > 0)
            a.idleMs = qMin(a.idleMs, c->idleMs());
    }
    return a;
}

void SyncRun::beginPhase(SyncPhase phase)
{
    endPhase(0);
    SyncPhaseTime time;
    time.phase = phase;
    time.startMs = m_runTimer.elapsed();
    m_phase = time;
    m_phaseBytes = m_report.stats.bytesSent + m_report.stats.bytesReceived;
}

void SyncRun::endPhase(int items)
{
    if (!m_phase)
        return;
    SyncPhaseTime time = *m_phase;
    m_phase.reset();
    time.durationMs = m_runTimer.elapsed() - time.startMs;
    time.items = items;
    time.bytes = m_report.stats.bytesSent + m_report.stats.bytesReceived - m_phaseBytes;
    m_report.phases.append(time);
    if (lcSync().isDebugEnabled()) {
        QString detail = u"%1 item(s)"_s.arg(items);
        if (time.bytes > 0)
            detail += u", "_s + formatBytes(time.bytes);
        qCDebug(lcSync).noquote() << u"%1 %2 %3: %4"_s.arg(tag(), syncPhaseName(time.phase), formatMs(time.durationMs),
                                                           detail);
    }
}

void SyncRun::onExchange(const HttpExchange &exchange)
{
    // Time without any answer while requests were open (best effort: since the last activity).
    m_report.stats.longestStallMs = qMax(m_report.stats.longestStallMs, m_activityTimer.elapsed());
    m_activityTimer.start();
    SyncStats &st = m_report.stats;
    ++st.requests;
    ++st.requestsByMethod[exchange.method];
    st.requestMs += exchange.elapsedMs;
    st.bytesSent += exchange.bytesSent;
    st.bytesReceived += exchange.bytesReceived;
    if (exchange.method == "PROPFIND")
        st.listingBytes += exchange.bytesReceived;
    if (exchange.timedOut)
        ++st.timeouts;
    const bool expected = exchange.status == 404 || exchange.status == 412
                          || (exchange.method == "MKCOL" && (exchange.status == 405 || exchange.status == 409))
                          || (exchange.method == "MOVE" && (exchange.status == 405 || exchange.status == 501));
    if (exchange.status == 0 || (exchange.status >= 400 && !expected))
        ++st.failedRequests;
    if (!exchange.protocol.isEmpty())
        st.protocol = exchange.protocol;
    const SyncRequestTime request{exchange.method, client() ? client()->relativePath(exchange.url) : QString(),
                                  exchange.status, exchange.elapsedMs, exchange.bytesSent + exchange.bytesReceived};
    const auto at = std::find_if(st.slowest.begin(), st.slowest.end(),
                                 [&](const SyncRequestTime &r) { return r.ms < request.ms; });
    if (at != st.slowest.end() || st.slowest.size() < kSlowestKept) {
        st.slowest.insert(at, request);
        if (st.slowest.size() > kSlowestKept)
            st.slowest.removeLast();
    }
}

void SyncRun::notePreconditionRetry(const VaultPath &path, int attempt)
{
    ++m_report.stats.preconditionRetries;
    if (attempt >= kMaxMergeAttempts)
        return; // reconcile() gives up (remote-busy)
    qCInfo(lcSync).noquote() << u"%1 %2 changed on the server meanwhile: merging again (attempt %3 of %4)"_s.arg(
            tag(), path, QString::number(attempt + 1), QString::number(kMaxMergeAttempts));
}

void SyncRun::progress(SyncProgress::Phase phase, int done, int total, const VaultPath &path)
{
    SyncProgress p;
    p.phase = phase;
    p.done = done;
    p.total = total;
    p.path = path;
    p.active = m_active;
    p.remoteEntries = m_remoteEntries;
    p.bytesDone = m_report.stats.bytesSent + m_report.stats.bytesReceived;
    p.changes = m_report.changeCount();
    p.errors = int(m_report.errors.size());
    p.elapsedMs = m_runTimer.elapsed();
    report(p);
}

QString SyncRun::summary() const
{
    const SyncReport &r = m_report;
    const QString trigger = syncTriggerName(r.trigger);
    QString head;
    if (r.cancelled) {
        head = u"%1 cancelled after %2 (%3); %4 change(s) were synced"_s.arg(tag(), formatMs(r.durationMs), trigger)
                       .arg(r.changeCount());
    } else if (r.syncError.isError()) {
        head = u"%1 refused after %2 (%3): %4 — %5"_s.arg(tag(), formatMs(r.durationMs), trigger,
                                                          errorCodeName(r.syncError.code), r.syncError.message);
    } else if (r.remoteError.isError()) {
        head = u"%1 failed after %2 (%3): %4 — %5"_s.arg(tag(), formatMs(r.durationMs), trigger,
                                                         errorCodeName(r.remoteError.code), r.remoteError.message);
    } else {
        head = u"%1 ok in %2 (%3)"_s.arg(tag(), formatMs(r.durationMs), trigger);
    }
    QString counts;
    const bool nothing =
            r.changeCount() == 0 && r.errors.isEmpty() && r.pendingDeletions.isEmpty() && r.skipped.isEmpty();
    if (nothing && (r.failed() || r.cancelled)) {
        // A run that stopped early changed nothing: the head says why.
    } else if (nothing) {
        counts = u"nothing to change"_s;
    } else {
        counts = u"up %1, down %2, merged %3, deleted %4/%5, conflicts %6, errors %7"_s.arg(r.uploaded)
                         .arg(r.downloaded)
                         .arg(r.merged)
                         .arg(r.deletedLocal)
                         .arg(r.deletedRemote)
                         .arg(r.conflicts.size())
                         .arg(r.errors.size());
        if (r.foldersCreated + r.foldersDeleted + r.foldersMoved + r.foldersMovedHere > 0)
            counts += u", folders: %1 created, %2 deleted, %3 moved, %4 moved here"_s.arg(r.foldersCreated)
                              .arg(r.foldersDeleted)
                              .arg(r.foldersMoved)
                              .arg(r.foldersMovedHere);
        if (r.filesMoved + r.filesMovedHere > 0)
            counts += u", files moved: %1 on the server, %2 here"_s.arg(r.filesMoved).arg(r.filesMovedHere);
        if (!r.skipped.isEmpty())
            counts += u", skipped %1"_s.arg(r.skipped.size());
        if (r.needsConfirmation)
            counts += u", %1 deletion(s) wait for confirmation"_s.arg(r.pendingDeletions.size());
    }
    QStringList phases;
    for (const SyncPhaseTime &p : r.phases) {
        switch (p.phase) {
        case SyncPhase::Connect:
            if (p.durationMs >= 100)
                phases.append(u"connect "_s + formatMs(p.durationMs));
            break;
        case SyncPhase::ListRemote:
            if (r.stats.remoteFolders == 0) { // the listing did not complete
                phases.append(u"remote "_s + formatMs(p.durationMs));
                break;
            }
            phases.append(u"remote %1 (%2 folders, %3 files, %4)"_s.arg(formatMs(p.durationMs))
                                  .arg(r.stats.remoteFolders)
                                  .arg(r.stats.remoteFiles)
                                  .arg(formatBytes(r.stats.listingBytes)));
            break;
        case SyncPhase::ListLocal:
            phases.append(u"local %1 (%2 files)"_s.arg(formatMs(p.durationMs)).arg(p.items));
            break;
        case SyncPhase::Compare: phases.append(u"compare "_s + formatMs(p.durationMs)); break;
        case SyncPhase::Transfer:
            phases.append(
                    u"transfer %1 (%2 paths, %3)"_s.arg(formatMs(p.durationMs)).arg(p.items).arg(formatBytes(p.bytes)));
            break;
        case SyncPhase::SaveState: phases.append(u"save "_s + formatMs(p.durationMs)); break;
        }
    }
    QString line = counts.isEmpty() ? head : head + u": "_s + counts;
    if (!phases.isEmpty())
        line += u" | "_s + phases.join(u" · "_s);
    const SyncStats &st = r.stats;
    if (st.requests > 0) {
        QStringList methods;
        for (auto it = st.requestsByMethod.cbegin(); it != st.requestsByMethod.cend(); ++it)
            methods.append(u"%1 %2"_s.arg(QString::fromLatin1(it.key())).arg(it.value()));
        QString requests = u" | %1 requests (%2), avg %3"_s.arg(st.requests)
                                   .arg(methods.join(u", "_s), formatMs(st.requestMs / st.requests));
        if (st.failedRequests > 0)
            requests += u", %1 failed"_s.arg(st.failedRequests);
        if (st.timeouts > 0)
            requests += u", %1 timed out"_s.arg(st.timeouts);
        if (st.preconditionRetries > 0)
            requests += u", %1 merged again (412)"_s.arg(st.preconditionRetries);
        if (!st.slowest.isEmpty()) {
            const SyncRequestTime &slow = st.slowest.first();
            requests += u", slowest %1 %2 %3"_s.arg(QString::fromLatin1(slow.method),
                                                    slow.path.isEmpty() ? u"(base)"_s : slow.path, formatMs(slow.ms));
        }
        if (!st.protocol.isEmpty())
            requests += u", "_s + QString::fromLatin1(st.protocol);
        line += requests;
    }
    return line;
}

void SyncRun::dependencyLost()
{
    if (m_finished || !m_engine)
        return;
    m_report.syncError = SyncError{SyncErrorCode::NotReady, u"The vault, WebDAV client or store went away during sync"_s};
    finish();
}

Vault *SyncRun::vault() const
{
    return m_engine ? m_engine->vault() : nullptr;
}

WebDavClient *SyncRun::client() const
{
    return m_engine ? m_engine->client() : nullptr;
}

LocalStore *SyncRun::store() const
{
    return m_engine ? m_engine->store() : nullptr;
}

QDateTime SyncRun::now() const
{
    return m_engine ? m_engine->now() : QDateTime::currentDateTime();
}

bool SyncRun::abandoned() const
{
    return !m_engine;
}

void SyncRun::report(const SyncProgress &progress)
{
    m_activityTimer.start();
    if (m_options.onProgress)
        m_options.onProgress(progress);
    if (m_engine)
        Q_EMIT m_engine->progress(progress);
}

// ---------------------------------------------------------------------------------------------
// Phases
// ---------------------------------------------------------------------------------------------

void SyncRun::start()
{
    m_report.started = now();
    qCInfo(lcSync).noquote() << u"%1 started (%2)"_s.arg(tag(), syncTriggerName(m_report.trigger));
    if (!vault() || !vault()->isOpen() || !client() || !store()) {
        m_report.syncError = SyncError{SyncErrorCode::NotReady, u"Sync needs an open vault, a WebDAV client and a store"_s};
        if (m_engine)
            detail::post(m_engine, [self = shared_from_this()] { self->finish(); });
        return;
    }
    if (!vault()->isStorageAvailable()) {
        // An unmounted card or share: the vault's view of the folder cannot be trusted.
        m_report.syncError =
                SyncError{SyncErrorCode::VaultUnavailable,
                          u"The vault folder is missing or on another storage: %1"_s.arg(vault()->rootPath())};
        detail::post(m_engine, [self = shared_from_this()] { self->finish(); });
        return;
    }
    m_exchangeConnection = QObject::connect(client(), &WebDavClient::exchanged, m_engine,
                                            [weak = weak_from_this()](const HttpExchange &exchange) {
                                                if (const std::shared_ptr<SyncRun> self = weak.lock())
                                                    self->onExchange(exchange);
                                            });
    beginPhase(SyncPhase::Connect);
    progress(SyncProgress::Phase::Connecting);
    checkRemoteIdentity();
    beginPhase(SyncPhase::ListRemote);
    progress(SyncProgress::Phase::Listing, 0, 1);
    listRemote();
}

void SyncRun::cancel()
{
    m_cancelled = true;
    if (m_pool)
        m_pool->stop();
}

void SyncRun::abandon()
{
    m_finished = true;
    m_engine = nullptr;
    if (m_pool) {
        m_pool->stop();
        m_pool.reset(); // breaks the run -> pool -> idle callback -> run cycle
    }
}

void SyncRun::checkRemoteIdentity()
{
    // Reset the bookkeeping when the account / folder changed since the last sync.
    const QJsonValue meta = store()->get(SyncEngine::metaKey());
    const QString identity = client()->identity();
    if (meta.toObject().value(u"remote"_s).toString() == identity && meta.isObject())
        return;
    if (!meta.isUndefined()) {
        qCInfo(lcSync).noquote()
                << u"%1 account or folder changed: sync bookkeeping reset (first-sync rules apply)"_s.arg(tag());
        m_engine->resetState();
    }
    store()->set(SyncEngine::metaKey(), QJsonObject{{u"remote"_s, identity}});
}

void SyncRun::listRemote()
{
    ListOptions options;
    options.skipDir = skipRemoteDir;
    options.cancelled = [weak = weak_from_this()] {
        const std::shared_ptr<SyncRun> self = weak.lock();
        return !self || self->m_cancelled || self->abandoned();
    };
    options.onProgress = [weak = weak_from_this()](int done, int known, int entries) {
        const std::shared_ptr<SyncRun> self = weak.lock();
        if (!self || self->m_finished || self->abandoned())
            return;
        self->m_remoteEntries = entries;
        self->progress(SyncProgress::Phase::Listing, done, known);
    };
    client()->list(options, guarded([this](const ListResult &result) {
        if (m_cancelled || result.error.is(WebDavErrorCode::Cancelled)) {
            m_cancelled = true;
            finish();
            return;
        }
        if (result.error.is(WebDavErrorCode::NotFound)) {
            // First sync into a folder that does not exist yet.
            qCInfo(lcSync).noquote() << u"%1 server folder missing: creating it"_s.arg(tag());
            client()->ensureBase(guarded([this](const WebDavError &error) {
                if (error.isError()) {
                    m_report.remoteError = error;
                    finish();
                    return;
                }
                onListed({});
            }));
            return;
        }
        if (result.error.isError()) {
            m_report.remoteError = result.error;
            finish();
            return;
        }
        onListed(result.entries);
    }));
}

void SyncRun::onListed(const QList<RemoteEntry> &entries)
{
    int folders = 1;
    for (const RemoteEntry &entry : entries) {
        if (entry.isDir)
            ++folders;
        m_remoteAll.insert(entry.path, entry);
        if (!entry.isDir && isSyncablePath(entry.path))
            m_remote.insert(entry.path, entry);
    }
    m_report.stats.remoteFolders = folders;
    m_report.stats.remoteFiles = int(m_remote.size());
    endPhase(folders);
    beginPhase(SyncPhase::ListLocal);
    progress(SyncProgress::Phase::ListingLocal);
    listLocal();
    m_report.stats.localFiles = int(m_local.size());
    endPhase(int(m_local.size()));
    beginPhase(SyncPhase::Compare);
    progress(SyncProgress::Phase::Comparing);
    loadStates();
    m_report.stats.trackedFiles = int(m_states.size());
    if (m_cancelled) {
        finish();
        return;
    }
    adoptCaseOnlyFolderRenames();
    // The guard first: a side that looked empty makes the folder renames stricter.
    if (!m_options.allowEmptySide && !guardEmptySides()) {
        finish();
        return;
    }
    // Renames on the server first (folders, then files): a local rename inside a folder the
    // server renamed is then recognised at its new path.
    adoptRemoteFolderMoves();
    adoptRemoteFileMoves();
    m_moves = findFolderMoves();
    endPhase(int(m_states.size()));
    startTransfer();
}

void SyncRun::listLocal()
{
    // A watching vault watches folders (and the files open in editors), so its listing misses
    // what other programs rewrote in place since its last rescan. Unchanged binary files are
    // judged by that listing alone (size and mtime): rescan first, or a download would replace
    // a picture edited in place without a conflict copy.
    if (vault()->isWatching())
        vault()->rescan();
    for (const FileStat &stat : vault()->files()) {
        if (!stat.isDir && isSyncablePath(stat.path))
            m_local.insert(stat.path, stat);
    }
    // files() does not list the vault config.
    if (const std::optional<FileStat> config = vault()->stat(vaultConfigPath()); config && !config->isDir)
        m_local.insert(config->path, *config);
}

void SyncRun::loadStates()
{
    const QString prefix = SyncEngine::stateKey(QString());
    const QJsonObject values = store()->values(prefix);
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const VaultPath path = it.key().mid(prefix.size());
        const std::optional<SyncFileState> state = SyncFileState::fromJson(it.value().toObject());
        if (state && isSyncablePath(path))
            m_states.insert(path, *state);
    }
    const QString folderPrefix = SyncEngine::folderKey(QString());
    for (const QString &key : store()->keys(folderPrefix)) {
        const VaultPath path = key.mid(folderPrefix.size());
        if (isSyncablePath(path))
            m_folderStates.insert(path);
    }
    // The rename journal; an entry whose rename was synced (the new path has a state) or whose
    // old path was forgotten meanwhile is done.
    const QString renamedPrefix = SyncEngine::renamedKey(QString());
    const QJsonObject renamed = store()->values(renamedPrefix);
    for (auto it = renamed.constBegin(); it != renamed.constEnd(); ++it) {
        const VaultPath to = it.key().mid(renamedPrefix.size());
        const VaultPath from = it.value().toObject().value(u"from"_s).toString();
        if (from.isEmpty() || from == to || m_states.contains(to) || !m_states.contains(from))
            store()->remove(it.key());
        else
            m_renamedHere.insert(to, from);
    }
    backfillRemoteIds();
}

void SyncRun::backfillRemoteIds()
{
    // Each state once: the first run after an upgrade, or the run after an upload (whose answer
    // carries no id). A path changed since the last sync is saved later in this run anyway.
    for (auto it = m_states.begin(); it != m_states.end(); ++it) {
        const auto remote = m_remote.constFind(it.key());
        if (remote == m_remote.cend() || remote->fileId.isEmpty() || remote->fileId == it->remoteId
            || remoteVersion(*remote) != it->remoteEtag) {
            continue;
        }
        it->remoteId = remote->fileId;
        store()->set(SyncEngine::stateKey(it.key()), it->toJson());
    }
}

bool SyncRun::guardEmptySides()
{
    // Refuse to mirror a side that suddenly lost every file synced before (wrong folder, wiped
    // server, vault storage lost): that would delete everything on the other side. Files created
    // since (today's journal, a default config) do not count — unless they hold the content of
    // most of the missing files: then they were moved (a renamed folder holding every note).
    int tracked = 0;
    QList<VaultPath> onlyRemote; // synced before, missing here, still on the server
    QList<VaultPath> onlyLocal;  // synced before, missing on the server, still here
    bool localHas = false;
    bool remoteHas = false;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        if (it.key() == vaultConfigPath())
            continue;
        ++tracked;
        const bool hasLocal = m_local.contains(it.key());
        const bool hasRemote = m_remote.contains(it.key());
        localHas = localHas || hasLocal;
        remoteHas = remoteHas || hasRemote;
        if (hasRemote && !hasLocal)
            onlyRemote.append(it.key());
        else if (hasLocal && !hasRemote)
            onlyLocal.append(it.key());
    }
    if (tracked < kEmptySideGuardMin)
        return true;
    if (!localHas && remoteHas) {
        QList<SyncFileState> wanted;
        for (const VaultPath &path : std::as_const(onlyRemote))
            wanted.append(m_states.value(path));
        QHash<QString, int> here = newLocalHashes(wanted);
        const int moved = countMoved(onlyRemote, true, here);
        if (moved * 2 > onlyRemote.size()) {
            qCInfo(lcSync).noquote() << u"%1 none of the %2 files synced before exist here under their names, but %3 "
                                        u"of them under new ones: moved, not lost"_s.arg(tag())
                                                .arg(tracked)
                                                .arg(moved);
            m_localLookedEmpty = true;
            return true;
        }
        m_report.syncError = SyncError{
            SyncErrorCode::LocalEmpty,
            u"None of the %1 files synced before exist locally; refusing to delete them on the server"_s.arg(tracked)};
        return false;
    }
    if (!remoteHas && localHas) {
        QHash<QString, int> none;
        const int moved = countMoved(onlyLocal, false, none);
        if (moved * 2 > onlyLocal.size()) {
            m_remoteLookedEmpty = true;
            return true;
        }
        // New files of the same sizes on the server may be the moved ones: decided by their
        // content after the first pass (which downloads new files and deletes nothing).
        QHash<qint64, int> sizes;
        for (auto it = m_remote.cbegin(); it != m_remote.cend(); ++it) {
            if (!m_states.contains(it.key()))
                ++sizes[it->size];
        }
        int plausible = 0;
        for (const VaultPath &path : std::as_const(onlyLocal)) {
            int &left = sizes[m_states.value(path).size];
            if (left > 0) {
                --left;
                ++plausible;
            }
        }
        if (plausible * 2 > onlyLocal.size()) {
            m_recheckRemoteEmpty = true;
            m_remoteLookedEmpty = true;
            return true;
        }
        m_report.syncError = SyncError{
            SyncErrorCode::RemoteEmpty,
            u"None of the %1 files synced before exist on the server; refusing to delete them locally"_s.arg(tracked)};
        return false;
    }
    return true;
}

bool SyncRun::recheckRemoteEmpty()
{
    QList<VaultPath> onlyLocal;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        if (it.key() != vaultConfigPath() && m_local.contains(it.key()) && !m_remote.contains(it.key()))
            onlyLocal.append(it.key());
    }
    QHash<QString, int> arrived = m_downloadedNew;
    const int moved = countMoved(onlyLocal, false, arrived);
    // None left: the files were renamed here as the server had renamed them (adoptRemote…Moves).
    if (onlyLocal.isEmpty() || moved * 2 > onlyLocal.size()) {
        qCInfo(lcSync).noquote() << u"%1 none of the files synced before exist on the server under their names, but "
                                    u"%2 of %3 under new ones: moved, not lost"_s.arg(tag())
                                            .arg(moved)
                                            .arg(onlyLocal.size());
        return true;
    }
    m_report.syncError = SyncError{SyncErrorCode::RemoteEmpty,
                                   u"None of the %1 files synced before exist on the server; refusing to delete them "
                                   u"locally"_s.arg(onlyLocal.size())};
    return false;
}

void SyncRun::guardMassDeletion()
{
    if (m_options.confirmDeletions || m_options.allowEmptySide)
        return;
    // Count what planSync() will decide for the paths synced before that are gone on one side:
    // deleted there unless the other side changed (then the file is restored instead). A file
    // whose content the first pass transferred to a new path (or that exists in another letter
    // case) was moved or renamed: not a deletion.
    const qint64 maxSize = m_engine->maxFileSize();
    QList<VaultPath> localDeletions;  // gone remotely, unchanged locally -> DeleteLocal
    QList<VaultPath> remoteDeletions; // gone locally, unchanged remotely -> DeleteRemote
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        const VaultPath &path = it.key();
        if (path == vaultConfigPath())
            continue;
        const auto local = m_local.constFind(path);
        const auto remote = m_remote.constFind(path);
        const bool hasLocal = local != m_local.cend();
        const bool hasRemote = remote != m_remote.cend();
        if (hasLocal == hasRemote)
            continue;
        if (hasRemote) {
            if (remote->size <= maxSize && remoteVersion(*remote) == it->remoteEtag)
                remoteDeletions.append(path);
            continue;
        }
        if (local->size > maxSize)
            continue; // skipped by syncPath()
        const ReadResult read = readLocal(path, *local, *it);
        if (read.snap && read.snap->hash == it->localHash)
            localDeletions.append(path);
    }
    QHash<QString, int> uploaded = m_uploadedNew;
    QHash<QString, int> downloaded = m_downloadedNew;
    const int movedHere = countMoved(remoteDeletions, true, uploaded);
    const int movedThere = countMoved(localDeletions, false, downloaded);
    const int local = int(localDeletions.size()) - movedThere;
    const int remote = int(remoteDeletions.size()) - movedHere;
    // A side that looked empty passed guardEmptySides() thanks to moved content only: whatever is
    // not proven moved there (a note missing, or found only in an older version) waits for the
    // user, however few.
    const bool unprovenOnEmptySide = (m_localLookedEmpty && remote > 0) || (m_remoteLookedEmpty && local > 0);
    m_holdDeletions = tripsMassDeletionGuard(local, countWithoutConfig(m_local))
                      || tripsMassDeletionGuard(remote, countWithoutConfig(m_remote)) || unprovenOnEmptySide;
    if (unprovenOnEmptySide) {
        qCInfo(lcSync).noquote() << u"%1 %2 had none of the files synced before under their names: %3 deletion(s) "
                                    u"not proven to be moves held back for confirmation"_s.arg(tag())
                                            .arg(m_localLookedEmpty ? u"this device"_s : u"the server"_s)
                                            .arg(m_localLookedEmpty ? remote : local);
    } else if (m_holdDeletions) {
        qCInfo(lcSync).noquote()
                << u"%1 mass deletion held back for confirmation: %2 local and %3 remote file(s) would be deleted"_s
                           .arg(tag())
                           .arg(local)
                           .arg(remote);
    } else if (movedHere + movedThere > 0) {
        qCInfo(lcSync).noquote() << u"%1 %2 file(s) moved or renamed (here %3, on the server %4): not counted as "
                                    u"deletions"_s.arg(tag())
                                            .arg(movedHere + movedThere)
                                            .arg(movedHere)
                                            .arg(movedThere);
    }
}

void SyncRun::startTransfer()
{
    beginPhase(SyncPhase::Transfer);
    // Folder renames first: what the server moved needs no deletion and no upload. Then the file
    // renames, recognised once the folder moves are applied (a file moved out of a renamed folder
    // is then found at the folder's new path on the server).
    moveFolders(0, [this] {
        if (!m_cancelled)
            m_fileMoves = findFileMoves();
        moveFiles([this] { processAll(); });
    });
}

void SyncRun::processAll()
{
    if (m_cancelled) {
        finish();
        return;
    }
    QSet<VaultPath> all;
    for (auto it = m_local.cbegin(); it != m_local.cend(); ++it)
        all.insert(it.key());
    for (auto it = m_remote.cbegin(); it != m_remote.cend(); ++it)
        all.insert(it.key());
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it)
        all.insert(it.key());
    QStringList paths(all.cbegin(), all.cend());
    std::sort(paths.begin(), paths.end());
    m_total = static_cast<int>(paths.size());
    m_report.stats.pathsChecked = m_total;
    progress(SyncProgress::Phase::Syncing, 0, m_total);

    // Names differing only in letter case are one file on case-insensitive storage: handled one
    // after another (see groupByCase), never in parallel. Groups with a deletion wait for the
    // mass-deletion guard, which counts what the others transferred.
    const QList<QStringList> groups = groupByCase(
        paths, [this](const VaultPath &p) { return m_local.contains(p); },
        [this](const VaultPath &p) { return m_remote.contains(p); });
    QList<QStringList> first;
    for (const QStringList &group : groups) {
        const bool deletes = std::any_of(group.cbegin(), group.cend(),
                                         [this](const VaultPath &p) { return isDeletionCandidate(p); });
        (deletes ? m_laterGroups : first).append(group);
    }
    runPass(first, [this] { afterFirstPass(); });
}

void SyncRun::runPass(const QList<QStringList> &groups, std::function<void()> next)
{
    m_pool = std::make_shared<detail::JobPool>(m_engine->concurrency(), m_engine);
    for (const QStringList &group : groups) {
        m_pool->add([weak = weak_from_this(), group](detail::JobPool::Done done) {
            if (const std::shared_ptr<SyncRun> self = weak.lock())
                self->runGroup(group, 0, std::move(done));
            else
                done();
        });
    }
    m_pool->start(guarded([next = std::move(next)] { next(); }));
}

void SyncRun::afterFirstPass()
{
    if (m_cancelled || m_report.remoteError.isError()) {
        finish();
        return;
    }
    if (m_recheckRemoteEmpty && !recheckRemoteEmpty()) {
        finish();
        return;
    }
    guardMassDeletion();
    runPass(std::exchange(m_laterGroups, {}), [this] { afterSecondPass(); });
}

void SyncRun::afterSecondPass()
{
    if (m_cancelled || m_report.remoteError.isError()) {
        finish();
        return;
    }
    syncFolders([this] { finish(); });
}

void SyncRun::runGroup(const QStringList &group, qsizetype index, std::function<void()> groupDone)
{
    if (index >= group.size() || m_cancelled || m_finished) {
        groupDone();
        return;
    }
    const VaultPath path = group.at(index);
    m_active.append(path);
    syncPath(path, guarded([this, group, index, groupDone, path](const Outcome &outcome) {
                 m_active.removeOne(path);
                 switch (outcome.kind) {
                 case Outcome::Kind::Done:
                 case Outcome::Kind::Skipped: break; // skipped: changed locally meanwhile, next sync
                 case Outcome::Kind::Failed: {
                     const SyncFileError error{path, logging::redact(outcome.message), outcome.code, outcome.status};
                     m_report.errors.append(error);
                     if (++m_loggedErrors <= kMaxLoggedErrors) {
                         // A server answer is told by its code and status (the URL is in the record); a
                         // local failure by its message.
                         const QString detail =
                                 error.status > 0 ? u" (HTTP %1)"_s.arg(error.status) : u" — "_s + error.message;
                         qCWarning(lcSync).noquote()
                                 << u"%1 %2 not synced: %3%4"_s.arg(tag(), path, error.code, detail);
                     }
                     break;
                 }
                 case Outcome::Kind::Fatal:
                     if (!m_report.remoteError.isError())
                         m_report.remoteError = outcome.error;
                     if (m_pool)
                         m_pool->stop();
                     break;
                 }
                 ++m_done;
                 progress(SyncProgress::Phase::Syncing, m_done, m_total, path);
                 if (outcome.kind == Outcome::Kind::Fatal) {
                     groupDone();
                     return;
                 }
                 runGroup(group, index + 1, groupDone);
             }));
}

void SyncRun::finish()
{
    if (m_finished)
        return;
    const std::shared_ptr<SyncRun> keepAlive = shared_from_this();
    m_finished = true;
    if (m_pool) {
        m_pool->stop();
        m_pool.reset();
    }
    m_report.cancelled = m_cancelled;
    std::sort(m_report.pendingDeletions.begin(), m_report.pendingDeletions.end(),
              [](const SyncChange &a, const SyncChange &b) { return a.path < b.path; });
    m_report.needsConfirmation = !m_report.pendingDeletions.isEmpty();
    if (m_phase && m_phase->phase == SyncPhase::Transfer)
        endPhase(m_done);
    else
        endPhase(0);
    if (store()) {
        beginPhase(SyncPhase::SaveState);
        progress(SyncProgress::Phase::Saving, m_done, m_total);
        if (!store()->flush())
            qCWarning(lcSync).noquote() << tag() << "could not save the sync state:" << store()->errorString();
        endPhase(0);
    }
    QObject::disconnect(m_exchangeConnection);
    m_report.finished = now();
    m_report.durationMs = m_runTimer.elapsed();
    if (!m_report.failed() && !m_cancelled)
        progress(SyncProgress::Phase::Done, m_done, m_total);
    if (m_report.remoteError.isError())
        m_report.remoteError.message = logging::redact(m_report.remoteError.message);
    if (m_report.syncError.isError())
        m_report.syncError.message = logging::redact(m_report.syncError.message);
    if (m_loggedErrors > kMaxLoggedErrors)
        qCWarning(lcSync).noquote() << u"%1 … and %2 more file(s) not synced"_s.arg(tag()).arg(m_loggedErrors
                                                                                               - kMaxLoggedErrors);
    if (m_report.failed() && !m_cancelled)
        qCWarning(lcSync).noquote() << summary();
    else
        qCInfo(lcSync).noquote() << summary();
    if (m_engine)
        m_engine->runFinished(m_report);
}

// ---------------------------------------------------------------------------------------------
// One path
// ---------------------------------------------------------------------------------------------

void SyncRun::syncPath(const VaultPath &path, Done done)
{
    PathCtx ctx;
    ctx.path = path;
    std::optional<FileStat> localStat;
    if (const auto it = m_local.constFind(path); it != m_local.cend())
        localStat = *it;
    if (const auto it = m_remote.constFind(path); it != m_remote.cend())
        ctx.remote = *it;
    if (const auto it = m_states.constFind(path); it != m_states.cend())
        ctx.state = *it;

    const qint64 maxSize = m_engine->maxFileSize();
    if ((localStat && localStat->size > maxSize) || (ctx.remote && ctx.remote->size > maxSize)) {
        m_report.skipped.append(path);
        const qint64 size = qMax(localStat ? localStat->size : 0, ctx.remote ? ctx.remote->size : 0);
        qCInfo(lcSync).noquote() << u"%1 skipped %2: %3 is above the %4 limit"_s.arg(tag(), path, formatBytes(size),
                                                                                     formatBytes(maxSize));
        done(Outcome::done());
        return;
    }
    if (localStat) {
        const ReadResult read = readLocal(path, *localStat, ctx.state);
        if (read.vanished) {
            // Deleted or renamed since the listing: handled by the next sync.
            done(Outcome::skipped(u"%1 disappeared locally during sync"_s.arg(path)));
            return;
        }
        if (!read.error.isEmpty()) {
            done(Outcome::failed(u"read-failed"_s, read.error));
            return;
        }
        ctx.snap = read.snap;
    }
    SyncFacts facts;
    facts.local = ctx.snap.has_value();
    facts.remote = ctx.remote.has_value();
    facts.known = ctx.state.has_value();
    facts.localChanged = ctx.snap && (!ctx.state || ctx.snap->hash != ctx.state->localHash);
    facts.remoteChanged = ctx.remote && (!ctx.state || remoteVersion(*ctx.remote) != ctx.state->remoteEtag);
    const SyncPlan plan = planSync(facts);
    if (m_holdDeletions && (plan == SyncPlan::DeleteRemote || plan == SyncPlan::DeleteLocal)) {
        // Held back by the mass-deletion guard; the sync state stays, so the next run plans it again.
        m_report.pendingDeletions.append(
                SyncChange{path, plan == SyncPlan::DeleteRemote ? SyncAction::DeleteRemote : SyncAction::DeleteLocal});
        done(Outcome::done());
        return;
    }
    switch (plan) {
    case SyncPlan::Noop: refreshState(ctx, std::move(done)); return;
    case SyncPlan::Upload: upload(ctx, std::move(done)); return;
    case SyncPlan::Download: download(ctx, std::move(done)); return;
    case SyncPlan::Reconcile: reconcile(ctx, 0, std::move(done)); return;
    case SyncPlan::DeleteRemote: deleteRemote(ctx, std::move(done)); return;
    case SyncPlan::DeleteLocal: deleteLocal(ctx, std::move(done)); return;
    case SyncPlan::Forget:
        forget(path);
        done(Outcome::done());
        return;
    }
}

SyncRun::Content SyncRun::classify(const VaultPath &path, const QByteArray &bytes)
{
    Content content;
    if (isSyncTextPath(path)) {
        content.text = decodeSyncText(bytes);
        if (content.text) {
            content.hash = text::contentHash(*content.text);
            return content;
        }
    }
    content.hash = text::fnv1a64Hex(bytes);
    return content;
}

SyncRun::ReadResult SyncRun::readLocal(const VaultPath &path, const FileStat &stat,
                                       const std::optional<SyncFileState> &state)
{
    ReadResult result;
    if (!isSyncTextPath(path) && state && state->size == stat.size && state->mtime == stat.mtime) {
        // Unchanged binary file (same size and mtime): not read again.
        LocalSnap snap;
        snap.hash = state->localHash;
        snap.stat = stat;
        result.snap = snap;
        return result;
    }
    bool ok = false;
    const QByteArray bytes = vault()->readBinary(path, &ok);
    if (!ok) {
        if (!QFileInfo::exists(vault()->absolutePath(path)))
            result.vanished = true;
        else
            result.error = vault()->errorString();
        return result;
    }
    const Content content = classify(path, bytes);
    LocalSnap snap;
    snap.isText = content.text.has_value();
    snap.text = content.text.value_or(QString());
    snap.hash = content.hash;
    snap.stat = stat;
    snap.bytes = bytes;
    snap.hasBytes = true;
    result.snap = snap;
    return result;
}

std::optional<QByteArray> SyncRun::localBytes(const VaultPath &path, const LocalSnap &snap, QString *error)
{
    if (snap.hasBytes)
        return snap.bytes;
    bool ok = false;
    QByteArray bytes = vault()->readBinary(path, &ok);
    if (!ok) {
        *error = vault()->errorString();
        return std::nullopt;
    }
    return bytes;
}

std::optional<SyncRun::Outcome> SyncRun::checkLocalUnchanged(const VaultPath &path, const std::optional<LocalSnap> &snap)
{
    const std::optional<FileStat> current = vault()->stat(path);
    if (!snap) {
        if (current)
            return Outcome::skipped(u"%1 was created locally during sync"_s.arg(path));
        // The vault knows no file at this path but the disk has one: on case-insensitive storage
        // another note whose name differs only in letter case (or a file another program has
        // just created). Never overwrite it.
        if (QFileInfo::exists(vault()->absolutePath(path))) {
            const QList<FileStat> files = vault()->files();
            const auto other = std::find_if(files.cbegin(), files.cend(), [&](const FileStat &f) {
                return f.path != path && f.path.compare(path, Qt::CaseInsensitive) == 0;
            });
            if (other != files.cend()) {
                return Outcome::failed(
                        u"case-clash"_s,
                        u"%1: a local file whose name differs only in letter case exists (%2); rename one of them"_s
                                .arg(path, other->path));
            }
            return Outcome::failed(u"unseen-local-file"_s,
                                   u"%1: a local file exists that the vault has not seen yet; will retry"_s.arg(path));
        }
        return std::nullopt;
    }
    if (!current || current->isDir)
        return Outcome::skipped(u"%1 was removed locally during sync"_s.arg(path));
    if (snap->isText) {
        bool ok = false;
        const QByteArray bytes = vault()->readBinary(path, &ok);
        if (!ok || classify(path, bytes).hash != snap->hash)
            return Outcome::skipped(u"%1 changed locally during sync"_s.arg(path));
    } else if (current->size != snap->stat.size || current->mtime != snap->stat.mtime) {
        return Outcome::skipped(u"%1 changed locally during sync"_s.arg(path));
    }
    return std::nullopt;
}

void SyncRun::fetchRemote(const VaultPath &path, const std::optional<RemoteEntry> &listed,
                          std::function<void(const RemoteContent &)> done)
{
    GetOptions options;
    if (listed)
        options.size = listed->size;
    client()->get(path, options, guarded([this, path, listed, done](const GetResult &result) {
        RemoteContent content;
        content.error = result.error;
        if (result.error.isError()) {
            done(content);
            return;
        }
        content.data = result.data;
        content.etag = !result.etag.isEmpty() ? result.etag : (listed ? listed->etag : QString());
        // Servers without ETags in listings: keep the listing-based version scheme.
        if (listed && listed->etag.isEmpty())
            content.version = remoteVersion(*listed);
        else if (!result.etag.isEmpty())
            content.version = result.etag;
        else if (listed)
            content.version = remoteVersion(*listed);
        if (!content.version.isEmpty()) {
            done(content);
            return;
        }
        versionAfterPut(path, {}, [content, done](const QString &version) mutable {
            content.version = version;
            done(content);
        });
    }));
}

void SyncRun::versionAfterPut(const VaultPath &path, const QString &etag, std::function<void(const QString &)> done)
{
    // The version after a PUT / GET: from the response, else from a PROPFIND.
    if (!etag.isEmpty()) {
        done(etag);
        return;
    }
    client()->stat(path, guarded([done](const StatResult &result) {
        done(result.entry ? remoteVersion(*result.entry) : QString());
    }));
}

void SyncRun::saveState(const VaultPath &path, const QString &hash, const QString &version,
                        const std::optional<QString> &baseText, const std::optional<FileStat> &stat)
{
    const std::optional<FileStat> st = stat ? stat : vault()->stat(path);
    SyncFileState state;
    state.localHash = hash;
    state.remoteEtag = version;
    state.size = st ? st->size : (baseText ? baseText->toUtf8().size() : 0);
    if (st)
        state.mtime = st->mtime;
    state.syncedAt = now().toMSecsSinceEpoch();
    // The server's file id as listed (kept on overwrite); a new upload learns it next run.
    state.remoteId = m_remoteAll.value(path).fileId;
    store()->set(SyncEngine::stateKey(path), state.toJson());
    store()->remove(SyncEngine::renamedKey(path)); // a rename to it is synced
    const bool keepBase = baseText && isSyncTextPath(path) && baseText->toUtf8().size() <= m_engine->maxBaseSize();
    store()->set(SyncEngine::baseKey(path), keepBase ? QJsonValue(*baseText) : QJsonValue(QJsonValue::Undefined));
}

void SyncRun::forget(const VaultPath &path)
{
    store()->remove(SyncEngine::stateKey(path));
    store()->remove(SyncEngine::baseKey(path));
}

void SyncRun::record(const VaultPath &path, SyncAction action)
{
    m_report.changes.append(SyncChange{path, action});
    switch (action) {
    case SyncAction::Upload: ++m_report.uploaded; break;
    case SyncAction::Download: ++m_report.downloaded; break;
    case SyncAction::Merge: ++m_report.merged; break;
    case SyncAction::Conflict: m_report.conflicts.append(path); break;
    case SyncAction::DeleteLocal: ++m_report.deletedLocal; break;
    case SyncAction::DeleteRemote: ++m_report.deletedRemote; break;
    case SyncAction::CreateFolderLocal:
    case SyncAction::CreateFolderRemote: ++m_report.foldersCreated; break;
    case SyncAction::DeleteFolderLocal:
    case SyncAction::DeleteFolderRemote: ++m_report.foldersDeleted; break;
    case SyncAction::MoveRemote: ++m_report.foldersMoved; break;
    case SyncAction::MoveLocal: ++m_report.foldersMovedHere; break;
    case SyncAction::MoveFileRemote: ++m_report.filesMoved; break;
    case SyncAction::MoveFileLocal: ++m_report.filesMovedHere; break;
    }
}

void SyncRun::refreshState(const PathCtx &ctx, Done done)
{
    // Nothing changed; keep the binary quick check (size + mtime) up to date.
    const LocalSnap &snap = *ctx.snap;
    const SyncFileState &state = *ctx.state;
    if (snap.stat.size != state.size || state.mtime != snap.stat.mtime) {
        SyncFileState next = state;
        next.size = snap.stat.size;
        next.mtime = snap.stat.mtime;
        store()->set(SyncEngine::stateKey(ctx.path), next.toJson());
    }
    done(Outcome::done());
}

void SyncRun::upload(const PathCtx &ctx, Done done)
{
    QString error;
    const std::optional<QByteArray> bytes = localBytes(ctx.path, *ctx.snap, &error);
    if (!bytes) {
        done(Outcome::failed(u"read-failed"_s, error));
        return;
    }
    PutOptions options;
    options.mtime = ctx.snap->stat.mtime;
    if (!ctx.remote)
        options.ifNoneMatch = true;
    else if (!ctx.remote->etag.isEmpty())
        options.ifMatch = ctx.remote->etag;
    client()->put(ctx.path, *bytes, options, guarded([this, ctx, done](const PutResult &result) {
        if (result.error.is(WebDavErrorCode::Precondition)) {
            // Someone changed / created the remote file since we listed it: compare and merge.
            notePreconditionRetry(ctx.path, 1);
            PathCtx fresh = ctx;
            fresh.remote.reset();
            reconcile(fresh, 1, done);
            return;
        }
        if (result.error.isError()) {
            done(Outcome::fromError(result.error));
            return;
        }
        noteRemoteWritten(ctx.path);
        versionAfterPut(ctx.path, result.etag, [this, ctx, done](const QString &version) {
            const LocalSnap &snap = *ctx.snap;
            saveState(ctx.path, snap.hash, version, snap.isText ? std::optional<QString>(snap.text) : std::nullopt,
                      snap.stat);
            record(ctx.path, SyncAction::Upload);
            if (!ctx.state)
                ++m_uploadedNew[snap.hash]; // maybe moved here from a path deleted in this run
            done(Outcome::done());
        });
    }));
}

void SyncRun::download(const PathCtx &ctx, Done done)
{
    fetchRemote(ctx.path, ctx.remote, [this, ctx, done](const RemoteContent &remote) {
        if (remote.error.is(WebDavErrorCode::NotFound)) {
            done(Outcome::done()); // deleted on the server meanwhile: the next sync sees it
            return;
        }
        if (remote.error.isError()) {
            done(Outcome::fromError(remote.error));
            return;
        }
        const Content content = classify(ctx.path, remote.data);
        if (ctx.snap && ctx.snap->hash == content.hash) {
            // Same content (e.g. a new ETag without a content change): just remember it.
            saveState(ctx.path, content.hash, remote.version, content.text, ctx.snap->stat);
            done(Outcome::done());
            return;
        }
        if (const std::optional<Outcome> changed = checkLocalUnchanged(ctx.path, ctx.snap)) {
            done(*changed);
            return;
        }
        // The exact remote bytes (BOM, line endings, legacy encodings stay as they are).
        if (!vault()->writeBinary(ctx.path, remote.data, ChangeSource::Sync)) {
            done(Outcome::failed(u"write-failed"_s, vault()->errorString()));
            return;
        }
        saveState(ctx.path, content.hash, remote.version, content.text, std::nullopt);
        record(ctx.path, SyncAction::Download);
        if (!ctx.state)
            ++m_downloadedNew[content.hash]; // maybe moved there from a path deleted in this run
        done(Outcome::done());
    });
}

void SyncRun::reconcile(const PathCtx &ctx, int attempt, Done done)
{
    if (attempt >= kMaxMergeAttempts) {
        done(Outcome::failed(errorCodeName(SyncErrorCode::RemoteBusy),
                             u"%1 keeps changing on the server; will retry on the next sync"_s.arg(ctx.path)));
        return;
    }
    fetchRemote(ctx.path, ctx.remote, [this, ctx, attempt, done](const RemoteContent &remote) {
        if (remote.error.is(WebDavErrorCode::NotFound)) {
            // Gone on the server meanwhile: upload ours as a new file.
            PathCtx fresh = ctx;
            fresh.remote.reset();
            upload(fresh, done);
            return;
        }
        if (remote.error.isError()) {
            done(Outcome::fromError(remote.error));
            return;
        }
        const Retry retry = [this, ctx, attempt, done] { reconcileAgain(ctx, attempt + 1, done); };
        compareAndMerge(ctx, remote, retry, done);
    });
}

void SyncRun::reconcileAgain(const PathCtx &ctx, int attempt, Done done)
{
    notePreconditionRetry(ctx.path, attempt);
    // The remote file changed again while we merged: start over with fresh local content.
    const std::optional<FileStat> current = vault()->stat(ctx.path);
    if (!current) {
        done(Outcome::skipped(u"%1 was removed locally during sync"_s.arg(ctx.path)));
        return;
    }
    const ReadResult fresh = readLocal(ctx.path, *current, std::nullopt);
    if (fresh.vanished) {
        done(Outcome::skipped(u"%1 disappeared locally during sync"_s.arg(ctx.path)));
        return;
    }
    if (!fresh.error.isEmpty()) {
        done(Outcome::failed(u"read-failed"_s, fresh.error));
        return;
    }
    PathCtx next = ctx;
    next.snap = fresh.snap;
    next.remote.reset();
    reconcile(next, attempt, std::move(done));
}

void SyncRun::compareAndMerge(const PathCtx &ctx, const RemoteContent &remote, const Retry &retry, Done done)
{
    const LocalSnap &snap = *ctx.snap;
    const std::optional<QString> remoteText = snap.isText ? decodeSyncText(remote.data) : std::nullopt;
    if (!snap.isText || !remoteText) {
        // Binary (or not UTF-8): equal bytes or both versions kept.
        if (text::fnv1a64Hex(remote.data) == snap.hash) {
            saveState(ctx.path, snap.hash, remote.version, std::nullopt, snap.stat);
            done(Outcome::done());
            return;
        }
        conflict(ctx, remote, retry, std::move(done));
        return;
    }
    if (*remoteText == snap.text) {
        saveState(ctx.path, snap.hash, remote.version, snap.text, snap.stat);
        done(Outcome::done());
        return;
    }
    const std::optional<QString> merged = mergeText(ctx.path, snap.text, *remoteText, ctx.state);
    if (!merged) {
        conflict(ctx, remote, retry, std::move(done));
        return;
    }
    publishMerge(ctx, remote, *merged, *remoteText, retry, std::move(done));
}

void SyncRun::publishMerge(const PathCtx &ctx, const RemoteContent &remote, const QString &merged,
                           const QString &remoteText, const Retry &retry, Done done)
{
    const LocalSnap &snap = *ctx.snap;
    if (const std::optional<Outcome> changed = checkLocalUnchanged(ctx.path, ctx.snap)) {
        done(*changed);
        return;
    }
    if (merged != snap.text && !vault()->writeText(ctx.path, merged, ChangeSource::Sync)) {
        done(Outcome::failed(u"write-failed"_s, vault()->errorString()));
        return;
    }
    const SyncAction action = merged == remoteText ? SyncAction::Download
        : merged == snap.text                     ? SyncAction::Upload
                                                  : SyncAction::Merge;
    const auto finishMerge = [this, path = ctx.path, merged, action, done](const QString &version) {
        saveState(path, text::contentHash(merged), version, merged, std::nullopt);
        record(path, action);
        done(Outcome::done());
    };
    if (merged == remoteText) {
        finishMerge(remote.version);
        return;
    }
    PutOptions options;
    if (!remote.etag.isEmpty())
        options.ifMatch = remote.etag;
    client()->put(ctx.path, mergedBytes(ctx, merged), options,
                  guarded([this, path = ctx.path, retry, finishMerge, done](const PutResult &result) {
                      if (result.error.is(WebDavErrorCode::Precondition)) {
                          retry();
                          return;
                      }
                      if (result.error.isError()) {
                          done(Outcome::fromError(result.error));
                          return;
                      }
                      noteRemoteWritten(path);
                      versionAfterPut(path, result.etag, finishMerge);
                  }));
}

QByteArray SyncRun::mergedBytes(const PathCtx &ctx, const QString &merged)
{
    // The bytes of the local file (a BOM the vault kept, the local line endings): the server
    // gets exactly what is on disk, as with a plain upload.
    const LocalSnap &snap = *ctx.snap;
    if (merged == snap.text && snap.hasBytes)
        return snap.bytes;
    bool ok = false;
    const QByteArray written = vault()->readBinary(ctx.path, &ok);
    if (ok && decodeSyncText(written) == merged)
        return written;
    return merged.toUtf8();
}

std::optional<VaultPath> SyncRun::keepRemoteCopy(const VaultPath &path, const QByteArray &data, bool *created,
                                                 QString *error)
{
    // Once only: an earlier run that failed later may already have saved this very version.
    *created = false;
    if (const std::optional<VaultPath> existing = findConflictCopy(path, data))
        return existing;
    const VaultPath copy = vault()->createBinary(conflictCopyPath(path, m_engine->device(), now()), data,
                                                 ChangeSource::Sync);
    if (copy.isEmpty()) {
        *error = u"Cannot save the conflict copy of %1: %2"_s.arg(path, vault()->errorString());
        return std::nullopt;
    }
    *created = true;
    m_report.conflictCopies.append(copy);
    return copy;
}

void SyncRun::conflict(const PathCtx &ctx, const RemoteContent &remote, const Retry &retry, Done done)
{
    // 1. Keep the remote version safe locally first.
    QString error;
    bool created = false;
    const std::optional<VaultPath> copy = keepRemoteCopy(ctx.path, remote.data, &created, &error);
    if (!copy) {
        done(Outcome::failed(u"conflict-copy-failed"_s, error));
        return;
    }
    // 2. The local version wins at the original path.
    const std::optional<QByteArray> bytes = localBytes(ctx.path, *ctx.snap, &error);
    if (!bytes) {
        done(Outcome::failed(u"read-failed"_s, error));
        return;
    }
    PutOptions options;
    if (!remote.etag.isEmpty())
        options.ifMatch = remote.etag;
    client()->put(ctx.path, *bytes, options,
                  guarded([this, ctx, copy = *copy, created, data = remote.data, retry, done](const PutResult &result) {
                      if (result.error.is(WebDavErrorCode::Precondition)) {
                          retry();
                          return;
                      }
                      if (result.error.isError()) {
                          done(Outcome::fromError(result.error));
                          return;
                      }
                      noteRemoteWritten(ctx.path);
                      versionAfterPut(ctx.path, result.etag, [this, ctx, copy, created, data, done](const QString &version) {
                          const LocalSnap &snap = *ctx.snap;
                          saveState(ctx.path, snap.hash, version,
                                    snap.isText ? std::optional<QString>(snap.text) : std::nullopt, snap.stat);
                          record(ctx.path, SyncAction::Conflict);
                          qCInfo(lcSync).noquote()
                                  << u"%1 conflict in %2: the server's version kept as %3"_s.arg(tag(), ctx.path, copy);
                          // 3. Upload a new copy right away so other devices see both versions. An
                          //    older copy is a normal local file of this run (or the next).
                          if (created)
                              uploadConflictCopy(copy, data, done);
                          else
                              done(Outcome::done());
                      });
                  }));
}

void SyncRun::uploadConflictCopy(const VaultPath &copy, const QByteArray &data, Done done)
{
    PutOptions options;
    options.ifNoneMatch = true;
    client()->put(copy, data, options, guarded([this, copy, data, done](const PutResult &result) {
        if (result.error.isError()) {
            if (result.error.isFatal()) {
                done(Outcome::fromError(result.error));
                return;
            }
            qCWarning(lcSync).noquote() << tag() << "the conflict copy" << copy
                                        << "will be uploaded on the next sync:" << result.error.message;
            done(Outcome::done());
            return;
        }
        noteRemoteWritten(copy);
        versionAfterPut(copy, result.etag, [this, copy, data, done](const QString &version) {
            const Content content = classify(copy, data);
            saveState(copy, content.hash, version, content.text, std::nullopt);
            done(Outcome::done());
        });
    }));
}

void SyncRun::deleteRemote(const PathCtx &ctx, Done done)
{
    RemoveOptions options;
    if (ctx.remote && !ctx.remote->etag.isEmpty())
        options.ifMatch = ctx.remote->etag;
    client()->remove(ctx.path, options, guarded([this, ctx, done](const WebDavError &error) {
        if (error.is(WebDavErrorCode::Precondition)) {
            // Changed on the server since we listed it: the modification wins, restore it locally.
            ++m_report.stats.preconditionRetries;
            PathCtx fresh = ctx;
            fresh.snap.reset();
            fresh.remote.reset();
            download(fresh, done);
            return;
        }
        if (error.isError()) {
            done(Outcome::fromError(error));
            return;
        }
        noteRemoteDeleted(ctx.path);
        forget(ctx.path);
        record(ctx.path, SyncAction::DeleteRemote);
        done(Outcome::done());
    }));
}

void SyncRun::deleteLocal(const PathCtx &ctx, Done done)
{
    if (const std::optional<Outcome> changed = checkLocalUnchanged(ctx.path, ctx.snap)) {
        done(*changed);
        return;
    }
    if (!vault()->remove(ctx.path, ChangeSource::Sync)) {
        done(Outcome::failed(u"write-failed"_s, vault()->errorString()));
        return;
    }
    forget(ctx.path);
    record(ctx.path, SyncAction::DeleteLocal);
    done(Outcome::done());
}

std::optional<QString> SyncRun::mergeText(const VaultPath &path, const QString &local, const QString &remote,
                                          const std::optional<SyncFileState> &state) const
{
    // No history: merge against an empty base (keeps the union of both versions).
    QString base;
    bool haveBase = true;
    if (state) {
        const QJsonValue stored = store()->get(SyncEngine::baseKey(path));
        haveBase = stored.isString();
        base = stored.toString();
    }
    if (path == vaultConfigPath()) {
        // The vault config is JSON: a line union (first sync) or a line merge can produce
        // invalid JSON (missing commas, duplicate keys), so merge key by key unless the line
        // merge is fine.
        if (!haveBase || base.isEmpty())
            return mergeJson3(QString(), local, remote);
        const MergeResult result = merge3(base, local, remote);
        if (result.clean && isJsonObject(result.text))
            return result.text;
        return mergeJson3(base, local, remote);
    }
    if (!haveBase)
        return std::nullopt;
    // A drawing changed on both sides: merged element by element against the base (new elements
    // of both, erased ones gone, the remote's order with this device's new elements on top),
    // never line by line. One side as the base (a new ETag, the same text: a client uploaded it
    // again) is no change there: the other side's version. A drawing that cannot be merged so (an
    // element Brulion does not draw, a broken scene, no base: two drawings born apart under one
    // name) keeps both versions (a conflict copy; the sync center merges it on request).
    if (isDrawingText(path, local) || isDrawingText(path, remote)) {
        if (remote == base)
            return local;
        if (local == base)
            return remote;
        const std::optional<QByteArray> merged =
                drawing::mergeDrawingFiles(base.toUtf8(), local.toUtf8(), remote.toUtf8(), now().toMSecsSinceEpoch());
        if (!merged)
            return std::nullopt;
        return decodeSyncText(*merged);
    }
    const MergeResult result = merge3(base, local, remote);
    return result.clean ? std::optional<QString>(result.text) : std::nullopt;
}

std::optional<VaultPath> SyncRun::findConflictCopy(const VaultPath &path, const QByteArray &content)
{
    const QString hash = text::fnv1a64Hex(content);
    for (const FileStat &file : vault()->files()) {
        if (file.isDir || file.size != content.size())
            continue;
        const std::optional<BrulionConflict> parsed = parseBrulionConflict(file.path);
        if (!parsed || parsed->original != path)
            continue;
        bool ok = false;
        const QByteArray bytes = vault()->readBinary(file.path, &ok);
        if (ok && text::fnv1a64Hex(bytes) == hash)
            return file.path;
    }
    return std::nullopt;
}

} // namespace brulion::sync
