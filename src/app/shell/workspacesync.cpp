#include "workspacesync.h"

#include "workspace.h"

#include "dates/dates.h"
#include "log/categories.h"
#include "log/redact.h"
#include "sync/sync.h"
#include "sync/synchistory.h"
#include "util/paths.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSysInfo>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>
#include <tuple>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

constexpr qint64 kDailyMs = 24LL * 60 * 60 * 1000;

QString isoOf(const QDateTime &time)
{
    return time.isValid() ? time.toLocalTime().toString(u"yyyy-MM-ddTHH:mm:ss"_s) : QString();
}

QString defaultFolder(const QString &folder)
{
    const QString f = folder.trimmed();
    return f.isEmpty() ? u"Brulion"_s : f;
}

} // namespace

bool sameNextcloudAccount(const QString &url, const QString &user, const QString &otherUrl, const QString &otherUser)
{
    if (user.trimmed() != otherUser.trimmed())
        return false;
    const auto server = [](const QString &address) {
        QUrl u(address.trimmed());
        if (u.scheme().isEmpty() && !address.trimmed().isEmpty())
            u = QUrl(u"https://"_s + address.trimmed());
        u = u.adjusted(QUrl::NormalizePathSegments | QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
        QString path = u.path();
        while (path.endsWith(u'/'))
            path.chop(1);
        const QString scheme = u.scheme().toLower();
        return std::tuple(scheme, u.host().toLower(), u.port(scheme == u"http" ? 80 : 443), path);
    };
    return !url.trimmed().isEmpty() && server(url) == server(otherUrl);
}

QString nextcloudRemote(const QString &url, const QString &user, const QString &folder)
{
    // As WorkspaceSync::rebuild() builds the client (davUrlFor()) and WebDavClient::identity().
    if (url.trimmed().isEmpty() || user.trimmed().isEmpty())
        return {};
    QString base = sync::nextcloudDavUrl(url.trimmed(), user.trimmed(), defaultFolder(folder));
    while (base.endsWith(u'/'))
        base.chop(1);
    return user.trimmed() + u'@' + base;
}

QString nextcloudRemoteFolder(const QString &url, const QString &user, const QString &remote)
{
    if (url.trimmed().isEmpty() || user.trimmed().isEmpty() || remote.isEmpty())
        return {};
    QString base = sync::nextcloudDavUrl(url.trimmed(), user.trimmed(), QString());
    while (base.endsWith(u'/'))
        base.chop(1);
    const QString prefix = user.trimmed() + u'@' + base + u'/';
    if (!remote.startsWith(prefix) || remote.size() == prefix.size())
        return {};
    return QUrl::fromPercentEncoding(remote.mid(prefix.size()).toUtf8());
}

bool NextcloudSettings::isComplete() const
{
    return enabled && !url.trimmed().isEmpty() && !user.trimmed().isEmpty() && !password.isEmpty();
}

// ---- Syncthing config ---------------------------------------------------------------------------

std::optional<SyncthingSettings> parseSyncthingConfig(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);
    SyncthingSettings out;
    QString address;
    bool tls = false;
    bool inGui = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const auto name = reader.name();
            if (name == u"gui" && !inGui) {
                inGui = true;
                tls = reader.attributes().value(u"tls"_s) == u"true";
            } else if (inGui && name == u"address") {
                address = reader.readElementText().trimmed();
            } else if (inGui && name == u"apikey") {
                out.apiKey = reader.readElementText().trimmed();
            }
        } else if (reader.isEndElement() && reader.name() == u"gui") {
            break;
        }
    }
    if (out.apiKey.isEmpty())
        return std::nullopt;
    if (!address.isEmpty() && !address.startsWith(u'/') && !address.startsWith(u"unix"_s)) {
        // A GUI listening on every interface is reached through this machine's loopback.
        if (address.startsWith(u"0.0.0.0:"_s))
            address = u"127.0.0.1"_s + address.mid(7);
        else if (address.startsWith(u"[::]:"_s))
            address = u"[::1]"_s + address.mid(4);
        else if (address.startsWith(u':'))
            address = u"127.0.0.1"_s + address;
        if (!address.contains(u"://"_s))
            address = (tls ? u"https://"_s : u"http://"_s) + address;
        out.url = address;
    }
    return out;
}

