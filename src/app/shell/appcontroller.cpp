#include "appcontroller.h"

#include "folderchoices.h"

#include "log/categories.h"

#include "appsettings.h"
#include "commandmodel.h"
#include "demovault.h"
#include "traypopup.h"
#include "workspace.h"
#include "workspaceintegration.h"

#include "autostart.h"  // src/platform
#include "traywindow.h" // src/platform

#include "notebook/session.h"
#include "notebook/share.h"
#include "periodic/periodic.h"
#include "sync/syncengine.h"
#include "vault/localstore.h"
#include "vault/vault.h"
#include "vault/vaultconfig.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLoggingCategory>
#include <QMetaMethod>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QWindow>

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

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

#ifndef BRULION_VERSION
#define BRULION_VERSION "0.0.0"
#endif

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace brulion::shell {

using logging::lcApp;

namespace {

AppController *g_instance = nullptr;

/// The tray calendar's distance from the taskbar and the screen's sides, as Windows' own flyouts.
constexpr int kTrayPopupMargin = 8;

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

/// An activation token as a launcher hands it over (XDG_ACTIVATION_TOKEN): 1–512 printable ASCII
/// characters, no spaces. Anything else from the socket is ignored.
bool isActivationToken(const QString &token)
{
    if (token.isEmpty() || token.size() > 512)
        return false;
    return std::ranges::all_of(token, [](QChar c) { return c.unicode() > 0x20 && c.unicode() < 0x7f; });
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

/// The folder of the documentation notebook in `language` inside the app data folder `dataDir`.
QString docsNotebookPath(const QString &dataDir, const QString &language)
{
    return QDir::cleanPath(dataDir + u'/' + notebook::docsNotebookFolder(language));
}

/// `vault` (the notebook a route belongs to; "" for any) is the notebook at `root`.
bool sameNotebook(const QString &vault, const QString &root)
{
    return vault.isEmpty() || (!root.isEmpty() && Vault::idForPath(vault) == Vault::idForPath(root));
}

/// `route`'s `key` after `oldPath` became `newPath`: that path or one inside it (a folder's).
/// Whether it changed.
bool rebasePath(QVariantMap &route, const QString &key, const QString &oldPath, const QString &newPath)
{
    const QString value = route.value(key).toString();
    if (value == oldPath)
        route.insert(key, newPath);
    else if (value.startsWith(oldPath + u'/'))
        route.insert(key, QString(newPath + value.mid(oldPath.size())));
    else
        return false;
    return true;
}

/// `member = value` when they differ; whether it changed (the caller signals it then).
template<typename T>
bool assignIfChanged(T &member, const std::type_identity_t<T> &value)
{
    if (member == value)
        return false;
    member = value;
    return true;
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
    // The Nextcloud switch and folder of 0.3.0 and older (one for all notebooks) belong to the
    // notebook that was open then — not the documentation: then the last notebook before it.
    const QString current = m_settings->currentVault();
    m_settings->recordSharedNextcloudOwner(isDocsPath(current) ? lastUserVault() : current);
    m_workspace = std::make_unique<Workspace>();
    m_workspace->setUiLanguage(m_settings->language());
    m_shortcuts = std::make_unique<ShortcutMap>(m_settings.get());
    m_commands = std::make_unique<CommandModel>(this);
    m_tabsSaveTimer = new QTimer(this);
    m_tabsSaveTimer->setSingleShot(true);
    m_tabsSaveTimer->setInterval(500ms);
    connect(m_tabsSaveTimer, &QTimer::timeout, this, &AppController::saveTabs);
    m_syncStatus = { { u"state"_s, u"disabled"_s }, { u"text"_s, QString() }, { u"lastSync"_s, QString() } };

    connect(m_settings.get(), &AppSettings::currentVaultChanged, this, &AppController::inDocsVaultChanged);
    connect(m_settings.get(), &AppSettings::languageChanged, this, [this] {
        // First the translation (main.cpp: DevTools::setLanguage swaps the translator), then the
        // workspace, whose texts (sync status, reminders) are rebuilt with tr() in the new language.
        Q_EMIT languageChanged(m_settings->language());
        m_workspace->setUiLanguage(m_settings->language());
        // The sample and documentation notebooks of the new language (their own folders); a
        // --locale of this run no longer counts, as for the translation.
        m_localeOverride.clear();
        Q_EMIT notebookPathsChanged();
    });
    // The built-in extensions the settings switch on (the gym), their messages as toasts.
    m_workspace->extensions()->setEnabled(m_settings->enabledExtensions());
    connect(m_settings.get(), &AppSettings::gymEnabledChanged, this,
            [this] { m_workspace->extensions()->setEnabled(m_settings->enabledExtensions()); });
    connect(m_workspace->extensions(), &WorkspaceExtensions::message, this,
            [this](const QString &text, const QString &kind) { toast(text, kind); });
    connect(m_workspace.get(), &Workspace::navigationRequested, this, &AppController::navigate);
    connect(m_workspace.get(), &Workspace::errorOccurred, this,
            [this](const QString &message) { toast(message, u"error"_s); });
    connect(m_workspace.get(), &Workspace::noticeOccurred, this,
            [this](const QString &message) { toast(message, u"info"_s); });
    connect(m_workspace.get(), &Workspace::pathRenamed, this, &AppController::onPathRenamed);
    connect(m_workspace.get(), &Workspace::folderRenamed, this, &AppController::onFolderRenamed);
    connect(m_workspace.get(), &Workspace::pathRemoved, this, &AppController::onPathRemoved);

    // Developer / screenshot options.
    const QStringList &args = options.arguments;
    m_devVault = cleanLocalPath(argValue(args, u"--vault"_s));
    m_devDemo = args.contains(u"--demo"_s);
    // The language of the sample and documentation notebooks follows the UI language: --locale
    // for this run (main.cpp: the translation), else the setting; --sample-language pins it (tests
    // written against one language's sample notes).
    for (const auto &[option, value] : {std::pair{u"--locale"_s, &m_localeOverride},
                                        std::pair{u"--sample-language"_s, &m_sampleLanguage}}) {
        if (const QString language = argValue(args, option); language == u"pl" || language == u"en")
            *value = language;
    }
    m_devOnboarding = args.contains(u"--onboarding"_s);
    m_devIntro = args.contains(u"--intro"_s);
    connect(m_settings.get(), &AppSettings::introDoneChanged, this, &AppController::introPendingChanged);
    m_startHidden = args.contains(u"--hidden"_s);
    m_startCapture = args.contains(u"--capture"_s);
    m_startToday = args.contains(u"--today"_s);
    m_devPopup = argValue(args, u"--popup"_s);
    const QString devUpdate = argValue(args, u"--update-available"_s);
    AppUpdates::Options updates;
    updates.settings = m_settings.get();
    updates.currentVersion = version();
    // Builds without the check (BRULION_UPDATE_CHECK=OFF, e.g. F-Droid's) never tell, not even with
    // --update-available.
    updates.supported =
            AppUpdates::checkBuiltIn()
            && (!devUpdate.isEmpty() || (options.checkForUpdates && (isAndroid() || platform() == "windows"_L1)));
    updates.network = devUpdate.isEmpty();
    updates.clock = [this] { return now(); };
    m_updates = std::make_unique<AppUpdates>(updates);
    if (!devUpdate.isEmpty())
        m_updates->setLatestForThisRun(devUpdate);
    // Files opened with Brulion (--open: a file manager's "Open with"), else --share.
    if (const notebook::SharedContent opened = notebook::openedFilesShare(options.openFiles, options.workingDirectory);
        !opened.files.isEmpty())
        m_startShare = QJsonDocument(notebook::sharedContentToJson(opened)).toJson(QJsonDocument::Compact);
    else
        m_startShare = argValue(args, u"--share"_s).toUtf8();
    m_startShareQuery = argValue(args, u"--share-query"_s);
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

    // "Start when you sign in" (Windows): only the default profile, whose names main() set —
    // tests and separate profiles never write the Run key. An entry of a moved portable copy
    // points to this program again.
    m_autostart = platform::Autostart::forThisProcess(QCoreApplication::organizationName() != u"Brulion");
    if (m_autostart->refresh())
        qCInfo(lcApp).noquote() << u"Start at sign-in: the entry now starts %1"_s.arg(
                QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));

    // Reminders (the platform's notifier) and sync (Nextcloud, Syncthing) of the Workspace,
    // configured from the settings; fills the sync seam below (workspaceintegration.h).
    integrateWorkspace(this);
}

AppController::~AppController()
{
    saveTabs();
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
    // Leaving the notebook (another one opens, it closes): its tabs are saved while the shell
    // that shows them still exists — it goes with this change.
    if (m_status == u"ready" && status != u"ready")
        saveTabs();
    m_status = status;
    m_error = error;
    Q_EMIT statusChanged();
}

void AppController::start()
{
    if (m_started)
        return;
    m_started = true;
    m_updates->start();
    // The documentation is never a recent notebook (the switcher opens it), but 0.3.0–0.3.5 listed
    // it there: the start screen and the switcher would offer it until it was opened again.
    for (const QString &language : {u"en"_s, u"pl"_s})
        m_settings->forgetRecentVault(docsNotebookPath(m_dataDir, language));
#ifdef Q_OS_WASM
    platform::WasmNotifier::Activation activation;
    if (platform::WasmNotifier::takeLaunchActivation(&activation) && !activation.path.isEmpty())
        openNotification(activation.path, activation.line);
#endif
    // Shown once the notebook is ready.
    if (!m_startShare.isEmpty())
        receiveShareJson(std::exchange(m_startShare, {}));
    if (!m_startShareQuery.isEmpty())
        receiveShareQuery(std::exchange(m_startShareQuery, {}));
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
    if (m_devOnboarding || m_devIntro) {
        setStatus(u"onboarding"_s);
        return;
    }
    QString current = m_settings->currentVault();
    if (isDocsPath(current)) {
        // Left in the documentation (0.3.0–0.3.5 also stored it as the start notebook): start in
        // the user's own notebook, where quick capture, the journal and the reminders belong.
        current = lastUserVault();
        m_settings->setCurrentVault(current);
    }
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

QVariantMap AppController::currentRoute() const
{
    return m_tabs.route(m_tabs.focusedBarTab());
}

QString AppController::currentView() const
{
    return currentRoute().value(u"view"_s).toString();
}

bool AppController::canGoBack() const
{
    const RouteHistory *h = m_tabs.history(m_tabs.focusedBarTab());
    return h && h->canGoBack();
}

bool AppController::canGoForward() const
{
    const RouteHistory *h = m_tabs.history(m_tabs.focusedBarTab());
    return h && h->canGoForward();
}

bool AppController::atHome() const
{
    return sameRoute(currentRoute(), m_tabs.home());
}

AppController::TabsSnapshot AppController::beginTabs() const
{
    TabsSnapshot s;
    s.bar = m_tabs.bar();
    s.windows = m_tabs.windows();
    s.main = m_tabs.mainTab();
    s.split = m_tabs.splitTab();
    s.active = m_tabs.active();
    s.focused = m_tabs.focusedBarTab();
    s.splitFocused = m_tabs.splitFocused();
    for (const QList<int> &ids : {s.bar, s.windows}) {
        for (const int id : ids) {
            const RouteHistory *h = m_tabs.history(id);
            s.routes.insert(id, h->current());
            s.nav.insert(id, (h->canGoBack() ? 1 : 0) | (h->canGoForward() ? 2 : 0));
        }
    }
    return s;
}

void AppController::endTabs(const TabsSnapshot &before, bool pageOpened)
{
    const TabsSnapshot after = beginTabs();
    bool barChanged = before.bar != after.bar || before.main != after.main || before.split != after.split
                      || before.active != after.active || before.splitFocused != after.splitFocused;
    QList<int> routed;
    for (const QList<int> &ids : {after.bar, after.windows}) {
        for (const int id : ids) {
            const auto was = before.routes.constFind(id);
            const bool moved = was == before.routes.constEnd() || !sameRoute(*was, after.routes.value(id));
            if (moved)
                routed.append(id);
            if (after.bar.contains(id) && (moved || before.nav.value(id, -1) != after.nav.value(id)))
                barChanged = true;
        }
    }
    const bool windowsChanged = before.windows != after.windows;
    const bool currentChanged = before.focused != after.focused
                                || !sameRoute(before.routes.value(before.focused), after.routes.value(after.focused));
    const bool navChanged = currentChanged || before.nav.value(before.focused) != after.nav.value(after.focused);
    if (!barChanged && !windowsChanged && routed.isEmpty())
        return;
    // Recent pages: a page the user opened in the tab they work in.
    if (pageOpened && routed.contains(after.active)) {
        const QVariantMap route = after.routes.value(after.active);
        if (route.value(u"view"_s).toString() == u"page")
            m_workspace->notePageOpened(route.value(u"path"_s).toString());
    }
    for (const int id : std::as_const(routed))
        Q_EMIT tabRouteChanged(id);
    if (barChanged)
        Q_EMIT tabsChanged();
    if (windowsChanged)
        Q_EMIT windowTabsChanged();
    if (currentChanged)
        Q_EMIT currentRouteChanged();
    if (navChanged)
        Q_EMIT historyChanged();
    if (m_tabsSaveTimer && !m_tabsVault.isEmpty())
        m_tabsSaveTimer->start();
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
    // openIn(): this navigation (one) goes to a new tab or window.
    if (!newPane && !m_openIn.isEmpty()) {
        const QString target = std::exchange(m_openIn, {});
        if (target == u"window" && windowsAllowed())
            openWindow(r);
        else if (target == u"tab" || target == u"window")
            openTab(r);
        else
            navigate(r);
        return;
    }
    const TabsSnapshot before = beginTabs();
    m_tabs.navigate(0, r);
    endTabs(before);
}

void AppController::replaceRoute(const QVariantMap &route)
{
    const TabsSnapshot before = beginTabs();
    m_tabs.replace(0, route);
    endTabs(before, false);
}

bool AppController::back(int tab)
{
    const TabsSnapshot before = beginTabs();
    if (!m_tabs.back(tab))
        return false;
    endTabs(before);
    return true;
}

bool AppController::forward(int tab)
{
    const TabsSnapshot before = beginTabs();
    if (!m_tabs.forward(tab))
        return false;
    endTabs(before);
    return true;
}

void AppController::goHome()
{
    navigate(m_tabs.home());
}

// ---- Tabs ---------------------------------------------------------------------------------------

QVariantList AppController::tabs() const
{
    QVariantList out;
    for (const int id : m_tabs.bar()) {
        const RouteHistory *h = m_tabs.history(id);
        // "tabId" too: a delegate's required property cannot be named "id".
        out.append(QVariantMap{{u"id"_s, id},
                               {u"tabId"_s, id},
                               {u"route"_s, h->current()},
                               {u"canGoBack"_s, h->canGoBack()},
                               {u"canGoForward"_s, h->canGoForward()}});
    }
    return out;
}

QVariantList AppController::windowTabs() const
{
    QVariantList out;
    for (const int id : m_tabs.windows())
        out.append(id);
    return out;
}

int AppController::newTab(const QVariantMap &route)
{
    return openTab(route.isEmpty() ? m_tabs.home() : route);
}

int AppController::openTab(const QVariantMap &route, bool background)
{
    const TabsSnapshot before = beginTabs();
    const int id = m_tabs.open(route, !background);
    if (id == 0) {
        toast(tr("Too many tabs are open: close some first."), u"warning"_s);
        return 0;
    }
    // From a page's own window: the new tab is in the main window.
    if (const Tab *from = m_tabs.tab(before.active); !background && from && from->windowed)
        Q_EMIT showWindowRequested();
    endTabs(before);
    return id;
}

int AppController::openWindow(const QVariantMap &route)
{
    if (!windowsAllowed())
        return openTab(route);
    const TabsSnapshot before = beginTabs();
    const int id = m_tabs.openWindow(route);
    if (id == 0) {
        toast(tr("Too many tabs are open: close some first."), u"warning"_s);
        return 0;
    }
    endTabs(before);
    return id;
}

void AppController::openIn(const QString &target, const QJSValue &open)
{
    if (!open.isCallable())
        return;
    m_openIn = target;
    open.call();
    m_openIn.clear();
}

bool AppController::closeTab(int id)
{
    const TabsSnapshot before = beginTabs();
    if (!m_tabs.close(id == 0 ? m_tabs.active() : id))
        return false;
    endTabs(before, false);
    return true;
}

bool AppController::closeOtherTabs(int id)
{
    const TabsSnapshot before = beginTabs();
    if (!m_tabs.closeOthers(id))
        return false;
    endTabs(before, false);
    return true;
}

bool AppController::selectTab(int id)
{
    const TabsSnapshot before = beginTabs();
    if (!m_tabs.select(id))
        return false;
    endTabs(before);
    return true;
}

bool AppController::selectTabAt(int index)
{
    const QList<int> bar = m_tabs.bar();
    return selectTab(bar.value(index, bar.value(bar.size() - 1)));
}

bool AppController::nextTab()
{
    return tabCount() > 1 && selectTab(m_tabs.neighbour(1));
}

bool AppController::previousTab()
{
    return tabCount() > 1 && selectTab(m_tabs.neighbour(-1));
}

bool AppController::moveTab(int from, int to)
{
    const TabsSnapshot before = beginTabs();
    if (!m_tabs.move(from, to))
        return false;
    endTabs(before, false);
    return true;
}

bool AppController::splitView(int id)
{
    if (!m_sidePanelAllowed) // a wide window only (a phone shows one view)
        return false;
    const TabsSnapshot before = beginTabs();
    if (!m_tabs.split(id))
        return false;
    endTabs(before, false);
    return true;
}

bool AppController::closeSplit()
{
    const TabsSnapshot before = beginTabs();
    if (!m_tabs.closeSplit())
        return false;
    endTabs(before, false);
    return true;
}

bool AppController::focusPane(bool split)
{
    const TabsSnapshot before = beginTabs();
    if (!m_tabs.focusPane(split))
        return false;
    endTabs(before, false);
    return true;
}

void AppController::mainWindowActivated()
{
    const TabsSnapshot before = beginTabs();
    if (m_tabs.focusMainWindow())
        endTabs(before, false);
}

bool AppController::detachTab(int id)
{
    if (!windowsAllowed())
        return false;
    const TabsSnapshot before = beginTabs();
    if (!m_tabs.detach(id == 0 ? m_tabs.focusedBarTab() : id))
        return false;
    endTabs(before, false);
    return true;
}

bool AppController::attachTab(int id)
{
    const TabsSnapshot before = beginTabs();
    if (!m_tabs.attach(id))
        return false;
    endTabs(before, false);
    Q_EMIT showWindowRequested();
    return true;
}

QVariantMap AppController::tabRoute(int id) const
{
    return m_tabs.route(id);
}

bool AppController::tabCanGoBack(int id) const
{
    const RouteHistory *h = m_tabs.contains(id) ? m_tabs.history(id) : nullptr;
    return h && h->canGoBack();
}

bool AppController::tabCanGoForward(int id) const
{
    const RouteHistory *h = m_tabs.contains(id) ? m_tabs.history(id) : nullptr;
    return h && h->canGoForward();
}

QVariantMap AppController::tabViewState(int id) const
{
    const Tab *t = m_tabs.tab(id);
    return t ? t->viewState : QVariantMap();
}

void AppController::setTabViewState(int id, const QVariantMap &state)
{
    if (m_tabs.setViewState(id, state) && m_tabsSaveTimer && !m_tabsVault.isEmpty())
        m_tabsSaveTimer->start();
}

QString AppController::tabsKey(const QString &vaultPath) const
{
    return vaultPath.isEmpty() ? QString() : u"tabs/"_s + Vault::idForPath(vaultPath);
}

void AppController::saveTabs()
{
    if (m_tabsVault.isEmpty())
        return;
    Q_EMIT tabViewStatesRequested();
    if (m_tabsSaveTimer)
        m_tabsSaveTimer->stop(); // (also started again by the states just handed over)
    m_settings->setValue(tabsKey(m_tabsVault), QString::fromUtf8(m_tabs.save()));
}

void AppController::restoreTabs(const QString &vaultPath)
{
    m_tabs.restore(m_settings->value(tabsKey(vaultPath)).toString().toUtf8());
    // Pages deleted, moved or renamed while the notebook was closed (sync, another program).
    m_tabs.removeIf([this](const QVariantMap &route) {
        const QString view = route.value(u"view"_s).toString();
        return (view == u"page" || view == u"history" || view == u"drawing")
               && !m_workspace->exists(route.value(u"path"_s).toString());
    });
    m_tabsVault = vaultPath;
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
    if (assignIfChanged(m_sidePanelVisible, visible))
        Q_EMIT sidePanelVisibleChanged();
}

void AppController::setSidePanelAllowed(bool allowed)
{
    if (assignIfChanged(m_sidePanelAllowed, allowed))
        Q_EMIT sidePanelAllowedChanged();
}

void AppController::setStatusBarShown(bool shown)
{
    if (assignIfChanged(m_statusBarShown, shown))
        Q_EMIT statusBarShownChanged();
}

void AppController::setSourceModeShown(bool shown)
{
    if (assignIfChanged(m_sourceModeShown, shown))
        Q_EMIT sourceModeShownChanged();
}

void AppController::clearSidePanelRoute()
{
    if (assignIfChanged(m_sideRoute, {}))
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
    navigate({{u"view"_s, u"journal"_s}, {u"date"_s, date}, {u"path"_s, m_workspace->journalFileFor(date)}});
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

void AppController::openRoute(const QVariantMap &route, const QString &vault)
{
    const QVariantMap r = normalizeRoute(route);
    if (r.isEmpty()) {
        qCWarning(lcApp) << "Ignoring an invalid route from outside" << route;
        return;
    }
    // Still opening: once the notebook is ready (finishOpening() checks the notebook then).
    if (m_status == u"boot" || m_status == u"loading") {
        m_routeAfterOpen = r;
        m_routeAfterOpenVault = vault;
        return;
    }
    if (m_status != u"ready")
        return;
    if (!sameNotebook(vault, m_workspace->rootPath())) {
        refuseOtherNotebookRoute(vault);
        return;
    }
    navigate(m_workspace->resolveRoute(r));
}

void AppController::refuseOtherNotebookRoute(const QString &vault)
{
    toast(tr("The command line asked to open a page of another notebook (%1).").arg(QDir::toNativeSeparators(vault)),
          u"warning"_s);
}

void AppController::openPeriodicNote(const QString &period, const QString &date, int offset)
{
    const auto p = periodic::parsePeriodName(period);
    const QDate day = date.isEmpty() ? now().date() : QDate::fromString(date, Qt::ISODate);
    if (!p || !day.isValid() || m_status != u"ready")
        return;
    const QDate start = periodic::shiftPeriod(*p, day, offset);
    const QVariantMap note = m_workspace->periodicNote(periodic::periodName(*p), start.toString(Qt::ISODate));
    if (const QString path = note.value(u"path"_s).toString(); !path.isEmpty())
        openPage(path);
}

QString AppController::today(int offsetDays) const
{
    return now().date().addDays(offsetDays).toString(Qt::ISODate);
}

void AppController::openTag(const QString &tag, bool newPane)
{
    QString t = tag.trimmed();
    if (t.startsWith(u'#'))
        t.remove(0, 1);
    // The page the tag names (Workspace::resolveRoute()); the tag overview is {view: "tag"}.
    m_workspace->navigate({{u"view"_s, u"tag"_s}, {u"tag"_s, t}}, newPane);
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
    // The documentation arms no reminders: this one belongs to the user's notebook (Android kept
    // its alarms armed while the documentation was open), also when the documentation has a page
    // of the same name ("Tasks.md"). Open it there, never the documentation's.
    const bool docs = inDocsVault();
    const QString back = docs ? lastUserVault() : QString();
    if (!back.isEmpty() && openRecentVault(back))
        return; // applied once that notebook is ready
    const QString path = std::exchange(m_pendingNotePath, {});
    const int line = std::exchange(m_pendingNoteLine, -1);
    if (!docs && m_workspace->exists(path))
        openPage(path, line);
    else if (back.isEmpty()) // else openRecentVault() said that its folder is gone
        toast(tr("The note of this reminder no longer exists."), u"warning"_s);
}

void AppController::onPathRenamed(const QString &oldPath, const QString &newPath)
{
    const auto rewrite = [&](QVariantMap &route) { return rebasePath(route, u"path"_s, oldPath, newPath); };
    const TabsSnapshot before = beginTabs();
    m_tabs.transform(rewrite);
    endTabs(before, false);
    if (rewrite(m_sideRoute))
        Q_EMIT sidePanelRouteChanged();
}

void AppController::onFolderRenamed(const QString &oldPath, const QString &newPath)
{
    // Pages and other files moved with the folder already (onPathRenamed); routes naming the
    // folder itself: All pages {path}, search {folder}, import/export {folder}.
    const auto rewrite = [&](QVariantMap &route) {
        bool changed = rebasePath(route, u"folder"_s, oldPath, newPath);
        if (route.value(u"view"_s).toString() == u"allPages")
            changed = rebasePath(route, u"path"_s, oldPath, newPath) || changed;
        return changed;
    };
    const TabsSnapshot before = beginTabs();
    m_tabs.transform(rewrite);
    endTabs(before, false);
    if (rewrite(m_sideRoute))
        Q_EMIT sidePanelRouteChanged();
}

void AppController::onPathRemoved(const QString &path)
{
    // Pages that no longer exist leave the history (their version history stays reachable).
    const auto gone = [&](const QVariantMap &route) {
        return route.value(u"view"_s).toString() == u"page" && route.value(u"path"_s).toString() == path;
    };
    const TabsSnapshot before = beginTabs();
    m_tabs.removeIf(gone);
    endTabs(before, false);
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
#elif defined(Q_OS_WIN)
    return u"windows"_s;
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

QString AppController::autostartAvailability() const
{
    switch (m_autostart->availability()) {
    case platform::Autostart::Availability::Available: return u"available"_s;
    case platform::Autostart::Availability::SeparateProfile: return u"profile"_s;
    case platform::Autostart::Availability::Unsupported: break;
    }
    return u"unsupported"_s;
}

bool AppController::startAtLogin() const
{
    return m_autostart->enabled();
}

void AppController::setStartAtLogin(bool on)
{
    if (on == startAtLogin())
        return;
    QString error;
    if (m_autostart->setEnabled(on, &error)) {
        qCInfo(lcApp) << "Start at sign-in" << (on ? "on" : "off");
    } else {
        qCWarning(lcApp).noquote() << u"Start at sign-in not changed: %1"_s.arg(error);
        toast(tr("Could not change the start at sign-in."), u"error"_s);
    }
    // Also when it failed: the switch shows the state again.
    Q_EMIT startAtLoginChanged();
}

void AppController::setAutostart(std::unique_ptr<platform::Autostart> autostart)
{
    if (!autostart)
        return;
    m_autostart = std::move(autostart);
    Q_EMIT startAtLoginChanged();
}

void AppController::setTrayAvailable(bool available)
{
    if (assignIfChanged(m_trayAvailable, available))
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

QString AppController::notebooksLanguage() const
{
    if (!m_sampleLanguage.isEmpty())
        return m_sampleLanguage;
    return m_localeOverride.isEmpty() ? m_settings->language() : m_localeOverride;
}

QString AppController::demoVaultPath() const
{
#ifdef Q_OS_WASM
    // The browser has one vault folder (IndexedDB); the sample notes go there.
    return defaultVaultPath();
#else
    // Each language's sample notebook has its own folder: one created in Polish stays Polish.
    return QDir::cleanPath(m_dataDir + u'/' + sampleNotebookFolder(notebooksLanguage()));
#endif
}

QString AppController::deviceStatePath(const QString &vaultPath) const
{
    // The same folder brulion-cli uses (History and LocalStore shared with it on this device).
    return notebook::deviceStatePath(m_dataDir, vaultPath);
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

bool AppController::openRecentVault(const QString &rawPath)
{
    const QString path = cleanLocalPath(rawPath);
#ifdef Q_OS_ANDROID
    // Without "All files access" a folder on shared storage only looks missing: openVault() asks
    // for the permission and opens it then.
    if (needsStoragePermission(path) && !platform::AndroidBridge::instance()->hasAllFilesAccess()) {
        openVault(path);
        return true;
    }
#endif
    if (!path.isEmpty() && !isWasm() && !QFileInfo(path).isDir()) {
        m_settings->forgetRecentVault(path);
        toast(tr("The notebook folder %1 is not available.").arg(QDir::toNativeSeparators(path)), u"warning"_s);
        return false;
    }
    openVault(path);
    return true;
}

void AppController::openVaultAt(const QString &path, const QVariantMap &route)
{
    m_routeAfterOpen = normalizeRoute(route);
    m_routeAfterOpenVault.clear();
    openVault(path);
}

QString AppController::docsVaultPath() const
{
    // Each language's documentation has its own folder (the other one stays as it was). In the
    // browser too: the app data folder is kept in IndexedDB like the notebook's (docs/WEB.md).
    return docsNotebookPath(m_dataDir, notebooksLanguage());
}

void AppController::openDocsVault()
{
    // Always the table of contents, not the journal: this notebook is for reading.
    m_routeAfterOpen = normalizeRoute({{u"view"_s, u"page"_s}, {u"path"_s, docsWelcomePage(notebooksLanguage())}});
    m_routeAfterOpenVault.clear();
    setStatus(u"loading"_s);
    scheduleOpening(docsVaultPath(), true);
}

bool AppController::isDocsPath(const QString &path) const
{
    return !docsLanguage(path).isEmpty();
}

QString AppController::docsLanguage(const QString &path) const
{
    // Either language's folder, whatever the UI language is now.
    return notebook::docsNotebookLanguage(m_dataDir, path);
}

bool AppController::inDocsVault() const
{
    return isDocsPath(m_settings->currentVault());
}

QString AppController::lastUserVault() const
{
    const QVariantList recent = m_settings->recentVaults();
    for (const QVariant &v : recent) {
        const QString path = v.toMap().value(u"path"_s).toString();
        if (!isDocsPath(path)) // 0.3.0–0.3.5 listed the documentation among the recent notebooks
            return path;
    }
    return {};
}

void AppController::toggleDocsVault()
{
    if (!inDocsVault()) {
        openDocsVault();
        return;
    }
    const QString back = lastUserVault();
    if (back.isEmpty())
        closeVault(); // no other notebook known: the start screen
    else
        openRecentVault(back); // a folder that is gone: a warning, and the documentation stays
}

void AppController::openDemoVault()
{
    m_routeAfterOpen.clear(); // the sample notebook opens on its welcome page
    m_routeAfterOpenVault.clear();
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
    constexpr auto kFirstFrameTimeout = 1s;
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
    QTimer::singleShot(kFirstFrameTimeout, this, once);
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
    const bool docs = isDocsPath(path);
    // The documentation has no reminders: its scheduler must not replace the armed set of the
    // user's notebook with an empty one (Android would cancel every alarm of it).
    m_workspace->setRemindersPaused(docs);
    m_workspace->backups()->setPaused(docs); // Brulion writes its pages again on every open
    // The sync settings are the notebook's: the previous notebook is closed and the new one is
    // current before sync attaches to the new folder, never the old notebook's server folder.
    // The documentation is current while it is open (its sync settings, the switcher), but it
    // never becomes a recent notebook, and start() never reopens it.
    const QString previousVault = m_settings->currentVault();
    m_workspace->close();
    m_settings->setCurrentVault(path);
    if (!m_workspace->open(path, deviceStatePath(path), &error)) {
        m_settings->setCurrentVault(previousVault);
        m_routeAfterOpen.clear();
        m_routeAfterOpenVault.clear();
        m_failedPath = path;
        m_failedSeedDemo = seedDemo;
        setStatus(u"error"_s, tr("Could not open the notebook %1: %2").arg(QDir::toNativeSeparators(path), error));
        return;
    }
    m_failedPath.clear();
    applyIntroNotebookConfig(docs || seedDemo);
    if (!docs) {
        // The sync settings of 0.3.0 and older go to the notebook they belong to (the one its
        // sync bookkeeping or the start says was synced, also the sample notebook), never to the
        // documentation; a notebook keeps the server folder it last synced with (appsettings.h).
        const QJsonObject meta = m_workspace->vault()->store()->get(sync::SyncEngine::metaKey()).toObject();
        m_settings->notebookOpened(m_workspace->rootPath(), meta.value(u"remote"_s).toString());
    }
    bool showWelcome = false;
    if (docs) {
        // Written on first use and brought up to date with every version of the app, without
        // touching the pages the user changed (updateDocsVault()).
        // Its own language's pages, also after the UI language changed.
        if (!updateDocsVault(*m_workspace->vault(), docsLanguage(path)).ok)
            toast(tr("Could not update the documentation: %1").arg(m_workspace->vault()->errorString()), u"error"_s);
        showWelcome = true; // it is for reading: its table of contents, not a journal
    } else if (seedDemo && m_workspace->vault()->markdownFiles().isEmpty()) {
        if (seedDemoVault(*m_workspace->vault(), now(), notebooksLanguage()) < 0)
            toast(tr("Could not write all sample notes: %1").arg(m_workspace->vault()->errorString()), u"error"_s);
        showWelcome = true;
    }
    // Also when its folder was opened as a notebook by another spelling (a relative path).
    if (const QString language = docs ? docsLanguage(path) : docsLanguage(m_workspace->rootPath()); !language.isEmpty())
        m_settings->forgetRecentVault(docsNotebookPath(m_dataDir, language)); // listed by 0.3.0–0.3.5
    else
        m_settings->addRecentVault(m_workspace->rootPath());
    // The notebook's tabs as they were left on this device (not for --route: screenshots and
    // tests start from one tab showing it, and leave the saved tabs alone).
    const TabsSnapshot tabsBefore = beginTabs();
    if (m_devRoute.isEmpty()) {
        restoreTabs(m_workspace->rootPath());
    } else {
        m_tabs.reset();
        m_tabsVault.clear();
    }
    m_sideRoute.clear();
    Q_EMIT sidePanelRouteChanged();

    // The first route before "ready": the shell is built then and shows it right away.
    const QVariantMap first = initialRoute();
    const QString afterVault = std::exchange(m_routeAfterOpenVault, {});
    const QVariantMap after = std::exchange(m_routeAfterOpen, {});
    const bool afterHere = sameNotebook(afterVault, m_workspace->rootPath());
    if (!after.isEmpty() && !afterHere)
        refuseOtherNotebookRoute(afterVault); // asked for (app open) while this notebook was opening
    if (!after.isEmpty() && afterHere) {
        m_startToday = false;
        m_tabs.navigate(0, afterVault.isEmpty() ? after : m_workspace->resolveRoute(after));
    } else if (m_startToday) {
        m_startToday = false;
        const QString date = today();
        m_tabs.navigate(0,
                        {{u"view"_s, u"journal"_s}, {u"date"_s, date}, {u"path"_s, m_workspace->journalFileFor(date)}});
    } else if (!first.isEmpty())
        m_tabs.navigate(0, first);
    else if (const QString welcome = docs ? docsWelcomePage(docsLanguage(path)) : demoWelcomePage(notebooksLanguage());
             showWelcome && m_workspace->exists(welcome))
        m_tabs.navigate(0, {{u"view"_s, u"page"_s}, {u"path"_s, welcome}});
    setStatus(u"ready"_s);
    endTabs(tabsBefore);
    // (Signalled also when nothing differs from before: the new shell shows the first route.)
    Q_EMIT currentRouteChanged();
    Q_EMIT historyChanged();
    applyPendingNotification();
    // The dialogs (share, quick capture) once the shell that owns them exists for "ready".
    if (m_share)
        QTimer::singleShot(0, this, &AppController::applyPendingShare);
    if (std::exchange(m_startCapture, false))
        QTimer::singleShot(0, this, &AppController::quickCapture);
}

void AppController::applyIntroNotebookConfig(bool special)
{
    const QVariantMap config = m_settings->newNotebookConfig();
    if (special || config.isEmpty())
        return;
    const Vault *vault = m_workspace->vault();
    const bool fresh = vault && vault->markdownFiles().isEmpty() && !vault->exists(vaultConfigPath())
            && vault->configAdoptedFromObsidian().isEmpty();
    if (fresh && !m_workspace->setConfig(config))
        qCWarning(lcApp) << "The intro's notebook settings could not be saved";
    else if (fresh)
        qCInfo(lcApp) << "The intro's notebook settings were saved to the new notebook";
    m_settings->setNewNotebookConfig({});
}

QVariantMap AppController::initialRoute() const
{
    return m_devRoute;
}

void AppController::closeVault()
{
    m_settings->setCurrentVault(QString());
    saveTabs();
    m_tabsVault.clear();
    const TabsSnapshot tabsBefore = beginTabs();
    m_tabs.reset();
    m_sideRoute.clear();
    Q_EMIT sidePanelRouteChanged();
    endTabs(tabsBefore, false);
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
    } else if (m_failedSeedDemo) {
        setStatus(u"loading"_s);
        scheduleOpening(m_failedPath, true);
    } else {
        openVault(m_failedPath);
    }
}

void AppController::showOnboarding()
{
    setStatus(u"onboarding"_s);
}

bool AppController::introPending() const
{
    return m_devIntro || (!m_devOnboarding && !m_settings->introDone());
}

void AppController::finishIntro()
{
    const bool was = introPending();
    m_devIntro = false;
    m_devOnboarding = false;
    m_settings->setIntroDone(true);
    if (was != introPending())
        Q_EMIT introPendingChanged();
}

void AppController::restartIntro()
{
    const bool was = introPending();
    m_devOnboarding = false;
    m_settings->setIntroDone(false);
    if (was != introPending())
        Q_EMIT introPendingChanged();
    if (m_status == u"ready")
        closeVault();
    else
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
    if (inDocsVault()) {
        // Notes go into the user's journal, never into the documentation: back to the user's
        // notebook, and the dialog opens there.
        const QString back = lastUserVault();
        if (back.isEmpty()) {
            toast(tr("Quick capture writes into your own notebook. Open or create one first."), u"warning"_s);
            return;
        }
        if (openRecentVault(back))
            m_startCapture = true;
        return;
    }
    requestUiAction(u"capture"_s);
}

bool AppController::capture(const QString &text)
{
    if (!m_workspace->isOpen() || inDocsVault())
        return false;
    // A date in the text makes it a task with a reminder / due date (notebook/capture.h).
    return !m_workspace->quickCapture(text).isEmpty();
}

void AppController::showWindow()
{
    // Whatever comes next (a reminder's page, quick capture) is for the main window.
    mainWindowActivated();
    Q_EMIT showWindowRequested();
}

void AppController::showTrayCalendar(const QRect &icon, const QPoint &cursor, bool toggle)
{
    m_trayIcon = icon;
    m_trayCursor = cursor.isNull() ? std::nullopt : std::optional(cursor);
    Q_EMIT uiActionRequested(toggle ? u"toggleTrayCalendar"_s : u"trayCalendar"_s);
}

QPoint AppController::trayPopupPosition(int width, int height) const
{
    return trayPopupPositionOnScreen(m_trayIcon, m_trayCursor, QSize(width, height), kTrayPopupMargin);
}

void AppController::trayPopupHidItself()
{
    trayPopupHidItselfAt(qobject_cast<QGuiApplication *>(QCoreApplication::instance()) ? std::optional(QCursor::pos())
                                                                                       : std::nullopt);
}

void AppController::trayPopupHidItselfAt(std::optional<QPoint> cursor)
{
    m_trayPopupHidden.start();
    m_trayPopupHiddenCursor = cursor;
}

bool AppController::trayPopupShouldReopen()
{
    const qint64 ms = m_trayPopupHidden.isValid() ? m_trayPopupHidden.elapsed() : -1;
    m_trayPopupHidden.invalidate();
    return shell::trayPopupShouldReopen(ms, std::exchange(m_trayPopupHiddenCursor, std::nullopt), m_trayIcon,
                                        m_trayCursor);
}

bool AppController::handleInstanceMessage(const QByteArray &verb, const QJsonObject &payload)
{
    if (verb != "activate" && verb != "capture" && verb != "today" && verb != "open" && verb != "hidden"
        && verb != "share") {
        qCInfo(lcApp) << "Ignoring an instance message with an unknown verb" << verb;
        return false;
    }
    // A second start with --hidden (the sign-in's entry while Brulion already runs): nothing.
    if (verb == "hidden")
        return true;
    // Wayland: the window raised for the second launch presents the launcher's activation token
    // (as DesktopNotifier does for a notification's click); other platforms ignore it.
    if (const QString token = payload.value(u"activationToken"_s).toString(); isActivationToken(token))
        qputenv("XDG_ACTIVATION_TOKEN", token.toLatin1());
    if (verb == "share") {
        // Files opened with Brulion while it runs (ipc::shareMessage). A message never names a
        // folder to remove: only the platform's own copies are ever removed.
        QJsonObject share = payload.value(u"share"_s).toObject();
        share.remove(u"dir"_s);
        receiveShare(share.toVariantMap()); // shows the window (or says there is nothing to save)
        return true;
    }
    showWindow();
    // They wait for the notebook when it is still opening.
    if (verb == "capture")
        quickCapture();
    else if (verb == "today")
        openToday();
    else if (verb == "open")
        openRoute(payload.value(u"route"_s).toObject().toVariantMap(), payload.value(u"vault"_s).toString());
    return true;
}

void AppController::setRemoteControl(const QVariantMap &status)
{
    if (assignIfChanged(m_remoteControl, status))
        Q_EMIT remoteControlChanged();
}

void AppController::quit()
{
    if (assignIfChanged(m_quitting, true))
        Q_EMIT quittingChanged();
    saveTabs();
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

void AppController::toastAction(const QString &text, const QString &kind, const QString &actionText,
                                const QJSValue &onAction)
{
    const int id = ++m_lastToastId;
    m_functionToasts.insert(id, onAction);
    Q_EMIT toastWithActionRequested(id, text, kind, actionText);
}

void AppController::requestUiAction(const QString &action)
{
    Q_EMIT uiActionRequested(action);
}

void AppController::requestItemAction(const QString &action, const QString &path)
{
    Q_EMIT itemActionRequested(action, path);
}

// ---- Sync seam --------------------------------------------------------------------------------

void AppController::setSyncAvailable(bool available)
{
    if (assignIfChanged(m_syncAvailable, available))
        Q_EMIT syncStatusChanged();
}

void AppController::setSyncStatus(const QVariantMap &status)
{
    if (assignIfChanged(m_syncStatus, status))
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

QString AppController::routeText(const QVariantMap &route, const QString &key) const
{
    const QVariant value = route.value(key);
    if (value.isNull()) // also the invalid value of a missing key
        return {};
    switch (value.typeId()) {
    case QMetaType::Double:
    case QMetaType::Float: {
        // A whole number without ".0", like JavaScript (12.0 → "12").
        const double d = value.toDouble();
        if (d == std::trunc(d) && std::abs(d) < 1e15)
            return QString::number(qint64(d));
        return QString::number(d, 'g', QLocale::FloatingPointShortest);
    }
    default: return value.toString();
    }
}

int AppController::routeInt(const QVariantMap &route, const QString &key, int fallback) const
{
    const QVariant value = route.value(key);
    if (value.isNull())
        return fallback;
    bool ok = false;
    const int n = value.toInt(&ok);
    return ok ? n : fallback;
}

bool AppController::routeFlag(const QVariantMap &route, const QString &key) const
{
    const QVariant value = route.value(key);
    if (value.isNull())
        return false;
    switch (value.typeId()) {
    case QMetaType::Bool: return value.toBool();
    case QMetaType::QString: return !value.toString().isEmpty();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float: {
        const double d = value.toDouble();
        return d != 0 && !std::isnan(d);
    }
    default: return true;
    }
}

int AppController::toastWithAction(const QString &text, const QString &kind, const QString &actionText)
{
    const int id = ++m_lastToastId;
    Q_EMIT toastWithActionRequested(id, text, kind, actionText);
    return id;
}

int AppController::toastRestore(const QString &text, const QString &actionText, const QStringList &trashPaths)
{
    const int id = ++m_lastToastId;
    m_restoreToasts.insert(id, trashPaths);
    Q_EMIT toastWithActionRequested(id, text, u"info"_s, actionText);
    return id;
}

int AppController::toastRestorePage(const QString &text, const QString &actionText, const QString &trashPath)
{
    return toastRestore(text, actionText, QStringList{trashPath});
}

void AppController::setToastHostId(int id, int hostId)
{
    if (id > 0 && hostId > 0)
        m_toastIdsByHostId.insert(hostId, id);
}

void AppController::hostToastActionTriggered(int hostId)
{
    const int id = m_toastIdsByHostId.take(hostId);
    if (id <= 0)
        return;
    if (m_restoreToasts.contains(id)) {
        if (m_workspace)
            m_workspace->restoreAll(m_restoreToasts.take(id));
        return;
    }
    if (m_functionToasts.contains(id)) {
        const QJSValue onAction = m_functionToasts.take(id);
        if (onAction.isCallable())
            onAction.call();
        return;
    }
    Q_EMIT toastActionTriggered(id);
}

void AppController::hostToastRemoved(int hostId)
{
    const int id = m_toastIdsByHostId.take(hostId);
    m_restoreToasts.remove(id);
    m_functionToasts.remove(id);
}

void AppController::beginOpenIn(const QString &target)
{
    m_openIn = target;
}

void AppController::endOpenIn()
{
    m_openIn.clear();
}

QStringList AppController::folderChain(const QString &folder) const
{
    QStringList out;
    if (folder.isEmpty())
        return out;
    for (qsizetype slash = folder.indexOf(u'/'); slash >= 0; slash = folder.indexOf(u'/', slash + 1))
        out.append(folder.left(slash));
    out.append(folder);
    return out;
}

QStringList AppController::routeStrings(const QVariantMap &route, const QString &key) const
{
    const QVariant value = route.value(key);
    return value.isNull() ? QStringList() : value.toStringList();
}

QVariantList AppController::folderChoices(const QString &query, const QString &ownFolder, const QString &currentFolder,
                                          bool moving, const QString &rootName, const QString &newFolderText,
                                          const QString &createText) const
{
    return shell::folderChoices(m_workspace ? m_workspace->folderPaths() : QStringList(), query, ownFolder,
                                currentFolder, moving, {rootName, newFolderText, createText});
}

QVariantList AppController::routeList(const QVariantMap &route, const QString &key) const
{
    const QVariant value = route.value(key);
    return value.isNull() ? QVariantList() : value.toList();
}

void AppController::placeTrayPopup(QWindow *popup) const
{
    if (!popup)
        return;
    // Wayland ignores a window's position: a layer-shell surface in the work area's bottom-right
    // corner instead (traywindow.h), set up once — before its first show.
    if (platform::trayPopupAnchorable()) {
        if (!popup->property("brulionLayerShell").toBool())
            popup->setProperty("brulionLayerShell", platform::anchorTrayPopup(popup, kTrayPopupMargin));
        return;
    }
    popup->setPosition(trayPopupPosition(popup->width(), popup->height()));
}

QVariantList AppController::recentPagesExcept(const QString &path, int limit) const
{
    QVariantList out;
    if (!m_workspace)
        return out;
    for (const QVariant &page : m_workspace->recentPages()) {
        if (out.size() >= limit)
            break;
        if (page.toMap().value(u"path"_s).toString() != path)
            out.append(page);
    }
    return out;
}

QVariantMap AppController::routeWith(const QVariantMap &base, const QVariantMap &route, const QStringList &keys) const
{
    QVariantMap out = base;
    for (const QString &key : keys) {
        if (const auto it = route.constFind(key); it != route.constEnd())
            out.insert(key, *it);
    }
    return out;
}

QVariant AppController::propertyOf(QObject *object, const QString &name) const
{
    if (!object)
        return {};
    const QByteArray key = name.toUtf8();
    if (object->metaObject()->indexOfProperty(key.constData()) < 0)
        return {};
    return object->property(key.constData());
}

} // namespace brulion::shell
