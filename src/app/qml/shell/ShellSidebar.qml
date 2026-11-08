pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellSidebar — the page panel beside the rail of views (ShellRail, docs/UI.md "Sidebar"): the
// notebook's name with "New page", then its sections in the order of Settings → Appearance →
// Layout (AppSettings.sidebarSections): Favorites, Recent and All pages — the notebook's
// folder tree. Favorites and Recent can be hidden (AppSettings.sidebarHidden); the sections
// before the tree are the list's header, the ones after it its footer. The same component is the
// left pane on wide windows (hidden with the top bar's sidebar button; the rail stays) and, beside
// the rail, the drawer's content on narrow ones (`navigated()` closes it).
//
// The tree (FolderTreeModel): a tap on a folder opens or closes it, on a page opens it; the
// context menu (right-click, press-and-hold, the Menu key, and on touch screens the row's "⋯"
// button) has the folder's and the page's actions — Move to…, Show folder pages, Search in
// folder, Export folder…, Copy link, …; the "Pages" header's "+" a menu with a new page or
// folder at the root, "Collapse all", "Show attachments" and "Show the open page" (reveal:
// the open page or daily note is shown and highlighted in the tree on every route change,
// `ui/sidebar/revealCurrent`, on by default — a collapsed "Pages" section stays collapsed, only
// "Show in tree" opens it). The revealed row is kept fully in the list's visible part — above
// the system's navigation bar — until the user scrolls the list: it is placed again whenever the list's
// geometry changes, since a closed drawer lays itself out only when it opens (its system-bar
// padding, a keyboard that goes, sections that grew meanwhile), so a row placed while it was
// closed or still opening could end up under the footer. A row revealed while it is in view (a
// click on it) leaves the list where it is: it never moves under the pointer. With a mouse or
// touchpad (never a finger: the list flicks and
// press-and-hold stay as they are) a row is dragged onto a folder (a closed one opens after
// 600 ms; the list scrolls near its edges) or onto the "Pages" header (the notebook's root) to
// move it there (`dropMoveRequested()`); the ghost names the target and what the move rewrites
// (or why it cannot move); Escape cancels the drag. Daily notes are files here like any other:
// they can be renamed, moved and dragged (a misfiled day can be put right).
Rectangle {
    id: sidebar

    property real topInset: 0
    property real bottomInset: 0

    signal navigated()
    signal newPageRequested(string folder)
    signal newFolderRequested(string folder)
    signal renameRequested(string path, bool isDir)
    signal deleteRequested(string path)
    signal moveRequested(string path, bool isDir)
    /// A row dragged onto a folder ("" = the notebook's root): the shell moves it.
    signal dropMoveRequested(string path, string folder)

    readonly property string routeView: AppController.currentView
    readonly property string routePath: AppController.routeText(AppController.currentRoute, "path")
    property bool favoritesOpen: flag(AppSettings.value("ui/sidebar/favorites", true))
    property bool recentOpen: flag(AppSettings.value("ui/sidebar/recent", true))
    property bool pagesOpen: flag(AppSettings.value("ui/sidebar/pages", true))
    /// The sections Favorites and Recent, unless hidden (Settings → Appearance → Layout).
    readonly property bool favoritesShown: AppSettings.sidebarHidden.indexOf("favorites") < 0
    readonly property bool recentShown: AppSettings.sidebarHidden.indexOf("recent") < 0
    /// The sections above the tree (the list's header) and below it (its footer), in the order of
    /// the settings: "favorites" | "recent".
    readonly property list<string> sectionsBefore: AppSettings.sidebarSections.slice(0, Math.max(0, AppSettings.sidebarSections.indexOf("pages")))
    readonly property list<string> sectionsAfter: AppSettings.sidebarSections.slice(AppSettings.sidebarSections.indexOf("pages") + 1)
    /// "Recent": up to 5 recent pages, without the one shown (the tree highlights it). Set once
    /// per change — a route change updates the recent pages and then the current path — and only
    /// when the list differs, so the header above the tree is not rebuilt (and resized) twice while
    /// the tree is placed for the new route.
    property list<var> recentOthers
    function updateRecentOthers(): void {
        // ([{ path, title }]: the same list when its text is.)
        const next = sidebar.recentNext()
        if (sidebar.textOf(next) === sidebar.textOf(sidebar.recentOthers))
            return
        sidebar.recentOthers = next
    }
    /// A list's JSON text (typed).
    function textOf(list: var): string {
        return Js.stringify(list)
    }
    function recentNext(): list<var> {
        return AppController.recentPagesExcept(sidebar.currentPath, 5)
    }
    // (Qt.callLater: once per turn of the event loop. The same for the other timers below.)
    Later { id: recentLater; onTriggered: sidebar.updateRecentOthers() }
    Later { id: revealRouteLater; onTriggered: sidebar.revealRoute() }
    Later { id: placeRevealedLater; onTriggered: sidebar.placeRevealed() }
    Later { id: placeListLater; onTriggered: sidebar.placeList() }
    Later { id: keepPlaceLater; onTriggered: list.keepPlace() }
    Later { id: dragAgainLater; onTriggered: sidebar.dragTo(sidebar.dragScenePos) }
    onCurrentPathChanged: recentLater.restart()
    /// Show the open page in the tree on every route change.
    property bool revealCurrent: flag(AppSettings.value("ui/sidebar/revealCurrent", true))
    /// The row `reveal()` keeps in view ("" = none): placed again on every change of the list's
    /// geometry (`placeList()`) until the user scrolls the list or another route shows no file.
    property string revealedPath
    /// The file the current route shows — a page, or a daily note opened as a page: highlighted
    /// in the tree.
    readonly property string currentPath: {
        if (sidebar.routeView === "page")
            return sidebar.routePath
        // (Read for real: a bare `void Workspace.revision` is compiled away with the dependency.)
        if (sidebar.routeView !== "journal" || Workspace.revision < 0)
            return ""
        const date = AppController.routeText(AppController.currentRoute, "date")
        const p = sidebar.routePath !== "" ? sidebar.routePath : (date !== "" ? Workspace.journalPath(date) : "")
        return p !== "" && Workspace.exists(p) ? p : ""
    }

    // ---- Dragging rows (mouse and touchpad) ------------------------------------------------
    /// A row is being dragged: its path, whether it is a folder, its title.
    property string dragPath
    property bool dragIsDir: false
    property string dragTitle
    readonly property bool dragging: dragPath !== ""
    /// Where it would go: a folder ("" = the root), valid only while `dropValid`.
    property string dropFolder
    /// `dropFolder` is a folder the item may go into (not itself, its subtree or where it is).
    property bool dropAllowed: false
    /// Workspace.moveImpact() of the drop (a moment after the target changed, planned without
    /// blocking — Workspace.requestMoveImpact(); `dropImpactKnown` false before).
    property var dropImpact: ({})
    property bool dropImpactKnown: false
    readonly property bool dropValid: dropAllowed && !(dropImpactKnown && !AppController.routeFlag(dropImpact, "ok"))
    /// The pointer is over the "Pages" header (the root).
    property bool dropOnRoot: false
    property point dragPoint
    /// The pointer's last scene position: the target is found again as the list scrolls under it.
    property point dragScenePos
    /// dragTo()'s pointer in the list's and the header's coordinates.
    property point _inList
    property point _inHeader
    /// The "Pages" header in the list's header: the drop target of the notebook's root.
    property Item pagesHeader: null

    /// QSettings hands booleans back as strings after a restart.
    function flag(value: var): bool {
        return value === true || value === "true"
    }
    function go(route: var): void {
        AppController.navigate(route)
        sidebar.navigated()
    }
    /// Open the page `path` as a click does — `target` "tab": in a new tab (OpenIn's navigation
    /// to a target: AppController.beginOpenIn … endOpenIn).
    function openPage(path: string, target: string): void {
        AppController.beginOpenIn(target)
        AppController.openPage(path)
        AppController.endOpenIn()
        sidebar.navigated()
    }
    /// The same where the click asks (Ctrl+click, Ctrl+Enter: a new tab — OpenIn).
    function openPageHere(path: string): void {
        sidebar.openPage(path, OpenIn.clickTarget(Qt.LeftButton, EditorInput.keyboardModifiers()))
    }
    function showContextMenu(item: Item, path: string, isDir: bool): void {
        contextMenu.path = path
        contextMenu.isDir = isDir
        contextMenu.popup(item, Theme.space6, item.height - 4)
    }
    /// Indentation of a row: 18 px (touch) / 14 px per level, 8 px per level below the fourth
    /// (a narrow drawer keeps room for deep names).
    function indent(depth: int): real {
        const step = Theme.touchUi ? 18 : 14
        return depth <= 4 ? depth * step : 4 * step + (depth - 4) * 8
    }
    /// Show `path` in the tree: the folders above it open, the row scrolled into view (and kept
    /// there: `revealedPath`). A collapsed "Pages" section is opened only when `force` ("Show in
    /// tree", a new folder); the reveal on every route change leaves it collapsed.
    function reveal(path: string, force: bool): void {
        if (path === "")
            return
        if (!sidebar.pagesOpen) {
            if (!force)
                return
            sidebar.pagesOpen = true
            AppSettings.setValue("ui/sidebar/pages", true)
        }
        if (tree.reveal(path) < 0)
            return
        sidebar.revealedPath = path
        if (!sidebar.keepIfInView())
            placeRevealedLater.restart()
    }
    function revealRoute(): void {
        if (sidebar.currentPath !== "")
            sidebar.reveal(sidebar.currentPath, false)
        else
            sidebar.revealedPath = "" // (a route with no file in the tree: the list stays as it is)
    }
    /// The revealed row lies wholly in the list's visible part as the list is laid out now (its
    /// own row: a model change not laid out yet leaves another row at its index): the list stays
    /// where it is, kept there (`list.scrolledAway`) — a click on a row the list was scrolled to
    /// must not move the row from under the pointer (to the bottom edge, where the list placed
    /// from its top would show it: the next click hit another row).
    function keepIfInView(): bool {
        if (!list || sidebar.revealedPath === "" || list.model !== tree || list.height <= 0)
            return false
        const item = sidebar.rowAt(tree.indexOf(sidebar.revealedPath)) // (a tree row: its `path`)
        if (!item || sidebar.pathOfRow(item) !== sidebar.revealedPath || item.y < list.contentY - 0.5
                || item.y + item.height > list.contentY + list.height + 0.5)
            return false
        list.scrolledAway = !list.atYBeginning
        return true
    }
    /// A row just revealed, once the list is laid out (folders opened above it): in view it
    /// stays, else the list is placed. Hidden (a closed drawer): placed as it shows.
    /// The list's row `index` (typed) and a row's `path` (a tree row's required property).
    function rowAt(index: int): Item {
        return list.itemAtIndex(index)
    }
    function pathOfRow(row: Item): string {
        return AppController.propertyOf(row, "path")
    }
    function placeRevealed(): void {
        if (!list || !sidebar.visible)
            return
        list.forceLayout()
        if (!sidebar.keepIfInView())
            sidebar.placeList()
    }
    /// The list's position after its geometry changed (the drawer opening, a keyboard, the window,
    /// a section of its header that grew) or a row revealed out of view: at the top (the
    /// navigation in sight) unless it is kept scrolled away, then the revealed row fully in view —
    /// with the least scrolling, so the navigation stays in sight where the row allows it. The
    /// row only while shown: a closed drawer's list still has the size it had (it is laid out
    /// again as it opens, and placed again then). Never while the user moves it.
    function placeList(): void {
        if (!list || list.moving)
            return
        if (!list.scrolledAway)
            list.positionViewAtBeginning()
        if (!sidebar.visible || sidebar.revealedPath === "" || list.model !== tree)
            return
        const row = tree.indexOf(sidebar.revealedPath)
        if (row >= 0)
            list.positionViewAtIndex(row, ListView.Contain)
    }
    /// "Copy link": the same text as the page menu's and the editor's (PageTools.wikiLink).
    function copyLink(path: string): void {
        const link = tools.wikiLink(path)
        if (tools.copyText(link))
            AppController.toast(qsTr("Copied %1").arg(link), "success")
    }

    // The drag itself: started by a row's DragHandler, followed by `pointerWatch` (it sees the
    // pointer whoever holds it — a row scrolled out of the list may lose its handler).
    function beginDrag(path: string, isDir: bool, title: string): void {
        sidebar.revealedPath = "" // (the list scrolls under the pointer)
        sidebar.dragPath = path
        sidebar.dragIsDir = isDir
        sidebar.dragTitle = title
        sidebar.dropFolder = ""
        sidebar.dropAllowed = false
        sidebar.dropImpact = ({})
        sidebar.dropImpactKnown = false
        sidebar.dropOnRoot = false
    }
    function cancelDrag(): void {
        if (sidebar.dragging)
            Workspace.cancelMoveImpact() // (not another sidebar's: the drawer's and the window's)
        sidebar.dragPath = ""
        sidebar.dropAllowed = false
        sidebar.dropImpact = ({})
        sidebar.dropImpactKnown = false
        sidebar.dropOnRoot = false
        expandTimer.stop()
        impactTimer.stop()
        scrollTimer.direction = 0
    }
    /// Can the dragged item go into `folder`? Not into itself, its own subtree or where it is.
    function canDropInto(folder: string): bool {
        if (!sidebar.dragging)
            return false
        if (sidebar.dragIsDir && (folder === sidebar.dragPath || Js.startsWith(folder, sidebar.dragPath + "/")))
            return false
        return folder !== Workspace.folderOf(sidebar.dragPath)
    }
    function dragTo(scenePos: point): void {
        if (!sidebar.dragging)
            return
        // (Copied first: a value-type parameter read after a call is not compiled.)
        const sceneX = scenePos.x
        const sceneY = scenePos.y
        sidebar.dragScenePos = scenePos
        sidebar.dragPoint = sidebar.mapFromItem(null, sceneX, sceneY)
        sidebar._inList = list.mapFromItem(null, sceneX, sceneY)
        // (The properties read again after each call: a call's result is not kept in a local
        // across another call by compiled code.)
        // Near the list's edges: scroll (scrollTimer finds the target again on every step).
        scrollTimer.direction = sidebar._inList.y < 36 && sidebar._inList.y >= 0 ? -1
                              : sidebar._inList.y > list.height - 36 && sidebar._inList.y <= list.height ? 1 : 0
        // Only what shows in the list is a target: the header scrolled above its top edge is
        // clipped away, though still `visible`.
        const inside = sidebar._inList.x >= 0 && sidebar._inList.x <= list.width && sidebar._inList.y >= 0
                       && sidebar._inList.y <= list.height
        // The folder under the pointer (`found`; "" the notebook's root).
        let found = false
        let folder = ""
        let hoveredFolder = ""
        let collapsed = false
        if (inside && sidebar.pagesHeader && sidebar.pagesHeader.visible) {
            sidebar._inHeader = sidebar.pagesHeader.mapFromItem(null, sceneX, sceneY)
            if (sidebar._inHeader.x >= 0 && sidebar._inHeader.y >= 0 && sidebar._inHeader.x <= sidebar.pagesHeader.width
                    && sidebar._inHeader.y <= sidebar.pagesHeader.height)
                found = true
        }
        if (!found && inside) {
            // (FolderTreeModel.get: { path, isDir, expanded, depth }; {} for no row.)
            sidebar._entry = tree.get(list.indexAt(sidebar._inList.x + list.contentX, sidebar._inList.y + list.contentY))
            if (Js.hasKey(sidebar._entry, "path")) {
                const entryPath = sidebar.entryText("path")
                const entryIsDir = sidebar.entryFlag("isDir")
                found = true
                folder = entryIsDir ? entryPath : sidebar.folderOf(entryPath)
                if (entryIsDir && !sidebar.entryFlag("expanded")) {
                    hoveredFolder = entryPath
                    collapsed = true
                }
            }
        }
        sidebar.dropOnRoot = found && folder === ""
        const allowed = found && sidebar.canDropInto(folder)
        const target = folder
        // What the move would rewrite: asked once the target stays a moment.
        if (target !== sidebar.dropFolder || allowed !== sidebar.dropAllowed) {
            sidebar.dropImpact = ({})
            sidebar.dropImpactKnown = false
            Workspace.cancelMoveImpact()
            if (allowed)
                impactTimer.restart()
            else
                impactTimer.stop()
        }
        sidebar.dropAllowed = allowed
        sidebar.dropFolder = target
        // A closed folder under the pointer opens after a moment.
        if (collapsed && expandTimer.folder !== hoveredFolder) {
            expandTimer.folder = hoveredFolder
            expandTimer.restart()
        } else if (!collapsed) {
            expandTimer.folder = ""
            expandTimer.stop()
        }
    }
    /// The tree row under the dragged pointer (FolderTreeModel.get) and its fields, typed.
    property var _entry: ({})
    function entryText(key: string): string {
        return AppController.routeText(sidebar._entry, key)
    }
    function entryFlag(key: string): bool {
        return AppController.routeFlag(sidebar._entry, key)
    }
    function folderOf(path: string): string {
        return Workspace.folderOf(path)
    }
    function drop(): void {
        const path = sidebar.dragPath
        const folder = sidebar.dropFolder
        const valid = sidebar.dropValid
        sidebar.cancelDrag()
        if (valid)
            sidebar.dropMoveRequested(path, folder)
    }

    color: Theme.bgSunken
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Sidebar")

    FolderTreeModel {
        id: tree
        workspace: Workspace
        showAttachments: sidebar.flag(AppSettings.value("ui/sidebar/attachments", false))
    }
    PageTools { id: tools; workspace: Workspace }

    // The open page follows into the tree (a page opened from search, a link, the switcher).
    Connections {
        target: AppController
        function onCurrentRouteChanged() {
            if (sidebar.revealCurrent)
                revealRouteLater.restart()
            else
                sidebar.revealedPath = ""
        }
    }
    Connections {
        target: Workspace
        function onRecentPagesChanged() { recentLater.restart() }
    }
    Component.onCompleted: {
        sidebar.updateRecentOthers()
        if (sidebar.revealCurrent)
            revealRouteLater.restart()
    }
    // Shown (the drawer opens, the window's sidebar comes back): its list is laid out again —
    // placed again (the header may have grown meanwhile: the navigation stays in sight).
    onVisibleChanged: if (visible) placeListLater.restart()

    Timer {
        id: expandTimer
        property string folder
        interval: 600
        onTriggered: {
            if (!sidebar.dragging || expandTimer.folder === "")
                return
            tree.setExpanded(expandTimer.folder, true)
            dragAgainLater.restart()
        }
    }
    Timer {
        id: scrollTimer
        property int direction: 0
        interval: 30
        repeat: true
        running: sidebar.dragging && direction !== 0
        onTriggered: {
            const max = Math.max(0, list.contentHeight + list.originY - list.height)
            list.contentY = Math.max(list.originY, Math.min(list.originY + max, list.contentY + scrollTimer.direction * 12))
            list.keepPlace() // (the user's scrolling too)
            // Other rows are under the still pointer now: they are the target (and open).
            sidebar.dragTo(sidebar.dragScenePos)
        }
    }
    // The ghost's second line: asked once the target stays a moment, planned a few notes per
    // frame (a big folder never stalls the drag), dropped when the target changes.
    Timer {
        id: impactTimer
        interval: 150
        onTriggered: if (sidebar.dragging && sidebar.dropAllowed)
                         Workspace.requestMoveImpact(sidebar.dragPath, sidebar.dropFolder)
    }
    Connections {
        target: Workspace
        function onMoveImpactReady(path: string, folder: string, impact: var) {
            if (sidebar.dragging && sidebar.dropAllowed && path === sidebar.dragPath && folder === sidebar.dropFolder) {
                sidebar.dropImpact = impact
                sidebar.dropImpactKnown = true
            }
        }
    }
    ShellMoveWords { id: words }
    Shortcut {
        sequence: "Escape"
        enabled: sidebar.dragging
        onActivated: sidebar.cancelDrag()
    }
    // (The row pressed has the focus: its Escape comes here too.)
    Keys.onEscapePressed: (event) => {
        event.accepted = sidebar.dragging
        sidebar.cancelDrag()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: sidebar.topInset
        spacing: 0

        // ---- The notebook's name (level with the rail's notebook button) -----------------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.touchUi ? Theme.touchTarget : Theme.controlHeight
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space1
            Layout.topMargin: Theme.space2
            Layout.bottomMargin: Theme.space1
            spacing: Theme.space2

            BText {
                objectName: "sidebarNotebookName"
                Layout.fillWidth: true
                text: Workspace.name
                variant: "label"
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
            }
            BIconButton {
                iconName: "square-pen"
                label: qsTr("New page")
                shortcut: ShortcutMap.sequences["page.new"] || ""
                size: "small"
                onClicked: sidebar.newPageRequested("")
            }
        }

        ListView {
            id: list
            TouchFlick.enabled: true
            objectName: "sidebarTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: sidebar.pagesOpen ? tree : null
            leftMargin: Theme.space1
            rightMargin: Theme.space1
            keyNavigationEnabled: true
            // Off while "Pages" is collapsed, which destroys the pooled rows: rows pooled before
            // the model went away (a folder moved out, rows scrolled off) were handed back after
            // it returned with their old required properties — the tree showed folders that were
            // gone instead of its rows.
            reuseItems: sidebar.pagesOpen
            // (Its handle dragged is the user's scrolling too.)
            T.ScrollBar.vertical: BScrollBar {
                onPressedChanged: {
                    sidebar.revealedPath = ""
                    if (!pressed)
                        list.keepPlace()
                }
            }
            /// The list stays scrolled away from its top where it is when its geometry changes: the
            /// user scrolled it (a flick, a drag, the wheel, the scroll bar, the arrow keys, a row
            /// dragged to its edge) or a row was revealed where the list showed it — not ending at
            /// the top. Otherwise it is placed from its top (`placeList()`).
            property bool scrolledAway: false
            function keepPlace(): void {
                list.scrolledAway = !list.atYBeginning
            }
            // The user scrolls: the revealed row is no longer kept in view.
            onMovementStarted: sidebar.revealedPath = ""
            onMovementEnded: keepPlace()
            // The arrow keys walk the rows (keyNavigationEnabled), scrolling to the current one:
            // the user's scrolling too. (Not accepted: the list moves on; where it ends up is
            // read after that.)
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                    sidebar.revealedPath = ""
                    keepPlaceLater.restart()
                }
                event.accepted = false
            }
            // Laid out again (the drawer opening, a keyboard, the window): placed again.
            onHeightChanged: placeListLater.restart()

            header: Column {
                width: list.width - list.leftMargin - list.rightMargin
                spacing: 0
                // A ListView keeps its rows in place when the header grows (a page added to
                // Recent), which scrolled the sections above the tree up out of sight (the drawer
                // once opened that way with its first entries hidden). Stay at the top instead (the
                // revealed row still in view: it moved down with the rows) — unless the list is
                // kept scrolled away: the rows stay where they are.
                onHeightChanged: placeListLater.restart()

                // ---- The sections before the tree --------------------------------------
                Repeater {
                    model: sidebar.sectionsBefore
                    delegate: Loader {
                        required property string modelData
                        width: parent.width
                        sourceComponent: modelData === "favorites" ? favoritesSection : recentSection
                    }
                }

                // ---- Pages (folder tree) ----------------------------------------------
                BSectionHeader {
                    id: pagesHeaderItem
                    objectName: "sidebarPagesHeader"
                    Component.onCompleted: sidebar.pagesHeader = pagesHeaderItem
                    Component.onDestruction: if (sidebar.pagesHeader === pagesHeaderItem) sidebar.pagesHeader = null
                    width: parent.width
                    text: qsTr("Pages")
                    // Pages without the daily notes (they have the journal and the calendar).
                    count: Workspace.pagesWithoutJournals
                    collapsible: true
                    expanded: sidebar.pagesOpen
                    actionIcon: "plus"
                    actionLabel: qsTr("New page or folder")
                    onActionClicked: pagesMenu.popup(pagesHeaderItem, pagesHeaderItem.width - pagesMenu.implicitWidth,
                                                     pagesHeaderItem.height)
                    onExpandedToggled: (open) => {
                        sidebar.pagesOpen = open
                        AppSettings.setValue("ui/sidebar/pages", open)
                        // Opened again: the open page shows (it was not revealed while collapsed).
                        if (open && sidebar.revealCurrent)
                            revealRouteLater.restart()
                    }
                    // A row dragged here goes to the notebook's root.
                    Rectangle {
                        anchors.fill: parent
                        visible: sidebar.dragging && sidebar.dropOnRoot
                        radius: Theme.radiusSm
                        color: sidebar.dropValid ? Theme.accentBg : "transparent"
                        border.width: 2
                        border.color: sidebar.dropValid ? Theme.accent : Theme.border
                    }
                }
            }

            delegate: BListItem {
                id: row
                objectName: "treeRow"
                required property int index
                required property string title
                required property string path
                required property bool isDir
                required property int depth
                required property bool expanded
                required property bool hasChildren
                required property string fileIcon
                required property string special
                required property string journalDate
                /// A row dragged over this folder would land in it.
                readonly property bool dropHere: sidebar.dragging && sidebar.dropValid && !sidebar.dropOnRoot
                                                 && isDir && sidebar.dropFolder === path

                width: list.width - list.leftMargin - list.rightMargin
                dense: true
                leftPadding: (Theme.touchUi ? 14 : 10) + sidebar.indent(depth)
                // Daily notes by their date ("Wed, Oct 14"), not by their file name.
                text: journalDate !== "" ? DatesHelper.formatShort(journalDate) : title
                // An empty folder opens too (folder-open) and says why nothing appears under it.
                subtitle: isDir && expanded && !hasChildren ? qsTr("Empty folder") : ""
                /// A page's own icon (docs/UI.md "Page icon and cover"), following its edits.
                readonly property var pageIcon: !isDir && Workspace.revision >= 0 ? Workspace.pageIconOf(path) : ({})
                readonly property string pageIconKind: AppController.routeText(pageIcon, "kind")
                readonly property string pageIconName: AppController.routeText(pageIcon, "name")
                iconName: special === "journals" ? fileIcon
                          : isDir && hasChildren ? (expanded ? "chevron-down" : "chevron-right")
                          : pageIconKind === "app" ? pageIconName : fileIcon
                iconEmoji: pageIconKind === "emoji" ? pageIconName : ""
                iconColor: special === "journals" ? Theme.accent : Theme.fgFaint
                selected: !isDir && sidebar.currentPath === path
                opacity: sidebar.dragging && sidebar.dragPath === path ? 0.5 : 1
                Accessible.description: isDir ? (expanded ? qsTr("expanded folder") : qsTr("collapsed folder")) : ""
                // Touch screens: the row's actions behind a visible button too (press-and-hold
                // stays). Created only there.
                trailing: Theme.touchUi ? moreLoader : null

                // Indent guides: a 1-px line under each ancestor's chevron, over the row's full
                // height (rows touch, so the lines run on), below the row's background (z -1 in a
                // Control). The selected row's pill covers them (its tint is translucent: none
                // drawn there).
                Repeater {
                    model: row.selected ? 0 : row.depth
                    delegate: Rectangle {
                        required property int index
                        z: -2
                        x: Math.round((Theme.touchUi ? 14 : 10) + sidebar.indent(index) + Theme.iconSize / 2)
                        width: 1
                        height: row.height
                        color: Theme.guideOnSunken
                    }
                }

                // A row back in the pool (reuseItems) is hidden until it is reused: the list
                // leaves it where it was, under the rows laid out since (a folder closed above it),
                // where it still counted as shown.
                ListView.onPooled: visible = false
                ListView.onReused: visible = true
                onClicked: {
                    if (row.isDir)
                        tree.toggle(row.index)
                    else
                        sidebar.openPageHere(row.path)
                }
                onPressAndHold: sidebar.showContextMenu(row, path, isDir)
                TapHandler {
                    // A middle click on a page: in a new tab.
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedButtons: Qt.MiddleButton
                    enabled: OpenIn.tabs && !row.isDir
                    onTapped: sidebar.openPage(row.path, "tab")
                }
                Keys.onRightPressed: if (isDir && !expanded) tree.toggle(index)
                Keys.onLeftPressed: if (isDir && expanded) tree.toggle(index)
                Keys.onMenuPressed: sidebar.showContextMenu(row, path, isDir)
                Keys.onDeletePressed: sidebar.deleteRequested(path)
                // The context menu on a right-click. Only for devices with buttons: a finger has
                // none, so Qt hands touch points to a TapHandler whatever its acceptedButtons —
                // every tap on a phone opened the (modal) menu, which then swallowed the release,
                // and the folder never expanded nor the page opened. Touch has press-and-hold.
                TapHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                    acceptedButtons: Qt.RightButton
                    onTapped: sidebar.showContextMenu(row, row.path, row.isDir)
                }
                // Dragging to another folder: a mouse or a touchpad only — a finger flicks the
                // list and holds for the menu (the drawer's edge swipe too). It takes the pointer
                // from the list (no flick) and keeps it.
                DragHandler {
                    id: rowDrag
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedButtons: Qt.LeftButton
                    target: null
                    dragThreshold: 6
                    grabPermissions: PointerHandler.CanTakeOverFromAnything
                    onActiveChanged: if (rowDrag.active) sidebar.beginDrag(row.path, row.isDir, row.text)
                }

                Loader {
                    id: moreLoader
                    active: Theme.touchUi
                    visible: active
                    sourceComponent: BIconButton {
                        id: moreButton
                        objectName: "treeRowMore"
                        iconName: "ellipsis"
                        // Quieter than the row's title (one on every row): faint like the
                        // row's icon (≥ 4.5:1 on bgSunken), the text colour while pressed.
                        iconColor: moreButton.down ? Theme.fg : Theme.fgFaint
                        label: qsTr("More: %1").arg(row.text)
                        size: "small"
                        showToolTip: false
                        onClicked: sidebar.showContextMenu(row, row.path, row.isDir)
                    }
                }
                // Where a dragged row would land.
                Loader {
                    anchors.fill: parent
                    active: row.dropHere
                    sourceComponent: Rectangle {
                        radius: Theme.radius
                        color: Theme.accentBg
                        opacity: 0.6
                        border.width: 2
                        border.color: Theme.accent
                    }
                }
            }

            footer: Column {
                width: list.width - list.leftMargin - list.rightMargin
                topPadding: sidebar.sectionsAfter.length > 0 ? Theme.space2 : 0
                bottomPadding: Theme.space4
                // ---- The sections after the tree -----------------------------------------
                Repeater {
                    model: sidebar.sectionsAfter
                    delegate: Loader {
                        required property string modelData
                        width: parent.width
                        sourceComponent: modelData === "favorites" ? favoritesSection : recentSection
                    }
                }
            }
        }

        // The system's navigation bar (phones, edge to edge).
        Item {
            Layout.preferredHeight: sidebar.bottomInset
        }
    }

    // Sees the pointer of a drag wherever it goes (a passive observer above the list: it takes
    // nothing from the rows), and its release: the drop.
    Item {
        id: pointerWatch
        anchors.fill: parent
        z: 10
        PointHandler {
            id: pointer
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onPointChanged: if (sidebar.dragging) sidebar.dragTo(pointer.point.scenePosition)
            onActiveChanged: if (!pointer.active && sidebar.dragging) sidebar.drop()
        }
    }
    // The dragged row under the pointer, with where it would go and what that rewrites (or why
    // it cannot go there).
    Rectangle {
        id: ghost
        objectName: "treeDragGhost"
        /// The second line: Workspace.moveImpact() of the target.
        readonly property string impactText: !sidebar.dragging || !sidebar.dropAllowed || !sidebar.dropImpactKnown ? ""
                                             : AppController.routeFlag(sidebar.dropImpact, "ok")
                                               ? words.impactText(words.count(sidebar.dropImpact, "links"),
                                                                  words.count(sidebar.dropImpact, "tags"),
                                                                  words.count(sidebar.dropImpact, "notes"),
                                                                  words.strings(sidebar.dropImpact, "specials"))
                                             : AppController.routeText(sidebar.dropImpact, "error")
        visible: sidebar.dragging
        z: 11
        x: Math.min(sidebar.dragPoint.x + 12, sidebar.width - width - 4)
        y: sidebar.dragPoint.y + 8
        width: Math.min(Math.max(ghostRow.implicitWidth, ghostImpact.visible ? ghostImpact.implicitWidth : 0)
                        + 2 * Theme.space2, sidebar.width - 8)
        height: ghostColumn.implicitHeight + Theme.space2
        radius: Theme.radius
        color: Theme.bgElevated
        border.width: 1
        border.color: sidebar.dropValid ? Theme.accent : Theme.border
        Column {
            id: ghostColumn
            x: Theme.space2
            width: ghost.width - 2 * Theme.space2
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Row {
                id: ghostRow
                spacing: Theme.space1
                BIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: sidebar.dragIsDir ? "folder" : "file-text"
                    size: Theme.iconSizeSm
                    color: Theme.fgMuted
                }
                BText {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, sidebar.width - 60)
                    elide: Text.ElideRight
                    text: sidebar.dropValid ? qsTr("%1 → %2").arg(sidebar.dragTitle)
                                                            .arg(sidebar.dropFolder !== "" ? Js.sliceFrom(sidebar.dropFolder, Js.lastIndexOf(sidebar.dropFolder, "/") + 1)
                                                                                           : qsTr("Notebook root"))
                                            : sidebar.dragTitle
                    font.pixelSize: Theme.textSm
                }
            }
            BText {
                id: ghostImpact
                objectName: "treeDragImpact"
                visible: text !== ""
                width: Math.min(implicitWidth, sidebar.width - 8 - 2 * Theme.space2)
                text: ghost.impactText
                wrapMode: Text.Wrap
                font.pixelSize: Theme.textXs
                color: sidebar.dropValid ? Theme.fgMuted : Theme.dangerText
            }
        }
    }

    // No edge line of its own: on wide windows the SplitView's handle (BSplitHandle) is the one
    // hairline beside the page; the drawer has its own edge.

    // ---- The page sections around the tree (their order: AppSettings.sidebarSections) ----------
    Component {
        id: favoritesSection
        Column {
            width: parent ? parent.width : 0
            // ---- Favourites (once a page is starred: the page header's star, a row's menu,
            // the palette) --------------------------------------------------------------------
            BSectionHeader {
                objectName: "sidebarFavoritesHeader"
                visible: sidebar.favoritesShown && Workspace.favorites.length > 0
                width: parent.width
                text: qsTr("Favorites")
                count: Workspace.favorites.length
                collapsible: true
                expanded: sidebar.favoritesOpen
                onExpandedToggled: (open) => {
                    sidebar.favoritesOpen = open
                    AppSettings.setValue("ui/sidebar/favorites", open)
                }
            }
            Column {
                visible: sidebar.favoritesShown && sidebar.favoritesOpen && Workspace.favorites.length > 0
                width: parent.width
                bottomPadding: Theme.space2

                Repeater {
                    model: Workspace.favorites
                    delegate: BListItem {
                        id: favoriteItem
                        // (Workspace.favorites: { path, title }.)
                        required property string path
                        required property string title
                        /// The page's own icon in place of the star (docs/UI.md "Page icon and cover").
                        readonly property var pageIcon: Workspace.revision >= 0 ? Workspace.pageIconOf(path) : ({})
                        readonly property string pageIconKind: AppController.routeText(pageIcon, "kind")
                        readonly property string pageIconName: AppController.routeText(pageIcon, "name")
                        width: parent.width
                        dense: true
                        text: title
                        iconName: pageIconKind === "app" ? pageIconName : "star"
                        iconEmoji: pageIconKind === "emoji" ? pageIconName : ""
                        selected: sidebar.routeView === "page" && sidebar.routePath === path
                        onClicked: sidebar.openPageHere(favoriteItem.path)
                        onPressAndHold: sidebar.showContextMenu(favoriteItem, favoriteItem.path, false)
                        TapHandler {
                            // A middle click: in a new tab (docs/UI.md "Tabs, split view and windows").
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            acceptedButtons: Qt.MiddleButton
                            enabled: OpenIn.tabs
                            onTapped: sidebar.openPage(favoriteItem.path, "tab")
                        }
                        TapHandler {
                            // Right-click only (see the tree rows below): never a finger.
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                            acceptedButtons: Qt.RightButton
                            onTapped: sidebar.showContextMenu(favoriteItem, favoriteItem.path, false)
                        }
                    }
                }
            }
        }
    }
    Component {
        id: recentSection
        Column {
            width: parent ? parent.width : 0
            // ---- Recent: the pages opened lately other than the one shown (the tree
            // highlights that one) ---------------------------------------------------------------
            BSectionHeader {
                objectName: "sidebarRecentHeader"
                visible: sidebar.recentShown && sidebar.recentOthers.length > 0
                width: parent.width
                text: qsTr("Recent")
                collapsible: true
                expanded: sidebar.recentOpen
                onExpandedToggled: (open) => {
                    sidebar.recentOpen = open
                    AppSettings.setValue("ui/sidebar/recent", open)
                }
            }
            Column {
                visible: sidebar.recentShown && sidebar.recentOpen && sidebar.recentOthers.length > 0
                width: parent.width
                bottomPadding: Theme.space2

                Repeater {
                    model: sidebar.recentOthers
                    delegate: BListItem {
                        id: recentItem
                        // (AppController.recentPagesExcept: { path, title }.)
                        required property string path
                        required property string title
                        readonly property var pageIcon: Workspace.revision >= 0 ? Workspace.pageIconOf(path) : ({})
                        readonly property string pageIconKind: AppController.routeText(pageIcon, "kind")
                        readonly property string pageIconName: AppController.routeText(pageIcon, "name")
                        width: parent.width
                        dense: true
                        text: title
                        iconName: pageIconKind === "app" ? pageIconName : "clock"
                        iconEmoji: pageIconKind === "emoji" ? pageIconName : ""
                        onClicked: sidebar.openPageHere(recentItem.path)
                        TapHandler {
                            // A middle click: in a new tab (docs/UI.md "Tabs, split view and windows").
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            acceptedButtons: Qt.MiddleButton
                            enabled: OpenIn.tabs
                            onTapped: sidebar.openPage(recentItem.path, "tab")
                        }
                    }
                }
            }
        }
    }

    BMenu {
        id: contextMenu
        objectName: "sidebarContextMenu"

        property string path
        property bool isDir: false
        // (Workspace.favorites: followed; it lists the favourites that exist.)
        readonly property bool favorite: !isDir && Workspace.favorites.length >= 0 && Workspace.isFavorite(contextMenu.path)
                                         && Workspace.exists(contextMenu.path)
        readonly property bool page: !isDir && Js.endsWith(Js.toLowerCase(contextMenu.path), ".md")

        BMenuItem {
            objectName: "treeMenuNewPage"
            visible: contextMenu.isDir
            height: visible ? implicitHeight : 0
            text: qsTr("New page here")
            iconName: "square-pen"
            onTriggered: sidebar.newPageRequested(contextMenu.path)
        }
        BMenuItem {
            objectName: "treeMenuNewFolder"
            visible: contextMenu.isDir
            height: visible ? implicitHeight : 0
            text: qsTr("New folder…")
            iconName: "folder-plus"
            onTriggered: sidebar.newFolderRequested(contextMenu.path)
        }
        BMenuSeparator {
            visible: contextMenu.isDir
            height: visible ? implicitHeight : 0
        }
        BMenuItem {
            objectName: "treeMenuShowPages"
            visible: contextMenu.isDir
            height: visible ? implicitHeight : 0
            text: qsTr("Show folder pages")
            iconName: "files"
            onTriggered: sidebar.go({ view: "allPages", path: contextMenu.path })
        }
        BMenuItem {
            objectName: "treeMenuSearch"
            visible: contextMenu.isDir
            height: visible ? implicitHeight : 0
            text: qsTr("Search in folder")
            iconName: "search"
            onTriggered: sidebar.go({ view: "search", folder: contextMenu.path })
        }
        BMenuItem {
            objectName: "treeMenuOpenTab"
            visible: !contextMenu.isDir
            height: visible ? implicitHeight : 0
            text: qsTr("Open in new tab")
            iconName: "plus"
            onTriggered: sidebar.openPage(contextMenu.path, "tab")
        }
        BMenuItem {
            objectName: "treeMenuOpenSide"
            visible: !contextMenu.isDir && !Theme.narrow
            height: visible ? implicitHeight : 0
            text: qsTr("Open in side panel")
            iconName: "panel-right"
            onTriggered: AppController.openPage(contextMenu.path, -1, true)
        }
        BMenuItem {
            objectName: "treeMenuOpenWindow"
            visible: !contextMenu.isDir && AppController.windowsAllowed && !Theme.narrow
            height: visible ? implicitHeight : 0
            text: qsTr("Open in new window")
            iconName: "app-window"
            onTriggered: AppController.openWindow({ view: "page", path: contextMenu.path })
        }
        BMenuItem {
            visible: contextMenu.page
            height: visible ? implicitHeight : 0
            text: contextMenu.favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
            iconName: "star"
            onTriggered: Workspace.toggleFavorite(contextMenu.path)
        }
        BMenuItem {
            objectName: "treeMenuMove"
            text: qsTr("Move to…")
            iconName: "folder-input"
            onTriggered: sidebar.moveRequested(contextMenu.path, contextMenu.isDir)
        }
        BMenuItem {
            objectName: "treeMenuCopyLink"
            visible: !contextMenu.isDir
            height: visible ? implicitHeight : 0
            text: qsTr("Copy link")
            iconName: "link"
            onTriggered: sidebar.copyLink(contextMenu.path)
        }
        BMenuItem {
            objectName: "treeMenuExport"
            visible: contextMenu.isDir
            height: visible ? implicitHeight : 0
            text: qsTr("Export folder…")
            iconName: "share"
            onTriggered: sidebar.go({ view: "importExport", tab: "export", scope: "folder", folder: contextMenu.path })
        }
        BMenuItem {
            objectName: "treeMenuRename"
            text: qsTr("Rename…")
            iconName: "pencil"
            onTriggered: sidebar.renameRequested(contextMenu.path, contextMenu.isDir)
        }
        BMenuItem {
            visible: contextMenu.page
            height: visible ? implicitHeight : 0
            text: qsTr("Page history")
            iconName: "history"
            onTriggered: sidebar.go({ view: "history", path: contextMenu.path })
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "treeMenuTrash"
            text: qsTr("Move to trash")
            iconName: "trash-2"
            danger: true
            onTriggered: sidebar.deleteRequested(contextMenu.path)
        }
    }

    // The "Pages" header's "+": new page or folder at the root, and how the tree shows.
    BMenu {
        id: pagesMenu
        objectName: "sidebarPagesMenu"

        BMenuItem {
            objectName: "pagesMenuNewPage"
            text: qsTr("New page")
            iconName: "square-pen"
            shortcut: ShortcutMap.sequences["page.new"] || ""
            onTriggered: sidebar.newPageRequested("")
        }
        BMenuItem {
            objectName: "pagesMenuNewFolder"
            text: qsTr("New folder")
            iconName: "folder-plus"
            onTriggered: sidebar.newFolderRequested("")
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "pagesMenuCollapseAll"
            text: qsTr("Collapse all")
            iconName: "chevrons-down-up"
            onTriggered: tree.collapseAll()
        }
        BMenuItem {
            objectName: "pagesMenuAttachments"
            text: qsTr("Show attachments")
            iconName: "paperclip"
            id: attachmentsItem
            checkable: true
            // (A Binding: it follows again after the item's own toggle; onTriggered sets it at once.)
            Binding on checked { value: tree.showAttachments }
            onTriggered: {
                const show = !tree.showAttachments
                tree.showAttachments = show
                AppSettings.setValue("ui/sidebar/attachments", show)
                attachmentsItem.checked = tree.showAttachments
            }
        }
        BMenuItem {
            objectName: "pagesMenuReveal"
            text: qsTr("Show the open page")
            iconName: "folder-tree"
            id: revealItem
            checkable: true
            Binding on checked { value: sidebar.revealCurrent }
            onTriggered: {
                sidebar.revealCurrent = !sidebar.revealCurrent
                AppSettings.setValue("ui/sidebar/revealCurrent", sidebar.revealCurrent)
                revealItem.checked = sidebar.revealCurrent
                if (sidebar.revealCurrent)
                    sidebar.revealRoute()
            }
        }
    }
}
