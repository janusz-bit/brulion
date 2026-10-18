// One SyncEngine run: list both sides, decide per path (planSync), transfer, merge, record.
#include "sync/syncrun_p.h"

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

namespace {

/// Tracked files (besides the vault config) before the "one side suddenly empty" guard applies.
constexpr int kEmptySideGuardMin = 2;
/// Attempts to merge when the remote file keeps changing under us (HTTP 412).
constexpr int kMaxMergeAttempts = 3;

bool isJsonObject(const QString &text)
{
    QString json = text;
    if (json.startsWith(QChar(0xFEFF)))
        json.remove(0, 1);
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    return error.error == QJsonParseError::NoError && doc.isObject();
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
        return Outcome{Kind::Fatal, error.message, error};
    return failed(error.message);
}

SyncRun::SyncRun(SyncEngine *engine, SyncRunOptions options)
    : m_engine(engine)
    , m_options(std::move(options))
{
}

template <typename F>
auto SyncRun::guarded(F &&fn)
{
    return [self = shared_from_this(), fn = std::forward<F>(fn)](auto &&...args) mutable {
        if (self->m_finished || !self->m_engine)
            return; // abandoned: the engine is gone
        if (!self->vault() || !self->client() || !self->store()) {
            self->dependencyLost();
            return;
        }
        fn(std::forward<decltype(args)>(args)...);
    };
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
    report(SyncProgress{SyncProgress::Phase::Connecting, 0, 0, {}});
    checkRemoteIdentity();
    report(SyncProgress{SyncProgress::Phase::Listing, 0, 0, {}});
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
    if (!meta.isUndefined())
        m_engine->resetState();
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
    client()->list(options, guarded([this](const ListResult &result) {
        if (m_cancelled || result.error.is(WebDavErrorCode::Cancelled)) {
            m_cancelled = true;
            finish();
            return;
        }
        if (result.error.is(WebDavErrorCode::NotFound)) {
            // First sync into a folder that does not exist yet.
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
    for (const RemoteEntry &entry : entries) {
        if (!entry.isDir && isSyncablePath(entry.path))
            m_remote.insert(entry.path, entry);
    }
    listLocal();
    loadStates();
    if (m_cancelled) {
        finish();
        return;
    }
    if (!m_options.allowEmptySide && !guardEmptySides()) {
        finish();
        return;
    }
    guardMassDeletion();
    processAll();
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
}

bool SyncRun::guardEmptySides()
{
    // Refuse to mirror a side that suddenly lost every file synced before (wrong folder, wiped
    // server, vault storage lost): that would delete everything on the other side. Files created
    // since (today's journal, a default config) do not count.
    int tracked = 0;
    bool localHas = false;
    bool remoteHas = false;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        if (it.key() == vaultConfigPath())
            continue;
        ++tracked;
        localHas = localHas || m_local.contains(it.key());
        remoteHas = remoteHas || m_remote.contains(it.key());
    }
    if (tracked < kEmptySideGuardMin)
        return true;
    if (!localHas && remoteHas) {
        m_report.syncError = SyncError{
            SyncErrorCode::LocalEmpty,
            u"None of the %1 files synced before exist locally; refusing to delete them on the server"_s.arg(tracked)};
        return false;
    }
    if (!remoteHas && localHas) {
        m_report.syncError = SyncError{
            SyncErrorCode::RemoteEmpty,
            u"None of the %1 files synced before exist on the server; refusing to delete them locally"_s.arg(tracked)};
        return false;
    }
    return true;
}

void SyncRun::guardMassDeletion()
{
    if (m_options.confirmDeletions || m_options.allowEmptySide)
        return;
    // Count what planSync() will decide for the paths synced before that are gone on one side:
    // deleted there unless the other side changed (then the file is restored instead).
    const qint64 maxSize = m_engine->maxFileSize();
    int localDeletions = 0;  // gone remotely, unchanged locally -> DeleteLocal
    int remoteDeletions = 0; // gone locally, unchanged remotely -> DeleteRemote
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
                ++remoteDeletions;
            continue;
        }
        if (local->size > maxSize)
            continue; // skipped by syncPath()
        const ReadResult read = readLocal(path, *local, *it);
        if (read.snap && read.snap->hash == it->localHash)
            ++localDeletions;
    }
    m_holdDeletions = tripsMassDeletionGuard(localDeletions, countWithoutConfig(m_local))
                      || tripsMassDeletionGuard(remoteDeletions, countWithoutConfig(m_remote));
    if (m_holdDeletions) {
        qCInfo(detail::lcSync).noquote()
                << u"Mass deletion held back for confirmation: %1 local and %2 remote file(s) would be deleted"_s
                           .arg(localDeletions)
                           .arg(remoteDeletions);
    }
}

void SyncRun::processAll()
{
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
    report(SyncProgress{SyncProgress::Phase::Syncing, 0, m_total, {}});

    const QList<QStringList> groups = groupByCase(
        paths, [this](const VaultPath &p) { return m_local.contains(p); },
        [this](const VaultPath &p) { return m_remote.contains(p); });
    m_pool = std::make_shared<detail::JobPool>(m_engine->concurrency(), m_engine);
    for (const QStringList &group : groups) {
        // Names differing only in letter case are one file on case-insensitive storage: handle
        // them one after another (see groupByCase), never in parallel.
        m_pool->add([weak = weak_from_this(), group](detail::JobPool::Done done) {
            if (const std::shared_ptr<SyncRun> self = weak.lock())
                self->runGroup(group, 0, std::move(done));
            else
                done();
        });
    }
    m_pool->start(guarded([this] { finish(); }));
}

void SyncRun::runGroup(const QStringList &group, qsizetype index, std::function<void()> groupDone)
{
    if (index >= group.size() || m_cancelled || m_finished) {
        groupDone();
        return;
    }
    const VaultPath path = group.at(index);
    syncPath(path, guarded([this, group, index, groupDone, path](const Outcome &outcome) {
        switch (outcome.kind) {
        case Outcome::Kind::Done:
        case Outcome::Kind::Skipped: break; // skipped: changed locally meanwhile, next sync
        case Outcome::Kind::Failed: m_report.errors.append(SyncFileError{path, outcome.message}); break;
        case Outcome::Kind::Fatal:
            if (!m_report.remoteError.isError())
                m_report.remoteError = outcome.error;
            if (m_pool)
                m_pool->stop();
            break;
        }
        ++m_done;
        report(SyncProgress{SyncProgress::Phase::Syncing, m_done, m_total, path});
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
    m_report.finished = now();
    m_report.cancelled = m_cancelled;
    std::sort(m_report.pendingDeletions.begin(), m_report.pendingDeletions.end(),
              [](const SyncChange &a, const SyncChange &b) { return a.path < b.path; });
    m_report.needsConfirmation = !m_report.pendingDeletions.isEmpty();
    if (!m_report.failed() && !m_cancelled)
        report(SyncProgress{SyncProgress::Phase::Done, m_done, m_total, {}});
    if (store() && !store()->flush())
        qCWarning(detail::lcSync).noquote() << "Could not save the sync state:" << store()->errorString();
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
            done(Outcome::failed(read.error));
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
                    u"%1: a local file whose name differs only in letter case exists (%2); rename one of them"_s
                        .arg(path, other->path));
            }
            return Outcome::failed(u"%1: a local file exists that the vault has not seen yet; will retry"_s.arg(path));
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
    store()->set(SyncEngine::stateKey(path), state.toJson());
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
        done(Outcome::failed(error));
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
            PathCtx fresh = ctx;
            fresh.remote.reset();
            reconcile(fresh, 1, done);
            return;
        }
        if (result.error.isError()) {
            done(Outcome::fromError(result.error));
            return;
        }
        versionAfterPut(ctx.path, result.etag, [this, ctx, done](const QString &version) {
            const LocalSnap &snap = *ctx.snap;
            saveState(ctx.path, snap.hash, version, snap.isText ? std::optional<QString>(snap.text) : std::nullopt,
                      snap.stat);
            record(ctx.path, SyncAction::Upload);
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
            done(Outcome::failed(vault()->errorString()));
            return;
        }
        saveState(ctx.path, content.hash, remote.version, content.text, std::nullopt);
        record(ctx.path, SyncAction::Download);
        done(Outcome::done());
    });
}

void SyncRun::reconcile(const PathCtx &ctx, int attempt, Done done)
{
    if (attempt >= kMaxMergeAttempts) {
        done(Outcome::failed(u"%1 keeps changing on the server; will retry on the next sync"_s.arg(ctx.path)));
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
        done(Outcome::failed(fresh.error));
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
        done(Outcome::failed(vault()->errorString()));
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
        done(Outcome::failed(error));
        return;
    }
    // 2. The local version wins at the original path.
    const std::optional<QByteArray> bytes = localBytes(ctx.path, *ctx.snap, &error);
    if (!bytes) {
        done(Outcome::failed(error));
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
                      versionAfterPut(ctx.path, result.etag, [this, ctx, copy, created, data, done](const QString &version) {
                          const LocalSnap &snap = *ctx.snap;
                          saveState(ctx.path, snap.hash, version,
                                    snap.isText ? std::optional<QString>(snap.text) : std::nullopt, snap.stat);
                          record(ctx.path, SyncAction::Conflict);
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
            qCWarning(detail::lcSync).noquote()
                << "The conflict copy" << copy << "will be uploaded on the next sync:" << result.error.message;
            done(Outcome::done());
            return;
        }
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
        done(Outcome::failed(vault()->errorString()));
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
