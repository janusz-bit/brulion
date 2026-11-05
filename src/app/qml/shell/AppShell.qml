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
// Narrow: the sidebar is a Drawer (the bottom navigation's Menu, or a swipe from the left edge — on
// touch UIs from a strip beyond the zone Android's gesture navigation keeps for "back"; the top
// bar's ☰ only on a view that replaces the bottom navigation, the drawing editor) and a bottom
// navigation bar with quick capture ("+ Notatka") before Menu; the Android back key walks back through
// AppController's history and only lets the app close from the home view.
//
// Owns the shell's popups (command palette, quick switcher, quick capture, new page or folder,
// rename, "Przenieś do…", delete confirmation, about) and the global shortcuts (ShortcutMap's,
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
// it (its "+ Notatka" then ends that editing before quick capture opens); a bar a view has at
// the bottom instead of the navigation (the drawing's tools) takes its place.
FocusScope {
    id: shell

    /// The window's toast host (Main.qml).
    property BToastHost toasts: null
    property bool sidebarVisible: flag(AppSettings.value("ui/sidebar/visible", true))

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
    readonly property var route: AppController.currentRoute
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
    readonly property bool toolbarDocked: shell.routeView.viewToolbarDocked || sidePanel.toolbarDocked
    /// Typing: the keyboard is up, or a toolbar stays docked after the system hid it.
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
    /// the keyboard went (the editing goes on) docks right above it.
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
    /// line is kept right above the keyboard and the toolbar, under the toasts' usual place.
    readonly property bool toastsAtTop: KeyboardInsets.shown || (toolbarDocked && shell.routeView.viewEditing)
    readonly property string title: shell.routeView.viewTitle !== "" ? shell.routeView.viewTitle
                                    : routeTitles.routeTitle(shell.currentView, shell.routePath,
                                                             routeTitles.field(AppController.currentRoute, "date"),
                                                             routeTitles.field(AppController.currentRoute, "tag"))
    /// Folders above the current page, or above the folder All pages shows (with "Wszystkie
    /// strony" first): their titles (`crumbs`) and paths (`crumbPaths`; "" the notebook's root) —
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
        // (Read for real: a bare `void Workspace.revision` is compiled away with the dependency.)
        if (shell.currentView !== "journal" || date === "" || Workspace.revision < 0)
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
    Timer {
        id: logTypingLater
        interval: 0
        onTriggered: shell.logTyping()
    }
    onTypingReportChanged: logTypingLater.restart()
    Connections {
        target: KeyboardInsets
        function onAnimatingChanged() { logTypingLater.restart() }
    }

    // Titles of routes and pages (also the tab bar's, the tab switcher's, a page window's).
    ShellRouteTitles { id: routeTitles }
    /// A page's title as people read it: daily notes by their date ("środa, 14 października
    /// 2026"), not by their file name ("2026-10-14").
    function pageLabel(path: string): string {
        return routeTitles.pageLabel(path)
    }

    function openPalette(query: string): void {
        palette.open()
        if (query !== "")
            palette.typeQuery(query)
    }
    function openSwitcher(query: string): void {
        switcher.open()
        if (query !== "")
            switcher.typeQuery(query)
    }
    function openCapture(): void {
        // Captured notes belong in the user's journal: from the documentation, AppController
        // goes back to the user's notebook and opens the dialog there.
        if (AppController.inDocsVault)
            AppController.quickCapture()
        else
            capture.open()
    }
    /// The share dialog for what another app shared (AppController.pendingShare; Android's
    /// "Udostępnij → Brulion", the --share option).
    function openShare(): void {
        if (AppController.routeText(AppController.pendingShare, "kind") === "")
            return
        if (shareDialog.opened)
            shareDialog.reload() // a newer share replaced the one shown
        else
            shareDialog.open()
    }
    /// A new page: in `folder`, or — Ctrl+N, the top bar, the sidebar's pencil pass "" — in the
    /// folder of the page shown, the root too (a removable chip in the dialog); else the
    /// notebook's folder for new pages.
    function openNewPage(folder: string): void {
        if (folder === "" && shell.currentView === "page" && shell.routePath !== ""
                && Workspace.journalDate(shell.routePath) === "") {
            newPage.openFor(Workspace.folderOf(shell.routePath), false, true)
            return
        }
        newPage.openFor(folder, false, false)
    }
    function openNewFolder(parentFolder: string): void { newPage.openFor(parentFolder, true, false) }
    function openRename(path: string, isDir: bool): void { rename.openFor(path, isDir) }
    /// "Przenieś do…" for a page, an attachment or a folder.
    function openMove(path: string, isDir: bool): void {
        if (path !== "")
            moveDialog.openFor(path, isDir)
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
    /// "Pokaż w drzewie": the sidebar (the drawer on phones) shows `path`, its "Strony" section
    /// opened if it was collapsed.
    function revealInTree(path: string): void {
        if (path === "")
            return
        if (Theme.narrow) {
            drawer.open()
            const sidebar = drawerSidebar.item as ShellSidebar
            if (sidebar)
                sidebar.reveal(path, true)
        } else {
            if (!shell.sidebarVisible)
                shell.toggleSidebar()
            const sidebar = sidebarLoader.item as ShellSidebar
            if (sidebar)
                sidebar.reveal(path, true)
        }
    }
    function openAbout(): void { about.open() }
    function confirmDelete(path: string): void {
        deleteDialog.path = path
        deleteDialog.open()
    }
    function toggleSidebar(): void {
        shell.sidebarVisible = !shell.sidebarVisible
        AppSettings.setValue("ui/sidebar/visible", shell.sidebarVisible)
    }
    /// AppController.uiActionRequested: the shell's popups.
    function handleUiAction(action: string): void {
        switch (action) {
        case "palette": shell.openPalette(""); break
        case "switcher": shell.openSwitcher(""); break
        case "capture": shell.openCapture(); break
        case "share": shell.openShare(); break
        case "newPage": shell.openNewPage(""); break
        case "newFolder": shell.openNewFolder(""); break
        case "movePage": if (shell.currentView === "page") shell.openMove(shell.routePath, false); break
        case "revealInTree": shell.revealInTree(shell.currentFile); break
        case "trashPage": if (shell.currentFile !== "") shell.confirmDelete(shell.currentFile); break
        case "about": shell.openAbout(); break
        case "toggleSidePanel": AppController.sidePanelVisible = !AppController.sidePanelVisible; break
        case "renamePage": if (shell.currentView === "page") shell.openRename(shell.routePath, false); break
        }
    }

    focus: true

    // Android back key / gesture: back through the history; at home the app may close. Focus
    // mode first ends (also Esc, once nothing in the view took it).
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Back)
            event.accepted = shell.focusMode || AppController.canGoBack || !AppController.atHome
        else if (event.key === Qt.Key_Escape && shell.focusMode) {
            shell.leaveFocusMode()
            event.accepted = true
        }
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Back && shell.focusMode) {
            shell.leaveFocusMode()
            event.accepted = true
        } else if (event.key === Qt.Key_Back) {
            event.accepted = AppController.handleBackKey()
        }
    }

    onWidthChanged: if (!Theme.narrow && drawer.opened) drawer.close()

    // The rail of views at the window's left edge (wide windows; phones have it in the drawer):
    // it stays when the sidebar's page panel is hidden, not in focus mode.
    Loader {
        id: rail
        active: !Theme.narrow
        visible: active && !shell.focusMode
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.bottomMargin: shell.keyboardRoom
        width: visible && rail.item ? (rail.item as Item).implicitWidth : 0
        sourceComponent: ShellRail {
            objectName: "shellRail"
            topInset: shell.SafeArea.margins.top
            bottomInset: Math.max(0, shell.navigationBar - shell.keyboardRoom)
        }
    }

    SplitView {
        id: split
        anchors.left: rail.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        // Everything ends at the keyboard's top: an editor in the side panel docks its toolbar
        // on the keyboard as well as the main view's.
        anchors.bottomMargin: shell.keyboardRoom
        orientation: Qt.Horizontal
        handle: BSplitHandle { orientation: Qt.Horizontal }

        Loader {
            id: sidebarLoader
            visible: !Theme.narrow && shell.sidebarVisible && !shell.focusMode
            active: !Theme.narrow
            SplitView.preferredWidth: Theme.sidebarWidth
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: 440
            sourceComponent: ShellSidebar {
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
                sidebarVisible: shell.sidebarVisible
                // Phones: one way to the drawer — the bottom navigation's Menu while that bar is
                // shown; ☰ whenever it is not: a view with its own bar instead (the drawing
                // editor) or the on-screen keyboard up (it takes the bar's place, and there is no
                // edge strip while typing: dragMargin), so the drawer is never out of reach.
                menuButtonShown: !Theme.narrow || !shell.bottomNavShown
                picture: shell.routeView.viewPicture
                pictureAsText: shell.routeView.viewPictureAsText
                pdf: shell.routeView.viewPdf
                fileOnly: shell.routeView.viewFile
                noteMode: shell.routeView.viewNoteMode
                onMenuClicked: drawer.open()
                onSidebarToggled: shell.toggleSidebar()
                onPaletteRequested: shell.openPalette("")
                onSwitcherRequested: shell.openSwitcher("")
                onCaptureRequested: shell.openCapture()
                onNewPageRequested: shell.openNewPage("")
                onRenameRequested: (path) => shell.openRename(path, false)
                onDeleteRequested: (path) => shell.confirmDelete(path)
                onMoveRequested: (path) => shell.openMove(path, false)
                onRevealRequested: (path) => shell.revealInTree(path)
                onAboutRequested: shell.openAbout()
            }
            // Not while typing: the room above the on-screen keyboard is the note's.
            ShellUpdateBanner {
                visible: !shell.keyboardUp && !shell.focusMode && AppController.updates.availableVersion !== ""
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space2
                Layout.rightMargin: Theme.space2
                Layout.topMargin: Theme.space2
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
                onMenuClicked: drawer.open()
                // "+ Notatka": a block still edited with the keyboard hidden (its toolbar docked on
                // the bar) is ended first, as a tab's view change ends it.
                onCaptureClicked: {
                    shell.routeView.endEditing()
                    shell.openCapture()
                }
            }
        }

        ShellSidePanel {
            id: sidePanel
            visible: !Theme.narrow && AppController.sidePanelVisible && !shell.focusMode
            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 260
            SplitView.maximumWidth: 600
            pinnedTitle: routeTitles.routeTitle(routeTitles.field(AppController.sidePanelRoute, "view"),
                                                routeTitles.field(AppController.sidePanelRoute, "path"),
                                                routeTitles.field(AppController.sidePanelRoute, "date"),
                                                routeTitles.field(AppController.sidePanelRoute, "tag"))
            contextPanel: shell.routeView.viewSidePanel
        }
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
        // swipe there is the view's. The bottom navigation's Menu (☰ in the top bar where a view
        // replaces that bar or the keyboard hides it) still opens it, a swipe on it still closes it. The strip follows
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

        Loader {
            id: drawerRail
            active: Theme.narrow
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
            active: Theme.narrow
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

    // ---- Popups -----------------------------------------------------------------------------

    BCommandPalette {
        id: palette
        objectName: "shellPalette"
        commands: CommandModel.commands
        onCommandIdActivated: (id) => CommandModel.run(id)
    }

    BQuickSwitcher {
        id: switcher
        objectName: "shellSwitcher"
        // Workspace.revision: refresh when pages come and go while it is open. Closed (visible
        // stays true until the exit transition ends), it neither queries nor follows the
        // revision: every save bumps it.
        pages: switcher.visible && Workspace.revision >= 0 ? Workspace.switcherPages(switcher.query, 60) : []
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

    ShellQuickCapture {
        id: capture
        objectName: "shellCapture"
        onCaptured: {
            if (shell.toasts)
                shell._captureToast = AppController.toastWithAction(qsTr("Added to today's journal"), "success", qsTr("Open"))
        }
    }

    ShellShareDialog { id: shareDialog; objectName: "shellShare" }

    ShellNewPageDialog { id: newPage; objectName: "shellNewPage" }
    ShellRenameDialog { id: rename; objectName: "shellRename" }
    ShellMoveDialog {
        id: moveDialog
        onMoveRequested: (path, folder, create) => shell.performMove(path, folder, create)
    }
    ShellMoveWords { id: moveWords }
    ShellAboutDialog { id: about }

    BConfirmDialog {
        id: deleteDialog
        objectName: "shellDeleteDialog"

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
        message: (folder && pages > 0
                  ? qsTr("The folder “%1” and %n page(s) in it will be moved to the trash.", "", pages).arg(name)
                    + " " + qsTr("You can restore them from there.")
                  : qsTr("“%1” will be moved to the trash. You can restore it from there.").arg(name))
                 + specialWarnings()
        confirmText: qsTr("Move to trash")
        destructive: true
        onAccepted: {
            const trashPath = Workspace.deletePage(deleteDialog.path)
            if (trashPath !== "" && shell.toasts) {
                deleteDialog.trashed = [trashPath]
                AppController.toastRestore(qsTr("Moved to trash"), qsTr("Undo"), deleteDialog.trashed)
            }
        }
    }

    Connections {
        target: AppController
        function onUiActionRequested(action: string) {
            shell.handleUiAction(action)
        }
        // A page or folder of the tree from a view (All pages, the page's menu, the palette).
        function onItemActionRequested(action: string, path: string) {
            const isDir = AppController.routeFlag(Workspace.folderInfo(path), "exists")
            switch (action) {
            case "move": shell.openMove(path, isDir); break
            case "rename": shell.openRename(path, isDir); break
            case "delete": shell.confirmDelete(path); break
            case "newPage": shell.openNewPage(path); break
            case "newFolder": shell.openNewFolder(path); break
            case "reveal": shell.revealInTree(path); break
            }
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
        case "panel.toggle": return !Theme.narrow
        case "page.rename": return shell.currentView === "page"
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
        case "panel.toggle": AppController.sidePanelVisible = !AppController.sidePanelVisible; break
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
        case "palette": return palette
        case "switcher": return switcher
        case "capture": return capture
        case "newPage":
        case "newFolder": return newPage
        case "move": return moveDialog
        case "about": return about
        case "drawer": return drawer
        case "more": return topBar.moreMenuPopup
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
    Timer {
        id: devPopupLater
        interval: 0
        onTriggered: shell.openDevPopupNow()
    }
    Timer {
        id: devPopupChooseLater
        interval: 0
        onTriggered: {
            const query = shell._devPopupQuery
            moveDialog.choose(Js.sliceFrom(query, Js.indexOf(query, ">") + 1), false)
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
        case "more": topBar.openMoreMenu(); break
        }
    }
}