std::optional<SyncthingSettings> discoverSyncthingConfig()
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_WASM)
    return std::nullopt;
#else
    const QString home = QDir::homePath();
    const auto env = [](const char *name) { return qEnvironmentVariable(name); };
    const QString state = env("XDG_STATE_HOME").isEmpty() ? home + u"/.local/state"_s : env("XDG_STATE_HOME");
    const QString config = env("XDG_CONFIG_HOME").isEmpty() ? home + u"/.config"_s : env("XDG_CONFIG_HOME");
    const QStringList files{ state + u"/syncthing/config.xml"_s, config + u"/syncthing/config.xml"_s };
    for (const QString &file : files) {
        QFile f(file);
        if (f.size() > 4 * 1024 * 1024 || !f.open(QIODevice::ReadOnly))
            continue;
        if (auto found = parseSyncthingConfig(f.readAll()))
            return found;
    }
    return std::nullopt;
#endif
}

// ---- WorkspaceSync --------------------------------------------------------------------------------

WorkspaceSync::WorkspaceSync(QObject *parent) : QObject(parent)
{
    m_syncthingTimer.setInterval(SyncthingPollMs);
    connect(&m_syncthingTimer, &QTimer::timeout, this, &WorkspaceSync::refreshSyncthing);
    // Progress arrives once per file: the UI gets at most four updates a second, and one a
    // second while syncing for the elapsed time. No timer runs while nothing syncs.
    m_liveTimer.setSingleShot(true);
    m_liveTimer.setInterval(250);
    connect(&m_liveTimer, &QTimer::timeout, this, &WorkspaceSync::updateLive);
    m_liveTick.setInterval(1000);
    connect(&m_liveTick, &QTimer::timeout, this, &WorkspaceSync::updateLive);
    m_startTimer.setSingleShot(true);
    m_startTimer.setInterval(StartDelayMs);
    connect(&m_startTimer, &QTimer::timeout, this, [this] {
        if (m_scheduler && m_automatic)
            m_scheduler->start(); // the first sync right away, then periodically
    });
    // qobject_cast, not qGuiApp: tests run without a QGuiApplication.
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState s) { setAppActive(s == Qt::ApplicationActive); });
    }
    m_syncthingStatus = { { u"enabled"_s, false }, { u"folder"_s, false } };
}

WorkspaceSync::~WorkspaceSync()
{
    teardown();
    m_syncthing.reset();
}

sync::HttpTransport *WorkspaceSync::transport()
{
    if (m_transport)
        return m_transport;
    if (!m_ownTransport)
        m_ownTransport = std::make_unique<sync::NetworkHttpTransport>();
    return m_ownTransport.get();
}

void WorkspaceSync::setTransport(sync::HttpTransport *transport)
{
    m_transport = transport;
    rebuild();
    rebuildSyncthing();
}

void WorkspaceSync::setDeviceName(const QString &name)
{
    if (m_deviceName == name.trimmed())
        return;
    m_deviceName = name.trimmed();
    rebuild();
}

QString WorkspaceSync::effectiveDeviceName() const
{
    if (!m_deviceName.isEmpty())
        return m_deviceName;
#if defined(Q_OS_ANDROID)
    return u"Android"_s;
#elif defined(Q_OS_WASM)
    return u"Przeglądarka"_s;
#else
    const QString host = QSysInfo::machineHostName();
    return host.isEmpty() || host == u"localhost" ? u"Komputer"_s : host;
#endif
}

QString WorkspaceSync::deviceName() const
{
    return sync::sanitizeDeviceName(effectiveDeviceName());
}

void WorkspaceSync::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
    if (m_engine && m_clock)
        m_engine->setClock(m_clock);
}

void WorkspaceSync::setLanguage(const QString &language)
{
    m_language = language == u"en" ? u"en"_s : u"pl"_s;
    Q_EMIT statusChanged();
    refreshConflicts();
}

void WorkspaceSync::setAutomatic(bool on)
{
    if (m_automatic == on)
        return;
    m_automatic = on;
    rebuild();
}

void WorkspaceSync::setOnlineCheck(std::function<bool()> isOnline)
{
    m_onlineCheck = std::move(isOnline);
    if (m_scheduler && m_onlineCheck)
        m_scheduler->setOnlineCheck(m_onlineCheck);
}

