pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
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
    readonly property real navigationBar: safe.bottom <= navigationBarMax ? safe.bottom : _lastNavigationBar
    /// The tallest system navigation bar (logical px; Android's are 16–56 dp, a tablet's taskbar
    /// included); as in ViewportKeyboard::MinKeyboard, anything taller is a keyboard.
    readonly property real navigationBarMax: 120
    property real _lastNavigationBar: 0
    function noteNavigationBar() {
        if (safe.bottom <= navigationBarMax)
            _lastNavigationBar = safe.bottom
    }
    onSafeChanged: noteNavigationBar()
    readonly property var route: AppController.currentRoute
    /// The main window shows two panes side by side (wide windows).
    readonly property bool split: !Theme.narrow && AppController.splitTab !== 0
    /// The focused pane's view: the top bar, the side panel's context and the keyboard rules are its.
    readonly property ShellRouteView routeView: split && AppController.splitFocused ? splitPane.routeView : mainPane.routeView
    /// The split pane is the focused one. The keyboard follows (Ctrl+Tab to the other pane's tab)
    /// when it was in one of the panes.
    readonly property bool splitPaneFocused: split && AppController.splitFocused
    onSplitPaneFocusedChanged: {
        const pane = splitPaneFocused ? splitPane : mainPane
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
    readonly property string title: shell.routeView.viewTitle !== "" ? shell.routeView.viewTitle : routeTitle(route)
    /// Folders above the current page, or above the folder All pages shows (with "Wszystkie
    /// strony" first): [{ title, path }] — a crumb opens All pages in that folder.
    readonly property var crumbs: {
        if (route.view === "allPages" && route.path) {
            const parts = String(route.path).split("/")
            return [{ title: qsTr("All pages"), path: "" }].concat(
                        parts.slice(0, -1).map((part, i) => ({ title: part, path: parts.slice(0, i + 1).join("/") })))
        }
        if (route.view !== "page" || !route.path)
            return []
        const folder = Workspace.folderOf(route.path)
        if (folder === "")
            return []
        const parts = folder.split("/")
        return parts.map((part, i) => ({ title: part, path: parts.slice(0, i + 1).join("/") }))
    }
    /// The file the current route shows: a page, or a daily note that exists.
    readonly property string currentFile: {
        if (route.view === "page")
            return route.path || ""
        // (Read for real: a bare `void Workspace.revision` is compiled away with the dependency.)
        if (route.view !== "journal" || !route.date || Workspace.revision < 0)
            return ""
        const path = route.path || Workspace.journalPath(route.date)
        return Workspace.exists(path) ? path : ""
    }

    function flag(value) {
        return value === true || value === "true"
    }

    /// Focus mode (AppSettings.focusMode; docs/UI.md "Statistics and focus mode"): where an editor
    /// shows a page or the journal, no sidebar, top bar, side panel or bottom navigation — the
    /// text column alone, centred. F8 (ShortcutMap "view.focus"), Esc once the view does not need
    /// it, the Back key and the corner button leave it; other views show their bars as usual.
    readonly property bool focusMode: AppSettings.focusMode && ["page", "journal", "journals"].indexOf(route.view) >= 0
    function leaveFocusMode() {
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
    function logTyping() {
        if (KeyboardInsets.animating || typingReport === _loggedTyping)
            return
        console.debug(platformLog, typingReport !== "" ? typingReport : "Typing ended")
        _loggedTyping = typingReport
    }
    onTypingReportChanged: Qt.callLater(shell.logTyping)
    Connections {
        target: KeyboardInsets
        function onAnimatingChanged() { Qt.callLater(shell.logTyping) }
    }

    /// A page's title as people read it: daily notes by their date ("środa, 14 października
    /// 2026"), not by their file name ("2026-10-14").
    function pageLabel(path) {
        const day = path ? Workspace.journalDate(path) : ""
        return day !== "" ? DatesHelper.formatLong(day) : Workspace.pageTitle(path)
    }

    /// Human title of a route (top bar, side panel header).
    function routeTitle(r) {
        switch (r.view) {
        case "journals": return qsTr("Journals")
        case "journal": return DatesHelper.formatLong(r.date)
        case "page": return pageLabel(r.path)
        case "calendar": return qsTr("Calendar")
        case "tasks": return qsTr("Tasks")
        case "habits": return qsTr("Habits")
        case "graph": return qsTr("Graph")
        case "search": return qsTr("Search")
        case "allPages": return r.path ? r.path.split("/").pop() : qsTr("All pages")
        case "tags": return qsTr("Tags")
        case "tag": return "#" + r.tag
        case "sync": return qsTr("Sync")
        case "importExport": return qsTr("Import and export")
        case "history": return qsTr("History of %1").arg(pageLabel(r.path))
        case "trash": return qsTr("Trash")
        case "cleanup": return qsTr("Clean up")
        case "replace": return qsTr("Replace in notebook")
        case "settings": return qsTr("Settings")
        case "drawing": return Workspace.pageTitle(r.path || "").replace(/\.(svg|excalidraw)$/i, "")
        }
        return "Brulion"
    }

    function openPalette(query) {
        palette.open()
        if (query)
            palette.typeQuery(query)
    }
    function openSwitcher(query) {
        switcher.open()
        if (query)
            switcher.typeQuery(query)
    }
    function openCapture() {
        // Captured notes belong in the user's journal: from the documentation, AppController
        // goes back to the user's notebook and opens the dialog there.
        if (AppController.inDocsVault)
            AppController.quickCapture()
        else
            capture.open()
    }
    /// The share dialog for what another app shared (AppController.pendingShare; Android's
    /// "Udostępnij → Brulion", the --share option).
    function openShare() {
        if (AppController.pendingShare.kind === undefined)
            return
        if (shareDialog.opened)
            shareDialog.reload() // a newer share replaced the one shown
        else
            shareDialog.open()
    }
    /// A new page: in `folder`, or — Ctrl+N, the top bar, the sidebar's pencil pass "" — in the
    /// folder of the page shown, the root too (a removable chip in the dialog); else the
    /// notebook's folder for new pages.
    function openNewPage(folder) {
        if (!folder && route.view === "page" && route.path && Workspace.journalDate(route.path) === "") {
            newPage.openFor(Workspace.folderOf(route.path), false, true)
            return
        }
        newPage.openFor(folder || "", false)
    }
    function openNewFolder(parentFolder) { newPage.openFor(parentFolder, true) }
    function openRename(path, isDir) { rename.openFor(path, isDir) }
    /// "Przenieś do…" for a page, an attachment or a folder.
    function openMove(path, isDir) {
        if (path)
            moveDialog.openFor(path, isDir === true)
    }
    /// Move `path` into `folder` (created first when `create`; "" = the root) and say how it went,
    /// with Undo (moving it back).
    function performMove(path, folder, create) {
        let target = folder
        if (create) {
            target = Workspace.createFolder(folder)
            if (target === "")
                return false
        }
        const impact = Workspace.moveImpact(path, target)
        const moved = Workspace.movePath(path, target)
        if (moved === "" || moved === path)
            return false
        const text = moveWords.movedText(Workspace.folderOf(moved), (impact.links || 0) + (impact.tags || 0),
                                         (impact.tags || 0) > 0)
        const back = Workspace.folderOf(path)
        if (shell.toasts)
            shell.toasts.show(text, { kind: "success", actionText: qsTr("Undo"), onAction: () => Workspace.movePath(moved, back) })
        return true
    }
    /// "Pokaż w drzewie": the sidebar (the drawer on phones) shows `path`, its "Strony" section
    /// opened if it was collapsed.
    function revealInTree(path) {
        if (!path)
            return
        if (Theme.narrow) {
            drawer.open()
            const sidebar = drawerSidebar.item as ShellSidebar
            if (sidebar)
                sidebar.reveal(path, true)
        } else {
            if (!sidebarVisible)
                toggleSidebar()
            const sidebar = sidebarLoader.item as ShellSidebar
            if (sidebar)
                sidebar.reveal(path, true)
        }
    }
    function openAbout() { about.open() }
    function confirmDelete(path) {
        deleteDialog.path = path
        deleteDialog.open()
    }
    function toggleSidebar() {
        sidebarVisible = !sidebarVisible
        AppSettings.setValue("ui/sidebar/visible", sidebarVisible)
    }
    /// AppController.uiActionRequested: the shell's popups.
    function handleUiAction(action) {
        switch (action) {
        case "palette": openPalette(); break
        case "switcher": openSwitcher(); break
        case "capture": openCapture(); break
        case "share": openShare(); break
        case "newPage": openNewPage(""); break
        case "newFolder": openNewFolder(""); break
        case "movePage": if (route.view === "page") openMove(route.path, false); break
        case "revealInTree": revealInTree(currentFile); break
        case "trashPage": if (currentFile !== "") confirmDelete(currentFile); break
        case "about": openAbout(); break
        case "toggleSidePanel": AppController.sidePanelVisible = !AppController.sidePanelVisible; break
        case "renamePage": if (route.view === "page") openRename(route.path, false); break
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

    SplitView {
        id: split
        anchors.fill: parent
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
                topInset: shell.safe.top
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
                Layout.preferredHeight: shell.safe.top
            }
            ShellTopBar {
                objectName: "shellTopBar"
                visible: !shell.focusMode
                Layout.fillWidth: true
                title: shell.title
                titleOf: (r) => shell.routeTitle(r)
                crumbs: shell.crumbs
                topInset: shell.safe.top
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
                onMenuClicked: drawer.open()
                onSidebarToggled: shell.toggleSidebar()
                onPaletteRequested: shell.openPalette()
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
                titleOf: (r) => shell.routeTitle(r)
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
            pinnedTitle: shell.routeTitle(AppController.sidePanelRoute)
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
        anchors.topMargin: shell.safe.top + Theme.space2
        anchors.rightMargin: shell.safe.right + Theme.space3
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
        width: Math.min(320, shell.width * 0.86)
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
                    : Math.max(shell.safe.left, GestureInsets.left) + shell.edgeSwipeStrip
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
            id: drawerSidebar
            anchors.fill: parent
            active: Theme.narrow
            sourceComponent: ShellSidebar {
                topInset: shell.safe.top
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
        onCommandActivated: (command) => CommandModel.run(command.id)
    }

    BQuickSwitcher {
        id: switcher
        objectName: "shellSwitcher"
        // Workspace.revision: refresh when pages come and go while it is open. Closed (visible
        // stays true until the exit transition ends), it neither queries nor follows the
        // revision: every save bumps it.
        pages: switcher.visible && Workspace.revision >= 0 ? Workspace.switcherPages(switcher.query, 60) : []
        onPageChosen: (page) => AppController.openPage(page.path)
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
                shell.toasts.show(qsTr("Added to today's journal"),
                                  { kind: "success", actionText: qsTr("Open"), onAction: () => AppController.openToday() })
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
        readonly property bool folder: info.exists === true
        readonly property string name: path.endsWith(".md") ? Workspace.pageTitle(path) : path.split("/").pop()

        /// What deleting a special folder, a folder holding some, or the journal template does to
        /// the notebook: a sentence for each.
        function specialWarnings() {
            return (info.specials || []).map(s => specialWarning(s.special, s.path)).filter(w => w !== "")
        }
        function specialWarning(special, specialPath) {
            const self = specialPath === path
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
        message: {
            const parts = []
            if (!folder)
                parts.push(qsTr("“%1” will be moved to the trash. You can restore it from there.").arg(name))
            else if ((info.pages || 0) > 0)
                parts.push(qsTr("The folder “%1” and %n page(s) in it will be moved to the trash.", "", info.pages).arg(name),
                           qsTr("You can restore them from there."))
            else
                parts.push(qsTr("“%1” will be moved to the trash. You can restore it from there.").arg(name))
            return parts.concat(specialWarnings()).join(" ")
        }
        confirmText: qsTr("Move to trash")
        destructive: true
        onAccepted: {
            const trashPath = Workspace.deletePage(path)
            if (trashPath !== "" && shell.toasts)
                shell.toasts.show(qsTr("Moved to trash"),
                                  { actionText: qsTr("Undo"), onAction: () => Workspace.restore(trashPath) })
        }
    }

    Connections {
        target: AppController
        function onUiActionRequested(action) {
            shell.handleUiAction(action)
        }
        // A page or folder of the tree from a view (All pages, the page's menu, the palette).
        function onItemActionRequested(action, path) {
            const isDir = Workspace.folderInfo(path).exists === true
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
    Connections {
        target: Workspace.replacing
        function onApplied(token, hits, files, changed, failed) {
            if (hits > 0) {
                AppController.toastAction(qsTr("%n occurrence(s) replaced", "", hits) + " · " + qsTr("%n note(s)", "", files),
                                          "success", qsTr("Undo"), Workspace.replacing.undo.bind(Workspace.replacing, token))
            }
            if (changed.length > 0)
                AppController.toast(qsTr("Left as they are, changed since the preview: %1").arg(changed.join(", ")), "warning")
            if (failed.length > 0)
                AppController.toast(qsTr("Could not write: %1").arg(failed.join(", ")), "error")
        }
        function onUndone(restored, left) {
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
    readonly property var viewShortcuts: ["page.find", "page.replace", "page.source", "page.addProperty"]
    Repeater {
        model: ShortcutMap.bindings.filter(b => shell.viewShortcuts.indexOf(b.id) < 0
                                           && !(b.id === "block.copyRef" && b.sequence === "Ctrl+Shift+C"))
        delegate: Item {
            id: binding
            required property var modelData
            Shortcut {
                sequence: binding.modelData.sequence
                enabled: shell.shortcutEnabled(binding.modelData.id)
                onActivated: shell.runShortcut(binding.modelData.id)
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
    function shortcutEnabled(id) {
        switch (id) {
        case "panel.toggle": return !Theme.narrow
        case "page.rename": return shell.route.view === "page"
        case "page.print": return shell.route.view === "page"
        }
        return true
    }
    /// What a command's shortcut does: the shell's own popups directly, the rest as the palette.
    function runShortcut(id) {
        switch (id) {
        case "palette": shell.openPalette(); break
        case "page.switch": shell.openSwitcher(); break
        case "go.search": AppController.search(""); break
        case "page.new": shell.openNewPage(""); break
        case "capture": shell.openCapture(); break
        case "panel.toggle": AppController.sidePanelVisible = !AppController.sidePanelVisible; break
        case "page.rename": shell.openRename(shell.route.path, false); break
        default: CommandModel.run(id)
        }
    }

    // ---- Tabs: links in a new tab (OpenIn), pages in windows of their own -------------------------

    /// tab id → its ShellPageWindow.
    property var pageWindows: ({})
    /// One window per AppController.windowTabs entry: new ones made and shown, gone ones destroyed.
    function syncPageWindows() {
        const ids = AppController.windowTabs
        const kept = {}
        for (const id of ids) {
            let w = pageWindows[id]
            if (!w) {
                w = pageWindowComponent.createObject(shell, { tabId: id, titleOf: (r) => shell.routeTitle(r) })
                if (!w)
                    continue
                w.show()
                w.raise()
                w.requestActivate()
            }
            kept[id] = w
        }
        for (const key of Object.keys(pageWindows)) {
            if (!kept[key]) {
                // Hidden at once (destroy() waits for the event loop).
                pageWindows[key].hide()
                pageWindows[key].destroy()
            }
        }
        pageWindows = kept
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
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state !== Qt.ApplicationActive)
                AppController.saveTabs()
        }
    }
    Binding { target: OpenIn; property: "windows"; value: AppController.windowsAllowed && !Theme.narrow }

    Component.onCompleted: {
        // The navigation bar's first margin.
        noteNavigationBar()
        OpenIn.openIn = (target, open) => AppController.openIn(target, open)
        syncPageWindows()
    }
    Component.onDestruction: OpenIn.openIn = null

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
            waited += interval
            if (!Workspace.ready && waited < 3000)
                return
            stop()
            shell.openDevPopup(AppController.devPopup)
        }
    }
    function openDevPopup(popup) {
        const name = popup.split(":")[0]
        const query = popup.indexOf(":") >= 0 ? popup.slice(popup.indexOf(":") + 1) : ""
        // --screenshot waits until the popup has opened (its enter transition over), or at once
        // when it is open already (DevTools.holdScreenshot).
        const target = ({ palette: palette, switcher: switcher, capture: capture, newPage: newPage,
                          newFolder: newPage, move: moveDialog, about: about, drawer: drawer })[name] || null
        if (target && !target.opened) {
            DevTools.holdScreenshot(true)
            const release = () => {
                if (!target.opened)
                    return
                target.openedChanged.disconnect(release)
                DevTools.holdScreenshot(false)
            }
            target.openedChanged.connect(release)
        }
        Qt.callLater(() => {
            switch (name) {
            case "palette": shell.openPalette(query); break
            case "switcher": shell.openSwitcher(query); break
            case "capture": shell.openCapture(); break
            case "newPage": shell.openNewPage(""); break
            case "newFolder": shell.openNewFolder(""); break
            // move:<path>[><folder>] (else the page shown): the move dialog for a page or a folder,
            // and with ">" the folder chosen in it (what the move changes shows).
            case "move": {
                const item = query.split(">")[0] || shell.route.path
                shell.openMove(item, Workspace.folderInfo(item).exists === true)
                if (query.indexOf(">") >= 0)
                    Qt.callLater(() => moveDialog.choose({ folder: query.slice(query.indexOf(">") + 1), create: false }))
                break
            }
            case "about": shell.openAbout(); break
            case "drawer": drawer.open(); break
            }
        })
    }
}
