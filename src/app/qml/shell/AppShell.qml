pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Brulion

// AppShell — the main UI once a vault is open (docs/UI.md "Window layout").
//
// Wide (≥ 900 px): SplitView — sidebar | top bar + current view | optional side panel.
// Narrow: the sidebar is a Drawer (the menu button, or a swipe from the left edge — on touch UIs
// from a strip beyond the zone Android's gesture navigation keeps for "back"), a bottom
// navigation bar and a floating quick-capture button; the Android back key walks back through
// AppController's history and only lets the app close from the home view.
//
// Owns the shell's popups (command palette, quick switcher, quick capture, new page or folder,
// rename, "Przenieś do…", delete confirmation, about) and the global shortcuts; bars, the sidebar
// and views call the open*() functions (views through AppController.requestItemAction(action,
// path): move, rename, delete, newPage, newFolder, reveal). Content is laid out edge-to-edge: bars extend under the system bars and pad
// themselves with the SafeArea margins.
// On-screen keyboard (docs/UI.md "On-screen keyboard"): the content — sidebar, view and side
// panel — ends at the keyboard's top (KeyboardInsets.overlap, a strip under it), so the editing
// toolbar a view docks on its bottom edge sits on the keyboard, in the side panel too; while the
// keyboard is up or a toolbar is docked (also after the system hid the keyboard, while editing
// goes on) the bottom navigation and the capture button make way, and the content ends above the
// system's navigation bar.
FocusScope {
    id: shell

    /// The window's toast host (Main.qml).
    property BToastHost toasts: null
    property bool sidebarVisible: flag(AppSettings.value("ui/sidebar/visible", true))

    readonly property var safe: SafeArea.margins
    readonly property var route: AppController.currentRoute
    /// How much of the window's bottom the on-screen keyboard covers.
    readonly property real keyboardOverlap: KeyboardInsets.overlap
    /// Width of the strip at the left edge whose swipe opens the drawer (touch UIs), beyond the
    /// system's own zones (drawer.dragMargin).
    readonly property real edgeSwipeStrip: 24
    /// A view's editing toolbar is docked at its bottom edge (touch UIs): the main view's or the
    /// side panel's.
    readonly property bool toolbarDocked: routeView.viewToolbarDocked || sidePanel.toolbarDocked
    /// Typing: the keyboard is up, or a toolbar stays docked after the system hid it.
    readonly property bool typing: KeyboardInsets.shown || toolbarDocked
    /// Room under the content: the keyboard (minus the bottom navigation, when that is still
    /// shown); while typing at least the system's navigation bar, which the docked toolbar must
    /// clear.
    readonly property real keyboardRoom: bottomNav.visible ? Math.max(0, keyboardOverlap - bottomNav.height)
                                         : typing ? Math.max(keyboardOverlap, safe.bottom) : keyboardOverlap
    /// What covers the window's bottom edge for toasts (Main.qml): the keyboard and a docked
    /// toolbar while typing; else the bottom navigation and the capture button on phones.
    readonly property real toastClearance: typing ? keyboardRoom + (toolbarDocked ? Theme.editToolbarHeight : 0)
                                           : Theme.narrow ? bottomNav.height + 56 + Theme.space4 : safe.bottom + keyboardRoom
    /// Toasts go to the window's top (Main.qml) while text is typed on a touch screen — the
    /// on-screen keyboard up, or an editing toolbar docked while a view edits text: the caret's
    /// line is kept right above the keyboard and the toolbar, under the toasts' usual place.
    readonly property bool toastsAtTop: KeyboardInsets.shown || (toolbarDocked && routeView.viewEditing)
    readonly property string title: routeView.viewTitle !== "" ? routeView.viewTitle : routeTitle(route)
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

    // The detailed event log notes where the content ends while typing, once the keyboard has
    // settled (scripts/android-emulator-test.sh --keyboard checks a docked toolbar above the
    // system's navigation bar with it).
    LoggingCategory {
        id: platformLog
        name: "brulion.platform"
        defaultLogLevel: LoggingCategory.Info
    }
    readonly property string typingReport: !typing ? ""
        : "Typing: the content ends %1 px above the window's bottom (keyboard %2 px, navigation bar %3 px), editing toolbar %4"
          .arg(Math.round(keyboardRoom)).arg(Math.round(keyboardOverlap)).arg(Math.round(safe.bottom))
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
        case "graph": return qsTr("Graph")
        case "search": return qsTr("Search")
        case "allPages": return r.path ? r.path.split("/").pop() : qsTr("All pages")
        case "tags": return qsTr("Tags")
        case "tag": return "#" + r.tag
        case "sync": return qsTr("Sync")
        case "importExport": return qsTr("Import and export")
        case "history": return qsTr("History of %1").arg(pageLabel(r.path))
        case "trash": return qsTr("Trash")
        case "settings": return qsTr("Settings")
        }
        return "Brulion"
    }

    function openPalette(query) {
        palette.open()
        if (query)
            palette.query = query
    }
    function openSwitcher(query) {
        switcher.open()
        if (query)
            switcher.query = query
    }
    function openCapture() {
        // Captured notes belong in the user's journal: from the documentation, AppController
        // goes back to the user's notebook and opens the dialog there.
        if (AppController.inDocsVault)
            AppController.quickCapture()
        else
            capture.open()
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
        case "newPage": openNewPage(""); break
        case "newFolder": openNewFolder(""); break
        case "movePage": if (route.view === "page") openMove(route.path, false); break
        case "revealInTree": revealInTree(currentFile); break
        case "about": openAbout(); break
        case "toggleSidePanel": AppController.sidePanelVisible = !AppController.sidePanelVisible; break
        }
    }

    focus: true

    // Android back key / gesture: back through the history; at home the app may close.
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Back)
            event.accepted = AppController.canGoBack || !AppController.atHome
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Back)
            event.accepted = AppController.handleBackKey()
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
            visible: !Theme.narrow && shell.sidebarVisible
            active: !Theme.narrow
            SplitView.preferredWidth: Theme.sidebarWidth
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: 440
            sourceComponent: ShellSidebar {
                topInset: shell.safe.top
                // (The navigation bar's part the keyboard's room leaves.)
                bottomInset: Math.max(0, shell.safe.bottom - shell.keyboardRoom)
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

            ShellTopBar {
                Layout.fillWidth: true
                title: shell.title
                crumbs: shell.crumbs
                topInset: shell.safe.top
                sidebarVisible: shell.sidebarVisible
                picture: routeView.viewPicture
                pictureAsText: routeView.viewPictureAsText
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
            ShellRouteView {
                id: routeView
                Layout.fillWidth: true
                Layout.fillHeight: true
                focus: true
                route: AppController.currentRoute
                // The calendar grids' swipe took the swipe that closes the drawer over them.
                covered: drawer.visible
            }
            // The palette's block commands follow the main view's source mode (CommandModel).
            Binding { target: AppController; property: "sourceModeShown"; value: routeView.viewSourceMode }
            ShellBottomNav {
                id: bottomNav
                objectName: "shellBottomNav"
                visible: Theme.narrow && !shell.typing
                Layout.fillWidth: true
                bottomInset: shell.safe.bottom
                onMenuClicked: drawer.open()
            }
        }

        ShellSidePanel {
            id: sidePanel
            visible: !Theme.narrow && AppController.sidePanelVisible
            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 260
            SplitView.maximumWidth: 600
            pinnedTitle: shell.routeTitle(AppController.sidePanelRoute)
            contextPanel: routeView.viewSidePanel
        }
    }

    // The keyboard's room (it covers this part of the window); under a docked toolbar when the
    // keyboard is hidden: the navigation bar's strip.
    Rectangle {
        objectName: "shellKeyboardRoom"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: shell.keyboardRoom
        visible: shell.keyboardRoom > 0
        color: Theme.bgElevated
    }

    ShellCaptureButton {
        objectName: "shellCaptureButton"
        // Not while a view edits text: it would cover the editor's toolbar (and the caret), nor
        // over a picture (the view's `editing`: it covered the photo). Nor
        // on views of forms and tools (settings, sync, import/export, history, trash, the graph
        // with its zoom buttons): there it covered their buttons and fields, and capturing a note
        // is not what they are for.
        // Hidden by the user (Ustawienia → Wygląd → Układ): quick capture stays in the menu,
        // the palette and its shortcut.
        visible: AppSettings.captureButtonShown && Theme.narrow && !routeView.viewEditing && !shell.typing
                 && ["settings", "sync", "importExport", "history", "trash", "graph"].indexOf(shell.route.view) < 0
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.space4 + shell.safe.right
        anchors.bottomMargin: bottomNav.height + Theme.space4
        onClicked: shell.openCapture()
    }

    Drawer {
        id: drawer
        objectName: "shellDrawer"
        width: Math.min(320, shell.width * 0.86)
        height: shell.height
        edge: Qt.LeftEdge
        interactive: Theme.narrow
        // The edge swipe: a finger that starts within dragMargin of the left edge and moves
        // right opens the drawer (on it or the scrim, to the left, closes it). Touch UIs get a
        // strip a finger hits (Qt's default is the start-drag distance, a few px) that begins
        // beyond what the system keeps at that edge: gesture navigation's "back" zone
        // (GestureInsets, ~30 dp; a swipe there leaves the app), or a display cutout / system bar
        // in landscape (SafeArea). Taps and vertical scrolls in the strip still reach the content:
        // the drawer takes only a mostly horizontal drag past its threshold (20 px, more than a
        // Flickable's), not one an item already keeps (a scrolling Flickable). Android's text
        // selection handles are windows of their own above the app: the strip never sees them.
        // While typing or selecting blocks (the keyboard up or an editing toolbar docked: the
        // bottom navigation makes way too) there is no strip: the drawer took the drag of the
        // selection's start grip, which lies at the left edge (its MouseArea's preventStealing
        // did not stop it). Nor on a view that swipes sideways from that edge itself (`edgeSwipe`:
        // the calendar's week and month grids, to the previous period): a swipe there is the
        // view's. The ☰ button still opens it, a swipe on it still closes it. The strip follows
        // the UI mode, not the pointer: a mouse in a touch UI gets it too.
        dragMargin: !Theme.touchUi ? Application.styleHints.startDragDistance
                    : shell.typing || routeView.viewEdgeSwipe ? 0
                    : Math.max(shell.safe.left, GestureInsets.left) + shell.edgeSwipeStrip
        dim: true
        // Focus while open (Drawer's default, stated because the keys rely on it): Escape / the
        // Android back key close the drawer instead of acting underneath it.
        focus: true
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle { color: Theme.bgSunken }

        Loader {
            id: drawerSidebar
            anchors.fill: parent
            active: Theme.narrow
            sourceComponent: ShellSidebar {
                topInset: shell.safe.top
                bottomInset: shell.safe.bottom
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

    Shortcut { sequence: "Ctrl+K"; onActivated: shell.openPalette() }
    Shortcut { sequence: "Ctrl+O"; onActivated: shell.openSwitcher() }
    Shortcut { sequence: "Ctrl+Shift+F"; onActivated: AppController.search("") }
    Shortcut { sequence: "Alt+Left"; onActivated: AppController.back() }
    Shortcut { sequence: "Alt+Right"; onActivated: AppController.forward() }
    Shortcut { sequence: "Ctrl+J"; onActivated: AppController.openToday() }
    Shortcut { sequence: "Ctrl+N"; onActivated: shell.openNewPage("") }
    Shortcut { sequence: "Ctrl+Shift+N"; onActivated: shell.openCapture() }
    Shortcut { sequence: "Ctrl+Shift+B"; onActivated: AppController.toggleDocsVault() }
    Shortcut { sequence: "Ctrl+,"; onActivated: AppController.navigate({ view: "settings" }) }
    Shortcut {
        sequence: "Ctrl+\\"
        enabled: !Theme.narrow
        onActivated: AppController.sidePanelVisible = !AppController.sidePanelVisible
    }
    Shortcut {
        sequence: "F2"
        enabled: shell.route.view === "page"
        onActivated: shell.openRename(shell.route.path, false)
    }

    // --popup (screenshots and manual checks).
    Component.onCompleted: {
        const popup = AppController.devPopup
        if (popup === "")
            return
        const name = popup.split(":")[0]
        const query = popup.indexOf(":") >= 0 ? popup.slice(popup.indexOf(":") + 1) : ""
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