void WorkspaceSync::setSyncthingDiscovery(bool on)
{
    m_discovery = on;
    rebuildSyncthing();
}

void WorkspaceSync::setNextcloud(const NextcloudSettings &settings)
{
    if (settings == m_nextcloud)
        return;
    m_nextcloud = settings;
    rebuild();
}

void WorkspaceSync::setSyncthing(const SyncthingSettings &settings)
{
    if (settings == m_syncthingSettings)
        return;
    m_syncthingSettings = settings;
    rebuildSyncthing();
}

void WorkspaceSync::attach(Vault *vault)
{
    detach();
    m_vault = vault;
    if (vault)
        m_vaultConnection = connect(vault, &Vault::changed, this, &WorkspaceSync::onVaultChanged);
    openHistory();
    refreshConflicts();
    rebuild();
    rebuildSyncthing();
}

void WorkspaceSync::detach()
{
    teardown();
    m_syncthing.reset();
    m_syncthingTimer.stop();
    m_syncthingFolderId.clear();
    ++m_syncthingGeneration;
    QObject::disconnect(m_vaultConnection);
    m_vault = nullptr;
    m_syncthingStatus = { { u"enabled"_s, false }, { u"folder"_s, false } };
    Q_EMIT syncthingChanged();
    refreshConflicts();
    if (m_history) {
        m_history.reset();
        m_historyStore.reset();
        Q_EMIT historyChanged();
    }
}

void WorkspaceSync::teardown()
{
    const bool had = m_scheduler != nullptr;
    m_startTimer.stop();
    // The scheduler first (it drives the engine), then the engine (abandons a running run),
    // then the client its requests belong to.
    m_scheduler.reset();
    m_engine.reset();
    m_client.reset();
    m_liveTimer.stop();
    m_liveTick.stop();
    if (!m_live.isEmpty()) {
        m_live.clear();
        Q_EMIT liveChanged();
    }
    if (had) {
        Q_EMIT configurationChanged();
        Q_EMIT statusChanged();
    }
}

