#include "sync/syncengine.h"

#include "sync/syncrun_p.h"
#include "sync/util_p.h"
#include "sync/webdavclient.h"
#include "util/paths.h"
#include "vault/localstore.h"
#include "vault/vault.h"
#include "vault/vaultconfig.h"

#include <QSet>

#include <algorithm>
#include <initializer_list>

using namespace Qt::StringLiterals;

namespace brulion::sync {

namespace {

/// paths::isIgnored(), with hidden (dot) segments compared in any letter case: on
/// case-insensitive storage (Android shared storage, FAT, Windows, macOS) a remote
/// ".GIT/hooks/x" or ".Obsidian/plugins/…" would land in the real ignored folder (code that
/// git or Obsidian runs). Visible names keep their case ("Import/Logseq/…" is a notes folder).
bool isIgnoredAnyCase(const VaultPath &path)
{
    if (paths::isIgnored(path))
        return true;
    QStringList segments = path.split(u'/');
    for (QString &segment : segments) {
        if (segment.startsWith(u'.'))
            segment = segment.toLower();
    }
    const QString lowered = segments.join(u'/');
    return lowered != path && paths::isIgnored(lowered);
}

} // namespace

bool isSyncTextPath(const VaultPath &path)
{
    static const QSet<QString> textExtensions = {
        u"md"_s, u"markdown"_s, u"txt"_s, u"json"_s, u"csv"_s, u"canvas"_s,
        u"css"_s, u"ics"_s, u"opml"_s, u"html"_s, u"svg"_s,
    };
    const QString name = paths::basename(path);
    const qsizetype dot = name.lastIndexOf(u'.');
    return dot > 0 && textExtensions.contains(name.mid(dot + 1).toLower());
}

bool isSyncablePath(const VaultPath &path)
{
    const VaultPath p = paths::normalize(path);
    if (p.isEmpty())
        return false;
    if (p == vaultConfigPath())
        return true;
    return !isIgnoredAnyCase(p);
}

std::optional<QString> decodeSyncText(const QByteArray &bytes)
{
    return detail::decodeUtf8Strict(bytes, true);
}

SyncPlan planSync(const SyncFacts &f)
{
    if (!f.known) {
        if (f.local && f.remote)
            return SyncPlan::Reconcile;
        if (f.local)
            return SyncPlan::Upload;
        if (f.remote)
            return SyncPlan::Download;
        return SyncPlan::Forget;
    }
    if (f.local && f.remote) {
        if (f.localChanged && f.remoteChanged)
            return SyncPlan::Reconcile;
        if (f.localChanged)
            return SyncPlan::Upload;
        if (f.remoteChanged)
            return SyncPlan::Download;
        return SyncPlan::Noop;
    }
    if (f.local)
        return f.localChanged ? SyncPlan::Upload : SyncPlan::DeleteLocal;
    if (f.remote)
        return f.remoteChanged ? SyncPlan::Download : SyncPlan::DeleteRemote;
    return SyncPlan::Forget;
}

QList<QStringList> groupByCase(const QStringList &sortedPaths, const std::function<bool(const VaultPath &)> &isLocal,
                               const std::function<bool(const VaultPath &)> &isRemote)
{
    QList<QStringList> groups;
    QHash<QString, qsizetype> groupOf;
    for (const VaultPath &path : sortedPaths) {
        const QString key = path.toLower();
        const auto it = groupOf.constFind(key);
        if (it == groupOf.cend()) {
            groupOf.insert(key, groups.size());
            groups.append(QStringList{path});
        } else {
            groups[*it].append(path);
        }
    }
    const auto rank = [&](const VaultPath &p) { return isLocal(p) && !isRemote(p) ? 0 : 1; };
    for (QStringList &group : groups) {
        if (group.size() > 1) {
            std::stable_sort(group.begin(), group.end(), [&](const VaultPath &a, const VaultPath &b) {
                const int ra = rank(a);
                const int rb = rank(b);
                return ra != rb ? ra < rb : a < b;
            });
        }
    }
    return groups;
}

// ---------------------------------------------------------------------------------------------
// SyncEngine
// ---------------------------------------------------------------------------------------------

QString SyncEngine::stateKey(const VaultPath &path)
{
    return u"sync/state/"_s + path;
}

QString SyncEngine::baseKey(const VaultPath &path)
{
    return u"sync/base/"_s + path;
}

QString SyncEngine::metaKey()
{
    return u"sync/meta"_s;
}

SyncEngine::SyncEngine(Vault *vault, WebDavClient *client, LocalStore *store, const QString &device, QObject *parent)
    : QObject(parent)
    , m_vault(vault)
    , m_client(client)
    , m_store(store)
    , m_device(device)
{
    // A dependency destroyed during a run takes its pending callbacks with it (the client's
    // requests above all): end the run instead of waiting for them forever.
    for (QObject *dependency : std::initializer_list<QObject *>{vault, client, store}) {
        if (dependency)
            connect(dependency, &QObject::destroyed, this, &SyncEngine::dependencyDestroyed);
    }
}

void SyncEngine::dependencyDestroyed()
{
    if (!m_run)
        return;
    // Posted: the dependency's destructor is still running.
    detail::post(this, [weak = std::weak_ptr<SyncRun>(m_run)] {
        if (const std::shared_ptr<SyncRun> run = weak.lock())
            run->dependencyLost();
    });
}

SyncEngine::~SyncEngine()
{
    if (m_run)
        m_run->abandon();
}

Vault *SyncEngine::vault() const
{
    return m_vault;
}

WebDavClient *SyncEngine::client() const
{
    return m_client;
}

LocalStore *SyncEngine::store() const
{
    return m_store;
}

void SyncEngine::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

QDateTime SyncEngine::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

void SyncEngine::setConcurrency(int count)
{
    m_concurrency = qMax(1, count);
}

void SyncEngine::setMaxFileSize(qint64 bytes)
{
    m_maxFileSize = qMax<qint64>(0, bytes);
}

void SyncEngine::setMaxBaseSize(qint64 bytes)
{
    m_maxBaseSize = qMax<qint64>(0, bytes);
}

bool SyncEngine::isSyncing() const
{
    return m_run != nullptr;
}

bool SyncEngine::sync(const SyncRunOptions &options, SyncDoneCallback done)
{
    if (done)
        m_waiters.append(std::move(done));
    if (m_run)
        return false;
    const std::shared_ptr<SyncRun> run = std::make_shared<SyncRun>(this, options);
    m_run = run;
    Q_EMIT started();
    run->start();
    return true;
}

void SyncEngine::cancel()
{
    if (m_run)
        m_run->cancel();
}

void SyncEngine::startSync(const SyncRunOptions &options, SyncDoneCallback done)
{
    sync(options, std::move(done));
}

void SyncEngine::cancelSync()
{
    cancel();
}

void SyncEngine::resetState()
{
    if (!m_store)
        return;
    m_store->removePrefix(stateKey(QString()));
    m_store->removePrefix(baseKey(QString()));
    m_store->remove(metaKey());
}

std::optional<SyncFileState> SyncEngine::stateOf(const VaultPath &path) const
{
    if (!m_store)
        return std::nullopt;
    return SyncFileState::fromJson(m_store->get(stateKey(paths::normalize(path))).toObject());
}

std::optional<QString> SyncEngine::mergeBaseOf(const VaultPath &path) const
{
    if (!m_store)
        return std::nullopt;
    const QJsonValue base = m_store->get(baseKey(paths::normalize(path)));
    return base.isString() ? std::optional<QString>(base.toString()) : std::nullopt;
}

void SyncEngine::runFinished(const SyncReport &report)
{
    m_run.reset();
    const QList<SyncDoneCallback> waiters = std::exchange(m_waiters, {});
    Q_EMIT finished(report);
    for (const SyncDoneCallback &callback : waiters)
        callback(report);
}

} // namespace brulion::sync
