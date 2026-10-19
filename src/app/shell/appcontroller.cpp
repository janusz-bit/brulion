#include "appcontroller.h"

#include "log/categories.h"

#include "appsettings.h"
#include "commandmodel.h"
#include "demovault.h"
#include "workspace.h"
#include "workspaceintegration.h"

#include "vault/vault.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaMethod>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QWindow>

#include <utility>

#ifdef Q_OS_ANDROID
#include "android/androidbridge.h"
#include <QJniEnvironment>
#include <QJniObject>
#include <QJsonArray>
#include <QtCore/qcoreapplication_platform.h>
#endif
#ifdef Q_OS_WASM
#include "wasm/wasmnotifier.h"
#include "wasm/wasmstorage.h"
#endif

#ifndef BRULION_VERSION
#define BRULION_VERSION "0.0.0"
#endif

using namespace Qt::StringLiterals;

namespace brulion::shell {

using logging::lcApp;

namespace {

AppController *g_instance = nullptr;

QString argValue(const QStringList &args, const QString &option)
{
    const qsizetype i = args.indexOf(option);
    if (i >= 0 && i + 1 < args.size())
        return args.at(i + 1);
    const QString prefix = option + u'=';
    for (const QString &arg : args) {
        if (arg.startsWith(prefix))
            return arg.mid(prefix.size());
    }
    return {};
}

QString cleanLocalPath(const QString &raw)
{
    QString path = raw.trimmed();
    if (path.startsWith(u"file:"_s))
        path = QUrl(path).toLocalFile();
    if (path.startsWith(u"~/"_s))
        path = QDir::homePath() + path.mid(1);
    if (path.isEmpty())
        return {};
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

} // namespace

AppController::AppController(const Options &options, QObject *parent)
    : QObject(parent),
      m_dataDir(options.dataDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                          : options.dataDir)
{
    if (!g_instance)
        g_instance = this;

    m_settings = std::make_unique<AppSettings>(options.settingsFile, m_dataDir);
    m_workspace = std::make_unique<Workspace>();
    m_workspace->setUiLanguage(m_settings->language());
    m_commands = std::make_unique<CommandModel>(this);
    m_syncStatus = { { u"state"_s, u"disabled"_s }, { u"text"_s, QString() }, { u"lastSync"_s, QString() } };

    connect(m_settings.get(), &AppSettings::languageChanged, this, [this] {
        // First the translation (main.cpp: DevTools::setLanguage swaps the translator), then the
        // workspace, whose texts (sync status, reminders) are rebuilt with tr() in the new language.
        Q_EMIT languageChanged(m_settings->language());
        m_workspace->setUiLanguage(m_settings->language());
    });
    connect(m_workspace.get(), &Workspace::navigationRequested, this,
            [this](const QVariantMap &route, bool newPane) { navigate(route, newPane); });
    connect(m_workspace.get(), &Workspace::errorOccurred, this,
            [this](const QString &message) { toast(message, u"error"_s); });
    connect(m_workspace.get(), &Workspace::noticeOccurred, this,
            [this](const QString &message) { toast(message, u"info"_s); });
    connect(m_workspace.get(), &Workspace::pathRenamed, this, &AppController::onPathRenamed);
    connect(m_workspace.get(), &Workspace::pathRemoved, this, &AppController::onPathRemoved);

    // Developer / screenshot options.
    const QStringList &args = options.arguments;
    m_devVault = cleanLocalPath(argValue(args, u"--vault"_s));
    m_devDemo = args.contains(u"--demo"_s);
    m_devOnboarding = args.contains(u"--onboarding"_s);
    m_startHidden = args.contains(u"--hidden"_s);
    m_startCapture = args.contains(u"--capture"_s);
    m_startToday = args.contains(u"--today"_s);
    m_devPopup = argValue(args, u"--popup"_s);
    const QString route = argValue(args, u"--route"_s);
    if (route.startsWith(u'{'))
        m_devRoute = normalizeRoute(QJsonDocument::fromJson(route.toUtf8()).object().toVariantMap());
    else if (!route.isEmpty())
        m_devRoute = normalizeRoute({ { u"view"_s, route } });

#ifdef Q_OS_ANDROID
    auto *bridge = platform::AndroidBridge::instance();
    connect(bridge, &platform::AndroidBridge::permissionsChanged, this, [this] {
        Q_EMIT permissionsChanged();
        // Granted "All files access" in the system settings: continue opening the vault.
        if (!m_pendingVault.isEmpty() && platform::AndroidBridge::instance()->hasAllFilesAccess()) {
            const QString path = m_pendingVault;
            m_pendingVault.clear();
            Q_EMIT pendingVaultPathChanged();
            openVault(path);
        }
    });
#endif
#ifdef Q_OS_WASM
    m_wasmNotifier = new platform::WasmNotifier(this);
    connect(m_wasmNotifier, &platform::WasmNotifier::permissionChanged, this, &AppController::permissionsChanged);
    connect(m_wasmNotifier, &platform::WasmNotifier::activated, this,
            [this](const QString &, const QString &path, int line) {
                if (!path.isEmpty())
                    openNotification(path, line);
            });
#endif

    // Reminders (the platform's notifier) and sync (Nextcloud, Syncthing) of the Workspace,
    // configured from the settings; fills the sync seam below (workspaceintegration.h).
    integrateWorkspace(this);
}

AppController::~AppController()
{
    m_workspace->close();
    if (g_instance == this)
        g_instance = nullptr;
}

AppController *AppController::instance()
{
    return g_instance;
}

void AppController::setStatus(const QString &status, const QString &error)
{
    if (m_status == status && m_error == error)
        return;
    m_status = status;
    m_error = error;
    Q_EMIT statusChanged();
}

void AppController::start()
{
    if (m_started)
        return;
    m_started = true;
#ifdef Q_OS_WASM
    platform::WasmNotifier::Activation activation;
    if (platform::WasmNotifier::takeLaunchActivation(&activation) && !activation.path.isEmpty())
        openNotification(activation.path, activation.line);
#endif
    if (m_devDemo && m_devVault.isEmpty()) {
        openDemoVault();
        return;
    }
    if (!m_devVault.isEmpty()) {
        if (m_devDemo) {
            setStatus(u"loading"_s);
            scheduleOpening(m_devVault, true);
        } else {
            openVault(m_devVault);
        }
        return;
    }
    if (m_devOnboarding) {
        setStatus(u"onboarding"_s);
        return;
    }
    const QString current = m_settings->currentVault();
    if (current.isEmpty()) {
        setStatus(u"onboarding"_s);
        return;
    }
#ifdef Q_OS_ANDROID
    // "All files access" was taken back in the system settings: the folder on shared storage
    // may look missing now. Show onboarding waiting for the permission (its card offers the
    // system settings; the notebook opens when it is granted) instead of calling it gone.
    if (needsStoragePermission(current) && !platform::AndroidBridge::instance()->hasAllFilesAccess()) {
        m_pendingVault = current;
        Q_EMIT pendingVaultPathChanged();
        setStatus(u"onboarding"_s);
        return;
    }
#endif
    if (!isWasm() && !QFileInfo(current).isDir()) {
        // An unmounted SD card or a deleted folder: never silently create an empty vault there.
        setStatus(u"onboarding"_s);
        toast(tr("The notebook folder %1 is not available.").arg(QDir::toNativeSeparators(current)), u"warning"_s);
        return;
    }
    openVault(current);
}

// ---- Navigation -------------------------------------------------------------------------------

QString AppController::currentView() const
{
    return m_history.current().value(u"view"_s).toString();
}

bool AppController::atHome() const
{
    return sameRoute(m_history.current(), m_history.home());
}

void AppController::routeChanged()
{
    const QVariantMap route = m_history.current();
    if (route.value(u"view"_s).toString() == u"page")
        m_workspace->notePageOpened(route.value(u"path"_s).toString());
    Q_EMIT currentRouteChanged();
    Q_EMIT historyChanged();
}

void AppController::navigate(const QVariantMap &route, bool newPane)
{
    const QVariantMap r = normalizeRoute(route);
    if (r.isEmpty()) {
        qCWarning(lcApp) << "Ignoring invalid route" << route;
        return;
    }
    if (newPane && m_sidePanelAllowed) {
        if (!sameRoute(r, m_sideRoute)) {
            m_sideRoute = r;
            Q_EMIT sidePanelRouteChanged();
        }
        setSidePanelVisible(true);
        return;
    }
    if (m_history.navigate(r))
        routeChanged();
}

void AppController::replaceRoute(const QVariantMap &route)
{
    const QVariantMap before = m_history.current();
    if (m_history.replace(route) && !sameRoute(before, m_history.current()))
        Q_EMIT currentRouteChanged();
}

bool AppController::back()
{
    if (!m_history.back())
        return false;
    routeChanged();
    return true;
}

bool AppController::forward()
{
    if (!m_history.forward())
        return false;
    routeChanged();
    return true;
}

void AppController::goHome()
{
    navigate(m_history.home());
}

bool AppController::handleBackKey()
{
    if (back())
        return true;
    if (!atHome()) {
        goHome();
        return true;
    }
    return false;
}

void AppController::setSidePanelVisible(bool visible)
{
    if (m_sidePanelVisible == visible)
        return;
    m_sidePanelVisible = visible;
    Q_EMIT sidePanelVisibleChanged();
}

void AppController::setSidePanelAllowed(bool allowed)
{
    if (m_sidePanelAllowed == allowed)
        return;
    m_sidePanelAllowed = allowed;
    Q_EMIT sidePanelAllowedChanged();
}

void AppController::clearSidePanelRoute()
{
    if (m_sideRoute.isEmpty())
        return;
    m_sideRoute.clear();
    Q_EMIT sidePanelRouteChanged();
}

void AppController::openPage(const QString &path, int line, bool newPane)
{
    QVariantMap route{ { u"view"_s, u"page"_s }, { u"path"_s, path } };
    if (line >= 0)
        route.insert(u"line"_s, line);
    navigate(route, newPane);
}

void AppController::openJournal(const QString &date)
{
    const QDate d = QDate::fromString(date, Qt::ISODate);
    if (!d.isValid())
        return;
    // Looking at a day never creates its note: the editor creates it with the first change, from
    // the journal template (PageController::startNewPage), like the journal stream.
    const QString path = m_workspace->journalFileFor(date);
    QVariantMap route{ { u"view"_s, u"journal"_s }, { u"date"_s, date } };
    if (!path.isEmpty())
        route.insert(u"path"_s, path);
    navigate(route);
}

void AppController::openToday()
{
    // A launcher / second launch asks while the notebook is still opening: do it once it is.
    if (m_status == u"boot" || m_status == u"loading") {
        m_startToday = true;
        return;
    }
    if (m_status == u"ready")
        openJournal(today());
}

QString AppController::today(int offsetDays) const
{
    return now().date().addDays(offsetDays).toString(Qt::ISODate);
}

void AppController::openTag(const QString &tag)
{
    QString t = tag.trimmed();
    if (t.startsWith(u'#'))
        t.remove(0, 1);
    navigate({ { u"view"_s, u"tag"_s }, { u"tag"_s, t } });
}

void AppController::search(const QString &query)
{
    navigate({ { u"view"_s, u"search"_s }, { u"query"_s, query } });
}

void AppController::openNotification(const QString &path, int line)
{
    m_pendingNotePath = path;
    m_pendingNoteLine = line;
    showWindow();
    if (m_status == u"ready")
        applyPendingNotification();
}

void AppController::applyPendingNotification()
{
    if (m_pendingNotePath.isEmpty())
        return;
    const QString path = m_pendingNotePath;
    const int line = m_pendingNoteLine;
    m_pendingNotePath.clear();
    m_pendingNoteLine = -1;
    if (m_workspace->exists(path))
        openPage(path, line);
    else
        toast(tr("The note of this reminder no longer exists."), u"warning"_s);
}

void AppController::onPathRenamed(const QString &oldPath, const QString &newPath)
{
    const auto rewrite = [&](QVariantMap &route) {
        const QString path = route.value(u"path"_s).toString();
        if (path == oldPath) {
            route.insert(u"path"_s, newPath);
            return true;
        }
        if (path.startsWith(oldPath + u'/')) {
            route.insert(u"path"_s, QString(newPath + path.mid(oldPath.size())));
            return true;
        }
        return false;
    };
    bool changed = false;
    m_history.transform([&](QVariantMap &route) { changed |= rewrite(route); });
    if (changed)
        Q_EMIT currentRouteChanged();
    if (rewrite(m_sideRoute))
        Q_EMIT sidePanelRouteChanged();
}

void AppController::onPathRemoved(const QString &path)
{
    // Pages that no longer exist leave the history (their version history stays reachable).
    const auto gone = [&](const QVariantMap &route) {
        return route.value(u"view"_s).toString() == u"page" && route.value(u"path"_s).toString() == path;
    };
    const QVariantMap before = m_history.current();
    if (m_history.removeIf(gone)) {
        if (!sameRoute(before, m_history.current()))
            routeChanged();
        else
            Q_EMIT historyChanged();
    }
    if (gone(m_sideRoute)) {
        m_sideRoute.clear();
        Q_EMIT sidePanelRouteChanged();
    }
}

// ---- Platform ---------------------------------------------------------------------------------

QString AppController::platform() const
{
#if defined(Q_OS_ANDROID)
    return u"android"_s;
#elif defined(Q_OS_WASM)
    return u"wasm"_s;
#else
    return u"linux"_s;
#endif
}

bool AppController::isAndroid() const
{
#ifdef Q_OS_ANDROID
    return true;
#else
    return false;
#endif
}

bool AppController::isWasm() const
{
#ifdef Q_OS_WASM
    return true;
#else
    return false;
#endif
}

bool AppController::isDesktop() const
{
    return !isAndroid() && !isWasm();
}

QString AppController::version() const
{
    return QStringLiteral(BRULION_VERSION);
}

void AppController::setTrayAvailable(bool available)
{
    if (m_trayAvailable == available)
        return;
    m_trayAvailable = available;
    Q_EMIT trayAvailableChanged();
}

// ---- Vaults -----------------------------------------------------------------------------------

QString AppController::defaultVaultPath() const
{
#if defined(Q_OS_ANDROID)
    return u"/storage/emulated/0/Brulion"_s;
#elif defined(Q_OS_WASM)
    return platform::WasmStorage::vaultRoot();
#else
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir::cleanPath((docs.isEmpty() ? QDir::homePath() : docs) + u"/Brulion"_s);
#endif
}

QString AppController::appStorageVaultPath() const
{
#ifdef Q_OS_WASM
    return defaultVaultPath();
#else
    return QDir::cleanPath(m_dataDir + u"/Brulion"_s);
#endif
}

QString AppController::demoVaultPath() const
{
#ifdef Q_OS_WASM
    // The browser has one vault folder (IndexedDB); the sample notes go there.
    return defaultVaultPath();
#else
    return QDir::cleanPath(m_dataDir + u"/Przykładowy brulion"_s);
#endif
}

QString AppController::deviceStatePath(const QString &vaultPath) const
{
    return QDir::cleanPath(m_dataDir + u"/vaults/"_s + Vault::idForPath(vaultPath));
}

QString AppController::localPath(const QUrl &url) const
{
    return url.isLocalFile() ? QDir::cleanPath(url.toLocalFile()) : cleanLocalPath(url.toString());
}

bool AppController::needsStoragePermission(const QString &rawPath) const
{
#ifdef Q_OS_ANDROID
    const QString path = cleanLocalPath(rawPath);
    // App-private storage needs no permission; everything else on the device does.
    return !path.startsWith(m_dataDir) && !path.startsWith(QDir::homePath());
#else
    Q_UNUSED(rawPath);
    return false;
#endif
}

void AppController::openVault(const QString &rawPath)
{
    const QString path = cleanLocalPath(rawPath);
    if (path.isEmpty()) {
        toast(tr("Choose a folder for the notebook."), u"warning"_s);
        return;
    }
#ifdef Q_OS_ANDROID
    if (needsStoragePermission(path) && !platform::AndroidBridge::instance()->hasAllFilesAccess()) {
        m_pendingVault = path;
        Q_EMIT pendingVaultPathChanged();
        setStatus(u"onboarding"_s);
        platform::AndroidBridge::instance()->requestAllFilesAccess();
        return;
    }
#endif
    if (!m_pendingVault.isEmpty()) {
        m_pendingVault.clear();
        Q_EMIT pendingVaultPathChanged();
    }
    setStatus(u"loading"_s);
    // Let the UI show the loading state before the folder is scanned.
    scheduleOpening(path, false);
}

void AppController::openVaultAt(const QString &path, const QVariantMap &route)
{
    m_routeAfterOpen = normalizeRoute(route);
    openVault(path);
}

QString AppController::docsVaultPath() const
{
#ifdef Q_OS_WASM
    return {}; // one vault folder in the browser
#else
    return QDir::cleanPath(m_dataDir + u"/Dokumentacja Brulionu"_s);
#endif
}

void AppController::openDocsVault()
{
    if (docsVaultPath().isEmpty())
        return;
    // Always the table of contents, not the journal: this notebook is for reading.
    m_routeAfterOpen = normalizeRoute({ { u"view"_s, u"page"_s }, { u"path"_s, docsWelcomePage() } });
    setStatus(u"loading"_s);
    scheduleOpening(docsVaultPath(), true);
}

bool AppController::inDocsVault() const
{
    return !docsVaultPath().isEmpty() && QDir::cleanPath(m_settings->currentVault()) == docsVaultPath();
}

void AppController::toggleDocsVault()
{
    if (!inDocsVault()) {
        openDocsVault();
        return;
    }
    const QVariantList recent = m_settings->recentVaults();
    for (const QVariant &v : recent) {
        const QString path = v.toMap().value(u"path"_s).toString();
        if (QDir::cleanPath(path) != docsVaultPath()) {
            openVault(path);
            return;
        }
    }
    closeVault(); // no other notebook known: the start screen
}

void AppController::openDemoVault()
{
    m_routeAfterOpen.clear(); // the sample notebook opens on its welcome page
    setStatus(u"loading"_s);
    scheduleOpening(demoVaultPath(), true);
}

void AppController::scheduleOpening(const QString &path, bool seedDemo)
{
    const auto open = [this, path, seedDemo] { finishOpening(path, seedDemo); };
    // At start-up the window has not drawn anything yet: show it (the splash) first, then scan
    // the vault — with thousands of notes that takes a moment, more so on a phone. Later (the
    // onboarding, switching notebooks), without a window (tests, started hidden to the tray)
    // or without a QGuiApplication: on the next turn of the event loop, as always.
    QWindow *window = nullptr;
    if (!m_framePresented && qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        const QWindowList windows = QGuiApplication::topLevelWindows();
        for (QWindow *w : windows) {
            if (w->isVisible() && w->metaObject()->indexOfSignal("frameSwapped()") >= 0) {
                window = w;
                break;
            }
        }
    }
    if (!window) {
        QTimer::singleShot(0, this, open);
        return;
    }
    // Whichever comes first: the window's first frame (QQuickWindow::frameSwapped, emitted by
    // the render thread; connected by name: this library does not link Qt Quick) or a timeout
    // for a window that is never exposed (minimised, on a hidden workspace).
    constexpr int kFirstFrameTimeoutMs = 1000;
    auto done = std::make_shared<bool>(false);
    const auto once = [done, open] {
        if (!std::exchange(*done, true))
            open();
    };
    m_afterFirstFrame.append(once);
    if (!m_frameConnection) {
        const QMetaObject *meta = window->metaObject();
        const QMetaMethod frameSwapped = meta->method(meta->indexOfSignal("frameSwapped()"));
        const QMetaMethod slot = staticMetaObject.method(staticMetaObject.indexOfSlot("onFirstFrame()"));
        m_frameConnection = connect(window, frameSwapped, this, slot, Qt::QueuedConnection);
    }
    QTimer::singleShot(kFirstFrameTimeoutMs, this, once);
}

void AppController::onFirstFrame()
{
    m_framePresented = true;
    disconnect(m_frameConnection);
    const QList<std::function<void()>> waiting = std::exchange(m_afterFirstFrame, {});
    for (const std::function<void()> &fn : waiting)
        fn();
}

void AppController::finishOpening(const QString &path, bool seedDemo)
{
    // Called a turn after the status left "ready" (openVault() and friends wait one): the old
    // shell is gone by now — its editors saved into the old vault before it closes below.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QString error;
    if (!m_workspace->open(path, deviceStatePath(path), &error)) {
        m_routeAfterOpen.clear();
        m_failedPath = path;
        m_failedSeedDemo = seedDemo;
        setStatus(u"error"_s, tr("Could not open the notebook %1: %2").arg(QDir::toNativeSeparators(path), error));
        return;
    }
    m_failedPath.clear();
    bool showWelcome = false;
    bool docs = false;
    if (seedDemo && m_workspace->vault()->markdownFiles().isEmpty()) {
        docs = !docsVaultPath().isEmpty() && QDir::cleanPath(path) == docsVaultPath();
        const int written = docs ? seedDocsVault(*m_workspace->vault())
                                 : seedDemoVault(*m_workspace->vault(), now());
        if (written < 0)
            toast(tr("Could not write all sample notes: %1").arg(m_workspace->vault()->errorString()), u"error"_s);
        showWelcome = true;
    }
    m_settings->addRecentVault(m_workspace->rootPath());
    m_history.reset();
    m_sideRoute.clear();
    Q_EMIT sidePanelRouteChanged();

    // The first route before "ready": the shell is built then and shows it right away.
    const QVariantMap first = initialRoute();
    if (const QVariantMap after = std::exchange(m_routeAfterOpen, {}); !after.isEmpty()) {
        m_startToday = false;
        m_history.navigate(after);
    } else if (m_startToday) {
        m_startToday = false;
        const QString date = today();
        m_history.navigate(
                {{u"view"_s, u"journal"_s}, {u"date"_s, date}, {u"path"_s, m_workspace->journalFileFor(date)}});
    } else if (!first.isEmpty())
        m_history.navigate(first);
    else if (showWelcome && m_workspace->exists(docs ? docsWelcomePage() : demoWelcomePage()))
        m_history.navigate({ { u"view"_s, u"page"_s }, { u"path"_s, docs ? docsWelcomePage() : demoWelcomePage() } });
    setStatus(u"ready"_s);
    routeChanged();
    applyPendingNotification();
    if (m_startCapture) {
        // After the shell (which owns the dialog) has been created for "ready".
        m_startCapture = false;
        QTimer::singleShot(0, this, [this] { quickCapture(); });
    }
}

QVariantMap AppController::initialRoute() const
{
    return m_devRoute;
}

void AppController::closeVault()
{
    m_settings->setCurrentVault(QString());
    m_history.reset();
    m_sideRoute.clear();
    Q_EMIT sidePanelRouteChanged();
    routeChanged();
    // The status change unloads the shell; QML deletes it (and the editors in it) a moment
    // later, and an editor saves its last keystrokes when it goes. Close the vault only after
    // that, or those words would be lost. A vault opened in the meantime (openVault() also
    // waits a turn) closes this one by itself.
    setStatus(u"onboarding"_s);
    ++m_closeGeneration;
    QTimer::singleShot(0, this, [this, generation = m_closeGeneration] {
        // The deletions requested before this turn (the old shell) happen first, here at the top
        // of the event loop where nothing of the shell is running any more.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (generation == m_closeGeneration && m_status != u"ready" && m_status != u"loading")
            m_workspace->close();
    });
}

void AppController::retryOpenVault()
{
    if (m_failedPath.isEmpty()) {
        openVault(m_settings->currentVault());
        return;
    }
    const QString path = m_failedPath;
    const bool seed = m_failedSeedDemo;
    if (seed) {
        setStatus(u"loading"_s);
        scheduleOpening(path, true);
    } else {
        openVault(path);
    }
}

void AppController::showOnboarding()
{
    setStatus(u"onboarding"_s);
}

// ---- Permissions ------------------------------------------------------------------------------

QVariantMap AppController::permissions() const
{
    QVariantMap p{
        { u"allFilesAccess"_s, true },
        { u"notifications"_s, true },
        { u"exactAlarms"_s, true },
        { u"fullScreenAlarms"_s, true },
        { u"storagePermissionNeeded"_s, false },
        { u"notificationsSupported"_s, true },
        { u"exactAlarmsNeeded"_s, false },
        { u"fullScreenNeeded"_s, false },
    };
#if defined(Q_OS_ANDROID)
    auto *bridge = platform::AndroidBridge::instance();
    p.insert(u"allFilesAccess"_s, bridge->hasAllFilesAccess());
    p.insert(u"notifications"_s, bridge->notificationPermission());
    p.insert(u"exactAlarms"_s, bridge->canScheduleExact());
    p.insert(u"fullScreenAlarms"_s, bridge->canUseFullScreenIntent());
    p.insert(u"storagePermissionNeeded"_s, true);
    p.insert(u"exactAlarmsNeeded"_s, true);
    p.insert(u"fullScreenNeeded"_s, true);
#elif defined(Q_OS_WASM)
    using Permission = platform::WasmNotifier::Permission;
    const Permission state = m_wasmNotifier->permission();
    p.insert(u"notifications"_s, state == Permission::Granted);
    p.insert(u"notificationsSupported"_s, state != Permission::Unsupported);
#endif
    return p;
}

void AppController::requestPermission(const QString &name)
{
#if defined(Q_OS_ANDROID)
    auto *bridge = platform::AndroidBridge::instance();
    if (name == u"allFilesAccess")
        bridge->requestAllFilesAccess();
    else if (name == u"notifications")
        bridge->requestNotificationPermission();
    else if (name == u"exactAlarms")
        bridge->requestExactAlarms();
    else if (name == u"fullScreenAlarms")
        bridge->requestFullScreenIntent();
#elif defined(Q_OS_WASM)
    if (name == u"notifications")
        m_wasmNotifier->requestPermission();
#else
    Q_UNUSED(name);
#endif
    Q_EMIT permissionsChanged();
}

void AppController::refreshPermissions()
{
    Q_EMIT permissionsChanged();
}

void AppController::setTestNotificationHandler(std::function<void(const QString &, const QString &)> handler)
{
    m_testNotificationHandler = std::move(handler);
}

void AppController::sendTestNotification()
{
    const QString title = tr("Brulion");
    const QString body = tr("Reminders work. This is how they will look.");
    if (m_testNotificationHandler) {
        m_testNotificationHandler(title, body);
        return;
    }
#if defined(Q_OS_ANDROID)
    // There is no "show now" on Android: arm a reminder a few seconds ahead. scheduleReminders()
    // replaces the armed set, so this fallback is only right while nothing else arms reminders;
    // the reminders integration takes the test over with setTestNotificationHandler().
    QJsonObject item{ { u"id"_s, 2'000'000'001 },
                      { u"title"_s, title },
                      { u"body"_s, body },
                      { u"at"_s, QDateTime::currentMSecsSinceEpoch() + 3000 },
                      { u"alarm"_s, false },
                      { u"path"_s, QString() },
                      { u"line"_s, 0 } };
    const auto result = platform::AndroidBridge::instance()->scheduleReminders(QJsonArray{ item });
    if (!result.ok)
        toast(tr("Could not schedule the notification: %1").arg(result.error), u"error"_s);
    else
        toast(tr("A test notification will appear in a few seconds."), u"info"_s);
#elif defined(Q_OS_WASM)
    if (!m_wasmNotifier->showNow(u"brulion-test"_s, title, body))
        toast(tr("The browser does not allow notifications for Brulion."), u"warning"_s);
#else
    // The tray shows it; without a tray (and before the reminders integration brings D-Bus
    // notifications) nothing would appear at all — say so instead of failing silently.
    if (m_trayAvailable)
        Q_EMIT notificationRequested(title, body);
    else
        toast(tr("No system tray on this desktop: notifications cannot be shown yet."), u"warning"_s);
#endif
}

// ---- Quick capture, window, app -----------------------------------------------------------------

void AppController::quickCapture()
{
    showWindow();
    // Still opening the notebook (a launcher action right at start, a second launch): the shell
    // that owns the dialog does not exist yet — open it once the notebook is ready.
    if (m_status == u"boot" || m_status == u"loading") {
        m_startCapture = true;
        return;
    }
    requestUiAction(u"capture"_s);
}

bool AppController::capture(const QString &text)
{
    if (!m_workspace->isOpen())
        return false;
    // A date in the text makes it a task with a reminder / due date (workspacecapture.h).
    return !m_workspace->quickCapture(text).isEmpty();
}

void AppController::showWindow()
{
    Q_EMIT showWindowRequested();
}

void AppController::quit()
{
    if (!m_quitting) {
        m_quitting = true;
        Q_EMIT quittingChanged();
    }
    m_settings->sync();
    Q_EMIT quitRequested();
}

bool AppController::moveToBackground()
{
#ifdef Q_OS_ANDROID
    // Back at home leaves the app the way Android 12+ leaves a root activity: the task moves to
    // the back and the process stays (warm start, nothing lost). Letting Qt end the process
    // instead (last window closed -> exit) races Android's render threads, which then abort on
    // destroyed mutexes ("pthread_mutex_lock called on a destroyed mutex", SIGABRT).
    auto done = QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (QNativeInterface::QAndroidApplication::isActivityContext() && activity.isValid()) {
            const jboolean moved = activity.callMethod<jboolean>("moveTaskToBack", "(Z)Z", jboolean(true));
            Q_UNUSED(moved);
        }
        QJniEnvironment().checkAndClearExceptions();
        return QVariant();
    });
    Q_UNUSED(done);
    return true;
#else
    return false;
#endif
}

void AppController::hiddenToTray()
{
    static const QString key = u"app/trayHintShown"_s;
    if (m_settings->value(key, false).toBool())
        return;
    m_settings->setValue(key, true);
    Q_EMIT notificationRequested(tr("Brulion is still running"),
                                 tr("Reminders keep working. To quit, use “Quit” in the tray icon's menu."));
}

void AppController::toast(const QString &text, const QString &kind)
{
    Q_EMIT toastRequested(text, kind);
}

void AppController::requestUiAction(const QString &action)
{
    Q_EMIT uiActionRequested(action);
}

// ---- Sync seam --------------------------------------------------------------------------------

void AppController::setSyncAvailable(bool available)
{
    if (m_syncAvailable == available)
        return;
    m_syncAvailable = available;
    Q_EMIT syncStatusChanged();
}

void AppController::setSyncStatus(const QVariantMap &status)
{
    if (m_syncStatus == status)
        return;
    m_syncStatus = status;
    Q_EMIT syncStatusChanged();
}

void AppController::requestSync()
{
    if (m_syncAvailable)
        Q_EMIT syncRequested();
}

// ---- Clock ------------------------------------------------------------------------------------

void AppController::setClock(std::function<QDateTime()> clock)
{
    m_clock = clock;
    m_workspace->setClock(std::move(clock));
}

QDateTime AppController::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

} // namespace brulion::shell
