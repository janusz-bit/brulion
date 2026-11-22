pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// AppShell — the main UI once a vault is open (docs/UI.md "Window layout").
//
// Wide (≥ 900 px): SplitView — sidebar | top bar + tab bar + current view (one pane, or two side by
// side: the split view) | optional side panel.
// Tabs (docs/UI.md "Tabs, split view and windows"; AppController's tabs, tabs.h): ShellTabBar while
// there are two or more (wide), the top bar's tab switcher on phones; each pane is a ShellTabPane
// showing one tab — the main pane AppController.mainTab, the split pane AppController.splitTab
// (narrow: one pane, the focused tab). Pages in windows of their own (desktop): ShellPageWindow, one
// per AppController.windowTabs entry, made and destroyed here. The editor and the lists open
// links in a new tab through OpenIn (set up here). Shortcuts: Ctrl+T, Ctrl+W, Ctrl+Tab,
// Ctrl+Shift+Tab, Ctrl+1…9.
// Narrow: the sidebar is a Drawer (the top bar's ☰, or a swipe from the left edge — on touch UIs
// from a strip beyond the zone Android's gesture navigation keeps for "back") and a bottom
// navigation bar of views only (quick capture is the top bar's +); the Android back key walks
// back through AppController's history and only lets the app close from the home view.
//
// Owns the shell's popups (command palette, quick switcher, quick capture, new page or folder,
// rename, "Move to…", delete confirmation, about) and the global shortcuts (ShortcutMap's,
// custom ones too; docs/UI.md "Custom shortcuts") and focus mode (no bars around a page or the
// journal: AppSettings.focusMode, "Statistics and focus mode"); bars, the sidebar
// and views call the open*() functions (views through AppController.requestItemAction(action,
// path): move, rename, delete, newPage, newFolder, reveal). Content is laid out edge-to-edge: bars extend under the system bars and pad
// themselves with the SafeArea margins.
// On-screen keyboard (docs/UI.md "On-screen keyboard"): the content — sidebar, view and side
// panel — ends at the keyboard's top (KeyboardInsets.overlap, a strip under it), so the editing
// toolbar a view docks on its bottom edge sits on the keyboard, in the side panel too. While the
// keyboard is up the bottom navigation makes way; when it goes (Back, the keyboard's own key)
// the navigation comes back and a toolbar that stays — the editing goes on — docks right above
// it (the top bar's + then ends that editing before quick capture opens); a bar a view has at
// the bottom instead of the navigation (the drawing's tools) takes its place.
FocusScope {
    id: shell

    /// The window's toast host (Main.qml).
    property BToastHost toasts: null
    property bool sidebarVisible: flag(AppSettings.value("ui/sidebar/visible", true))
    /// How much of the page panel shows, 0…1: it follows `sidebarVisible` over Theme.duration — the
    /// panel slides under the rail and the top bar takes its room — while the panel's button stays
    /// where it is (`sidebarToggle`). (Not on creation: a Behavior leaves the first value alone.)
    property real sidebarReveal: sidebarVisible ? 1 : 0
    Behavior on sidebarReveal { NumberAnimation { duration: Theme.duration; easing.type: Theme.easing } }
    /// The panel's width while it slides: the one the user's split handle left it, taken as it hides.
    property real sidebarFullWidth: Theme.sidebarWidth

    readonly property var safe: SafeArea.margins
    /// The system's navigation bar under the window's bottom edge (the SafeArea margin), without
    /// the keyboard: on Android Qt's SafeArea counts the keyboard in at times — 336 px with it up,
    /// and for a moment after it went (the emulator's logs) —, which would lift the bottom
    /// navigation's tabs into the middle of the screen and the content with them. No navigation
    /// bar is that tall: a margin above `navigationBarMax` keeps the last one below it. The
    /// bottom navigation pads itself with it, the content clears it while typing.
    readonly property real navigationBar: shell.SafeArea.margins.bottom <= navigationBarMax ? shell.SafeArea.margins.bottom
                                                                                            : _lastNavigationBar
    /// The tallest system navigation bar (logical px; Android's are 16–56 dp, a tablet's taskbar
    /// included); as in ViewportKeyboard::MinKeyboard, anything taller is a keyboard.
    readonly property real navigationBarMax: 120
    property real _lastNavigationBar: 0
    function noteNavigationBar(): void {
        if (shell.SafeArea.margins.bottom <= shell.navigationBarMax)
            shell._lastNavigationBar = shell.SafeArea.margins.bottom
    }
    onSafeChanged: noteNavigationBar()
    /// The current route's view and path ("" when it has none).
    readonly property string currentView: AppController.currentView
    readonly property string routePath: routeTitles.field(AppController.currentRoute, "path")
    /// The main window shows two panes side by side (wide windows).
    readonly property bool split: !Theme.narrow && AppController.splitTab !== 0
    /// The focused pane's view: the top bar, the side panel's context and the keyboard rules are its.
    readonly property ShellRouteView routeView: split && AppController.splitFocused ? splitPane.routeView : mainPane.routeView
    /// The split pane is the focused one. The keyboard follows (Ctrl+Tab to the other pane's tab)
    /// when it was in one of the panes.
    readonly property bool splitPaneFocused: split && AppController.splitFocused
    onSplitPaneFocusedChanged: {
        const pane = shell.splitPaneFocused ? splitPane : mainPane
        if (!pane.activeFocus && (mainPane.activeFocus || splitPane.activeFocus))
            pane.forceActiveFocus()
    }
    /// The tab bar (wide windows): two tabs or more, or a split view.
    readonly property bool tabBarShown: !Theme.narrow && (AppController.tabCount > 1 || AppController.splitTab !== 0)
    /// How much of the window's bottom the on-screen keyboard covers.
    readonly property real keyboardOverlap: KeyboardInsets.overlap
    /// Width of the strip at the left edge whose swipe opens the drawer (touch UIs), beyond the
    /// system's own zones (drawer.dragMargin).
    readonly property real edgeSwipeStrip: 24
    /// A view's editing toolbar is docked at its bottom edge (touch UIs): the main view's or the
    /// side panel's.
    readonly property bool toolbarDocked: shell.routeView.viewToolbarDocked || (sidePanel !== null && sidePanel.toolbarDocked)
    /// The side panel (wide windows), null until it first shows.
    readonly property ShellSidePanel sidePanel: sidePanelLoader.item as ShellSidePanel
    property bool _sidePanelMade: false
    /// Typing: the keyboard is up, or a toolbar stays docked (a menu took the keys and the system
    /// hid the keyboard; one hidden by Back takes its toolbar with it).
    readonly property bool typing: KeyboardInsets.shown || toolbarDocked
    /// The bottom navigation's height (with the system's navigation bar under it), shown or not.
    readonly property real bottomNavHeight: bottomNav.implicitHeight
    /// The keyboard takes the bottom navigation's place: it is up — but while it still slides in
    /// over the window below the navigation's top (KeyboardInsets.slidesOver: Android 11+, a
    /// simulated keyboard) the navigation stays under it until covered, so a toolbar docked on
    /// the navigation never drops before it rises with the keyboard. (The browser resizes the
    /// page for it: the navigation goes at once.)
    readonly property bool keyboardUp: KeyboardInsets.shown
        && !(KeyboardInsets.animating && KeyboardInsets.slidesOver && keyboardOverlap < bottomNavHeight)
    /// Phones: the bottom navigation shows unless the keyboard is up or the view has a bar of
    /// its own at the bottom instead (the drawing's tools). An editing toolbar that stays after
    /// the keyboard went (a menu above it took the keys) docks right above it.
    readonly property bool bottomNavShown: Theme.narrow && !keyboardUp && !shell.routeView.viewReplacesBottomNav && !focusMode
    /// Room under the content: the keyboard (minus the bottom navigation, while that is shown:
    /// sliding, the keyboard covers or uncovers it first); while typing without the navigation
    /// at least the system's navigation bar, which the docked toolbar must clear, and the
    /// navigation's place under a keyboard that took it (`keyboardFloor`).
    readonly property real keyboardRoom: bottomNavShown ? Math.max(0, keyboardOverlap - bottomNavHeight)
                                         : typing ? Math.max(keyboardOverlap, navigationBar, keyboardFloor) : keyboardOverlap
    /// Phones: a keyboard sliding over the window that took the bottom navigation's place but
    /// ends below its top (a suggestion strip over a hardware keyboard, shorter than the
    /// navigation, which stayed under it while it came) leaves the content the navigation's
    /// room. A toolbar docked on the navigation stays where it was when such a keyboard lands —
    /// it never drops onto it after it came — and when it goes. (A keyboard reported in one step
    /// took the navigation at once, the toolbar moving with it then.)
    readonly property real keyboardFloor: Theme.narrow && keyboardUp && KeyboardInsets.slidesOver && keyboardOverlap > 0
                                          && !shell.routeView.viewReplacesBottomNav ? bottomNavHeight : 0
    /// What covers the window's bottom edge for toasts (Main.qml): on phones the bottom
    /// navigation, with a toolbar docked on it; else the keyboard and a docked toolbar while
    /// typing; else the system's navigation bar.
    readonly property real toastClearance: bottomNavShown
        ? keyboardRoom + bottomNavHeight + (toolbarDocked ? Theme.editToolbarHeight : 0)
        : typing ? keyboardRoom + (toolbarDocked ? Theme.editToolbarHeight : 0) : navigationBar + keyboardRoom
    /// Toasts go to the window's top (Main.qml) while text is typed on a touch screen — the
    /// on-screen keyboard up, or an editing toolbar docked while a view edits text: the caret's
    /// line is kept right above the keyboard and the toolbar, under the toasts' usual place — and
    /// while an extension's question is a bottom sheet: the AI assistant's "Asking AI…" covered
    /// the change it asks about and its choices.
    readonly property bool toastsAtTop: KeyboardInsets.shown || (toolbarDocked && shell.routeView.viewEditing)
                                        || (extensionQuestion !== null && extensionQuestion.opened && extensionQuestion.sheet)
    readonly property string title: shell.routeView.viewTitle !== "" ? shell.routeView.viewTitle
                                    : routeTitles.routeTitle(shell.currentView, shell.routePath,
                                                             routeTitles.field(AppController.currentRoute, "date"),
                                                             routeTitles.field(AppController.currentRoute, "tag"),
                                                             routeTitles.field(AppController.currentRoute, "title"))
    /// Folders above the current page, or above the folder All pages shows (with "All
    /// pages" first): their titles (`crumbs`) and paths (`crumbPaths`; "" the notebook's root) —
    /// a crumb opens All pages in that folder.
    readonly property string crumbFolder: currentView === "allPages" ? Js.slice(routePath, 0, Math.max(0, Js.lastIndexOf(routePath, "/")))
                                        : currentView === "page" && routePath !== "" ? Workspace.folderOf(routePath) : ""
    readonly property list<string> crumbFolderParts: crumbFolder !== "" ? Js.split(crumbFolder, "/") : []
    readonly property list<string> crumbs: currentView === "allPages" && routePath !== ""
                                           ? Js.insertedStringAt(crumbFolderParts, 0, qsTr("All pages")) : crumbFolderParts
    readonly property list<string> crumbPaths: currentView === "allPages" && routePath !== ""
                                               ? Js.insertedStringAt(AppController.folderChain(crumbFolder), 0, "")
                                               : AppController.folderChain(crumbFolder)
    /// The file the current route shows: a page, or a daily note that exists.
    readonly property string currentFile: {
        if (shell.currentView === "page")
            return shell.routePath
        const date = routeTitles.field(AppController.currentRoute, "date")
        // (The names: the daily note created or deleted. Read for real: a bare `void
        // Workspace.namesRevision` is compiled away with the dependency.)
        if (shell.currentView !== "journal" || date === "" || Workspace.namesRevision < 0)
            return ""
        const path = shell.routePath !== "" ? shell.routePath : Workspace.journalPath(date)
        return Workspace.exists(path) ? path : ""
    }

    function flag(value: var): bool {
        return value === true || value === "true"
    }

    /// Focus mode (AppSettings.focusMode; docs/UI.md "Statistics and focus mode"): where an editor
    /// shows a page or the journal, no sidebar, top bar, side panel or bottom navigation — the
    /// text column alone, centred. F8 (ShortcutMap "view.focus"), Esc once the view does not need
    /// it, the Back key and the corner button leave it; other views show their bars as usual.
    readonly property bool focusMode: AppSettings.focusMode && ["page", "journal", "journals"].indexOf(currentView) >= 0
    function leaveFocusMode(): void {
        AppSettings.focusMode = false
    }

    // The detailed event log notes where the view ends while typing, once the keyboard has
    // settled (scripts/android-emulator-test.sh --keyboard checks a docked toolbar on the
    // keyboard's top, and on the bottom navigation after Back, with it).
    LoggingCategory {
        id: platformLog
        name: "brulion.platform"
        defaultLogLevel: LoggingCategory.Info
    }
    readonly property string typingReport: !typing ? ""
        : "Typing: the content ends %1 px above the window's bottom (keyboard %2 px, navigation bar %3 px, bottom navigation %4 px), editing toolbar %5"
          .arg(Math.round(keyboardRoom + (bottomNavShown ? bottomNavHeight : 0))).arg(Math.round(keyboardOverlap))
          .arg(Math.round(navigationBar)).arg(bottomNavShown ? Math.round(bottomNavHeight) : 0)
          .arg(toolbarDocked ? "docked" : "not docked")
    property string _loggedTyping: ""
    function logTyping(): void {
        if (KeyboardInsets.animating || shell.typingReport === shell._loggedTyping)
            return
        console.debug(platformLog, shell.typingReport !== "" ? shell.typingReport : "Typing ended")
        shell._loggedTyping = shell.typingReport
    }
    // (Qt.callLater: once per turn of the event loop.)
    Later {
        id: logTypingLater
        onTriggered: shell.logTyping()
    }
    onTypingReportChanged: logTypingLater.restart()
    Connections {
        target: KeyboardInsets
        function onAnimatingChanged() { logTypingLater.restart() }
    }

    // Titles of routes and pages (also the tab bar's, the tab switcher's, a page window's).
    ShellRouteTitles { id: routeTitles }

    function openPalette(query: string): void {
        const popup = shell.paletteNow()
        popup.open()
        if (query !== "")
            popup.typeQuery(query)
    }
    function openSwitcher(query: string): void {
        const popup = shell.switcherNow()
        popup.open()
        if (query !== "")
            popup.typeQuery(query)
    }
    function openCapture(): void {
        // Captured notes belong in the user's journal: from the documentation, AppController
        // goes back to the user's notebook and opens the dialog there.
        if (AppController.inDocsVault)
            AppController.quickCapture()
        else
            shell.captureNow().open()
    }
    /// The share dialog for what another app shared (AppController.pendingShare; Android's
    /// "Share → Brulion", the --share option).
    function openShare(): void {
        if (AppController.routeText(AppController.pendingShare, "kind") === "")
            return
        const dialog = shell.shareDialogNow()
        if (dialog.opened)
            dialog.reload() // a newer share replaced the one shown
        else
            dialog.open()
    }
    /// The confirmation of a brulion:// address that asks to write without the notebook's key
    /// (AppController.pendingAutomation; docs/UI.md "Automation").
    function openAutomation(): void {
        if (AppController.routeText(AppController.pendingAutomation, "action") === "")
            return
        const dialog = shell.automationDialogNow()
        if (!dialog.opened)
            dialog.open()
    }
    /// A new page: in `folder`, or — Ctrl+N, the top bar, the sidebar's pencil pass "" — in the
    /// folder of the page shown, the root too (a removable chip in the dialog); else the
    /// notebook's folder for new pages.
    function openNewPage(folder: string): void {
        if (folder === "" && shell.currentView === "page" && shell.routePath !== ""
                && Workspace.journalDate(shell.routePath) === "") {
            shell.newPageNow().openFor(Workspace.folderOf(shell.routePath), false, true)
            return
        }
        shell.newPageNow().openFor(folder, false, false)
    }
    function openNewFolder(parentFolder: string): void { shell.newPageNow().openFor(parentFolder, true, false) }
    function openRename(path: string, isDir: bool): void { shell.renameNow().openFor(path, isDir) }
    /// "Move to…" for a page, an attachment or a folder.
    function openMove(path: string, isDir: bool): void {
        if (path !== "")
            shell.moveDialogNow().openFor(path, isDir)
    }
    /// The last move's Undo: its toast (AppController.toastWithAction) and where it moves back.
    property int _moveToast: 0
    property string _movedPath
    property string _moveBack
    property var _moveImpact: ({})
    /// Move `path` into `folder` (created first when `create`; "" = the root) and say how it went,
    /// with Undo (moving it back).
    function performMove(path: string, folder: string, create: bool): bool {
        let target = folder
        if (create) {
            target = shell.textOf(Workspace.createFolder(folder))
            if (target === "")
                return false
        }
        // (Kept in a property: a call's result read after another call is not compiled.)
        shell._moveImpact = Workspace.moveImpact(path, target)
        const tags = shell.intOf(shell._moveImpact, "tags")
        const links = shell.intOf(shell._moveImpact, "links")
        const moved = shell.textOf(Workspace.movePath(path, target))
        if (moved === "" || moved === path)
            return false
        const text = moveWords.movedText(Workspace.folderOf(moved), links + tags, tags > 0, "")
        if (shell.toasts) {
            shell._movedPath = moved
            shell._moveBack = Workspace.folderOf(path)
            shell._moveToast = AppController.toastWithAction(text, "success", qsTr("Undo"))
        }
        return true
    }
    /// A map's field as an int (AppController.routeInt, 0 when missing) / a text, typed.
    function intOf(map: var, key: string): int {
        return AppController.routeInt(map, key, 0)
    }
    function textOf(text: string): string {
        return text
    }
    function intValue(value: int): int {
        return value
    }
    /// "Show in tree": the sidebar (the drawer on phones) shows `path`, its "Pages" section
    /// opened if it was collapsed.
    function revealInTree(path: string): void {
        if (path === "")
            return
        if (Theme.narrow)
            drawer.open()
        else if (!shell.sidebarVisible)
            shell.toggleSidebar()
        const sidebar = (Theme.narrow ? drawerSidebar.item : sidebarLoader.item) as ShellSidebar
        if (sidebar)
            sidebar.reveal(path, true)
    }
    function openAbout(): void { shell.aboutNow().open() }
    function confirmDelete(path: string): void {
        const dialog = shell.deleteDialogNow()
        dialog.path = path
        dialog.open()
    }
    function toggleSidebar(): void {
        if (shell.sidebarVisible && sidebarSlot.visible && sidebarSlot.width > 0)
            shell.sidebarFullWidth = sidebarSlot.width
        shell.sidebarVisible = !shell.sidebarVisible
        AppSettings.setValue("ui/sidebar/visible", shell.sidebarVisible)
    }
    /// AppController.uiActionRequested: the shell's popups.
    function handleUiAction(action: string): void {
        // An extension page's approval, an extension's settings, a copy to another notebook
        // (docs/UI.md "Extensions"): the action names the page or the extension.
        if (Js.startsWith(action, "approveExtension:")) {
            shell.openExtensionApproval(Js.sliceFrom(action, 17))
            return
        }
        if (Js.startsWith(action, "extensionSettings:")) {
            shell.openExtensionSettings(Js.sliceFrom(action, 18))
            return
        }
        if (Js.startsWith(action, "copyExtension:")) {
            shell.openExtensionCopy(Js.sliceFrom(action, 14))
            return
        }
        switch (action) {
        case "palette": shell.openPalette(""); break
        case "switcher": shell.openSwitcher(""); break
        case "capture": shell.openCapture(); break
        case "share": shell.openShare(); break
        case "automation": shell.openAutomation(); break
        case "newPage": shell.openNewPage(""); break
        case "newEncryptedPage": shell.openNewEncryptedPage(""); break
        case "newFolder": shell.openNewFolder(""); break
        case "movePage": if (shell.currentView === "page") shell.openMove(shell.routePath, false); break
        case "revealInTree": shell.revealInTree(shell.currentFile); break
        case "trashPage": if (shell.currentFile !== "") shell.confirmDelete(shell.currentFile); break
        case "about": shell.openAbout(); break
        case "toggleSidePanel": AppController.sidePanelVisible = !AppController.sidePanelVisible; break
        case "toggleContextDrawer": contextDrawer.toggle(); break
        case "renamePage": if (shell.currentView === "page") shell.openRename(shell.routePath, false); break
        // A voice note: the note on screen takes it at its caret (PageView); anywhere else it goes
        // into today's journal.
        case "recordVoiceNote":
            if (shell.currentView !== "page" || !Js.test(shell.routePath, "\\.(md|markdown)$", "i"))
                shell.recordVoiceNote()
            break
        }
    }

    focus: true

    // Android back key / gesture: back through the history; at home the app may close. Focus
    // mode first ends (also Esc, once nothing in the view took it), then the view takes a step of
    // its own (a block zoomed into zooms out one level). Each is one Back: the press is taken and
    // the step done on the release, which is taken too, so it never walks the history as well.
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Back)
            event.accepted = shell.focusMode || shell.routeView.viewCanStepBack || AppController.canGoBack
                             || !AppController.atHome
        else if (event.key === Qt.Key_Escape && shell.focusMode) {
            shell.leaveFocusMode()
            event.accepted = true
        }
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Back && shell.focusMode) {
            shell.leaveFocusMode()
            event.accepted = true
        } else if (event.key === Qt.Key_Back && shell.viewStepBack()) {
            event.accepted = true
        } else if (event.key === Qt.Key_Back) {
            event.accepted = AppController.handleBackKey()
        }
    }
    /// Back's step in the shown view before the history (a zoomed block zooms out); true when it
    /// had one. Also Main.qml's step for a Back no item took.
    function viewStepBack(): bool {
        if (!shell.routeView.viewCanStepBack)
            return false
        shell.routeView.stepBack()
        return true
    }

    onWidthChanged: if (!Theme.narrow && drawer.opened) drawer.close()

    // The rail of views at the window's left edge (wide windows; phones have it in the drawer):
    // it stays when the sidebar's page panel is hidden, not in focus mode. Above the split: expanded
    // (the names under the pointer) it lies over the page panel — as wide as it shows itself
    // (faceWidth, so its wider part takes the pointer), the split beside its narrow place.
    Loader {
        id: rail
        /// Its place in the layout: the narrow rail's width.
        readonly property real layoutWidth: visible && rail.item ? (rail.item as Item).implicitWidth : 0
        z: 1
        active: !Theme.narrow
        visible: active && !shell.focusMode
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: statusBar.top
        width: visible && rail.item ? (rail.item as ShellRail).faceWidth : 0
        sourceComponent: ShellRail {
            objectName: "shellRail"
            expandable: AppSettings.railExpands && !Theme.touchUi
            topInset: shell.SafeArea.margins.top
            bottomInset: Math.max(0, shell.navigationBar - shell.keyboardRoom)
        }
    }

    // The status bar along the bottom of wide desktop windows (sync, the view's counts, the
    // palette's key); the rest ends at its top, or at the keyboard's.
    ShellStatusBar {
        id: statusBar
        objectName: "shellStatusBar"
        readonly property bool shown: !Theme.narrow && !Theme.touchUi && !shell.focusMode
        visible: shown
        height: shown ? implicitHeight : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: shell.keyboardRoom
        routeView: shell.routeView
        onPaletteRequested: shell.openPalette("")
    }
    Binding { target: AppController; property: "statusBarShown"; value: statusBar.shown }

    SplitView {
        id: split
        anchors.left: parent.left
        anchors.leftMargin: rail.layoutWidth
        anchors.right: parent.right
        anchors.top: parent.top
        // Everything ends at the keyboard's top (the status bar's on the desktop): an editor in
        // the side panel docks its toolbar on the keyboard as well as the main view's.
        anchors.bottom: statusBar.top
        orientation: Qt.Horizontal
        handle: BSplitHandle { orientation: Qt.Horizontal }

        // The page panel. While it slides (`sidebarReveal` between 0 and 1) its room is the full
        // width times the share shown — the maximum caps the width the handle left, which stays
        // the preferred one —, and the panel keeps its full width inside, its right edge at the
        // room's: it goes under the rail and fades instead of squeezing its rows.
        Item {
            id: sidebarSlot
            readonly property bool sliding: shell.sidebarReveal < 1
            visible: !Theme.narrow && shell.sidebarReveal > 0 && !shell.focusMode
            clip: sliding
            SplitView.preferredWidth: Theme.sidebarWidth
            SplitView.minimumWidth: sliding ? 0 : 200
            SplitView.maximumWidth: sliding ? shell.sidebarFullWidth * shell.sidebarReveal : 440

            Loader {
                id: sidebarLoader
                active: !Theme.narrow
                width: sidebarSlot.sliding ? shell.sidebarFullWidth : sidebarSlot.width
                height: sidebarSlot.height
                x: sidebarSlot.width - width
                opacity: shell.sidebarReveal
                sourceComponent: ShellSidebar {
                    // (Its header's name starts after the panel's button, which stays over it.)
                    leadingRoom: sidebarToggle.x + sidebarToggle.width + Theme.space2 - rail.layoutWidth
                    topInset: shell.SafeArea.margins.top
                    // (The navigation bar's part the keyboard's room leaves.)
                    bottomInset: Math.max(0, shell.navigationBar - shell.keyboardRoom)
                    onNewPageRequested: (folder) => shell.openNewPage(folder)
                    onNewFolderRequested: (folder) => shell.openNewFolder(folder)
                    onRenameRequested: (path, isDir) => shell.openRename(path, isDir)
                    onDeleteRequested: (path) => shell.confirmDelete(path)
                    onMoveRequested: (path, isDir) => shell.openMove(path, isDir)
                    onDropMoveRequested: (path, folder) => shell.performMove(path, folder, false)
                }
            }
        }

        ColumnLayout {
            id: mainColumn
            SplitView.fillWidth: true
            SplitView.minimumWidth: 300
            spacing: 0

            // Focus mode: no top bar; the content clears the system's status bar itself.
            Item {
                visible: shell.focusMode
                Layout.preferredHeight: shell.SafeArea.margins.top
            }
            ShellTopBar {
                id: topBar
                objectName: "shellTopBar"
                visible: !shell.focusMode
                Layout.fillWidth: true
                title: shell.title
                crumbs: shell.crumbs
                crumbPaths: shell.crumbPaths
                topInset: shell.SafeArea.margins.top
                // (The part of the bar the page panel's button covers: all of it while the
                // panel is hidden, less as the panel slides in, nothing once it shows.)
                leadingRoom: sidebarToggle.visible ? sidebarToggle.x + sidebarToggle.width - split.x - mainColumn.x : 0
                picture: shell.routeView.viewPicture
                pictureAsText: shell.routeView.viewPictureAsText
                pdf: shell.routeView.viewPdf
                fileOnly: shell.routeView.viewFile
                noteMode: shell.routeView.viewNoteMode
                onMenuClicked: drawer.open()
                onPaletteRequested: shell.openPalette("")
                onSwitcherRequested: shell.openSwitcher("")
                // On a phone a block still edited with the keyboard hidden (its toolbar docked on
                // the bottom navigation) is ended first, as a tab's view change ends it.
                onCaptureRequested: {
                    if (Theme.narrow)
                        shell.routeView.endEditing()
                    shell.openCapture()
                }
                onNewPageRequested: shell.openNewPage("")
                onRenameRequested: (path) => shell.openRename(path, false)
                onDeleteRequested: (path) => shell.confirmDelete(path)
                onMoveRequested: (path) => shell.openMove(path, false)
                onRevealRequested: (path) => shell.revealInTree(path)
                onAboutRequested: shell.openAbout()
                contextAvailable: contextDrawer.available
                onContextRequested: contextDrawer.open()
            }
            // Not while typing: the room above the on-screen keyboard is the note's.
            // (Made once a newer version is known: docs/PERFORMANCE.md "L10".)
            Loader {
                active: AppController.updates.availableVersion !== ""
                visible: active && !shell.keyboardUp && !shell.focusMode
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space2
                Layout.rightMargin: Theme.space2
                Layout.topMargin: Theme.space2
                sourceComponent: ShellUpdateBanner {}
            }
            // The extensions' timers (b.timer): every view but the one that shows its own (the
            // gym's rest in the Gym view).
            Loader {
                id: timerBanner
                readonly property string hiddenExtension: shell.currentView === "gym" ? "silownia" : ""
                readonly property int shown: Workspace.extensions.timers.length
                                             - (hiddenExtension !== "" && Workspace.extensions.restRemaining > 0 ? 1 : 0)
                active: shown > 0
                visible: active && !shell.keyboardUp && !shell.focusMode
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space2
                Layout.rightMargin: Theme.space2
                Layout.topMargin: Theme.space2
                sourceComponent: ShellTimerBanner { hiddenExtension: timerBanner.hiddenExtension }
            }
            ShellTabBar {
                id: tabBar
                objectName: "shellTabBar"
                visible: shell.tabBarShown
                Layout.fillWidth: true
            }
            // One pane, or two side by side (the split view). Only the tabs shown have views.
            SplitView {
                id: panes
                Layout.fillWidth: true
                Layout.fillHeight: true
                focus: true
                orientation: Qt.Horizontal
                handle: BSplitHandle { orientation: Qt.Horizontal }

                ShellTabPane {
                    id: mainPane
                    objectName: "shellMainPane"
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 280
                    focus: !shell.split || !AppController.splitFocused
                    // Phones: one pane, the focused tab.
                    tabId: Theme.narrow ? AppController.focusedTab : AppController.mainTab
                    paneActive: !shell.split || !AppController.splitFocused
                    // The calendar grids' swipe took the swipe that closes the drawer over them.
                    covered: drawer.visible
                    // Tasks, Calendar… are built in the background: the first switch is instant.
                    preloadViews: true
                    onActivated: if (!Theme.narrow) AppController.focusPane(false)
                }
                ShellTabPane {
                    id: splitPane
                    objectName: "shellSplitPane"
                    visible: shell.split
                    SplitView.preferredWidth: panes.width / 2
                    SplitView.minimumWidth: 280
                    focus: shell.split && AppController.splitFocused
                    tabId: shell.split ? AppController.splitTab : 0
                    paneActive: shell.split && AppController.splitFocused
                    onActivated: if (shell.split) AppController.focusPane(true)
                }
            }

            // The palette's block commands follow the main view's source mode (CommandModel).
            Binding { target: AppController; property: "sourceModeShown"; value: shell.routeView.viewSourceMode }
            // Focus mode on a phone: no bottom navigation; the content clears the system's
            // navigation bar (while typing the keyboard's room does).
            Item {
                visible: shell.focusMode && !shell.typing
                Layout.preferredHeight: shell.navigationBar
            }
            ShellBottomNav {
                id: bottomNav
                objectName: "shellBottomNav"
                visible: shell.bottomNavShown
                // Back after the keyboard (in one step where the platform reports no slide, the
                // browser): it fades in where it already has its room, so the view and a toolbar
                // docked on it move once. Under a keyboard sliding away the fade is covered.
                opacity: shell.bottomNavShown ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                Layout.fillWidth: true
                bottomInset: shell.navigationBar
            }
        }

        // The side panel: made the first time it shows (docs/PERFORMANCE.md "L10"), then kept.
        Loader {
            id: sidePanelLoader
            visible: !Theme.narrow && AppController.sidePanelVisible && !shell.focusMode
            active: shell._sidePanelMade
            // (Not from the Loader's own signal: setting `active` from it would be a binding loop.)
            onVisibleChanged: if (visible) shell._sidePanelMade = true
            Component.onCompleted: if (visible) shell._sidePanelMade = true
            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 260
            SplitView.maximumWidth: 600
            sourceComponent: ShellSidePanel {
                pinnedTitle: routeTitles.routeTitle(routeTitles.field(AppController.sidePanelRoute, "view"),
                                                    routeTitles.field(AppController.sidePanelRoute, "path"),
                                                    routeTitles.field(AppController.sidePanelRoute, "date"),
                                                    routeTitles.field(AppController.sidePanelRoute, "tag"),
                                                    routeTitles.field(AppController.sidePanelRoute, "title"))
                contextPanel: shell.routeView.viewSidePanel
            }
        }
    }
    // The page panel's button (wide windows), over the top bar's start while the panel is hidden
    // and over the panel's header while it shows: the same place either way, so it never jumps
    // under the pointer as the panel goes and comes back (the top bar and the panel's header
    // leave it room: their leadingRoom).
    BIconButton {
        id: sidebarToggle
        objectName: "shellSidebarToggle"
        visible: !Theme.narrow && !shell.focusMode
        // (Beside the narrow rail: the widened one lies over it, nothing moves.)
        x: rail.layoutWidth + Theme.space2
        y: split.y + topBar.topInset + Math.round((topBar.height - topBar.topInset - height) / 2)
        iconName: "panel-left"
        label: shell.sidebarVisible ? qsTr("Hide sidebar") : qsTr("Show sidebar")
        checkable: true
        checked: shell.sidebarVisible
        onClicked: shell.toggleSidebar()
    }

    // The keyboard's room (it covers this part of the window); under a docked toolbar when the
    // keyboard is hidden and the bottom navigation is not shown (a wide touch window): the
    // navigation bar's strip.
    Rectangle {
        objectName: "shellKeyboardRoom"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: shell.keyboardRoom
        visible: shell.keyboardRoom > 0
        // Magenta in the browser's keyboard self-test: a screenshot shows any gap above the keyboard.
        color: KeyboardInsets.marked ? "#ff00ff" : Theme.bgElevated
    }

    // The sound that plays on while nothing on screen shows it (docs/UI.md "Audio and video"):
    // over the bottom of the window, above the bottom navigation and the keyboard.
    MediaNowPlaying {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: shell.keyboardRoom + (shell.bottomNavShown ? shell.bottomNavHeight + shell.navigationBar : shell.navigationBar)
                              + Theme.space3
        width: Math.min(implicitWidth, parent.width - 2 * Theme.space4)
        z: 50
    }

    // Focus mode's way out where no key is at hand (phones): a quiet button in the top corner,
    // clearer under the pointer.
    BIconButton {
        id: focusExit
        objectName: "focusModeExit"
        visible: shell.focusMode
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: shell.SafeArea.margins.top + Theme.space2
        anchors.rightMargin: shell.SafeArea.margins.right + Theme.space3
        z: 5
        iconName: "minimize"
        toolTipBelow: true
        label: qsTr("Leave focus mode")
        shortcut: ShortcutMap.sequences["view.focus"] || ""
        opacity: Theme.touchUi || hovered || activeFocus ? 1 : 0.35
        onClicked: shell.leaveFocusMode()
    }

    /// The drawer's content exists (made once: drawer's onVisibleChanged, drawerTimer).
    property bool _drawerMade: false
    Timer {
        id: drawerTimer
        interval: 600
        running: Theme.narrow && !shell._drawerMade && Workspace.ready
        onTriggered: shell._drawerMade = true
    }
    Drawer {
        id: drawer
        objectName: "shellDrawer"
        // The rail and the page panel beside it.
        width: Math.min(340, shell.width * 0.88)
        height: shell.height
        edge: Qt.LeftEdge
        // (Not in focus mode: nothing but the text; its button or Back leave it first.)
        interactive: Theme.narrow && !shell.focusMode
        // The edge swipe: a finger that starts within dragMargin of the left edge and moves
        // right opens the drawer (on it or the scrim, to the left, closes it). Touch UIs get a
        // strip a finger hits (Qt's default is the start-drag distance, a few px) that begins
        // beyond what the system keeps at that edge: gesture navigation's "back" zone
        // (GestureInsets, ~30 dp; a swipe there leaves the app), or a display cutout / system bar
        // in landscape (SafeArea). Taps and vertical scrolls in the strip still reach the content:
        // the drawer takes only a mostly horizontal drag past its threshold (20 px, more than a
        // Flickable's), not one an item already keeps (a scrolling Flickable). Android's text
        // selection handles are windows of their own above the app: the strip never sees them.
        // While typing or selecting blocks (the keyboard up or an editing toolbar docked, also
        // on the bottom navigation after the keyboard went) there is no strip: the drawer took
        // the drag of the selection's start grip, which lies at the left edge (its MouseArea's
        // preventStealing did not stop it). Nor on a view that swipes sideways from that edge
        // itself (`edgeSwipe`: the calendar's week and month grids, to the previous period): a
        // swipe there is the view's. The top bar's ☰ still opens it, a swipe on it still closes
        // it. The strip follows
        // the UI mode, not the pointer: a mouse in a touch UI gets it too.
        dragMargin: !Theme.touchUi ? Application.styleHints.startDragDistance
                    : shell.typing || shell.routeView.viewEdgeSwipe ? 0
                    : Math.max(shell.SafeArea.margins.left, GestureInsets.left) + shell.edgeSwipeStrip
        dim: true
        // Focus while open (Drawer's default, stated because the keys rely on it): Escape / the
        // Android back key close the drawer instead of acting underneath it.
        focus: true
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle { color: Theme.bgSunken }
        // Edge to edge: the system bars are counted once, by the sidebar's own insets (topInset,
        // bottomInset below), its bgSunken under the status and navigation bars. The Basic
        // style pads a Drawer by the SafeArea margins itself (topPadding: SafeArea.margins.top
        // …), which came on top of those insets: about 190 px of the tree lost on an Android
        // phone, a band above the notebook row and a footer twice as tall. The side insets
        // stay the style's (a cutout or a bar in landscape).
        topPadding: 0
        bottomPadding: 0

        // Its rail and page panel (phones) are made after the first content — 0.6 s after the
        // notebook is ready (drawerTimer below) — or when the drawer first opens: not before the
        // first frame (docs/PERFORMANCE.md "L10"); kept after.
        onVisibleChanged: if (visible) shell._drawerMade = true
        Loader {
            id: drawerRail
            active: Theme.narrow && shell._drawerMade
            // (Built ahead in pieces over several frames; opened before it is ready, at once.)
            asynchronous: !drawer.visible
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: drawerRail.item ? (drawerRail.item as Item).implicitWidth : 0
            sourceComponent: ShellRail {
                objectName: "drawerRail"
                topInset: shell.SafeArea.margins.top
                bottomInset: shell.navigationBar
                onNavigated: drawer.close()
            }
        }
        Loader {
            id: drawerSidebar
            anchors.left: drawerRail.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            active: Theme.narrow && shell._drawerMade
            asynchronous: !drawer.visible
            sourceComponent: ShellSidebar {
                topInset: shell.SafeArea.margins.top
                bottomInset: shell.navigationBar
                onNavigated: drawer.close()
                onNewPageRequested: (folder) => {
                    drawer.close()
                    shell.openNewPage(folder)
                }
                onNewFolderRequested: (folder) => {
                    drawer.close()
                    shell.openNewFolder(folder)
                }
                onRenameRequested: (path, isDir) => {
                    drawer.close()
                    shell.openRename(path, isDir)
                }
                onDeleteRequested: (path) => {
                    drawer.close()
                    shell.confirmDelete(path)
                }
                onMoveRequested: (path, isDir) => {
                    drawer.close()
                    shell.openMove(path, isDir)
                }
                onDropMoveRequested: (path, folder) => shell.performMove(path, folder, false)
            }
        }
    }

    // The side panel of narrow windows: a drawer from the right edge with its handle (wide
    // windows dock ShellSidePanel beside the text instead).
    ShellContextDrawer {
        id: contextDrawer
        objectName: "shellContextDrawer"
        anchors.fill: parent
        anchors.bottomMargin: shell.keyboardRoom
        contextPanel: shell.routeView.viewSidePanel
        title: shell.title
        allowed: !shell.focusMode && !drawer.visible
        topInset: shell.SafeArea.margins.top
        bottomInset: Math.max(0, shell.navigationBar - shell.keyboardRoom)
        onClosed: shell.routeView.forceActiveFocus()
    }

    // ---- Popups -----------------------------------------------------------------------------
    // Not made with the shell (docs/PERFORMANCE.md "L10: lazy cold popups"): a closed popup still
    // creates all of its items — the command palette even filters every command into the rows of
    // its list —, and none of them is on the first screen. The ones used most — the switcher (the
    // top bar's search field), the palette, quick capture and the top bar's More menu — are built
    // ahead in the background once the notebook is ready (prebuildTimer below), so their first
    // opening is as quick as before; the others are made the first time they open. Until then the
    // property is null; the open*() functions above make it through its *Now() function (also
    // --popup and the self-tests: the dialogs' objectNames are found once they exist).

    /// The popups built ahead so far (prebuildNext()).
    property int _prebuilt: 0
    Timer {
        id: prebuildTimer
        // After the kept views (ShellRouteView.preloadViews: 1.2 s after ready, then 0.4 s each).
        interval: shell._prebuilt === 0 ? 3000 : 400
        repeat: true
        running: Workspace.ready && shell._prebuilt < 4
        onTriggered: shell.prebuildNext()
    }
    /// Starts building the next popup used most, in pieces over several frames (an asynchronous
    /// Loader; opened before it is ready, the rest is made at once).
    function prebuildNext(): void {
        switch (shell._prebuilt) {
        case 0: switcherLoader.active = true; break
        case 1: paletteLoader.active = true; break
        case 2: topBar.prebuildMoreMenu(); break
        case 3: captureLoader.active = true; break
        }
        ++shell._prebuilt
    }

    // (Not `palette`: Item has a property of that name.)
    readonly property BCommandPalette commandPalette: paletteLoader.item as BCommandPalette
    function paletteNow(): BCommandPalette {
        // (Synchronous: an incubation in progress, built ahead, finishes at once.)
        paletteLoader.asynchronous = false
        paletteLoader.active = true
        return paletteLoader.item as BCommandPalette
    }
    Loader {
        id: paletteLoader
        active: false
        asynchronous: true
        sourceComponent: BCommandPalette {
            objectName: "shellPalette"
            commands: CommandModel.commands
            onCommandIdActivated: (id) => CommandModel.run(id)
        }
    }

    readonly property BQuickSwitcher switcher: switcherLoader.item as BQuickSwitcher
    function switcherNow(): BQuickSwitcher {
        switcherLoader.asynchronous = false
        switcherLoader.active = true
        return switcherLoader.item as BQuickSwitcher
    }
    Loader {
        id: switcherLoader
        active: false
        asynchronous: true
        sourceComponent: BQuickSwitcher {
            id: switcherPopup
            objectName: "shellSwitcher"
            // Workspace.namesRevision / propertiesRevision: refresh when pages come and go (or their
            // aliases or icons change) while it is open. Closed (visible stays true until the exit
            // transition ends), it neither queries nor follows them.
            pages: switcherPopup.visible && Workspace.namesRevision >= 0 && Workspace.propertiesRevision >= 0
                   ? Workspace.switcherPages(switcherPopup.query, 60) : []
            // The top bar's search field opens it: "> " turns to the commands, the last result
            // searches the notes' text.
            placeholderText: qsTr("Go to page… (> for commands)")
            offerSearch: true
            commandPrefix: true
            onSearchRequested: (text) => AppController.search(text)
            onCommandsRequested: (text) => shell.openPalette(text)
            onPagePathChosen: (path) => AppController.openPage(path)
            onCreateRequested: (name) => {
                const path = Workspace.createPage(name)
                if (path !== "")
                    AppController.openPage(path)
            }
        }
    }

    /// A voice note into today's journal (the palette's "Record voice note" off a note, quick
    /// capture's microphone; docs/UI.md "Voice notes"): the recorder, then the file stored as an
    /// attachment and embedded in a new block at the end of today's journal — transcribed there by
    /// itself when a speech model is here.
    function recordVoiceNote(): void {
        if (!MediaPlayback.available) {
            AppController.toast(qsTr("This version of Brulion cannot record."), "info")
            return
        }
        voiceLoader.active = true
        const dialog = voiceLoader.item as VoiceRecorderDialog
        if (dialog)
            dialog.open()
    }
    function storeVoiceNote(file: string): void {
        journalVoice.map = AppController.captureRecording(file)
        const path = "" + (journalVoice.map.path || "")
        const page = "" + (journalVoice.map.page || "")
        const target = "" + (journalVoice.map.target || "")
        if (path === "")
            return
        if (shell.toasts)
            shell._captureToast = AppController.toastWithAction(qsTr("Voice note added to today's journal"), "success", qsTr("Open"))
        if (AppController.speech.ready && AppController.speech.automatic)
            AppController.speech.transcribe(path, page, target, -1)
    }
    MapHolder { id: journalVoice }
    Loader {
        id: voiceLoader
        active: false
        sourceComponent: VoiceRecorderDialog {
            objectName: "shellVoiceRecorder"
            onRecorded: (file) => shell.storeVoiceNote(file)
            onReleased: voiceLater.restart()
        }
    }
    Later {
        id: voiceLater
        onTriggered: {
            // (Not one opened again meanwhile.)
            const dialog = voiceLoader.item as VoiceRecorderDialog
            if (!dialog || !dialog.visible)
                voiceLoader.active = false
        }
    }

    readonly property ShellQuickCapture capture: captureLoader.item as ShellQuickCapture
    function captureNow(): ShellQuickCapture {
        captureLoader.asynchronous = false
        captureLoader.active = true
        return captureLoader.item as ShellQuickCapture
    }
    Loader {
        id: captureLoader
        active: false
        asynchronous: true
        sourceComponent: ShellQuickCapture {
            objectName: "shellCapture"
            onCaptured: {
                if (shell.toasts)
                    shell._captureToast = AppController.toastWithAction(qsTr("Added to today's journal"), "success", qsTr("Open"))
            }
            onVoiceRequested: shell.recordVoiceNote()
        }
    }

    property ShellAutomationDialog automationDialog: null
    function automationDialogNow(): ShellAutomationDialog {
        if (!shell.automationDialog)
            shell.automationDialog = automationComponent.createObject(shell) as ShellAutomationDialog
        return shell.automationDialog
    }
    Component {
        id: automationComponent
        ShellAutomationDialog { objectName: "shellAutomation" }
    }

    property ShellShareDialog shareDialog: null
    function shareDialogNow(): ShellShareDialog {
        if (!shell.shareDialog)
            shell.shareDialog = shareComponent.createObject(shell) as ShellShareDialog
        return shell.shareDialog
    }
    Component {
        id: shareComponent
        ShellShareDialog { objectName: "shellShare" }
    }

    // The question an extension's command waits on (the gym: which plan, a set's weight and reps):
    // the dialog is made with the first question and follows the next ones itself.
    property ShellExtensionQuestion extensionQuestion: null
    Component {
        id: extensionQuestionComponent
        ShellExtensionQuestion {}
    }
    Connections {
        target: Workspace.extensions
        enabled: shell.extensionQuestion === null
        function onQuestionChanged() {
            if (Workspace.extensions.questionKind === "")
                return
            shell.extensionQuestion = extensionQuestionComponent.createObject(shell) as ShellExtensionQuestion
            // (Made in this signal's handler: its own Connections hear the next one.)
            if (shell.extensionQuestion)
                shell.extensionQuestion.questionChanged()
        }
    }

    // A macro's placeholder clicked in the editor (Workspace.extensions.showMacro), a view block's
    // "Run…": run, switch on or open its extension. Code waiting for approval is read first — the
    // approval with its code (ShellExtensionApproval), as "Read and run" on its page.
    property ShellMacroDialog macroDialog: null
    Component {
        id: macroDialogComponent
        ShellMacroDialog {}
    }
    Connections {
        target: Workspace.extensions
        function onMacroHelpRequested(extension: string, macro: string) {
            const waiting = Workspace.extensions.approvalPageOf(extension)
            if (waiting !== "") {
                shell.openExtensionApproval(waiting)
                return
            }
            if (!shell.macroDialog)
                shell.macroDialog = macroDialogComponent.createObject(shell) as ShellMacroDialog
            if (shell.macroDialog)
                shell.macroDialog.show(extension, macro)
        }
    }
    property ShellExtensionApproval extensionApproval: null
    function openExtensionApproval(path: string): void {
        if (!shell.extensionApproval)
            shell.extensionApproval = extensionApprovalComponent.createObject(shell) as ShellExtensionApproval
        if (shell.extensionApproval)
            shell.extensionApproval.show(path)
    }
    Component {
        id: extensionApprovalComponent
        ShellExtensionApproval {}
    }
    property ShellExtensionSettings extensionSettings: null
    function openExtensionSettings(extension: string): void {
        if (!shell.extensionSettings)
            shell.extensionSettings = extensionSettingsComponent.createObject(shell) as ShellExtensionSettings
        if (shell.extensionSettings)
            shell.extensionSettings.show(extension)
    }
    Component {
        id: extensionSettingsComponent
        ShellExtensionSettings {}
    }
    property ShellExtensionCopy extensionCopy: null
    function openExtensionCopy(path: string): void {
        if (!shell.extensionCopy)
            shell.extensionCopy = extensionCopyComponent.createObject(shell) as ShellExtensionCopy
        if (shell.extensionCopy)
            shell.extensionCopy.show(path)
    }
    Component {
        id: extensionCopyComponent
        ShellExtensionCopy {}
    }

    property ShellNewPageDialog newPage: null
    function newPageNow(): ShellNewPageDialog {
        if (!shell.newPage)
            shell.newPage = newPageComponent.createObject(shell) as ShellNewPageDialog
        return shell.newPage
    }
    Component {
        id: newPageComponent
        ShellNewPageDialog { objectName: "shellNewPage" }
    }

    property ShellRenameDialog rename: null
    function renameNow(): ShellRenameDialog {
        if (!shell.rename)
            shell.rename = renameComponent.createObject(shell) as ShellRenameDialog
        return shell.rename
    }
    Component {
        id: renameComponent
        ShellRenameDialog { objectName: "shellRename" }
    }

    property ShellMoveDialog moveDialog: null
    function moveDialogNow(): ShellMoveDialog {
        if (!shell.moveDialog)
            shell.moveDialog = moveComponent.createObject(shell) as ShellMoveDialog
        return shell.moveDialog
    }
    Component {
        id: moveComponent
        ShellMoveDialog {
            onMoveRequested: (path, folder, create) => shell.performMove(path, folder, create)
        }
    }
    ShellMoveWords { id: moveWords }

    property ShellAboutDialog about: null
    function aboutNow(): ShellAboutDialog {
        if (!shell.about)
            shell.about = aboutComponent.createObject(shell) as ShellAboutDialog
        return shell.about
    }
    Component {
        id: aboutComponent
        ShellAboutDialog {}
    }

    property DeleteDialog deleteDialog: null
    function deleteDialogNow(): DeleteDialog {
        if (!shell.deleteDialog)
            shell.deleteDialog = deleteComponent.createObject(shell) as DeleteDialog
        return shell.deleteDialog
    }
    Component {
        id: deleteComponent
        DeleteDialog {
            id: trashDialog
            objectName: "shellDeleteDialog"
            onAccepted: {
                const trashPath = Workspace.deletePage(trashDialog.path)
                if (trashPath !== "" && shell.toasts) {
                    trashDialog.trashed = [trashPath]
                    AppController.toastRestore(qsTr("Moved to trash"), qsTr("Undo"), trashDialog.trashed)
                }
            }
        }
    }
    /// "Move to trash?" for a page, an attachment or a folder (made when first asked: deleteDialogNow()).
    component DeleteDialog: BConfirmDialog {
        id: deleteDialog

        property string path
        /// Workspace.folderInfo() of a folder ({} for a file).
        readonly property var info: path !== "" && visible ? Workspace.folderInfo(path) : ({})
        readonly property bool folder: AppController.routeFlag(info, "exists")
        readonly property int pages: AppController.routeInt(info, "pages", 0)
        /// [{ special, path }]: the special folders and files it is or holds.
        readonly property list<var> specials: AppController.routeList(info, "specials")
        readonly property string name: Js.endsWith(path, ".md") ? Workspace.pageTitle(path) : Js.sliceFrom(path, Js.lastIndexOf(path, "/") + 1)
        /// The trash path of what was deleted last (its toast's Undo).
        property list<string> trashed

        /// What deleting a special folder, a folder holding some, or the journal template does to
        /// the notebook: a sentence for each, after a space.
        function specialWarnings(): string {
            let text = ""
            for (let i = 0; i < deleteDialog.specials.length; ++i) {
                const warning = deleteDialog.specialWarning(deleteDialog.fieldOf(i, "special"), deleteDialog.fieldOf(i, "path"))
                if (warning !== "")
                    text += " " + warning
            }
            return text
        }
        function fieldOf(index: int, key: string): string {
            return AppController.routeText(deleteDialog.specials[index], key)
        }
        function specialWarning(special: string, specialPath: string): string {
            const self = specialPath === deleteDialog.path
            switch (special) {
            case "journals":
                return self ? qsTr("This is the journals folder: new daily notes will create it again.")
                            : qsTr("It holds the journals folder “%1”: new daily notes will create it again.").arg(specialPath)
            case "templates":
                return self ? qsTr("This is the templates folder: its templates will no longer be offered.")
                            : qsTr("It holds the templates folder “%1”: its templates will no longer be offered.").arg(specialPath)
            case "attachments":
                return self ? qsTr("This is the attachments folder: the pictures and files in it go too.")
                            : qsTr("It holds the attachments folder “%1”: the pictures and files in it go too.").arg(specialPath)
            case "newPages":
                return self ? qsTr("This is the folder for new pages: new pages will create it again.")
                            : qsTr("It holds the folder for new pages “%1”: new pages will create it again.").arg(specialPath)
            case "journalTemplate":
                return self ? qsTr("This is the journal template: new daily notes will start empty.")
                            : qsTr("It holds the journal template “%1”: new daily notes will start empty.").arg(specialPath)
            case "weekly":
                return self ? qsTr("This is the folder of weekly notes: new weekly notes will create it again.")
                            : qsTr("It holds the folder of weekly notes “%1”: new weekly notes will create it again.").arg(specialPath)
            case "monthly":
                return self ? qsTr("This is the folder of monthly notes: new monthly notes will create it again.")
                            : qsTr("It holds the folder of monthly notes “%1”: new monthly notes will create it again.").arg(specialPath)
            case "weeklyTemplate":
                return self ? qsTr("This is the weekly note template: new weekly notes will start empty.")
                            : qsTr("It holds the weekly note template “%1”: new weekly notes will start empty.").arg(specialPath)
            case "monthlyTemplate":
                return self ? qsTr("This is the monthly note template: new monthly notes will start empty.")
                            : qsTr("It holds the monthly note template “%1”: new monthly notes will start empty.").arg(specialPath)
            }
            return ""
        }

        title: qsTr("Move to trash?")
        subject: name
        subjectIcon: folder ? "folder" : Js.endsWith(path, ".md") ? "file-text" : "file"
        message: (folder && pages > 0
                  ? qsTr("This folder and %n page(s) in it will be moved to the trash.", "", pages)
                    + " " + qsTr("You can restore them from there.")
                  : qsTr("It will be moved to the trash. You can restore it from there."))
                 + specialWarnings()
        confirmText: qsTr("Move to trash")
        destructive: true
    }

    Connections {
        target: AppController
        function onUiActionRequested(action: string) {
            shell.handleUiAction(action)
        }
        // A page or folder of the tree from a view (All pages, the page's menu, the palette).
        function onItemActionRequested(action: string, path: string) {
            switch (action) {
            case "move": shell.openMove(path, shell.isFolder(path)); break
            case "rename": shell.openRename(path, shell.isFolder(path)); break
            case "delete": shell.confirmDelete(path); break
            case "newPage": shell.openNewPage(path); break
            case "newEncryptedPage": shell.openNewEncryptedPage(path); break
            case "newFolder": shell.openNewFolder(path); break
            case "reveal": shell.revealInTree(path); break
            }
        }
    }

    // "New encrypted page…" (docs/UI.md "Encrypted notes"): the palette, a folder's menu in the
    // sidebar. Made when first opened; the new page opens unlocked.
    property ContentEncryptionDialog newEncryptedDialog: null
    function openNewEncryptedPage(folder: string): void {
        if (!newEncryptedDialog)
            newEncryptedDialog = newEncryptedComponent.createObject(T.Overlay.overlay) as ContentEncryptionDialog
        newEncryptedDialog.openFor("new", folder)
    }
    Component {
        id: newEncryptedComponent
        ContentEncryptionDialog {
            onDone: (path) => AppController.openPage(path)
        }
    }

    // ---- Shortcuts (docs/UI.md) -----------------------------------------------------------------

    // How a replace in the whole notebook went, also when ReplaceView has gone meanwhile.
    // Its Undo: the toast (AppController.toastWithAction) and the replace's token.
    property int _replaceToast: 0
    property int _replaceToken: 0
    Connections {
        target: Workspace.replacing
        function onApplied(token: int, hits: int, files: int, changed: list<string>, failed: list<string>) {
            if (hits > 0) {
                shell._replaceToken = token
                shell._replaceToast = AppController.toastWithAction(qsTr("%n occurrence(s) replaced", "", hits) + " · "
                                                                    + qsTr("%n note(s)", "", files), "success", qsTr("Undo"))
            }
            if (changed.length > 0)
                AppController.toast(qsTr("Left as they are, changed since the preview: %1").arg(changed.join(", ")), "warning")
            if (failed.length > 0)
                AppController.toast(qsTr("Could not write: %1").arg(failed.join(", ")), "error")
        }
        function onUndone(restored: int, left: list<string>) {
            AppController.toast(left.length > 0
                                ? qsTr("%n note(s) restored", "", restored) + " · "
                                  + qsTr("Changed since, left as they are: %1").arg(left.join(", "))
                                : qsTr("%n note(s) restored", "", restored),
                                left.length > 0 ? "warning" : "info")
        }
    }

    // The commands' shortcuts (docs/UI.md "Custom shortcuts"): one Shortcut per action that has
    // one, as ShortcutMap has them now (custom ones too). Find, replace, Markdown source and a
    // page property are the views' own (PageView, JournalsView: their state decides); the editor
    // copies a block reference with Ctrl+Shift+C itself (another key for it is a Shortcut here).
    // (ShortcutMap.shellBindings: without those.)
    Repeater {
        model: ShortcutMap.shellBindings
        delegate: Item {
            id: binding
            required property string action
            required property string sequence
            Shortcut {
                sequence: binding.sequence
                enabled: shell.shortcutEnabled(binding.action)
                onActivated: shell.runShortcut(binding.action)
            }
        }
    }
    // Tabs (docs/UI.md "Tabs, split view and windows"): Ctrl+T, Ctrl+W, Ctrl+Tab and Ctrl+Shift+Tab
    // are the tab commands' shortcuts above; these are the browsers' other keys for them.
    Shortcut { sequences: ["Ctrl+PgDown"]; onActivated: AppController.nextTab() }
    // (Shift+Tab arrives as Backtab.)
    Shortcut { sequences: ["Ctrl+Shift+Backtab", "Ctrl+PgUp"]; onActivated: AppController.previousTab() }
    Instantiator {
        model: 9
        delegate: Shortcut {
            required property int index
            sequence: "Ctrl+" + (index + 1)
            // Ctrl+9: the last tab, as in browsers.
            onActivated: AppController.selectTabAt(index === 8 ? -1 : index)
        }
    }
    function shortcutEnabled(id: string): bool {
        switch (id) {
        case "panel.toggle": return !Theme.narrow || contextDrawer.available
        case "page.rename":
        case "page.print": return shell.currentView === "page"
        }
        return true
    }
    /// What a command's shortcut does: the shell's own popups directly, the rest as the palette.
    function runShortcut(id: string): void {
        switch (id) {
        case "palette": shell.openPalette(""); break
        case "page.switch": shell.openSwitcher(""); break
        case "go.search": AppController.search(""); break
        case "page.new": shell.openNewPage(""); break
        case "capture": shell.openCapture(); break
        case "panel.toggle":
            if (Theme.narrow)
                contextDrawer.toggle()
            else
                AppController.sidePanelVisible = !AppController.sidePanelVisible
            break
        case "page.rename": shell.openRename(shell.routePath, false); break
        default: CommandModel.run(id)
        }
    }

    // ---- Tabs: links in a new tab (OpenIn), pages in windows of their own -------------------------

    /// The tabs that have their ShellPageWindow (a window whose tab goes destroys itself).
    property list<var> _windowTabsShown
    /// One window per AppController.windowTabs entry: new ones made and shown (gone ones: the
    /// window itself, ShellPageWindow).
    function syncPageWindows(): void {
        for (let i = 0; i < AppController.windowTabs.length; ++i) {
            if (shell._windowTabsShown.indexOf(AppController.windowTabs[i]) >= 0)
                continue
            const w = pageWindowComponent.createObject(shell, { tabId: AppController.windowTabs[i] }) as ShellPageWindow
            if (!w)
                continue
            w.show()
            w.raise()
            w.requestActivate()
        }
        shell._windowTabsShown = AppController.windowTabs
    }
    Component {
        id: pageWindowComponent
        ShellPageWindow {}
    }
    Connections {
        target: AppController
        function onWindowTabsChanged() { shell.syncPageWindows() }
    }
    // The tabs saved (the panes hand their view states over first) when the app goes to the
    // background, where Android may end it.
    Connections {
        target: Application
        function onStateChanged() {
            if (Application.state !== Qt.ApplicationActive)
                AppController.saveTabs()
        }
    }
    Binding { target: OpenIn; property: "windows"; value: AppController.windowsAllowed && !Theme.narrow }
    // A link's click in a new tab or window: OpenIn.begin(target) … end() around its navigation.
    Connections {
        target: OpenIn
        function onBeginRequested(target: string) { AppController.beginOpenIn(target) }
        function onEndRequested() { AppController.endOpenIn() }
    }
    // The shell's toasts' buttons (AppController.toastWithAction).
    property int _captureToast: 0
    Connections {
        target: AppController
        function onToastActionTriggered(id: int) {
            if (id === shell._moveToast)
                Workspace.movePath(shell._movedPath, shell._moveBack)
            else if (id === shell._replaceToast)
                Workspace.replacing.undo(shell._replaceToken)
            else if (id === shell._captureToast)
                AppController.openToday()
        }
    }

    Component.onCompleted: {
        // The navigation bar's first margin.
        shell.noteNavigationBar()
        shell.syncPageWindows()
        OpenIn.tabs = true
    }
    Component.onDestruction: OpenIn.tabs = false

    // --popup (screenshots, the UI benchmark, manual checks): opened once the notebook is open and
    // indexed — a popup that lists pages (the switcher) then shows them, as a user sees it —, or
    // after 3 s without one (onboarding).
    Timer {
        id: devPopupTimer
        property int waited: 0
        interval: 50
        repeat: true
        running: AppController.devPopup !== ""
        onTriggered: {
            devPopupTimer.waited += devPopupTimer.interval
            if (!Workspace.ready && devPopupTimer.waited < 3000)
                return
            devPopupTimer.stop()
            shell.openDevPopup(AppController.devPopup)
        }
    }
    /// --popup's name and query (the part after ":"), kept for the deferred opening.
    property string _devPopupName
    property string _devPopupQuery
    /// The popup --screenshot waits for (DevTools.holdScreenshot) until it has opened.
    property T.Popup _devPopupHeld: null
    function devPopupTarget(name: string): T.Popup {
        switch (name) {
        case "palette": return shell.paletteNow()
        case "switcher": return shell.switcherNow()
        case "capture": return shell.captureNow()
        case "newPage":
        case "newFolder": return shell.newPageNow()
        case "move": return shell.moveDialogNow()
        case "about": return shell.aboutNow()
        case "drawer": return drawer
        case "context": return contextDrawer.drawerItem
        case "more": return topBar.moreMenuNow()
        }
        return shell._noPopup
    }
    property T.Popup _noPopup: null
    function openDevPopup(popup: string): void {
        const colon = shell.intValue(Js.indexOf(popup, ":"))
        shell._devPopupName = colon >= 0 ? Js.slice(popup, 0, colon) : popup
        shell._devPopupQuery = colon >= 0 ? Js.sliceFrom(popup, colon + 1) : ""
        // --screenshot waits until the popup has opened (its enter transition over), or at once
        // when it is open already (DevTools.holdScreenshot).
        const held = shell.devPopupTarget(shell._devPopupName)
        if (held && !held.opened) {
            DevTools.holdScreenshot(true)
            shell._devPopupHeld = held
        }
        devPopupLater.restart()
    }
    Connections {
        target: shell._devPopupHeld
        function onOpenedChanged() {
            if (!shell._devPopupHeld || !shell._devPopupHeld.opened)
                return
            shell._devPopupHeld = null
            DevTools.holdScreenshot(false)
        }
    }
    // (Qt.callLater: opened a turn of the event loop later.)
    Later {
        id: devPopupLater
        onTriggered: shell.openDevPopupNow()
    }
    Later {
        id: devPopupChooseLater
        onTriggered: {
            const query = shell._devPopupQuery
            shell.moveDialogNow().choose(Js.sliceFrom(query, Js.indexOf(query, ">") + 1), false)
        }
    }
    function isFolder(path: string): bool {
        return AppController.routeFlag(Workspace.folderInfo(path), "exists")
    }
    function openDevPopupNow(): void {
        const query = shell._devPopupQuery
        switch (shell._devPopupName) {
        case "palette": shell.openPalette(query); break
        case "switcher": shell.openSwitcher(query); break
        case "capture": shell.openCapture(); break
        case "newPage": shell.openNewPage(""); break
        case "newFolder": shell.openNewFolder(""); break
        // move:<path>[><folder>] (else the page shown): the move dialog for a page or a folder,
        // and with ">" the folder chosen in it (what the move changes shows).
        case "move": {
            const arrow = shell.intValue(Js.indexOf(query, ">"))
            const named = arrow >= 0 ? shell.textOf(Js.slice(query, 0, arrow)) : query
            const item = named !== "" ? named : shell.routePath
            shell.openMove(item, shell.isFolder(item))
            if (arrow >= 0)
                devPopupChooseLater.restart()
            break
        }
        case "about": shell.openAbout(); break
        case "drawer": drawer.open(); break
        // context[:<section>]: the side panel's drawer (narrow windows), that section unfolded.
        case "context":
            if (query !== "")
                AppSettings.setValue("ui/sidePanel/tab", query)
            contextDrawer.open()
            break
        case "more": topBar.openMoreMenu(); break
        }
    }
}
