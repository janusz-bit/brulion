// AppController — the app's top-level state (QML singleton `AppController`, created by main()):
// lifecycle (boot → onboarding / loading → ready, or error), the vault being opened, the
// navigation history of the main view and the optional side panel, platform facts, permissions,
// quick capture, shares from other apps, window/tray requests and the seam for the sync
// integration.
//
// Navigation (routes: see navigation.h):
//   navigate(route, newPane)   main view (pushes history) or, with newPane on a wide window
//                              (sidePanelAllowed), the side panel
//   back(), forward(), goHome(), replaceRoute(route), handleBackKey() (Android back)
//   openPage(path, line), openJournal(date), openToday(), openTag(tag) (the tag's page),
//   search(query)
// Renamed pages are followed in the history; deleted pages drop out of it.
//
// Tabs, the split view and windows (tabs.h; docs/UI.md "Tabs, split view and windows"): every
// tab has its own history. `currentRoute`, `canGoBack`, `canGoForward` and `atHome` are those of
// the main window's focused tab (`focusedTab`: the main pane's, or the split pane's when that one
// is focused); navigate(), replaceRoute(), back() and forward() act on the active tab
// (`activeTab`: the focused tab, or a page's own window while that has the focus — its views
// navigate in it). newTab(), openTab(route), openWindow(route) (desktop), closeTab(), selectTab(),
// nextTab() / previousTab(), moveTab(), splitView() / closeSplit(), detachTab() / attachTab();
// openIn("tab" | "window", fn) runs `fn` (a link's click) with its navigation going there. The
// panes and windows show a tab each (tabRoute(id), tabRouteChanged(id)) and keep its view state
// (setTabViewState()). The tabs are saved per notebook on this device ("tabs/<vault id>") and
// come back when it opens again.
//
// Vault lifecycle: start() opens the last vault (or shows onboarding); openVault(path)
// (created when missing), openDemoVault() (the sample notebook of the UI language, seeded with
// its sample notes: notebooksLanguage(), demovault.h),
// closeVault(); openRecentVault(path) for a known notebook (never recreates a missing folder).
// The documentation notebook (openDocsVault(), toggleDocsVault()) is for reading:
// it never becomes the start notebook or a recent one, arms no reminders (the user's notebook
// keeps its armed set), and quick capture and reminder taps go back to the user's notebook.
// Android: a vault on shared storage first needs "All files access" —
// openVault() then asks for it (pendingVaultPath) and continues when it is granted.
// WebAssembly: one fixed vault folder in IndexedDB (defaultVaultPath); construct the controller
// only after WasmStorage::ready().
//
// Developer / screenshot options (parsed from Options::arguments; see also ui/devtools.h):
//   --vault <path>         open this vault at start (created when missing)
//   --demo                 seed the sample notes (into --vault, else the demo vault) and open it
//   --sample-language pl|en  the language of the sample and documentation notebooks of this run
//                          (default: the UI language, --locale included)
//   --onboarding           start on the onboarding screen
//   --route <view|json>    initial route, e.g. "settings" or '{"view":"page","path":"a.md"}'
//   --popup <name[:query]> open a shell popup after start: palette, switcher, capture,
//                          newPage, about (with an optional query for palette/switcher)
//   --share <json>         receive a share as Android's ShareActivity hands it over
//                          ({"subject", "text", "url", "files": [{"path", "name", "mime"}]},
//                          notebook/share.h; appshare.cpp) once the notebook is ready
//   --share-query <query>  the browser version's share target ("share-title=…&share-text=…&
//                          share-url=…": wasm/index.html passes the launch's parameters so)
//   --update-available <version>  show the message about a new version as if <version> were the
//                          latest release, on any platform, without asking GitHub (this run only)
//   --hidden               start minimised to the system tray (desktop; when a tray exists;
//                          Windows' "Start when you sign in" starts Brulion so)
//   --selftest-shell       drive the real shell with key events and exit with the number of
//                          failed checks (desktop; main.cpp, shellselftest.h; ctest tst_shell_qml)
// and for launchers (desktop entry actions): --capture opens quick capture, --today today's
// journal, once the vault is open (a second launch asks the running instance instead, main.cpp);
// `--open <file>…` (the desktop entry's `Exec=brulion --open %F`, Windows' "Open with":
// every argument after it is a file — Options::openFiles) offers the files in the share dialog, a
// file of the open notebook opens as its page.
#pragma once

#include "applockcontroller.h"
#include "appsettings.h"
#include "appupdates.h"
#include "localai.h"
#include "navigation.h"
#include "recentnotebooks.h"
#include "tabs.h"
#include "transcriber.h"
#include "workspace.h"

#include "automation/plan.h"
#include "notebook/share.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJSValue>
#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

namespace brulion::platform {
class Autostart;
class WasmNotifier;
}

class QWindow;

namespace brulion::shell {

class AppScripting;

class CommandModel;
class ShortcutMap;

class AppController : public QObject {
    Q_OBJECT

    /// "boot" | "onboarding" | "loading" | "ready" | "error"
    Q_PROPERTY(QString status READ status NOTIFY statusChanged FINAL)
    /// The status "onboarding" starts with the intro (IntroView: language, look, permissions,
    /// the first notebook's settings) before the notebook is chosen (OnboardingView): it was
    /// never finished on this device (AppSettings.introDone), or --intro; --onboarding skips it.
    Q_PROPERTY(bool introPending READ introPending NOTIFY introPendingChanged FINAL)
    /// Why opening the vault failed (status "error"), translated.
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged FINAL)
    Q_PROPERTY(brulion::shell::AppSettings *settings READ settings CONSTANT FINAL)
    Q_PROPERTY(brulion::shell::Workspace *workspace READ workspace CONSTANT FINAL)
    /// The message about a newer version (appupdates.h).
    Q_PROPERTY(brulion::shell::AppUpdates *updates READ updates CONSTANT FINAL)
    /// The model on the device: downloads, and whether this build runs one (localai.h).
    Q_PROPERTY(brulion::shell::LocalAi *localAi READ localAi CONSTANT FINAL)
    /// Speech recognition on the device for recordings in notes: the Whisper models, the queue
    /// (media/transcriber.h; docs/UI.md "Voice notes").
    Q_PROPERTY(brulion::media::Transcriber *speech READ speech CONSTANT FINAL)
    /// The app lock (applockcontroller.h; QML: the singleton AppLock).
    Q_PROPERTY(brulion::shell::AppLockController *appLock READ appLock CONSTANT FINAL)

