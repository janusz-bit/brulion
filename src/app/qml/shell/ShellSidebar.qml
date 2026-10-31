pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellSidebar — navigation (Dziennik, Kalendarz, Zadania, Wszystkie strony, Tagi, Graf, Szukaj),
// favourites, recent pages (entries and these two sections can be hidden: AppSettings.sidebarHidden), the vault's folder tree and the sync status footer. The same
// component is the left pane on wide windows and the drawer's content on narrow ones
// (`navigated()` closes it).
//
// The tree (FolderTreeModel): a tap on a folder opens or closes it, on a page opens it; the
// context menu (right-click, press-and-hold, the Menu key, and on touch screens the row's "⋯"
// button) has the folder's and the page's actions — Przenieś do…, Pokaż strony folderu, Szukaj w
// folderze, Eksportuj folder…, Kopiuj link, …; the "Strony" header's "+" a menu with a new page or
// folder at the root, "Zwiń wszystkie", "Pokaż załączniki" and "Pokazuj otwartą stronę" (reveal:
// the open page or daily note is shown and highlighted in the tree on every route change,
// `ui/sidebar/revealCurrent`, on by default — a collapsed "Strony" section stays collapsed, only
// "Pokaż w drzewie" opens it). The revealed row is kept fully in the list's visible part — above
// the sync footer — until the user scrolls the list: it is placed again whenever the list's
// geometry changes, since a closed drawer lays itself out only when it opens (its system-bar
// padding, a keyboard that goes, sections that grew meanwhile), so a row placed while it was
// closed or still opening could end up under the footer. A row revealed while it is in view (a
// click on it) leaves the list where it is: it never moves under the pointer. With a mouse or
// touchpad (never a finger: the list flicks and
// press-and-hold stay as they are) a row is dragged onto a folder (a closed one opens after
// 600 ms; the list scrolls near its edges) or onto the "Strony" header (the notebook's root) to
// move it there (`dropMoveRequested()`); the ghost names the target and what the move rewrites
// (or why it cannot move); Escape cancels the drag. Daily notes are files here like any other:
// they can be renamed, moved and dragged (a misfiled day can be put right).
Rectangle {
    id: sidebar

    property real topInset: 0
    property real bottomInset: 0
    /// Views left out of the navigation list: the phone drawer's, which the bottom navigation
    /// already shows (AppShell). The window's sidebar lists them all.
    property var omitViews: []

    signal navigated()
    signal newPageRequested(string folder)
    signal newFolderRequested(string folder)
    signal renameRequested(string path, bool isDir)
    signal deleteRequested(string path)
    signal moveRequested(string path, bool isDir)
    /// A row dragged onto a folder ("" = the notebook's root): the shell moves it.
    signal dropMoveRequested(string path, string folder)

    readonly property var route: AppController.currentRoute
    property bool favoritesOpen: flag(AppSettings.value("ui/sidebar/favorites", true))
    property bool recentOpen: flag(AppSettings.value("ui/sidebar/recent", true))
    property bool pagesOpen: flag(AppSettings.value("ui/sidebar/pages", true))
    /// The sections Ulubione and Ostatnie, unless hidden (Ustawienia → Wygląd → Układ).
    readonly property bool favoritesShown: AppSettings.sidebarHidden.indexOf("favorites") < 0
    readonly property bool recentShown: AppSettings.sidebarHidden.indexOf("recent") < 0
    /// "Ostatnie": up to 5 recent pages, without the one shown (the tree highlights it). Set once
    /// per change — a route change updates the recent pages and then the current path — and only
    /// when the list differs, so the header above the tree is not rebuilt (and resized) twice while
    /// the tree is placed for the new route.
    property var recentOthers: []
    function updateRecentOthers() {
        const next = Workspace.recentPages.filter(p => p.path !== currentPath).slice(0, 5)
        if (next.length === recentOthers.length
                && next.every((p, i) => p.path === recentOthers[i].path && p.title === recentOthers[i].title))
            return
        recentOthers = next
    }
    onCurrentPathChanged: Qt.callLater(updateRecentOthers)
    /// Show the open page in the tree on every route change.
    property bool revealCurrent: flag(AppSettings.value("ui/sidebar/revealCurrent", true))
    /// The row `reveal()` keeps in view ("" = none): placed again on every change of the list's
    /// geometry (`placeList()`) until the user scrolls the list or another route shows no file.
    property string revealedPath
    /// The file the current route shows — a page, or a daily note opened as a page: highlighted
    /// in the tree.
    readonly property string currentPath: {
        if (route.view === "page")
            return route.path || ""
        // (Read for real: a bare `void Workspace.revision` is compiled away with the dependency.)
        if (route.view !== "journal" || Workspace.revision < 0)
            return ""
        const p = route.path || (route.date ? Workspace.journalPath(route.date) : "")
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
    /// blocking — Workspace.requestMoveImpact(); null before).
    property var dropImpact: null
    readonly property bool dropValid: dropAllowed && !(dropImpact && dropImpact.ok === false)
    /// The pointer is over the "Strony" header (the root).
    property bool dropOnRoot: false
    property point dragPoint
    /// The pointer's last scene position: the target is found again as the list scrolls under it.
    property point dragScenePos
    /// The "Strony" header in the list's header: the drop target of the notebook's root.
    property Item pagesHeader: null

    /// QSettings hands booleans back as strings after a restart.
    function flag(value) {
        return value === true || value === "true"
    }
    function go(route) {
        AppController.navigate(route)
        sidebar.navigated()
    }
    function openPage(path) {
        AppController.openPage(path)
        sidebar.navigated()
    }
    function showContextMenu(item, path, isDir) {
        contextMenu.path = path
        contextMenu.isDir = isDir
        contextMenu.popup(item, Theme.space6, item.height - 4)
    }
    /// Indentation of a row: 18 px (touch) / 14 px per level, 8 px per level below the fourth
    /// (a narrow drawer keeps room for deep names).
    function indent(depth) {
        const step = Theme.touchUi ? 18 : 14
        return depth <= 4 ? depth * step : 4 * step + (depth - 4) * 8
    }
    /// Show `path` in the tree: the folders above it open, the row scrolled into view (and kept
    /// there: `revealedPath`). A collapsed "Strony" section is opened only when `force` ("Pokaż w
    /// drzewie", a new folder); the reveal on every route change leaves it collapsed.
    function reveal(path, force) {
        if (!path)
            return
        if (!pagesOpen) {
            if (force !== true)
                return
            pagesOpen = true
            AppSettings.setValue("ui/sidebar/pages", true)
        }
        if (tree.reveal(path) < 0)
            return
        revealedPath = path
        if (!keepIfInView())
            Qt.callLater(placeRevealed)
    }
    function revealRoute() {
        if (currentPath !== "")
            reveal(currentPath, false)
        else
            revealedPath = "" // (a route with no file in the tree: the list stays as it is)
    }
    /// The revealed row lies wholly in the list's visible part as the list is laid out now (its
    /// own row: a model change not laid out yet leaves another row at its index): the list stays
    /// where it is, kept there (`list.scrolledAway`) — a click on a row the list was scrolled to
    /// must not move the row from under the pointer (to the bottom edge, where the list placed
    /// from its top would show it: the next click hit another row).
    function keepIfInView() {
        if (!list || revealedPath === "" || list.model !== tree || list.height <= 0)
            return false
        const item = list.itemAtIndex(tree.indexOf(revealedPath)) // (a tree row: its `path`)
        if (!item || item.path !== revealedPath || item.y < list.contentY - 0.5
                || item.y + item.height > list.contentY + list.height + 0.5)
            return false
        list.scrolledAway = !list.atYBeginning
        return true
    }
    /// A row just revealed, once the list is laid out (folders opened above it): in view it
    /// stays, else the list is placed. Hidden (a closed drawer): placed as it shows.
    function placeRevealed() {
        if (!list || !sidebar.visible)
            return
        list.forceLayout()
        if (!keepIfInView())
            placeList()
    }
    /// The list's position after its geometry changed (the drawer opening, a keyboard, the window,
    /// a section of its header that grew) or a row revealed out of view: at the top (the
    /// navigation in sight) unless it is kept scrolled away, then the revealed row fully in view —
    /// with the least scrolling, so the navigation stays in sight where the row allows it. The
    /// row only while shown: a closed drawer's list still has the size it had (it is laid out
    /// again as it opens, and placed again then). Never while the user moves it.
    function placeList() {
        if (!list || list.moving)
            return
        if (!list.scrolledAway)
            list.positionViewAtBeginning()
        if (!sidebar.visible || revealedPath === "" || list.model !== tree)
            return
        const row = tree.indexOf(revealedPath)
        if (row >= 0)
            list.positionViewAtIndex(row, ListView.Contain)
    }
    /// "Kopiuj link": the same text as the page menu's and the editor's (PageTools.wikiLink).
    function copyLink(path) {
        const link = tools.wikiLink(path)
        if (tools.copyText(link))
            AppController.toast(qsTr("Copied %1").arg(link), "success")
    }

    // The drag itself: started by a row's DragHandler, followed by `pointerWatch` (it sees the
    // pointer whoever holds it — a row scrolled out of the list may lose its handler).
    function beginDrag(path, isDir, title) {
        revealedPath = "" // (the list scrolls under the pointer)
        dragPath = path
        dragIsDir = isDir
        dragTitle = title
        dropFolder = ""
        dropAllowed = false
        dropImpact = null
        dropOnRoot = false
    }
    function cancelDrag() {
        if (dragging)
            Workspace.cancelMoveImpact() // (not another sidebar's: the drawer's and the window's)
        dragPath = ""
        dropAllowed = false
        dropImpact = null
        dropOnRoot = false
        expandTimer.stop()
        impactTimer.stop()
        scrollTimer.direction = 0
    }
    /// Can the dragged item go into `folder`? Not into itself, its own subtree or where it is.
    function canDropInto(folder) {
        if (!dragging)
            return false
        if (dragIsDir && (folder === dragPath || folder.startsWith(dragPath + "/")))
            return false
        return folder !== Workspace.folderOf(dragPath)
    }
    function dragTo(scenePos) {
        if (!dragging)
            return
        dragScenePos = scenePos
        dragPoint = sidebar.mapFromItem(null, scenePos.x, scenePos.y)
        const inList = list.mapFromItem(null, scenePos.x, scenePos.y)
        // Near the list's edges: scroll (scrollTimer finds the target again on every step).
        scrollTimer.direction = inList.y < 36 && inList.y >= 0 ? -1 : inList.y > list.height - 36 && inList.y <= list.height ? 1 : 0
        // Only what shows in the list is a target: the header scrolled above its top edge is
        // clipped away, though still `visible`.
        const inside = inList.x >= 0 && inList.x <= list.width && inList.y >= 0 && inList.y <= list.height
        const header = sidebar.pagesHeader
        let folder = null
        let hoveredFolder = ""
        let collapsed = false
        if (inside && header && header.visible) {
            const h = header.mapFromItem(null, scenePos.x, scenePos.y)
            if (h.x >= 0 && h.y >= 0 && h.x <= header.width && h.y <= header.height)
                folder = ""
        }
        if (folder === null && inside) {
            const index = list.indexAt(inList.x + list.contentX, inList.y + list.contentY)
            const entry = index >= 0 ? tree.get(index) : null
            if (entry && entry.path !== undefined) {
                folder = entry.isDir ? entry.path : Workspace.folderOf(entry.path)
                if (entry.isDir && !entry.expanded) {
                    hoveredFolder = entry.path
                    collapsed = true
                }
            }
        }
        dropOnRoot = folder === ""
        const allowed = folder !== null && canDropInto(folder)
        const target = folder !== null ? folder : ""
        // What the move would rewrite: asked once the target stays a moment.
        if (target !== dropFolder || allowed !== dropAllowed) {
            dropImpact = null
            Workspace.cancelMoveImpact()
            if (allowed)
                impactTimer.restart()
            else
                impactTimer.stop()
        }
        dropAllowed = allowed
        dropFolder = target
        // A closed folder under the pointer opens after a moment.
        if (collapsed && expandTimer.folder !== hoveredFolder) {
            expandTimer.folder = hoveredFolder
            expandTimer.restart()
        } else if (!collapsed) {
            expandTimer.folder = ""
            expandTimer.stop()
        }
    }
    function drop() {
        const path = dragPath
        const folder = dropFolder
        const valid = dropValid
        cancelDrag()
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
                Qt.callLater(sidebar.revealRoute)
            else
                sidebar.revealedPath = ""
        }
    }
    Connections {
        target: Workspace
        function onRecentPagesChanged() { Qt.callLater(sidebar.updateRecentOthers) }
    }
    Component.onCompleted: {
        updateRecentOthers()
        if (revealCurrent)
            Qt.callLater(revealRoute)
    }
    // Shown (the drawer opens, the window's sidebar comes back): its list is laid out again —
    // placed again (the header may have grown meanwhile: the navigation stays in sight).
    onVisibleChanged: if (visible) Qt.callLater(placeList)

    Timer {
        id: expandTimer
        property string folder
        interval: 600
        onTriggered: {
            if (!sidebar.dragging || folder === "")
                return
            tree.setExpanded(folder, true)
            Qt.callLater(() => sidebar.dragTo(sidebar.dragScenePos))
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
            list.contentY = Math.max(list.originY, Math.min(list.originY + max, list.contentY + direction * 12))
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
        function onMoveImpactReady(path, folder, impact) {
            if (sidebar.dragging && sidebar.dropAllowed && path === sidebar.dragPath && folder === sidebar.dropFolder)
                sidebar.dropImpact = impact
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

        // ---- Vault header ----------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space1
            Layout.topMargin: Theme.space2
            Layout.bottomMargin: Theme.space1
            spacing: Theme.space2

            Image {
                source: "qrc:/images/logo.svg"
                sourceSize: Qt.size(24, 24)
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                Accessible.ignored: true
            }
            BText {
                Layout.fillWidth: true
                text: Workspace.name
                variant: "label"
                elide: Text.ElideRight
            }
            BIconButton {
                id: switchButton
                objectName: "notebookSwitch"
                iconName: "repeat"
                label: qsTr("Switch notebook")
                size: "small"
                onClicked: notebookMenu.popup(switchButton, 0, switchButton.height)
            }
            BIconButton {
                iconName: "square-pen"
                label: qsTr("New page")
                shortcut: "Ctrl+N"
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
            // Off while "Strony" is collapsed, which destroys the pooled rows: rows pooled before
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
            function keepPlace() {
                scrolledAway = !atYBeginning
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
                    Qt.callLater(list.keepPlace)
                }
                event.accepted = false
            }
            // Laid out again (the drawer opening, a keyboard, the window): placed again.
            onHeightChanged: Qt.callLater(sidebar.placeList)

            header: Column {
                width: list.width - list.leftMargin - list.rightMargin
                spacing: 0
                // A ListView keeps its rows in place when the header grows (a page added to
                // Recent), which scrolled the navigation up out of sight: the drawer then opened
                // on "Tasks" with "Journals" and "Calendar" hidden. Stay at the top instead (the
                // revealed row still in view: it moved down with the rows) — unless the list is
                // kept scrolled away: the rows stay where they are.
                onHeightChanged: Qt.callLater(sidebar.placeList)

                // ---- Navigation --------------------------------------------------------
                Column {
                    width: parent.width
                    topPadding: Theme.space1
                    bottomPadding: Theme.space2

                    Repeater {
                        // Without the entries hidden in Ustawienia → Wygląd → Układ, nor (the
                        // phone drawer) those the bottom navigation has.
                        model: [
                            { view: "journals", also: "journal", title: qsTr("Journals"), icon: "book-open" },
                            { view: "calendar", title: qsTr("Calendar"), icon: "calendar" },
                            { view: "tasks", title: qsTr("Tasks"), icon: "list-todo" },
                            { view: "allPages", title: qsTr("All pages"), icon: "files" },
                            { view: "tags", also: "tag", title: qsTr("Tags"), icon: "tags" },
                            { view: "graph", title: qsTr("Graph"), icon: "waypoints" },
                            { view: "search", title: qsTr("Search"), icon: "search" }
                        ].filter(e => AppSettings.sidebarHidden.indexOf(e.view) < 0
                                      && sidebar.omitViews.indexOf(e.view) < 0)
                        delegate: BListItem {
                            required property var modelData
                            objectName: "sidebarNav_" + modelData.view
                            width: parent.width
                            dense: true
                            text: modelData.title
                            iconName: modelData.icon
                            selected: sidebar.route.view === modelData.view
                                      || (modelData.also !== undefined && sidebar.route.view === modelData.also)
                            selectionBar: true
                            onClicked: sidebar.go({ view: modelData.view })
                        }
                    }
                }

                // ---- Import (the drawer on phones: notes from other apps, docs/UI.md) --
                BListItem {
                    objectName: "drawerImportNotes"
                    visible: Theme.narrow
                    width: parent.width
                    height: visible ? implicitHeight : 0
                    dense: true
                    text: qsTr("Import notes")
                    iconName: "download"
                    selected: sidebar.route.view === "importExport"
                    selectionBar: true
                    onClicked: sidebar.go({ view: "importExport", tab: "import" })
                }

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
                            required property var modelData
                            width: parent.width
                            dense: true
                            text: modelData.title
                            iconName: "star"
                            selected: sidebar.route.view === "page" && sidebar.route.path === modelData.path
                            onClicked: sidebar.openPage(modelData.path)
                            onPressAndHold: sidebar.showContextMenu(favoriteItem, modelData.path, false)
                            TapHandler {
                                // Right-click only (see the tree rows below): never a finger.
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                                acceptedButtons: Qt.RightButton
                                onTapped: sidebar.showContextMenu(favoriteItem, favoriteItem.modelData.path, false)
                            }
                        }
                    }
                }

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
                            required property var modelData
                            width: parent.width
                            dense: true
                            text: modelData.title
                            iconName: "clock"
                            onClicked: sidebar.openPage(modelData.path)
                        }
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
                            Qt.callLater(sidebar.revealRoute)
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
                // Daily notes by their date ("śr, 14 paź"), not by their file name.
                text: journalDate !== "" ? DatesHelper.formatShort(journalDate) : title
                // An empty folder opens too (folder-open) and says why nothing appears under it.
                subtitle: isDir && expanded && !hasChildren ? qsTr("Empty folder") : ""
                iconName: special === "journals" ? fileIcon
                          : isDir && hasChildren ? (expanded ? "chevron-down" : "chevron-right") : fileIcon
                iconColor: Theme.fgFaint
                selected: !isDir && sidebar.currentPath === path
                opacity: sidebar.dragging && sidebar.dragPath === path ? 0.5 : 1
                Accessible.description: isDir ? (expanded ? qsTr("expanded folder") : qsTr("collapsed folder")) : ""
                // Touch screens: the row's actions behind a visible button too (press-and-hold
                // stays). Created only there.
                trailing: Theme.touchUi ? moreLoader : null

                onClicked: {
                    if (isDir)
                        tree.toggle(index)
                    else
                        sidebar.openPage(path)
                }
                onPressAndHold: sidebar.showContextMenu(row, path, isDir)
                Keys.onRightPressed: if (isDir && !expanded) tree.toggle(index)
                Keys.onLeftPressed: if (isDir && expanded) tree.toggle(index)
                Keys.onMenuPressed: sidebar.showContextMenu(row, path, isDir)
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
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedButtons: Qt.LeftButton
                    target: null
                    dragThreshold: 6
                    grabPermissions: PointerHandler.CanTakeOverFromAnything
                    onActiveChanged: if (active) sidebar.beginDrag(row.path, row.isDir, row.text)
                }

                Loader {
                    id: moreLoader
                    active: Theme.touchUi
                    visible: active
                    sourceComponent: BIconButton {
                        objectName: "treeRowMore"
                        iconName: "ellipsis"
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

            footer: Item {
                width: list.width - list.leftMargin - list.rightMargin
                height: Theme.space4
            }
        }

        ShellSyncFooter {
            Layout.fillWidth: true
            safeBottom: sidebar.bottomInset
            onClicked: sidebar.go({ view: "sync" })
        }
    }

    // Sees the pointer of a drag wherever it goes (a passive observer above the list: it takes
    // nothing from the rows), and its release: the drop.
    Item {
        id: pointerWatch
        anchors.fill: parent
        z: 10
        PointHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onPointChanged: if (sidebar.dragging) sidebar.dragTo(point.scenePosition)
            onActiveChanged: if (!active && sidebar.dragging) sidebar.drop()
        }
    }
    // The dragged row under the pointer, with where it would go and what that rewrites (or why
    // it cannot go there).
    Rectangle {
        id: ghost
        objectName: "treeDragGhost"
        /// The second line: Workspace.moveImpact() of the target.
        readonly property string impactText: !sidebar.dragging || !sidebar.dropAllowed || !sidebar.dropImpact ? ""
                                             : sidebar.dropImpact.ok ? words.impactText(sidebar.dropImpact)
                                             : (sidebar.dropImpact.error || "")
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
                                                            .arg(sidebar.dropFolder !== "" ? sidebar.dropFolder.split("/").pop() : qsTr("Notebook root"))
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

    // Right edge line on wide windows (the drawer has its own edge).
    Rectangle {
        visible: !Theme.narrow
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }

    // Quick switch between notebooks: the recent ones, the documentation, any other folder.
    BMenu {
        id: notebookMenu
        objectName: "notebookMenu"

        readonly property string current: AppSettings.currentVault
        readonly property string docsPath: AppController.docsVaultPath

        Repeater {
            model: AppSettings.recentVaults.filter(v => v.path !== notebookMenu.docsPath)
            delegate: BMenuItem {
                required property var modelData
                objectName: "notebookMenuRecent"
                // Not `checked`: a checkable item has already toggled it when onTriggered runs,
                // so the first click on another notebook would do nothing.
                readonly property bool isCurrent: modelData.path === notebookMenu.current
                text: modelData.name
                iconName: "folder"
                checkable: true
                checked: isCurrent
                onTriggered: {
                    checked = Qt.binding(() => isCurrent)
                    if (!isCurrent)
                        AppController.openRecentVault(modelData.path)
                    sidebar.navigated()
                }
            }
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "notebookMenuDocs"
            visible: notebookMenu.docsPath !== ""
            height: visible ? implicitHeight : 0
            text: qsTr("Brulion documentation")
            iconName: "book-open"
            readonly property bool isCurrent: notebookMenu.docsPath === notebookMenu.current
            checkable: true
            checked: isCurrent
            onTriggered: {
                checked = Qt.binding(() => isCurrent)
                if (!isCurrent)
                    AppController.openDocsVault()
                sidebar.navigated()
            }
        }
        BMenuItem {
            text: qsTr("Open another notebook…")
            iconName: "folder-open"
            onTriggered: {
                sidebar.navigated()
                AppController.closeVault()
            }
        }
    }

    BMenu {
        id: contextMenu
        objectName: "sidebarContextMenu"

        property string path
        property bool isDir: false
        readonly property bool favorite: !isDir && Workspace.favorites.some(f => f.path === contextMenu.path)
        readonly property bool page: !isDir && contextMenu.path.toLowerCase().endsWith(".md")

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
            visible: !contextMenu.isDir && !Theme.narrow
            height: visible ? implicitHeight : 0
            text: qsTr("Open in side panel")
            iconName: "panel-right"
            onTriggered: AppController.openPage(contextMenu.path, -1, true)
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

    // The "Strony" header's "+": new page or folder at the root, and how the tree shows.
    BMenu {
        id: pagesMenu
        objectName: "sidebarPagesMenu"

        BMenuItem {
            objectName: "pagesMenuNewPage"
            text: qsTr("New page")
            iconName: "square-pen"
            shortcut: "Ctrl+N"
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
            checkable: true
            checked: tree.showAttachments
            onTriggered: {
                const show = !tree.showAttachments
                tree.showAttachments = show
                AppSettings.setValue("ui/sidebar/attachments", show)
                checked = Qt.binding(() => tree.showAttachments)
            }
        }
        BMenuItem {
            objectName: "pagesMenuReveal"
            text: qsTr("Show the open page")
            iconName: "folder-tree"
            checkable: true
            checked: sidebar.revealCurrent
            onTriggered: {
                sidebar.revealCurrent = !sidebar.revealCurrent
                AppSettings.setValue("ui/sidebar/revealCurrent", sidebar.revealCurrent)
                checked = Qt.binding(() => sidebar.revealCurrent)
                if (sidebar.revealCurrent)
                    sidebar.revealRoute()
            }
        }
    }
}
