// AppController — the app's top-level state (QML singleton `AppController`, created by main()):
// lifecycle (boot → onboarding / loading → ready, or error), the vault being opened, the
// navigation history of the main view and the optional side panel, platform facts, permissions,
// quick capture, window/tray requests and the seam for the sync integration.
//
// Navigation (routes: see navigation.h):
//   navigate(route, newPane)   main view (pushes history) or, with newPane on a wide window
//                              (sidePanelAllowed), the side panel
//   back(), forward(), goHome(), replaceRoute(route), handleBackKey() (Android back)
//   openPage(path, line), openJournal(date), openToday(), openTag(tag) (the tag's page),
//   search(query)
// Renamed pages are followed in the history; deleted pages drop out of it.
//
// Vault lifecycle: start() opens the last vault (or shows onboarding); openVault(path)
// (created when missing), openDemoVault() ("Przykładowy brulion", seeded with the sample notes),
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
//   --onboarding           start on the onboarding screen
//   --route <view|json>    initial route, e.g. "settings" or '{"view":"page","path":"a.md"}'
//   --popup <name[:query]> open a shell popup after start: palette, switcher, capture,
//                          newPage, about (with an optional query for palette/switcher)
//   --hidden               start minimised to the system tray (desktop; when a tray exists)
//   --selftest-shell       drive the real shell with key events and exit with the number of
//                          failed checks (desktop; main.cpp, shellselftest.h; ctest tst_shell_qml)
// and for launchers (desktop entry actions): --capture opens quick capture, --today today's
// journal, once the vault is open (a second launch asks the running instance instead, main.cpp).
#pragma once

#include "appsettings.h"
#include "navigation.h"
#include "workspace.h"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace brulion::platform {
class WasmNotifier;
}

namespace brulion::shell {

class CommandModel;

class AppController : public QObject {
    Q_OBJECT

    /// "boot" | "onboarding" | "loading" | "ready" | "error"
    Q_PROPERTY(QString status READ status NOTIFY statusChanged FINAL)
    /// Why opening the vault failed (status "error"), translated.
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged FINAL)
    Q_PROPERTY(brulion::shell::AppSettings *settings READ settings CONSTANT FINAL)
    Q_PROPERTY(brulion::shell::Workspace *workspace READ workspace CONSTANT FINAL)

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

    // ---- Platform -----------------------------------------------------------------------
    /// "linux", "android" or "wasm".
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
    /// quit() was called (the window must not veto closing any more).
    Q_PROPERTY(bool quitting READ quitting NOTIFY quittingChanged FINAL)

    // ---- Vault locations ----------------------------------------------------------------
    /// Suggested folder for a new vault (desktop: ~/Documents/Brulion, Android:
    /// /storage/emulated/0/Brulion, browser: the fixed IndexedDB folder).
    Q_PROPERTY(QString defaultVaultPath READ defaultVaultPath CONSTANT FINAL)
    /// Private app storage (Android without "All files access"; no Syncthing there).
    Q_PROPERTY(QString appStorageVaultPath READ appStorageVaultPath CONSTANT FINAL)
    /// Where "Przykładowy brulion" lives.
    Q_PROPERTY(QString demoVaultPath READ demoVaultPath CONSTANT FINAL)
    /// The notebook with the documentation ("Dokumentacja Brulionu"; empty in the browser).
    Q_PROPERTY(QString docsVaultPath READ docsVaultPath CONSTANT FINAL)
    /// Vault waiting for the storage permission (Android), "" otherwise.
    Q_PROPERTY(QString pendingVaultPath READ pendingVaultPath NOTIFY pendingVaultPathChanged FINAL)
    /// The vault that could not be opened (status "error"; retryOpenVault()), "" otherwise.
    Q_PROPERTY(QString failedVaultPath READ failedVaultPath NOTIFY statusChanged FINAL)
    /// The documentation notebook is the open one.
    Q_PROPERTY(bool inDocsVault READ inDocsVault NOTIFY inDocsVaultChanged FINAL)

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

public:
    struct Options {
        /// QSettings INI file ("" = the platform default).
        QString settingsFile;
        /// App data folder (History, LocalStore, secrets, demo vault); "" = AppDataLocation.
        QString dataDir;
        /// Command line (dev options above); usually QCoreApplication::arguments().
        QStringList arguments;
    };

    explicit AppController(const Options &options, QObject *parent = nullptr);
    explicit AppController(QObject *parent = nullptr) : AppController(Options{}, parent) { }
    ~AppController() override;

    /// The first AppController created (the one main() made); the QML singleton.
    static AppController *instance();

    AppSettings *settings() const { return m_settings.get(); }
    Workspace *workspace() const { return m_workspace.get(); }
    CommandModel *commands() const { return m_commands.get(); }

    /// Boot: open the last vault (or the one from --vault), else show onboarding. Call once,
    /// after the QML UI is loaded (so it sees the first toasts).
    void start();

    QString status() const { return m_status; }
    QString errorString() const { return m_error; }

    // ---- Navigation ---------------------------------------------------------------------
    QVariantMap currentRoute() const { return m_history.current(); }
    QString currentView() const;
    bool canGoBack() const { return m_history.canGoBack(); }
    bool canGoForward() const { return m_history.canGoForward(); }
    bool atHome() const;
    QVariantMap sidePanelRoute() const { return m_sideRoute; }
    bool sidePanelVisible() const { return m_sidePanelVisible; }
    void setSidePanelVisible(bool visible);
    bool sidePanelAllowed() const { return m_sidePanelAllowed; }
    void setSidePanelAllowed(bool allowed);