    // ---- Navigation ---------------------------------------------------------------------
    Q_PROPERTY(QVariantMap currentRoute READ currentRoute NOTIFY currentRouteChanged FINAL)
    /// currentRoute.view
    Q_PROPERTY(QString currentView READ currentView NOTIFY currentRouteChanged FINAL)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged FINAL)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged FINAL)
    /// The main view shows the home route (journals).
    Q_PROPERTY(bool atHome READ atHome NOTIFY currentRouteChanged FINAL)
    /// Route shown in the side panel ({} = the current view's context panel).
    Q_PROPERTY(QVariantMap sidePanelRoute READ sidePanelRoute NOTIFY sidePanelRouteChanged FINAL)
    Q_PROPERTY(bool sidePanelVisible READ sidePanelVisible WRITE setSidePanelVisible NOTIFY sidePanelVisibleChanged FINAL)
    /// Set by the shell: a side panel fits (wide window). Otherwise newPane opens in the main view.
    Q_PROPERTY(bool sidePanelAllowed READ sidePanelAllowed WRITE setSidePanelAllowed NOTIFY sidePanelAllowedChanged FINAL)
    /// Set by the shell: the windows show a status bar (wide, not in focus mode) — the focused
    /// pane's page leaves its word count in the corner to it (PageView).
    Q_PROPERTY(bool statusBarShown READ statusBarShown WRITE setStatusBarShown NOTIFY statusBarShownChanged FINAL)
    /// Set by the shell: the main view's editor shows its page as Markdown source (source mode,
    /// Ctrl+E). The palette's commands for blocks and headings are off then.
    Q_PROPERTY(bool sourceModeShown READ sourceModeShown WRITE setSourceModeShown NOTIFY sourceModeShownChanged FINAL)

    // ---- Tabs (tabs.h) ------------------------------------------------------------------
    /// The main window's bar: [{ id, tabId (= id), route, canGoBack, canGoForward }] in order.
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged FINAL)
    Q_PROPERTY(int tabCount READ tabCount NOTIFY tabsChanged FINAL)
    /// The tab navigation goes to: the focused pane's, or a page window's while it has the focus.
    Q_PROPERTY(int activeTab READ activeTab NOTIFY tabsChanged FINAL)
    /// The main pane's tab; the split pane's (0: no split); which of the two is focused.
    Q_PROPERTY(int mainTab READ mainTab NOTIFY tabsChanged FINAL)
    Q_PROPERTY(int splitTab READ splitTab NOTIFY tabsChanged FINAL)
    Q_PROPERTY(bool splitFocused READ splitFocused NOTIFY tabsChanged FINAL)
    /// The main window's focused pane's tab (its route is currentRoute).
    Q_PROPERTY(int focusedTab READ focusedTab NOTIFY tabsChanged FINAL)
    /// The tabs shown in windows of their own (desktop), in the order they were opened.
    Q_PROPERTY(QVariantList windowTabs READ windowTabs NOTIFY windowTabsChanged FINAL)
    /// Pages can open in windows of their own (desktop; not Android, not the browser).
    Q_PROPERTY(bool windowsAllowed READ windowsAllowed CONSTANT FINAL)

    // ---- Platform -----------------------------------------------------------------------
    /// "linux", "windows", "android" or "wasm".
    Q_PROPERTY(QString platform READ platform CONSTANT FINAL)
    Q_PROPERTY(bool isAndroid READ isAndroid CONSTANT FINAL)
    Q_PROPERTY(bool isWasm READ isWasm CONSTANT FINAL)
    Q_PROPERTY(bool isDesktop READ isDesktop CONSTANT FINAL)
    /// Touch-first platform (Android).
    Q_PROPERTY(bool touch READ touch CONSTANT FINAL)
    Q_PROPERTY(QString version READ version CONSTANT FINAL)
    /// A system tray icon exists (desktop): close-to-tray and --hidden are possible.
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY trayAvailableChanged FINAL)
    /// Started with --hidden and a tray exists: the window starts hidden.
    Q_PROPERTY(bool startHidden READ startHidden NOTIFY trayAvailableChanged FINAL)
    /// The interface scale (ui/uiscale.h; Settings → Appearance → "Interface size"): whether this
    /// run can change it (desktop windows; not with --scale, --size or a headless platform), the
    /// factor "Automatic" picks for the screen Brulion started on, and the factor in effect.
    Q_PROPERTY(bool interfaceScaleSupported READ interfaceScaleSupported NOTIFY interfaceScaleChanged FINAL)
    Q_PROPERTY(qreal autoInterfaceScale READ autoInterfaceScale NOTIFY interfaceScaleChanged FINAL)
    Q_PROPERTY(qreal interfaceScale READ interfaceScale NOTIFY interfaceScaleChanged FINAL)
    /// quit() was called (the window must not veto closing any more).
    Q_PROPERTY(bool quitting READ quitting NOTIFY quittingChanged FINAL)
    /// "Start when you sign in" (src/platform/autostart.h): "available" (Windows), "profile"
    /// (Windows with a separate BRULION_PROFILE: shown, disabled) or "unsupported" (not offered).
    Q_PROPERTY(QString autostartAvailability READ autostartAvailability NOTIFY startAtLoginChanged FINAL)
    /// Windows starts Brulion hidden in the tray at sign-in (the Run key); a failed change says so
    /// in a toast and the switch goes back.
    Q_PROPERTY(bool startAtLogin READ startAtLogin WRITE setStartAtLogin NOTIFY startAtLoginChanged FINAL)

    // ---- Vault locations ----------------------------------------------------------------
    /// Suggested folder for a new vault (desktop: ~/Documents/Brulion, Android:
    /// /storage/emulated/0/Brulion, browser: the fixed IndexedDB folder).
    Q_PROPERTY(QString defaultVaultPath READ defaultVaultPath CONSTANT FINAL)
    /// Private app storage (Android without "All files access"; no Syncthing there).
    Q_PROPERTY(QString appStorageVaultPath READ appStorageVaultPath CONSTANT FINAL)
    /// Where the sample notebook of the UI language lives ("Sample notebook", in Polish
    /// "Przykładowy brulion"; demovault.h).
    Q_PROPERTY(QString demoVaultPath READ demoVaultPath NOTIFY notebookPathsChanged FINAL)
    /// The documentation notebook of the UI language ("Brulion Documentation", in Polish
    /// "Dokumentacja Brulionu"; in the app data folder, in the browser too). The other language's
    /// folder, if any, is still the documentation (inDocsVault) and keeps its language's pages.
    Q_PROPERTY(QString docsVaultPath READ docsVaultPath NOTIFY notebookPathsChanged FINAL)
    /// Vault waiting for the storage permission (Android), "" otherwise.
    Q_PROPERTY(QString pendingVaultPath READ pendingVaultPath NOTIFY pendingVaultPathChanged FINAL)
    /// The vault that could not be opened (status "error"; retryOpenVault()), "" otherwise.
    Q_PROPERTY(QString failedVaultPath READ failedVaultPath NOTIFY statusChanged FINAL)
    /// The documentation notebook is the open one.
    Q_PROPERTY(bool inDocsVault READ inDocsVault NOTIFY inDocsVaultChanged FINAL)
    /// The recent notebooks with their folders' state and when they were last opened (the start
    /// screen, OnboardingView; recentnotebooks.h).
    Q_PROPERTY(brulion::shell::RecentNotebooks *recentNotebooks READ recentNotebooks CONSTANT FINAL)

    // ---- Permissions --------------------------------------------------------------------
    /// { allFilesAccess, notifications, exactAlarms, fullScreenAlarms } (true when granted or
    /// not needed) plus { storagePermissionNeeded, notificationsSupported,
    /// exactAlarmsNeeded, fullScreenNeeded } telling which rows the settings page shows.
    Q_PROPERTY(QVariantMap permissions READ permissions NOTIFY permissionsChanged FINAL)

    // ---- Sync seam (filled in by the sync integration) ------------------------------------
    /// A sync engine is configured and "Sync now" can run.
    Q_PROPERTY(bool syncAvailable READ syncAvailable NOTIFY syncStatusChanged FINAL)
    /// { state: "disabled" | "idle" | "syncing" | "error" | "offline", text, lastSync (ISO) }
    Q_PROPERTY(QVariantMap syncStatus READ syncStatus NOTIFY syncStatusChanged FINAL)

    /// --popup value for screenshots ("" normally).
    Q_PROPERTY(QString devPopup READ devPopup CONSTANT FINAL)
    /// The remote control of the running app for AI agents (desktop: remotecontrol.h, set by
    /// main()): { available (built and started for this run), setting (the device setting
    /// devtools/remoteControl), active (listening), forced (--remote-control), socket, error };
    /// { available: false } elsewhere. Settings → Application → "For developers".
    Q_PROPERTY(QVariantMap remoteControl READ remoteControl NOTIFY remoteControlChanged FINAL)

    // ---- Shares from other apps (appshare.cpp) --------------------------------------------
    /// The share waiting for the share dialog ({} when none): { kind: "text" | "files" ("files"
    /// when a file can be saved, or when there is no text to save instead), text (one capture
    /// text: notebook::composeSharedText; for "text"), files: [{ name, title, preview
    /// (the first lines), size, error ("" or a translated reason it cannot be saved) }], readable
    /// (files that can be saved), target ("journal" for text and several files, "page" for one
    /// file), title (the new page's suggested title), source ("open": files opened with Brulion
    /// on a desktop — copies are saved, the files stay as they are; "" another app's share) }.
    Q_PROPERTY(QVariantMap pendingShare READ pendingShare NOTIFY pendingShareChanged FINAL)
    /// A brulion:// address waiting for the user's "Write" (appautomation.cpp; docs/UI.md
    /// "Automation"): { action, access ("write" | "run"), kind, notebook, target (the page's title,
    /// "" for today's journal), path, heading, block (the text written, plain), journal, day
    /// ("YYYY-MM-DD"), newPage, title, extension, command, when (a reminder, "YYYY-MM-DD HH:mm"),
    /// source (the caller's own name: never verified), origin ("link" | "note" | "android" | "web" |
    /// "launch") }; {} when none.
    Q_PROPERTY(QVariantMap pendingAutomation READ pendingAutomation NOTIFY pendingAutomationChanged FINAL)
    /// What brulion:// addresses without the key may do on this device: "ask" (writing asks
    /// first, the default), "key" (writing only with the key), "off" (addresses do nothing).
    Q_PROPERTY(QString automationPolicy READ automationPolicy WRITE setAutomationPolicy NOTIFY automationChanged FINAL)
    /// The open notebook's automation key on this device ("" none, or no notebook open).
    Q_PROPERTY(QString automationKey READ automationKey NOTIFY automationChanged FINAL)

