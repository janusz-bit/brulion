pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellSidebar — navigation (Dziennik, Kalendarz, Zadania, Wszystkie strony, Tagi, Graf, Szukaj),
// favourites, recent pages, the vault's folder tree (lazy, collapsible, context menu: new page,
// new folder, rename, history, delete) and the sync status footer. The same component is the
// left pane on wide windows and the drawer's content on narrow ones (`navigated()` closes it).
Rectangle {
    id: sidebar

    property real topInset: 0
    property real bottomInset: 0

    signal navigated()
    signal newPageRequested(string folder)
    signal newFolderRequested(string folder)
    signal renameRequested(string path, bool isDir)
    signal deleteRequested(string path)

    readonly property var route: AppController.currentRoute
    property bool favoritesOpen: flag(AppSettings.value("ui/sidebar/favorites", true))
    property bool recentOpen: flag(AppSettings.value("ui/sidebar/recent", true))
    property bool pagesOpen: flag(AppSettings.value("ui/sidebar/pages", true))

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

    color: Theme.bgSunken
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Sidebar")

    FolderTreeModel {
        id: tree
        workspace: Workspace
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
            reuseItems: true
            T.ScrollBar.vertical: BScrollBar {}
            /// The user scrolled down into the list (a flick or drag that did not end at the top).
            property bool userScrolled: false
            onMovementEnded: userScrolled = !atYBeginning

            header: Column {
                width: list.width - list.leftMargin - list.rightMargin
                spacing: 0
                // A ListView keeps its rows in place when the header grows (a page added to
                // Recent), which scrolled the navigation up out of sight: the drawer then opened
                // on "Tasks" with "Journals" and "Calendar" hidden. Stay at the top instead.
                onHeightChanged: if (!list.userScrolled) Qt.callLater(() => { if (list) list.positionViewAtBeginning() })

                // ---- Navigation --------------------------------------------------------
                Column {
                    width: parent.width
                    topPadding: Theme.space1
                    bottomPadding: Theme.space2

                    Repeater {
                        model: [
                            { view: "journals", also: "journal", title: qsTr("Journals"), icon: "book-open" },
                            { view: "calendar", title: qsTr("Calendar"), icon: "calendar" },
                            { view: "tasks", title: qsTr("Tasks"), icon: "list-todo" },
                            { view: "allPages", title: qsTr("All pages"), icon: "files" },
                            { view: "tags", also: "tag", title: qsTr("Tags"), icon: "tags" },
                            { view: "graph", title: qsTr("Graph"), icon: "waypoints" },
                            { view: "search", title: qsTr("Search"), icon: "search" }
                        ]
                        delegate: BListItem {
                            required property var modelData
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

                // ---- Favourites --------------------------------------------------------
                BSectionHeader {
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
                    visible: sidebar.favoritesOpen
                    width: parent.width
                    bottomPadding: Theme.space2

                    BText {
                        visible: Workspace.favorites.length === 0
                        width: parent.width - 2 * Theme.space3
                        x: Theme.space3
                        text: qsTr("Pages you star appear here.")
                        variant: "faint"
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.Wrap
                    }
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

                // ---- Recent ------------------------------------------------------------
                BSectionHeader {
                    visible: Workspace.recentPages.length > 0
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
                    visible: sidebar.recentOpen && Workspace.recentPages.length > 0
                    width: parent.width
                    bottomPadding: Theme.space2

                    Repeater {
                        model: Workspace.recentPages.slice(0, 5)
                        delegate: BListItem {
                            required property var modelData
                            width: parent.width
                            dense: true
                            text: modelData.title
                            iconName: "clock"
                            selected: sidebar.route.view === "page" && sidebar.route.path === modelData.path
                            onClicked: sidebar.openPage(modelData.path)
                        }
                    }
                }

                // ---- Pages (folder tree) ----------------------------------------------
                BSectionHeader {
                    width: parent.width
                    text: qsTr("Pages")
                    count: Workspace.pageCount
                    collapsible: true
                    expanded: sidebar.pagesOpen
                    actionIcon: "plus"
                    actionLabel: qsTr("New page")
                    onActionClicked: sidebar.newPageRequested("")
                    onExpandedToggled: (open) => {
                        sidebar.pagesOpen = open
                        AppSettings.setValue("ui/sidebar/pages", open)
                    }
                }
            }

            delegate: BListItem {
                id: row
                required property int index
                required property string title
                required property string path
                required property bool isDir
                required property int depth
                required property bool expanded
                required property bool hasChildren
                required property string fileIcon

                width: list.width - list.leftMargin - list.rightMargin
                dense: true
                leftPadding: (Theme.touchUi ? 14 : 10) + depth * (Theme.touchUi ? 18 : 14)
                text: title
                // An empty folder opens too (folder-open) and says why nothing appears under it.
                subtitle: isDir && expanded && !hasChildren ? qsTr("Empty folder") : ""
                iconName: isDir && hasChildren ? (expanded ? "chevron-down" : "chevron-right") : fileIcon
                iconColor: Theme.fgFaint
                selected: !isDir && sidebar.route.view === "page" && sidebar.route.path === path
                Accessible.description: isDir ? (expanded ? qsTr("expanded folder") : qsTr("collapsed folder")) : ""

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

    // Right edge line on wide windows (the drawer has its own edge).
    Rectangle {
        visible: !Theme.narrow
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }

    BMenu {
        id: contextMenu
        objectName: "sidebarContextMenu"

        property string path
        property bool isDir: false
        readonly property bool favorite: !isDir && Workspace.favorites.some(f => f.path === contextMenu.path)

        BMenuItem {
            visible: contextMenu.isDir
            height: visible ? implicitHeight : 0
            text: qsTr("New page here")
            iconName: "square-pen"
            onTriggered: sidebar.newPageRequested(contextMenu.path)
        }
        BMenuItem {
            visible: contextMenu.isDir
            height: visible ? implicitHeight : 0
            text: qsTr("New folder…")
            iconName: "folder"
            onTriggered: sidebar.newFolderRequested(contextMenu.path)
        }
        BMenuItem {
            visible: !contextMenu.isDir && !Theme.narrow
            height: visible ? implicitHeight : 0
            text: qsTr("Open in side panel")
            iconName: "panel-right"
            onTriggered: AppController.openPage(contextMenu.path, -1, true)
        }
        BMenuItem {
            visible: !contextMenu.isDir
            height: visible ? implicitHeight : 0
            text: contextMenu.favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
            iconName: "star"
            onTriggered: Workspace.toggleFavorite(contextMenu.path)
        }
        BMenuItem {
            text: qsTr("Rename…")
            iconName: "pencil"
            onTriggered: sidebar.renameRequested(contextMenu.path, contextMenu.isDir)
        }
        BMenuItem {
            visible: !contextMenu.isDir
            height: visible ? implicitHeight : 0
            text: qsTr("Page history")
            iconName: "history"
            onTriggered: sidebar.go({ view: "history", path: contextMenu.path })
        }
        BMenuSeparator {}
        BMenuItem {
            text: qsTr("Move to trash")
            iconName: "trash-2"
            danger: true
            onTriggered: sidebar.deleteRequested(contextMenu.path)
        }
    }
}