    Q_INVOKABLE void navigate(const QVariantMap &route, bool newPane = false);
    Q_INVOKABLE void replaceRoute(const QVariantMap &route);
    Q_INVOKABLE bool back();
    Q_INVOKABLE bool forward();
    Q_INVOKABLE void goHome();
    /// Android back key / gesture: back(), else go home; false at home (the app may close).
    Q_INVOKABLE bool handleBackKey();
    /// Show the current view's context panel (backlinks, outline) instead of a pinned route.
    Q_INVOKABLE void clearSidePanelRoute();
    Q_INVOKABLE void openPage(const QString &path, int line = -1, bool newPane = false);
    /// Open the daily note of "YYYY-MM-DD" (created from the journal template when missing).
    Q_INVOKABLE void openJournal(const QString &date);
    /// Today's journal; while the notebook is still opening, once it is ready.
    Q_INVOKABLE void openToday();
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
    bool quitting() const { return m_quitting; }
    QString devPopup() const { return m_devPopup; }

    // ---- Vaults -------------------------------------------------------------------------
    QString defaultVaultPath() const;
    QString appStorageVaultPath() const;
    QString demoVaultPath() const;
    QString docsVaultPath() const;
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
    /// says so, drops it from the recent list and returns false.
    Q_INVOKABLE bool openRecentVault(const QString &path);
    /// openVault(), then show `route` instead of the home view (onboarding's "Import notes":
    /// the import screen of the new notebook). Kept while Android asks for "All files access".
    Q_INVOKABLE void openVaultAt(const QString &path, const QVariantMap &route);
    /// Create (first time) and open "Przykładowy brulion" with the sample notes.
    Q_INVOKABLE void openDemoVault();
    /// Opens the documentation notebook, writing it on first use and updating the pages nobody
    /// changed to this version (updateDocsVault(); no-op in the browser).
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
    /// Bring the main window to the front (tray, second launch, notification tap).
    Q_INVOKABLE void showWindow();
    /// A message of a second launch or brulion-cli over the single-instance socket
    /// (ipc/appmessage.h, parsed by main()): `activate`, `capture`, `today` and `open {"route",
    /// "vault"}` show the window and do their part; false for any other verb, which changes
    /// nothing (not even the window) — newer senders stay harmless.
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
    /// Ask the shell to open one of its popups: "palette", "switcher", "capture", "newPage",
    /// "about", "toggleTheme", "toggleSidePanel".
    Q_INVOKABLE void requestUiAction(const QString &action);
    /// Ask the shell to act on a page or folder of the tree from any view (its dialogs live in
    /// the shell): "move" (Przenieś do…), "rename", "delete" (the confirmation), "newPage" /
    /// "newFolder" (in that folder), "reveal" (show it in the sidebar's tree).
    Q_INVOKABLE void requestItemAction(const QString &action, const QString &path);

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

Q_SIGNALS:
    void statusChanged();
    void currentRouteChanged();
    void historyChanged();
    void sidePanelRouteChanged();
    void sidePanelVisibleChanged();
    void sidePanelAllowedChanged();
    void trayAvailableChanged();
    void quittingChanged();
    void pendingVaultPathChanged();
    void inDocsVaultChanged();
    void permissionsChanged();
    void syncStatusChanged();
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

private Q_SLOTS:
    void onFirstFrame();

private:
    void setStatus(const QString &status, const QString &error = {});
    /// Opens the vault a turn later — after the window's first frame (the splash) while it has
    /// not shown one yet: scanning a big vault must not keep the window blank.
    void scheduleOpening(const QString &path, bool seedDemo);
    void finishOpening(const QString &path, bool seedDemo);
    void routeChanged();
    void onPathRenamed(const QString &oldPath, const QString &newPath);
    /// A folder moved: folder routes (All pages, search in a folder, a folder export) follow.
    void onFolderRenamed(const QString &oldPath, const QString &newPath);
    void onPathRemoved(const QString &path);
    void applyPendingNotification();
    QVariantMap initialRoute() const;
    /// `path` is the documentation notebook's folder.
    bool isDocsPath(const QString &path) const;
    /// The notice for openRoute()'s route of another notebook than the open one.
    void refuseOtherNotebookRoute(const QString &vault);

    std::unique_ptr<AppSettings> m_settings;
    std::unique_ptr<Workspace> m_workspace;
    std::unique_ptr<CommandModel> m_commands;
    QString m_dataDir;
    QString m_status = QStringLiteral("boot");
    QString m_error;
    RouteHistory m_history;
    QVariantMap m_sideRoute;
    bool m_sidePanelVisible = false;
    bool m_sidePanelAllowed = true;
    bool m_trayAvailable = false;
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
    bool m_syncAvailable = false;
    QVariantMap m_syncStatus;
    std::function<QDateTime()> m_clock;
    // Dev options.
    QString m_devVault;
    bool m_devDemo = false;
    bool m_devOnboarding = false;
    QVariantMap m_devRoute;
    QString m_devPopup;
    bool m_startCapture = false;
    bool m_startToday = false;
    QVariantMap m_routeAfterOpen; ///< openVaultAt()
    QString m_routeAfterOpenVault; ///< openRoute() while opening: the notebook the route belongs to
#ifdef Q_OS_WASM
    platform::WasmNotifier *m_wasmNotifier = nullptr;
#endif
};

} // namespace brulion::shell