public:
    struct Options {
        /// QSettings INI file ("" = the platform default).
        QString settingsFile;
        /// App data folder (History, LocalStore, secrets, demo vault); "" = AppDataLocation.
        QString dataDir;
        /// Command line (dev options above); usually QCoreApplication::arguments() before
        /// "--open" (notebook::splitOpenArguments).
        QStringList arguments;
        /// Files to offer once started: the arguments after "--open" (main.cpp), paths relative
        /// to `workingDirectory` ("" = QDir::currentPath()) or file:// URLs.
        QStringList openFiles;
        QString workingDirectory;
        /// Asks GitHub about new versions where the app tells about them (main(), not its
        /// self-tests; off in tests).
        bool checkForUpdates = false;
        /// The model on the device (localai.h). What is left empty is the app's own: the models
        /// folder <dataDir>/models, the model runner's plugin of this build, the device's memory.
        /// Tests give small models and a fake runner.
        LocalAi::Options localAi;
        /// Search by meaning (Workspace.semantic): its folders, plugin and opener (tests: a fake
        /// model); empty fields as the app has them.
        SemanticSearch::Options semantic;
    };

    explicit AppController(const Options &options, QObject *parent = nullptr);
    explicit AppController(QObject *parent = nullptr) : AppController(Options{}, parent) { }
    ~AppController() override;

    /// The first AppController created (the one main() made); the QML singleton.
    static AppController *instance();

    AppSettings *settings() const { return m_settings.get(); }
    RecentNotebooks *recentNotebooks() const { return m_recentNotebooks.get(); }
    Workspace *workspace() const { return m_workspace.get(); }
    AppUpdates *updates() const { return m_updates.get(); }
    LocalAi *localAi() const { return m_localAi.get(); }
    media::Transcriber *speech() const { return m_speech.get(); }
    AppLockController *appLock() const { return m_appLock.get(); }
    /// The app as extensions and live clients change it (b.app; appscripting.h). QML: AppScripting.
    AppScripting *scripting() const { return m_scripting.get(); }
    CommandModel *commands() const { return m_commands.get(); }
    /// The commands' keyboard shortcuts, custom ones included (Settings → Keyboard shortcuts).
    ShortcutMap *shortcuts() const { return m_shortcuts.get(); }

    /// Boot: open the last vault (or the one from --vault), else show onboarding. Call once,
    /// after the QML UI is loaded (so it sees the first toasts).
    void start();

    QString status() const { return m_status; }
    QString errorString() const { return m_error; }

    // ---- Navigation ---------------------------------------------------------------------
    QVariantMap currentRoute() const;
    QString currentView() const;
    bool canGoBack() const;
    bool canGoForward() const;
    bool atHome() const;
    QVariantMap sidePanelRoute() const { return m_sideRoute; }
    bool sidePanelVisible() const { return m_sidePanelVisible; }
    void setSidePanelVisible(bool visible);
    bool sidePanelAllowed() const { return m_sidePanelAllowed; }
    void setSidePanelAllowed(bool allowed);
    bool statusBarShown() const { return m_statusBarShown; }
    void setStatusBarShown(bool shown);
    bool sourceModeShown() const { return m_sourceModeShown; }
    void setSourceModeShown(bool shown);

    Q_INVOKABLE void navigate(const QVariantMap &route, bool newPane = false);
    Q_INVOKABLE void replaceRoute(const QVariantMap &route);
    /// Back / forward in tab `tab` (0: the active tab).
    Q_INVOKABLE bool back(int tab = 0);
    Q_INVOKABLE bool forward(int tab = 0);
    Q_INVOKABLE void goHome();
    /// Android back key / gesture: back(), else go home; false at home (the app may close).
    Q_INVOKABLE bool handleBackKey();
    /// Show the current view's context panel (backlinks, outline) instead of a pinned route.
    Q_INVOKABLE void clearSidePanelRoute();
    Q_INVOKABLE void openPage(const QString &path, int line = -1, bool newPane = false);
    /// Open the page of a [[link]] target `name` (an extension's link, a toast's "Open") or of a
    /// note's path ("Nawyki.md"), made when it does not exist yet; at the 0-based `line` when ≥ 0.
    Q_INVOKABLE void openNamedPage(const QString &name, int line = -1);
    /// Open the daily note of "YYYY-MM-DD" (created from the journal template when missing).
    Q_INVOKABLE void openJournal(const QString &date);
    /// Today's journal; while the notebook is still opening, once it is ready.
    Q_INVOKABLE void openToday();
    /// The weekly or monthly note (`period` "week" | "month") of the period holding `date`
    /// ("YYYY-MM-DD"; "" = today), `offset` periods later (negative: earlier). Opening never
    /// creates it: the editor does with the first change, from the period's template.
    Q_INVOKABLE void openPeriodicNote(const QString &period, const QString &date = QString(), int offset = 0);
    /// A route asked for from outside (brulion-cli `app open`, ipc/appmessage.h): navigate to it
    /// once the notebook is ready (resolved like the editor's routes: Workspace::resolveRoute).
    /// `vault`, when given, must be the notebook that is open (or opening): a route of another
    /// notebook is refused with a notice.
    void openRoute(const QVariantMap &route, const QString &vault = QString());
    /// "YYYY-MM-DD" of today (+ `offsetDays`) by the app clock.
    Q_INVOKABLE QString today(int offsetDays = 0) const;
    /// The page the tag names (docs/SYNTAX.md §3; created by its first content when it does not
    /// exist yet) — through Workspace::navigate(). The tag overview is the route {view: "tag"}.
    Q_INVOKABLE void openTag(const QString &tag, bool newPane = false);
    Q_INVOKABLE void search(const QString &query);
    /// Open a reminder's note (notification tap); waits for the vault when it is not ready. In
    /// the documentation (which arms no reminders) it opens the user's notebook first.
    Q_INVOKABLE void openNotification(const QString &path, int line);

    // ---- Tabs ---------------------------------------------------------------------------
    QVariantList tabs() const;
    int tabCount() const { return int(m_tabs.bar().size()); }
    int activeTab() const { return m_tabs.active(); }
    int mainTab() const { return m_tabs.mainTab(); }
    int splitTab() const { return m_tabs.splitTab(); }
    bool splitFocused() const { return m_tabs.splitFocused(); }
    int focusedTab() const { return m_tabs.focusedBarTab(); }
    QVariantList windowTabs() const;
    bool windowsAllowed() const { return isDesktop(); }

    /// A new tab after the focused one, shown and focused: `route`, the home route for {} (Ctrl+T).
    /// Its id; 0 when TabSet::MaxTabs are open (a notice says so).
    Q_INVOKABLE int newTab(const QVariantMap &route = {});
    /// The same, `background`: it stays behind the focused tab.
    Q_INVOKABLE int openTab(const QVariantMap &route, bool background = false);
    /// `route` in a window of its own (desktop; elsewhere a new tab). Its id.
    Q_INVOKABLE int openWindow(const QVariantMap &route);
    /// Run `open` (a function following a link or opening a page as a click does) with its
    /// navigation going to `target`: "tab" (a new tab), "window" (a window of its own; a tab where
    /// there are none), anything else as usual. Nothing that `open` does not navigate.
    Q_INVOKABLE void openIn(const QString &target, const QJSValue &open);
    /// Close tab `id` (0: the active one; a window's tab closes that window).
    Q_INVOKABLE bool closeTab(int id = 0);
    Q_INVOKABLE bool closeOtherTabs(int id);
    /// Show tab `id` (TabSet::select: the focused pane gets it, or its pane gets the focus).
    Q_INVOKABLE bool selectTab(int id);
    /// The bar tab at `index` (Ctrl+1…8); -1 or past the end: the last one (Ctrl+9).
    Q_INVOKABLE bool selectTabAt(int index);
    Q_INVOKABLE bool nextTab();
    Q_INVOKABLE bool previousTab();
    Q_INVOKABLE bool moveTab(int from, int to);
    /// Split the main window: bar tab `id` beside the main pane (0: a copy of the focused tab).
    Q_INVOKABLE bool splitView(int id = 0);
    Q_INVOKABLE bool closeSplit();
    /// The main window's split pane (or main pane) got the focus.
    Q_INVOKABLE bool focusPane(bool split);
    /// The main window got the focus (Main.qml): its focused tab is the active one again.
    Q_INVOKABLE void mainWindowActivated();
    /// Bar tab `id` (0: the focused one) into a window of its own (desktop); back into the bar.
    Q_INVOKABLE bool detachTab(int id = 0);
    Q_INVOKABLE bool attachTab(int id);
    /// Tab `id`'s current route ({} when there is none), whether it can go back / forward.
    Q_INVOKABLE QVariantMap tabRoute(int id) const;
    Q_INVOKABLE bool tabCanGoBack(int id) const;
    Q_INVOKABLE bool tabCanGoForward(int id) const;
    /// Where tab `id`'s view was scrolled for its current route ({} when nothing is kept); set by
    /// the pane showing it before it shows another tab (and when the tabs are saved).
    Q_INVOKABLE QVariantMap tabViewState(int id) const;
    Q_INVOKABLE void setTabViewState(int id, const QVariantMap &state);
    /// Save the tabs of the open notebook now (the panes hand over their view states first:
    /// tabViewStatesRequested); the app going to the background, leaving the notebook, quitting.
    Q_INVOKABLE void saveTabs();

    // ---- Platform -----------------------------------------------------------------------
    QString platform() const;
    bool isAndroid() const;
    bool isWasm() const;
    bool isDesktop() const;
    bool touch() const { return isAndroid(); }
    QString version() const;
    bool trayAvailable() const { return m_trayAvailable; }
    /// main(): whether the desktop integration created a tray icon.
    void setTrayAvailable(bool available);
    bool startHidden() const { return m_startHidden && m_trayAvailable; }
    bool interfaceScaleSupported() const { return m_interfaceScaleSupported; }
    qreal autoInterfaceScale() const { return m_autoInterfaceScale; }
    /// The factor in effect: the setting's, or "Automatic"'s (1 where unsupported).
    qreal interfaceScale() const;
    /// main(), before the first window: this run scales the interface, "Automatic" being
    /// `automatic` (the screen's choice); applies the setting now and whenever it changes.
    void enableInterfaceScale(qreal automatic);
    /// The zoom commands: `steps` larger (> 0) or smaller (< 0) on the settings' steps, 0 back
    /// to "Automatic"; a toast says the new size.
    Q_INVOKABLE void zoomInterface(int steps);
    bool quitting() const { return m_quitting; }
    QString autostartAvailability() const;
    bool startAtLogin() const;
    void setStartAtLogin(bool on);
    /// Replaces this process's autostart (tests: one over INI files).
    void setAutostart(std::unique_ptr<platform::Autostart> autostart);
    QString devPopup() const { return m_devPopup; }
    QVariantMap remoteControl() const { return m_remoteControl; }
    void setRemoteControl(const QVariantMap &status);

    // ---- Vaults -------------------------------------------------------------------------
    QString defaultVaultPath() const;
    QString appStorageVaultPath() const;
    QString demoVaultPath() const;
    QString docsVaultPath() const;
    /// The language of the sample and documentation notebooks opened from now on ("en" | "pl"):
    /// the UI language — --locale of this run until the setting changes, else the setting —, or
    /// --sample-language (tests written against one language's sample notes).
    QString notebooksLanguage() const;
    QString pendingVaultPath() const { return m_pendingVault; }
    QString failedVaultPath() const { return m_failedPath; }
    /// Device-local state folder of a vault (History, LocalStore): <dataDir>/vaults/<id>.
    QString deviceStatePath(const QString &vaultPath) const;
    QString dataDir() const { return m_dataDir; }

    /// Open (or create) the vault folder `path` (a local path or a file:// URL). Asynchronous:
    /// status becomes "loading", then "ready" or "error".
    Q_INVOKABLE void openVault(const QString &path);
    /// Open a notebook known from before (the recent list, "back" from the documentation). Like
    /// start(), never creates its folder when it is gone (deleted, moved, an unmounted card):
    /// says so and returns false. The notebook stays on the recent list, marked missing (a card
    /// put back brings it back) — one in a temporary folder leaves it (RecentNotebooks).
    Q_INVOKABLE bool openRecentVault(const QString &path);
    /// openVault(), then show `route` instead of the home view (onboarding's "Import notes":
    /// the import screen of the new notebook). Kept while Android asks for "All files access".
    Q_INVOKABLE void openVaultAt(const QString &path, const QVariantMap &route);
    /// Create (first time) and open the sample notebook of the UI language with its notes.
    Q_INVOKABLE void openDemoVault();
    /// Opens the documentation notebook, writing it on first use and updating the pages nobody
    /// changed to this version (updateDocsVault(); also in the browser).
    Q_INVOKABLE void openDocsVault();
    /// True while the documentation notebook is the open one.
    bool inDocsVault() const;
    /// The documentation notebook, or back to the notebook used before it (Ctrl+Shift+B).
    Q_INVOKABLE void toggleDocsVault();
    /// The user's notebook used last: the first recent one that is not the documentation ("" when
    /// there is none). start() opens it when the app was left in the documentation.
    QString lastUserVault() const;
    /// Show onboarding and close the vault (a moment later: the shell's editors save first).
    Q_INVOKABLE void closeVault();
    /// "Try again" after an error: open failedVaultPath() again (else the current vault).
    Q_INVOKABLE void retryOpenVault();
    Q_INVOKABLE void showOnboarding();
    bool introPending() const;
    /// The intro is finished (or skipped): the notebook is chosen next.
    Q_INVOKABLE void finishIntro();
    /// Settings → General → "Run the intro again": closes the notebook (closeVault()) and starts
    /// the intro; the notebook is chosen again at its end.
    Q_INVOKABLE void restartIntro();
    /// Local path of a folder URL from a file dialog ("file:///home/x" -> "/home/x").
    Q_INVOKABLE QString localPath(const QUrl &url) const;
    /// Whether a vault at `path` would need Android's "All files access" (shared storage).
    Q_INVOKABLE bool needsStoragePermission(const QString &path) const;

    // ---- Permissions --------------------------------------------------------------------
    QVariantMap permissions() const;
    /// "allFilesAccess" | "notifications" | "exactAlarms" | "fullScreenAlarms"
    Q_INVOKABLE void requestPermission(const QString &name);
    Q_INVOKABLE void refreshPermissions();
    /// Show a notification now to check that reminders get through: through the handler of
    /// the reminders integration when it set one, else the platform's default (desktop: the
    /// tray via notificationRequested(); Android: a reminder armed 3 s ahead — which REPLACES
    /// the armed set, so the reminders integration must take this over; browser: at once).
    Q_INVOKABLE void sendTestNotification();
    /// The reminders integration delivers test notifications (Android: it adds the test to the
    /// set it arms, since scheduleReminders() replaces all armed reminders).
    void setTestNotificationHandler(std::function<void(const QString &title, const QString &body)> handler);

    // ---- Quick capture, window, app ---------------------------------------------------------
    /// Open the quick capture dialog (also from the tray / a launcher shortcut); while the
    /// notebook is still opening, once it is ready. In the documentation it goes back to the
    /// user's notebook first: captured notes never land in the documentation.
    Q_INVOKABLE void quickCapture();
    /// Append `text` to today's journal (Workspace::quickCapture: a date in it makes a task with
    /// a ⏰ reminder or 📅 due date); false when nothing was saved (also in the documentation).
    Q_INVOKABLE bool capture(const QString &text);
    /// A voice note recorded off a note (VoiceRecorderDialog's `file`, in the cache folder): moved
    /// into the attachments folder and embedded in a new block at the end of today's journal.
    /// Returns { path (the attachment), page (the journal), target (the embed's) } — {} when it
    /// could not be stored (a toast says why).
    Q_INVOKABLE QVariantMap captureRecording(const QString &file);
    /// Bring the main window to the front (tray, second launch, notification tap). Hides the
    /// tray calendar (Main.qml).
    Q_INVOKABLE void showWindow();
    /// The tray calendar (ShellTrayCalendar, docs/UI.md "Window layout") next to the tray icon
    /// `icon` (QSystemTrayIcon::geometry(); empty when unknown) or the cursor `cursor` (a null
    /// point: unknown). `toggle` (a click on the icon): a second click closes it, and a click
    /// right after it hid itself keeps it closed (trayPopupShouldReopen) — uiActionRequested
    /// "toggleTrayCalendar"; else (the tray menu) it opens — "trayCalendar".
    Q_INVOKABLE void showTrayCalendar(const QRect &icon, const QPoint &cursor, bool toggle = false);
    /// Where the tray calendar of this size goes (traypopup.h): next to the anchor of the last
    /// showTrayCalendar(), inside the work area of its screen.
    Q_INVOKABLE QPoint trayPopupPosition(int width, int height) const;
    /// The tray calendar hid itself on losing the activation (ShellTrayCalendar): the time and
    /// the cursor (QCursor::pos(); unknown without a QGuiApplication) for the next click's
    /// trayPopupShouldReopen().
    Q_INVOKABLE void trayPopupHidItself();
    /// trayPopupHidItself() with the cursor given (tests).
    void trayPopupHidItselfAt(std::optional<QPoint> cursor);
    /// traypopup.h's guard for the click on the tray icon of the last showTrayCalendar(): false
    /// when it is the click that has just taken the activation from the calendar (it then
    /// closes it). Forgets that hide — asked once per tray action (Main.qml).
    Q_INVOKABLE bool trayPopupShouldReopen();
    /// A message of a second launch or brulion-cli over the single-instance socket
    /// (ipc/appmessage.h, parsed by main()): `activate`, `capture`, `today`, `open {"route",
    /// "vault"}` and `share {"share"}` (files opened with Brulion: receiveShare() without the
    /// share's "dir") show the window and do their part, `hidden` does nothing; false for any
    /// other verb, which changes nothing (not even the window) — newer senders stay harmless. A
    /// valid "activationToken" (Wayland: the launcher's XDG_ACTIVATION_TOKEN) is set for the
    /// window's next activation.
    bool handleInstanceMessage(const QByteArray &verb, const QJsonObject &payload);
    /// Quit for real (also when closing to the tray is on).
    Q_INVOKABLE void quit();
    /// Android: send the app to the background (the back key at home), as Android 12+ does for
    /// a root activity — instead of ending the process. False elsewhere (nothing done).
    Q_INVOKABLE bool moveToBackground();
    /// The window was closed into the tray (Main.qml): the first time, a tray message tells
    /// that Brulion keeps running and how to quit.
    Q_INVOKABLE void hiddenToTray();
    /// Show a toast: kind "info" | "success" | "warning" | "error".
    Q_INVOKABLE void toast(const QString &text, const QString &kind = QStringLiteral("info"));
    /// A file dragged from the sidebar's tree let go beside it, at (sceneX, sceneY) of the window:
    /// a view that takes files may add it (the canvas view: a card where it lands).
    Q_INVOKABLE void dropOnView(const QString &path, qreal sceneX, qreal sceneY)
    {
        Q_EMIT fileDroppedOnView(path, sceneX, sceneY);
    }
    /// Show a toast with a button (the editor's "Undo"): `onAction`, a function, runs when it is
    /// pressed (Main.qml shows it as toastWithAction()'s).
    Q_INVOKABLE void toastAction(const QString &text, const QString &kind, const QString &actionText,
                                 const QJSValue &onAction);
    /// Ask the shell to open one of its popups: "palette", "switcher", "capture", "newPage",
    /// "about", "share" (the share dialog for pendingShare), "toggleTheme", "toggleSidePanel";
    /// Main.qml: "toggleWindow" (the tray), "trayCalendar" / "toggleTrayCalendar"
    /// (showTrayCalendar()).
    Q_INVOKABLE void requestUiAction(const QString &action);
    /// Ask the shell to act on a page or folder of the tree from any view (its dialogs live in
    /// the shell): "move" (Move to…), "rename", "delete" (the confirmation), "newPage" /
    /// "newFolder" (in that folder), "reveal" (show it in the sidebar's tree).
    Q_INVOKABLE void requestItemAction(const QString &action, const QString &path);

    // ---- Shares from other apps (appshare.cpp; docs/ANDROID.md "Share → Brulion") ------------
    /// Text or .md/.txt files shared from another app (Android's ShareActivity through
    /// AndroidBridge::shared, the --share option, the browser's share target: receiveShareQuery())
    /// or opened with Brulion on a desktop (--open, a second launch's `share` message; source
    /// "open"): the files are read at once (text types only — a file without a type gets one from
    /// its name and content —, at most notebook::SharedFileMaxBytes each), the window comes up and
    /// the shell's share dialog opens (uiActionRequested("share")) — once the notebook is ready, in
    /// the user's notebook (from the documentation it goes back to it first); with no notebook a
    /// notice asks for one and the share waits. An opened file that is a page of that notebook
    /// opens as the page instead (the dialog lists only the others). A newer share replaces one
    /// still waiting; opened files are added to opened files still waiting
    /// (notebook::mergeOpenedShares). Nothing is written until saveShare(), and an opened file is
    /// never changed or removed.
    Q_INVOKABLE void receiveShare(const QVariantMap &share);
    /// receiveShare() of the platform's JSON (notebook::sharedContentFromJson).
    void receiveShareJson(const QByteArray &json);
    /// The browser version's share target (wasm/index.html passes the launch's share parameters as
    /// --share-query; another tab hands them over through platform::WasmShare): receiveShare() of
    /// notebook::sharedContentFromQuery(query).
    Q_INVOKABLE void receiveShareQuery(const QString &query);
    QVariantMap pendingShare() const;
    /// Save the waiting share: { target: "journal" | "page", title (a single new page's title),
    /// text (the edited text of a text share) }. Journal: the text as one block of today's journal
    /// (no date parsing: "jutro" in a shared title makes no reminder), files one block each
    /// (notebook::sharedFileBlock). Page: one new page per file (its text as it is, the title
    /// from its name; "Dom 2" when taken) or one from the text, in the notebook's folder for new
    /// pages; a page whose name differs only in letter case is never written over — one page:
    /// nothing saved ({ ok: false, error }) and the share waits; several: that file is skipped.
    /// Then the copies are removed, the journal or the (first) page opens and a toast says so.
    /// Returns { ok, path, count (blocks or pages written), skipped [names], error }.
    Q_INVOKABLE QVariantMap saveShare(const QVariantMap &choice);
    /// Drop the waiting share ("Cancel"): nothing is written, the copies are removed.
    Q_INVOKABLE void dismissShare();
    /// Where the platform puts its copies of shared files (<dataDir>/share-in; Android's
    /// ShareActivity: files/share-in/<stamp>). Only a share's folder inside it is ever removed.
    QString shareInboxPath() const;

    // ---- brulion:// addresses (appautomation.cpp; docs/UI.md "Automation") ----------------------
    /// A brulion:// or web+brulion: address from `origin` ("link": the system handed it over —
    /// a launch, a second launch, Android's VIEW intent; "note": a link clicked in a note; "web": the
    /// browser's protocol handler; "android": an automation intent): read (automation::parseUri),
    /// sent to the notebook it names (another known one is opened first; a request waits while a
    /// notebook opens, the documentation goes back to the user's notebook), worked out
    /// (automation::plan) and checked (automation::decide with automationPolicy and the
    /// notebook's key): opening runs at once, writing and extension commands with the key too —
    /// without it the window comes up with the confirmation dialog (uiActionRequested
    /// "automation", pendingAutomation). Errors are toasts; a request with the valid key gets its
    /// x-success / x-error address opened (never brulion:, file:, data: or javascript:).
    Q_INVOKABLE void receiveUri(const QString &uri, const QString &origin = QStringLiteral("link"));
    QVariantMap pendingAutomation() const;
    /// "Write" (or "Run") in the confirmation dialog: carries out the waiting request: { ok, error }.
    Q_INVOKABLE QVariantMap confirmAutomation();
    /// "Cancel": nothing is written.
    Q_INVOKABLE void dismissAutomation();
    QString automationPolicy() const;
    void setAutomationPolicy(const QString &policy);
    QString automationKey() const;
    /// The open notebook's key, made now when it has none ("" when it cannot be saved: a toast).
    Q_INVOKABLE QString createAutomationKey();
    /// A new key for the open notebook: links and automations with the old one stop working.
    Q_INVOKABLE QString renewAutomationKey();
    /// The open notebook has no key any more (writing through links asks again).
    Q_INVOKABLE void removeAutomationKey();
    /// A brulion:// address of `action` with `params` for the open notebook (`vault` its name),
    /// with its key when `withKey` (and it has one): Settings → Automation's builder.
    Q_INVOKABLE QString automationLink(const QString &action, const QVariantMap &params, bool withKey) const;
    /// The address that opens `path` (at `heading` or the block `blockId` when given) in this
    /// notebook: the page menu's "Copy Brulion link".
    Q_INVOKABLE QString pageAutomationLink(const QString &path, const QString &heading = QString(),
                                           const QString &blockId = QString()) const;
    /// The browser: ask it to open web+brulion: links with this Brulion
    /// (navigator.registerProtocolHandler; the browser asks the user). Nothing elsewhere.
    Q_INVOKABLE void registerWebLinks();

    // ---- Sync seam ----------------------------------------------------------------------
    bool syncAvailable() const { return m_syncAvailable; }
    QVariantMap syncStatus() const { return m_syncStatus; }
    void setSyncAvailable(bool available);
    void setSyncStatus(const QVariantMap &status);
    /// "Sync now": emits syncRequested() for the sync integration.
    Q_INVOKABLE void requestSync();

    // ---- Clock --------------------------------------------------------------------------
    /// Clock for journals and the sample notes (default: current local time).
    void setClock(std::function<QDateTime()> clock);
    QDateTime now() const;

    // ---- Route parameters for compiled QML ------------------------------------------------
    // Views get their route as `property var route` (a QVariantMap); reading `route.key` there
    // is not compiled by qmlcachegen, these are (also for other maps held in `var`, e.g. the
    // results of Workspace calls). All accept a missing key and a null or undefined route — a
    // QVariant, not a QVariantMap: Qt 6.12 throws a TypeError for undefined passed as a map.
    /// The value of `key` as text ("" when missing or null; a number as its text: 12 → "12").
    Q_INVOKABLE QString routeText(const QVariant &route, const QString &key) const;
    /// The value of `key` as an int (`fallback` when missing, null or not a number).
    Q_INVOKABLE int routeInt(const QVariant &route, const QString &key, int fallback) const;
    /// JavaScript's truthiness of the value of `key` (`if (route.key)`): false when missing,
    /// null, false, 0, NaN or ""; true otherwise (also for the text "false" and an empty list).
    Q_INVOKABLE bool routeFlag(const QVariant &route, const QString &key) const;

    // ---- The shell's compiled QML (no functions as values) ------------------------------------
    /// A toast with a button, without a function to run (compiled QML): its id (> 0);
    /// toastActionTriggered(id) when the button is pressed. Main.qml shows it
    /// (toastWithActionRequested) and reports its toast host's id (setToastHostId) and the
    /// button (hostToastActionTriggered).
    Q_INVOKABLE int toastWithAction(const QString &text, const QString &kind, const QString &actionText);
    /// An "info" toast whose button restores `trashPaths` from the trash (Workspace::restoreAll),
    /// also once the view that deleted them is gone: its id.
    Q_INVOKABLE int toastRestore(const QString &text, const QString &actionText, const QStringList &trashPaths);
    /// toastRestore() for one page (compiled QML cannot pass a list literal).
    Q_INVOKABLE int toastRestorePage(const QString &text, const QString &actionText, const QString &trashPath);
    /// A "success" toast whose button renames the page `path` back to `oldTitle`
    /// (Workspace::renamePage), also once the view that renamed it is gone: its id.
    Q_INVOKABLE int toastUndoRename(const QString &text, const QString &actionText, const QString &path,
                                    const QString &oldTitle);
    /// Main.qml: toast `id` of toastWithAction() is shown by the window's toast host as `hostId`.
    Q_INVOKABLE void setToastHostId(int id, int hostId);
    /// Main.qml: the button of the toast host's toast `hostId` was pressed (toastAction()'s
    /// function runs, toastRestore()'s pages come back, else toastActionTriggered()).
    Q_INVOKABLE void hostToastActionTriggered(int hostId);
    /// Main.qml: the toast host's toast `hostId` went (its action is forgotten).
    Q_INVOKABLE void hostToastRemoved(int hostId);
    /// openIn() in two steps (OpenIn.begin / OpenIn.end): the next navigation goes to `target`
    /// ("tab" | "window") until endOpenIn().
    Q_INVOKABLE void beginOpenIn(const QString &target);
    Q_INVOKABLE void endOpenIn();
    /// The folders down to `folder`, each by its path: "a/b/c" → ["a", "a/b", "a/b/c"]; "" → []
    /// (split at every "/", as JavaScript's split: "a//b" → ["a", "a/", "a//b"]). The top bar's
    /// breadcrumb.
    Q_INVOKABLE QStringList folderChain(const QString &folder) const;
    /// The value of `key` as a list of strings ([] when missing or null; one string → [it]), as
    /// routeText() for lists: the specials of Workspace.moveImpact() / folderInfo().
    Q_INVOKABLE QStringList routeStrings(const QVariant &route, const QString &key) const;
    /// The value of `key` as a list ([] when missing or null): the specials of folderInfo().
    Q_INVOKABLE QVariantList routeList(const QVariant &route, const QString &key) const;
    /// Move the tray calendar `popup` to trayPopupPosition() for its size (a QPoint result's x
    /// and y cannot be read by compiled QML); under Wayland anchor it to the work area's
    /// bottom-right corner through the layer shell instead (src/platform/traywindow.h).
    Q_INVOKABLE void placeTrayPopup(QWindow *popup) const;
    /// The recent pages ([{ path, title }], Workspace::recentPages) without `path`, at most
    /// `limit` of them: the sidebar's "Recent".
    Q_INVOKABLE QVariantList recentPagesExcept(const QString &path, int limit) const;
    /// `object`'s property `name` (invalid when it has none): a property a QML type adds that its
    /// base type, all compiled QML knows of it, lacks (a list delegate's required property).
    Q_INVOKABLE QVariant propertyOf(QObject *object, const QString &name) const;
    /// "Move to…"'s rows for `query` over the notebook's folders (folderchoices.h): the root
    /// (`rootName`), the folders matching it without `ownFolder` and its subfolders, a new folder
    /// (`newFolderText`'s %1, subtitle `createText`); `moving`: `currentFolder`'s row disabled.
    Q_INVOKABLE QVariantList folderChoices(const QString &query, const QString &ownFolder, const QString &currentFolder,
                                           bool moving, const QString &rootName, const QString &newFolderText,
                                           const QString &createText) const;
    /// `base` plus the `keys` that `route` has (even null ones): a view's route made from
    /// another's parameters (JournalsView's one day as a PageView route).
    Q_INVOKABLE QVariantMap routeWith(const QVariant &base, const QVariant &route, const QStringList &keys) const;