void WorkspaceSync::rebuild()
{
    teardown();
    if (!m_vault || !m_nextcloud.isComplete()) {
        Q_EMIT configurationChanged();
        Q_EMIT statusChanged();
        return;
    }
    const QString base = currentDavUrl();
    m_client = std::make_unique<sync::WebDavClient>(base, m_nextcloud.user.trimmed(), m_nextcloud.password, transport());
    m_engine = std::make_unique<sync::SyncEngine>(m_vault.data(), m_client.get(), m_vault->store(), deviceName());
    if (m_clock)
        m_engine->setClock(m_clock);
    if (m_history)
        m_engine->setNextRunNumber(m_history->nextNumber());
    m_scheduler = std::make_unique<sync::SyncScheduler>(m_engine.get());
    m_scheduler->setInterval(m_nextcloud.intervalMinutes > 0 ? qint64(m_nextcloud.intervalMinutes) * 60'000 : kDailyMs);
    if (m_onlineCheck)
        m_scheduler->setOnlineCheck(m_onlineCheck);
    else
        m_scheduler->useSystemNetworkInformation();
    connect(m_scheduler.get(), &sync::SyncScheduler::statusChanged, this, [this] {
        m_lastStatusText = statusText();
        Q_EMIT statusChanged();
        scheduleLive();
    });
    connect(m_scheduler.get(), &sync::SyncScheduler::progressChanged, this, &WorkspaceSync::scheduleLive);
    connect(m_scheduler.get(), &sync::SyncScheduler::runStarted, this, &WorkspaceSync::scheduleLive);
    connect(m_scheduler.get(), &sync::SyncScheduler::runFinished, this, &WorkspaceSync::onRunFinished);
    connect(m_scheduler.get(), &sync::SyncScheduler::synced, this, &WorkspaceSync::onSynced);
    // Which account and folder (never the password): the first lines to look at in a log.
    const QUrl server = QUrl::fromUserInput(m_nextcloud.url.trimmed());
    qCInfo(logging::lcSync).noquote() << u"Nextcloud sync: user %1 at %2, folder %3, every %4"_s.arg(
            m_nextcloud.user.trimmed(),
            server.adjusted(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment).toString(),
            defaultFolder(m_nextcloud.folder),
            m_nextcloud.intervalMinutes > 0 ? u"%1 min"_s.arg(m_nextcloud.intervalMinutes) : u"day"_s);
    if (m_automatic)
        m_startTimer.start();
    Q_EMIT configurationChanged();
    Q_EMIT statusChanged();
}

void WorkspaceSync::setAppActive(bool active)
{
    m_appActive = active;
    if (m_scheduler)
        m_scheduler->setAppActive(active);
    if (m_syncthing) {
        if (active) {
            refreshSyncthing();
            m_syncthingTimer.start();
        } else {
            m_syncthingTimer.stop();
        }
    }
}

void WorkspaceSync::onVaultChanged(const QList<VaultChange> &changes)
{
    if (m_scheduler)
        m_scheduler->notifyChanges(changes);
    const bool conflictTouched = std::any_of(changes.cbegin(), changes.cend(), [](const VaultChange &c) {
        return c.path.contains(u"conflict"_s, Qt::CaseInsensitive) || c.oldPath.contains(u"conflict"_s, Qt::CaseInsensitive);
    });
    if (conflictTouched)
        refreshConflicts();
}

void WorkspaceSync::onSynced(const sync::SyncReport &report)
{
    refreshConflicts();
    if (!report.conflicts.isEmpty())
        Q_EMIT conflictsFound(int(report.conflicts.size()));
}

// ---- state --------------------------------------------------------------------------------------

QString WorkspaceSync::currentDavUrl() const
{
    return davUrlFor(m_nextcloud.url, m_nextcloud.user, m_nextcloud.folder);
}

QString WorkspaceSync::davUrlFor(const QString &url, const QString &user, const QString &folder) const
{
    if (url.trimmed().isEmpty() || user.trimmed().isEmpty())
        return {};
    return sync::nextcloudDavUrl(url.trimmed(), user.trimmed(), defaultFolder(folder));
}

QString WorkspaceSync::state() const
{
    if (!m_scheduler)
        return u"disabled"_s;
    const sync::SyncStatus s = m_scheduler->status();
    // Started by hand only (not automatic): between runs it is idle, not "disabled".
    if (s.state == sync::SyncState::Disabled)
        return s.lastErrorCode.isEmpty() ? u"idle"_s : u"error"_s;
    return sync::syncStateName(s.state);
}

QString WorkspaceSync::human(const QDateTime &time) const
{
    if (!time.isValid())
        return {};
    const QDateTime local = time.toLocalTime();
    const QDate today = (m_clock ? m_clock() : QDateTime::currentDateTime()).date();
    return dates::formatHuman(DateSpec{ local.date(), QTime(local.time().hour(), local.time().minute()) },
                              m_language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish, today);
}

QString WorkspaceSync::errorText(const QString &code, const QString &fallback) const
{
    const sync::SyncStatus s = m_scheduler ? m_scheduler->status() : sync::SyncStatus();
    if (code == u"auth")
        return tr("Wrong user name or app password.");
    if (code == u"forbidden")
        return tr("The server refused access to the folder.");
    if (code == u"not-found")
        return tr("The folder was not found on the server.");
    if (code == u"network")
        return tr("Cannot reach the server. Check the address and the connection.");
    if (code == u"timeout")
        return tr("The server did not answer in time.");
    if (code == u"parse")
        return tr("This address does not lead to a WebDAV folder. Check the server address.");
    if (code == u"insufficient-storage")
        return tr("There is no space left on the server.");
    if (code == u"server")
        return tr("The server reported an error. Brulion will try again later.");
    if (code == u"stalled")
        return tr("The server stopped answering, so the sync was stopped. Brulion will try again.");
    if (code == u"local-empty")
        return tr("Sync paused: the notebook folder looks empty although it was synced before.");
    if (code == u"remote-empty")
        return tr("Sync paused: the folder on the server is empty although it was synced before.");
    if (code == u"vault-unavailable")
        return tr("The notebook folder is not available (an unmounted card or drive?).");
    if (code == u"needs-confirmation") {
        const int n = s.lastReport ? int(s.lastReport->pendingDeletions.size()) : 0;
        return tr("%n file(s) would be deleted. Confirm in the sync center.", "", n);
    }
    if (code == u"files") {
        const int n = s.lastReport ? int(s.lastReport->errors.size()) : 0;
        return tr("%n file(s) could not be synced.", "", n);
    }
    return fallback.isEmpty() ? tr("Sync failed.") : logging::redact(fallback);
}

QString WorkspaceSync::statusText() const
{
    if (!m_nextcloud.isComplete())
        return m_nextcloud.enabled ? tr("Nextcloud is not set up completely") : QString();
    if (!m_scheduler)
        return tr("No notebook open");
    const sync::SyncStatus s = m_scheduler->status();
    switch (s.state) {
    case sync::SyncState::Syncing:
        if (s.abandoning)
            return tr("Stopping: the server is not answering…");
        if (s.stalled)
            return tr("The server has not answered for %1…").arg(durationText(s.stalledMs));
        if (!s.progress)
            return tr("Connecting…");
        switch (s.progress->phase) {
        case sync::SyncProgress::Phase::Connecting: return tr("Connecting…");
        case sync::SyncProgress::Phase::Listing: return tr("Checking files on the server…");
        case sync::SyncProgress::Phase::ListingLocal: return tr("Checking files on this device…");
        case sync::SyncProgress::Phase::Comparing: return tr("Comparing changes…");
        case sync::SyncProgress::Phase::Syncing:
            if (s.progress->total > 0)
                return tr("Syncing… %1 of %2 checked").arg(s.progress->done).arg(s.progress->total);
            return tr("Syncing…");
        case sync::SyncProgress::Phase::Saving: return tr("Saving the sync state…");
        case sync::SyncProgress::Phase::Done: return tr("Syncing…");
        }
        return tr("Syncing…");
    case sync::SyncState::Offline:
        return tr("Offline — will sync later");
    case sync::SyncState::Error:
        return errorText(s.lastErrorCode, s.lastError);
    case sync::SyncState::Idle:
    case sync::SyncState::Disabled:
        if (!s.lastErrorCode.isEmpty())
            return errorText(s.lastErrorCode, s.lastError);
        return s.lastSync.isValid() ? tr("Synced %1").arg(human(s.lastSync)) : tr("Not synced yet");
    }
    return {};
}

QString WorkspaceSync::lastSync() const
{
    return m_scheduler ? isoOf(m_scheduler->status().lastSync) : QString();
}

QString WorkspaceSync::lastSyncText() const
{
    return m_scheduler ? human(m_scheduler->status().lastSync) : QString();
}

QString WorkspaceSync::lastError() const
{
    if (!m_scheduler)
        return {};
    const sync::SyncStatus s = m_scheduler->status();
    return s.lastErrorCode.isEmpty() && s.lastError.isEmpty() ? QString() : errorText(s.lastErrorCode, s.lastError);
}

QString WorkspaceSync::lastErrorCode() const
{
    return m_scheduler ? m_scheduler->status().lastErrorCode : QString();
}

qreal WorkspaceSync::progress() const
{
    if (!m_scheduler)
        return -1;
    const sync::SyncStatus s = m_scheduler->status();
    if (s.state != sync::SyncState::Syncing || !s.progress || s.progress->total <= 0
        || s.progress->phase != sync::SyncProgress::Phase::Syncing)
        return -1;
    return std::clamp(qreal(s.progress->done) / qreal(s.progress->total), 0.0, 1.0);
}

QString WorkspaceSync::nextSync() const
{
    return m_scheduler ? isoOf(m_scheduler->status().nextSync) : QString();
}

QVariantMap WorkspaceSync::lastReport() const
{
    if (!m_scheduler)
        return {};
    const sync::SyncStatus status = m_scheduler->status();
    if (!status.lastReport)
        return {};
    const sync::SyncReport &r = *status.lastReport;
    QVariantList errors;
    for (const sync::SyncFileError &e : r.errors)
        errors.append(QVariantMap{{u"path"_s, e.path},
                                  {u"code"_s, e.code},
                                  {u"text"_s, fileErrorText(e.code, e.status)},
                                  {u"message"_s, logging::redact(e.message)}});
    return {
        { u"uploaded"_s, r.uploaded },
        { u"downloaded"_s, r.downloaded },
        { u"merged"_s, r.merged },
        { u"deletedLocal"_s, r.deletedLocal },
        { u"deletedRemote"_s, r.deletedRemote },
        { u"conflicts"_s, int(r.conflicts.size()) },
        { u"conflictPaths"_s, QStringList(r.conflicts) },
        { u"errors"_s, errors },
        { u"skipped"_s, QStringList(r.skipped) },
        { u"changes"_s, r.changeCount() },
        { u"started"_s, isoOf(r.started) },
        { u"finished"_s, isoOf(r.finished) },
        { u"cancelled"_s, r.cancelled },
        { u"failed"_s, r.failed() },
        { u"failure"_s, r.failed() ? errorText(r.failureCode(), r.failureMessage()) : QString() },
    };
}

bool WorkspaceSync::needsConfirmation() const
{
    if (!m_scheduler)
        return false;
    const sync::SyncStatus status = m_scheduler->status();
    return status.lastReport && status.lastReport->needsConfirmation;
}

QVariantList WorkspaceSync::pendingDeletions() const
{
    QVariantList out;
    if (!needsConfirmation())
        return out;
    const sync::SyncStatus status = m_scheduler->status();
    for (const sync::SyncChange &c : status.lastReport->pendingDeletions)
        out.append(QVariantMap{ { u"path"_s, c.path }, { u"action"_s, sync::syncActionName(c.action) } });
    return out;
}

QVariantMap WorkspaceSync::appStatus() const
{
    return { { u"state"_s, state() }, { u"text"_s, statusText() }, { u"lastSync"_s, lastSync() } };
}

// ---- actions ------------------------------------------------------------------------------------

void WorkspaceSync::syncNow()
{
    if (!m_scheduler) {
        if (m_nextcloud.enabled && !m_nextcloud.isComplete())
            Q_EMIT errorOccurred(tr("Nextcloud is not set up completely: server, user and app password are needed."));
        return;
    }
    m_scheduler->syncNow();
}

void WorkspaceSync::confirmDeletions(const QStringList &expectedPaths)
{
    if (!m_scheduler || !needsConfirmation())
        return;
    // The list may have changed since the user looked at it (another run planned other
    // deletions while the dialog was open): never confirm what was not shown.
    const sync::SyncStatus status = m_scheduler->status();
    QStringList pending;
    for (const sync::SyncChange &c : status.lastReport->pendingDeletions)
        pending.append(c.path);
    QStringList expected = expectedPaths;
    pending.sort();
    expected.sort();
    if (!expectedPaths.isEmpty() && pending != expected) {
        Q_EMIT errorOccurred(tr("The list of files to delete has changed. Check it again."));
        Q_EMIT statusChanged();
        return;
    }
    m_scheduler->syncNowConfirmingDeletions();
}

void WorkspaceSync::cancelSync()
{
    if (m_engine && m_engine->isSyncing())
        m_engine->cancel(); // stops between files; what is done stays synced
}

void WorkspaceSync::testConnection(const QString &url, const QString &user, const QString &password,
                                   const QString &folder)
{
    const QString base = davUrlFor(url, user, folder);
    if (base.isEmpty()) {
        Q_EMIT connectionTested(false, tr("Enter the server address and the user name."));
        return;
    }
    // The stored app password belongs to the saved server and user: it is never sent to another
    // address typed into the form (a typo, a look-alike host).
    const bool sameAccount = sameNextcloudAccount(url, user, m_nextcloud.url, m_nextcloud.user);
    const QString secret = !password.isEmpty() ? password : sameAccount ? m_nextcloud.password : QString();
    if (secret.isEmpty()) {
        Q_EMIT connectionTested(false, tr("Enter an app password."));
        return;
    }
    auto *client = new sync::WebDavClient(base, user.trimmed(), secret, transport(), this);
    ++m_testing;
    Q_EMIT testingChanged();
    client->test([this, client](const sync::WebDavTestResult &result) {
        client->deleteLater();
        --m_testing;
        Q_EMIT testingChanged();
        if (result.ok) {
            Q_EMIT connectionTested(true, tr("Connected. The folder is ready for syncing."));
            return;
        }
        // A missing folder is created by the first sync: the account works.
        if (result.error.is(sync::WebDavErrorCode::NotFound)) {
            Q_EMIT connectionTested(true, tr("Connected. The folder will be created by the first sync."));
            return;
        }
        Q_EMIT connectionTested(false, errorText(sync::errorCodeName(result.error.code), result.error.message));
    });
}

// ---- Syncthing ----------------------------------------------------------------------------------

void WorkspaceSync::rebuildSyncthing()
{
    m_syncthing.reset();
    m_syncthingTimer.stop();
    m_syncthingFolderId.clear();
    ++m_syncthingGeneration;
    const bool folderMarker = m_vault && QFileInfo::exists(m_vault->rootPath() + u"/.stfolder"_s);
    m_syncthingStatus = { { u"enabled"_s, false }, { u"folder"_s, folderMarker } };
    if (m_vault) {
        SyncthingSettings settings = m_syncthingSettings;
        if (settings.apiKey.isEmpty() && m_discovery) {
            if (const auto found = discoverSyncthingConfig()) {
                settings.apiKey = found->apiKey;
                if (settings.url.isEmpty())
                    settings.url = found->url;
            }
        }
        if (!settings.apiKey.isEmpty()) {
            m_syncthing = std::make_unique<sync::SyncthingClient>(settings.apiKey, transport(), settings.url);
            m_syncthing->setHome(QDir::homePath());
            m_syncthingStatus.insert(u"enabled"_s, true);
            m_syncthingStatus.insert(u"state"_s, u"unknown"_s);
        } else if (folderMarker) {
            m_syncthingStatus.insert(u"text"_s, tr("Shared with Syncthing. Add its API key to see the sync status."));
        }
    }
    Q_EMIT syncthingChanged();
    if (m_syncthing) {
        refreshSyncthing();
        if (m_appActive)
            m_syncthingTimer.start();
    }
}

void WorkspaceSync::refreshSyncthing()
{
    if (!m_syncthing || !m_vault)
        return;
    const int generation = ++m_syncthingGeneration;
    const QPointer<WorkspaceSync> self(this);
    const auto applyStatus = [self, generation](const sync::SyncthingStatus &st, const QString &label) {
        if (!self || generation != self->m_syncthingGeneration)
            return;
        QVariantMap m = self->m_syncthingStatus;
        m.insert(u"state"_s, st.state);
        m.insert(u"completion"_s, st.completion);
        m.insert(u"needBytes"_s, st.needBytes);
        m.insert(u"globalBytes"_s, st.globalBytes);
        m.insert(u"connected"_s, st.connected.value_or(-1));
        m.insert(u"errors"_s, st.errors);
        m.insert(u"folderId"_s, st.folderId);
        m.insert(u"folderLabel"_s, label);
        m.insert(u"error"_s, st.error);
        QString text;
        if (st.state == u"offline")
            text = tr("Syncthing is not running on this device.");
        else if (st.state == u"error" || st.errors > 0)
            text = tr("Syncthing reports errors in this folder.");
        else if (st.state == u"syncing" || st.state == u"sync-preparing")
            text = tr("Syncthing is syncing (%1 %).").arg(QString::number(st.completion, 'f', 0));
        else if (st.state == u"scanning")
            text = tr("Syncthing is scanning the folder.");
        else if (st.connected && *st.connected == 0)
            text = tr("Up to date. No other device is connected right now.");
        else if (st.connected)
            text = tr("Up to date with %n device(s).", "", *st.connected);
        else
            text = tr("Up to date.");
        m.insert(u"text"_s, text);
        if (m != self->m_syncthingStatus) {
            self->m_syncthingStatus = m;
            Q_EMIT self->syncthingChanged();
        }
    };
    if (!m_syncthingFolderId.isEmpty()) {
        const QString label = m_syncthingStatus.value(u"folderLabel"_s).toString();
        m_syncthing->status(m_syncthingFolderId,
                            [applyStatus, label](const sync::SyncthingStatus &st) { applyStatus(st, label); });
        return;
    }
    m_syncthing->findFolderForPath(m_vault->rootPath(), [self, generation, applyStatus](
                                                                const std::optional<sync::SyncthingFolder> &folder,
                                                                const sync::SyncthingError &error) {
        if (!self || generation != self->m_syncthingGeneration || !self->m_syncthing)
            return;
        if (error.isError() || !folder) {
            QVariantMap m = self->m_syncthingStatus;
            m.insert(u"state"_s, error.isError() ? u"offline"_s : u"not-shared"_s);
            m.insert(u"error"_s, error.message);
            m.insert(u"text"_s, error.isError()
                                        ? (error.status == 401 || error.status == 403
                                                   ? tr("Syncthing refused the API key.")
                                                   : tr("Syncthing is not running on this device."))
                                        : tr("This notebook folder is not shared in Syncthing."));
            if (m != self->m_syncthingStatus) {
                self->m_syncthingStatus = m;
                Q_EMIT self->syncthingChanged();
            }
            return;
        }
        self->m_syncthingFolderId = folder->id;
        self->m_syncthingStatus.insert(u"folderLabel"_s, folder->label);
        const QString label = folder->label;
        self->m_syncthing->status(folder->id,
                                  [applyStatus, label](const sync::SyncthingStatus &st) { applyStatus(st, label); });
    });
}

// ---- conflicts ----------------------------------------------------------------------------------

void WorkspaceSync::refreshConflicts()
{
    QVariantList list;
    if (m_vault) {
        for (const sync::ConflictCopy &c : sync::findConflicts(m_vault->files())) {
            const QDateTime time = QDateTime::fromString(c.time, Qt::ISODate);
            list.append(QVariantMap{
                { u"original"_s, c.original },
                { u"copy"_s, c.copy },
                { u"title"_s, paths::pageTitle(c.original) },
                { u"time"_s, c.time },
                { u"when"_s, human(time) },
                { u"device"_s, c.device },
                { u"source"_s, c.source == sync::ConflictCopy::Source::Syncthing ? u"syncthing"_s : u"brulion"_s },
                { u"text"_s, sync::isSyncTextPath(c.copy) },
            });
        }
    }
    if (list == m_conflicts)
        return;
    m_conflicts = list;
    Q_EMIT conflictsChanged();
}

std::optional<QVariantMap> WorkspaceSync::conflictFor(const QString &copy) const
{
    const QString c = paths::normalize(copy);
    for (const QVariant &v : m_conflicts) {
        const QVariantMap m = v.toMap();
        if (m.value(u"copy"_s).toString() == c)
            return m;
    }
    return std::nullopt;
}

QVariantMap WorkspaceSync::conflictDetails(const QString &copy)
{
    const std::optional<QVariantMap> conflict = conflictFor(copy);
    if (!conflict || !m_vault)
        return {};
    QVariantMap out = *conflict;
    if (out.value(u"text"_s).toBool()) {
        bool ok = false;
        out.insert(u"ours"_s, m_vault->readText(out.value(u"original"_s).toString(), &ok));
        out.insert(u"originalExists"_s, ok);
        out.insert(u"theirs"_s, m_vault->readText(out.value(u"copy"_s).toString()));
    }
    return out;
}

bool WorkspaceSync::resolveConflict(const QString &original, const QString &copy, const QString &mergedText)
{
    const std::optional<QVariantMap> conflict = conflictFor(copy);
    const QString o = paths::normalize(original);
    // Only real conflict copies of this original are removed (never an ordinary note).
    if (!m_vault || !conflict || conflict->value(u"original"_s).toString() != o)
        return false;
    bool ok = false;
    const QString current = m_vault->readText(o, &ok);
    // The text being replaced stays in the history (the user is told so), also when the note
    // was edited minutes ago.
    if ((!ok || current != mergedText) && !writeTextKeepingHistory(*m_vault, o, mergedText)) {
        Q_EMIT errorOccurred(tr("Could not save %1: %2").arg(paths::pageTitle(o), m_vault->errorString()));
        return false;
    }
    return discardConflictCopy(copy);
}

bool WorkspaceSync::discardConflictCopy(const QString &copy)
{
    const std::optional<QVariantMap> conflict = conflictFor(copy);
    if (!m_vault || !conflict)
        return false;
    if (!m_vault->remove(conflict->value(u"copy"_s).toString(), ChangeSource::User)) {
        Q_EMIT errorOccurred(tr("Could not move the conflict copy to the trash: %1").arg(m_vault->errorString()));
        return false;
    }
    refreshConflicts();
    return true;
}

} // namespace brulion::shell