Q_SIGNALS:
    /// dropOnView().
    void fileDroppedOnView(const QString &path, qreal sceneX, qreal sceneY);
    void statusChanged();
    void introPendingChanged();
    void currentRouteChanged();
    void historyChanged();
    void sidePanelRouteChanged();
    void sidePanelVisibleChanged();
    void sidePanelAllowedChanged();
    void statusBarShownChanged();
    void sourceModeShownChanged();
    void tabsChanged();
    void windowTabsChanged();
    /// Tab `id`'s current route changed (a pane or window showing it follows).
    void tabRouteChanged(int id);
    /// saveTabs(): the panes set the view states of the tabs they show (setTabViewState()) now.
    void tabViewStatesRequested();
    void trayAvailableChanged();
    void interfaceScaleChanged();
    void quittingChanged();
    void startAtLoginChanged();
    void pendingVaultPathChanged();
    void inDocsVaultChanged();
    /// demoVaultPath and docsVaultPath changed (the UI language did).
    void notebookPathsChanged();
    void permissionsChanged();
    void syncStatusChanged();
    void remoteControlChanged();
    void pendingShareChanged();
    void pendingAutomationChanged();
    void automationChanged();
    /// The UI language setting changed ("pl" | "en"); main() swaps the translation.
    void languageChanged(const QString &language);
    void toastRequested(const QString &text, const QString &kind);
    void showWindowRequested();
    void uiActionRequested(const QString &action);
    void itemActionRequested(const QString &action, const QString &path);
    void syncRequested();
    /// Desktop: the tray shows it (the reminders integration may take over).
    void notificationRequested(const QString &title, const QString &body);
    /// quit() was called: main() ends the event loop.
    void quitRequested();
    /// toastWithAction(): Main.qml shows toast `id`.
    void toastWithActionRequested(int id, const QString &text, const QString &kind, const QString &actionText);
    /// The button of toastWithAction()'s toast `id` was pressed.
    void toastActionTriggered(int id);

private Q_SLOTS:
    void onFirstFrame();

private:
    void setStatus(const QString &status, const QString &error = {});
    /// Opens the vault a turn later — after the window's first frame (the splash) while it has
    /// not shown one yet: scanning a big vault must not keep the window blank.
    void scheduleOpening(const QString &path, bool seedDemo);
    void finishOpening(const QString &path, bool seedDemo);
    /// The intro's settings for the first notebook (AppSettings::newNotebookConfig) go to the
    /// notebook just opened when it is new — no notes, no .brulion/config.json, no Obsidian
    /// settings — and are dropped after the first notebook of the user's (not the documentation
    /// or the sample notebook: `special`) either way.
    void applyIntroNotebookConfig(bool special);
    /// What the tab signals compare (beginTabs() before a change, endTabs() after it emits
    /// tabsChanged, tabRouteChanged, currentRouteChanged, historyChanged… for what differs).
    struct TabsSnapshot {
        QList<int> bar;
        QList<int> windows;
        int main = 0, split = 0, active = 0, focused = 0;
        bool splitFocused = false;
        QHash<int, QVariantMap> routes;
        QHash<int, int> nav; ///< bit 1: can go back, bit 2: forward
    };
    TabsSnapshot beginTabs() const;
    /// `pageOpened`: a page the focused tab now shows was opened (recent pages); false for a
    /// rename, a route a view updates.
    void endTabs(const TabsSnapshot &before, bool pageOpened = true);
    /// The settings key of the open notebook's tabs ("" when none).
    QString tabsKey(const QString &vaultPath) const;
    /// The tabs of the notebook at `vaultPath` (else one tab at home), pages gone since dropped.
    void restoreTabs(const QString &vaultPath);
    void onPathRenamed(const QString &oldPath, const QString &newPath);
    /// A folder moved: folder routes (All pages, search in a folder, a folder export) follow.
    void onFolderRenamed(const QString &oldPath, const QString &newPath);
    void onPathRemoved(const QString &path);
    void applyPendingNotification();
    /// The share dialog for the waiting share, once a user notebook is ready (appshare.cpp).
    void applyPendingShare();
    /// Forget the waiting share and remove its copies (inside shareInboxPath() only).
    void clearShare();
    /// Whether the waiting share saves its files rather than its text: a file can be saved, or
    /// there is no text. A link shared with a preview image, or with an attachment that cannot
    /// be read, is still that link.
    bool shareSavesFiles() const;
    /// brulion:// (appautomation.cpp): the connections of the automation toasts (constructor).
    void initAutomation();
    /// The waiting addresses once a user notebook is ready (finishOpening()).
    void applyPendingUris();
    /// A read request for the open notebook: worked out, checked, done or asked.
    void handleAutomation(const automation::Request &request, const QString &origin);
    /// Carry out an allowed request: show it, write it (a toast with "Open"), or run the command.
    automation::Outcome carryOutAutomation(const automation::Request &request, const automation::Plan &plan);
    /// The x-success / x-error address of a keyed request, opened with the outcome's parameters.
    void callBack(const automation::Request &request, const automation::Outcome &outcome);
    /// The translated message of an automation error code.
    QString automationErrorText(const QString &error, const QString &detail) const;
    /// Android: what its background run of automation intents writes into (the open notebook,
    /// the policy, the language); cleared without a user notebook.
    void publishAutomationTarget();
    QVariantMap initialRoute() const;
    /// `path` is the folder of a documentation notebook (either language's).
    bool isDocsPath(const QString &path) const;
    /// The language of the documentation notebook at `path` ("en" | "pl"), "" for any other folder.
    QString docsLanguage(const QString &path) const;
    /// The notice for openRoute()'s route of another notebook than the open one.
    void refuseOtherNotebookRoute(const QString &vault);
    /// The speech recognizer, its settings and where it writes (constructor).
    void setUpSpeech();

    std::unique_ptr<AppSettings> m_settings;
    std::unique_ptr<RecentNotebooks> m_recentNotebooks;
    /// Before the workspace (its extensions hand it their requests): destroyed after it.
    std::unique_ptr<LocalAi> m_localAi;
    std::unique_ptr<Workspace> m_workspace;
    /// After the workspace (it writes transcripts there): destroyed before it.
    std::unique_ptr<media::Transcriber> m_speech;
    std::unique_ptr<AppLockController> m_appLock;
    std::unique_ptr<AppScripting> m_scripting;
    std::unique_ptr<AppUpdates> m_updates;
    std::unique_ptr<ShortcutMap> m_shortcuts;
    std::unique_ptr<CommandModel> m_commands;
    std::unique_ptr<platform::Autostart> m_autostart;
    QString m_dataDir;
    QString m_status = QStringLiteral("boot");
    QString m_error;
    TabSet m_tabs;
    /// The notebook m_tabs belong to ("" none) and the timer that saves them a moment after a change.
    QString m_tabsVault;
    QTimer *m_tabsSaveTimer = nullptr;
    /// openIn()'s target while its function runs.
    QString m_openIn;
    QVariantMap m_sideRoute;
    bool m_sidePanelVisible = false;
    bool m_sidePanelAllowed = true;
    bool m_statusBarShown = false;
    bool m_sourceModeShown = false;
    bool m_trayAvailable = false;
    bool m_interfaceScaleSupported = false;
    qreal m_autoInterfaceScale = 1.0;
    QRect m_trayIcon;                  ///< showTrayCalendar()'s anchor
    std::optional<QPoint> m_trayCursor;
    QElapsedTimer m_trayPopupHidden;   ///< since trayPopupHidItself() (invalid: not since the last click)
    std::optional<QPoint> m_trayPopupHiddenCursor;
    bool m_startHidden = false;
    bool m_quitting = false;
    bool m_started = false;
    bool m_framePresented = false;                  ///< a window of the app has shown a frame
    QList<std::function<void()>> m_afterFirstFrame; ///< openings waiting for that frame
    QMetaObject::Connection m_frameConnection;
    QString m_pendingVault;
    QString m_failedPath;
    bool m_failedSeedDemo = false;
    int m_closeGeneration = 0;
    std::function<void(const QString &, const QString &)> m_testNotificationHandler;
    QString m_pendingNotePath;
    int m_pendingNoteLine = -1;
    /// A shared file as read when the share came (appshare.cpp).
    struct ShareFile {
        QString path; ///< the file read ("" when there was none)
        QString name;
        QString title;   ///< its page title (notebook::sharedPageTitle)
        QString content; ///< decoded text ("" when `error`)
        qint64 size = -1;
        QString error; ///< notebook::SharedFile::error code
    };
    std::optional<notebook::SharedContent> m_share; ///< waiting for saveShare() / dismissShare()
    QList<ShareFile> m_shareFiles;
    /// A brulion:// request waiting for the confirmation dialog (appautomation.cpp).
    struct PendingAutomation {
        automation::Request request;
        automation::Plan plan;
        QString origin;
    };
    std::optional<PendingAutomation> m_automation;
    /// Addresses received while no user notebook was ready: (address, origin), at most a few.
    QList<std::pair<QString, QString>> m_waitingUris;
    /// The automation toasts' "Open": toastWithAction() ids → the route.
    QHash<int, QVariantMap> m_automationToasts;
    bool m_syncAvailable = false;
    QVariantMap m_syncStatus;
    QVariantMap m_remoteControl{{QStringLiteral("available"), false}};
    std::function<QDateTime()> m_clock;
    // Dev options.
    QString m_devVault;
    bool m_devDemo = false;
    QString m_localeOverride; ///< --locale pl|en (until the language setting changes)
    QString m_sampleLanguage; ///< --sample-language pl|en
    bool m_devOnboarding = false;
    bool m_devIntro = false; ///< --intro: the intro also on a device that finished it
    QVariantMap m_devRoute;
    QString m_devPopup;
    QByteArray m_startShare;   ///< --share, or the --open files as share JSON (received by start())
    QString m_startShareQuery; ///< --share-query (received by start())
    bool m_startCapture = false;
    bool m_startToday = false;
    QVariantMap m_routeAfterOpen; ///< openVaultAt()
    QString m_routeAfterOpenVault; ///< openRoute() while opening: the notebook the route belongs to
    int m_lastToastId = 0;         ///< toastWithAction()'s ids
    QHash<int, int> m_toastIdsByHostId;      ///< the toast host's id → toastWithAction()'s
    QHash<int, QStringList> m_restoreToasts; ///< toastRestore()'s ids → the trash paths
    QHash<int, std::pair<QString, QString>> m_renameToasts; ///< toastUndoRename()'s ids → {path, old title}
    QHash<int, QString> m_chatToasts; ///< an AI agent's question off screen: the toast's id → its conversation
    QHash<int, QJSValue> m_functionToasts;   ///< toastAction()'s ids → the function
    QHash<int, int> m_extensionToasts;       ///< toastWithAction()'s ids → an extension's button
#ifdef Q_OS_WASM
    platform::WasmNotifier *m_wasmNotifier = nullptr;
#endif
};

} // namespace brulion::shell
