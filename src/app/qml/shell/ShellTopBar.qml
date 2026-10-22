pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellTopBar — the bar above the current view: sidebar/drawer button, back/forward, the title
// (a breadcrumb for pages in folders), search, command palette, quick capture, side panel toggle
// and the "more" menu with page actions. On narrow windows it keeps only what fits a phone.
// On a picture attachment (`picture`) the menu's "Markdown source" is the picture bar's "Show as
// text" / "Show picture" (Ctrl+E there too), and the note mode is not offered (no note to switch).
// `topInset` extends the bar under the Android status bar (edge-to-edge).
Rectangle {
    id: bar

    property string title
    /// Folder parts above the title (pages in folders): [{ title, path }].
    property var crumbs: []
    property real topInset: 0
    property bool sidebarVisible: true
    /// The view shows a picture attachment, as text (`pictureAsText`) or as the picture.
    property bool picture: false
    property bool pictureAsText: false

    signal menuClicked()
    signal sidebarToggled()
    signal paletteRequested()
    signal captureRequested()
    signal newPageRequested()
    signal renameRequested(string path)
    signal deleteRequested(string path)
    signal aboutRequested()

    readonly property var route: AppController.currentRoute
    /// A daily note opened as a page ("journal" route) whose file exists: it gets the page
    /// actions too (favorite, history, side panel, trash), but not Rename (its name is its date).
    readonly property string journalPagePath: {
        void Workspace.revision
        if (route.view !== "journal" || !route.date)
            return ""
        const path = route.path || Workspace.journalPath(route.date)
        return Workspace.exists(path) ? path : ""
    }
    readonly property bool onPage: route.view === "page" || journalPagePath !== ""
    /// The view shows a page in the editor (a page or a daily note).
    readonly property bool inEditor: onPage || route.view === "journal" || route.view === "journals"
    readonly property string pagePath: route.view === "page" ? route.path : journalPagePath
    readonly property bool favorite: onPage && Workspace.favorites.some(f => f.path === bar.pagePath)

    implicitHeight: topInset + (Theme.touchUi ? 56 : 46)
    color: Theme.bg

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.borderSubtle
    }

    RowLayout {
        anchors.fill: parent
        anchors.topMargin: bar.topInset
        anchors.leftMargin: Theme.narrow ? 4 : Theme.space2
        anchors.rightMargin: Theme.narrow ? 4 : Theme.space2
        spacing: Theme.narrow ? 0 : 2

        BIconButton {
            objectName: "topBarMenu"
            visible: Theme.narrow
            iconName: "menu"
            label: qsTr("Menu")
            onClicked: bar.menuClicked()
        }
        BIconButton {
            visible: !Theme.narrow
            iconName: "panel-left"
            label: bar.sidebarVisible ? qsTr("Hide sidebar") : qsTr("Show sidebar")
            checkable: true
            checked: bar.sidebarVisible
            onClicked: bar.sidebarToggled()
        }
        BIconButton {
            visible: !Theme.narrow || AppController.canGoBack
            enabled: AppController.canGoBack
            iconName: "arrow-left"
            label: qsTr("Back")
            shortcut: "Alt+Left"
            onClicked: AppController.back()
        }
        BIconButton {
            visible: !Theme.narrow
            enabled: AppController.canGoForward
            iconName: "arrow-right"
            label: qsTr("Forward")
            shortcut: "Alt+Right"
            onClicked: AppController.forward()
        }

        // Title or breadcrumb.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.narrow ? 4 : Theme.space2

            BText {
                visible: bar.crumbs.length === 0
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                text: bar.title
                variant: "title"
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
            }
            BBreadcrumb {
                visible: bar.crumbs.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                model: bar.crumbs.map(c => ({ title: c.title })).concat([{ title: bar.title }])
                onActivated: (index) => {
                    const crumb = bar.crumbs[index]
                    if (crumb)
                        AppController.navigate({ view: "allPages", path: crumb.path })
                }
            }
        }

        BIconButton {
            iconName: "search"
            label: qsTr("Search")
            shortcut: "Ctrl+Shift+F"
            onClicked: AppController.search("")
        }
        BIconButton {
            visible: !Theme.narrow
            iconName: "command"
            label: qsTr("Command palette")
            shortcut: "Ctrl+K"
            onClicked: bar.paletteRequested()
        }
        BIconButton {
            visible: !Theme.narrow
            iconName: "plus"
            label: qsTr("Quick capture")
            shortcut: "Ctrl+Shift+N"
            onClicked: bar.captureRequested()
        }
        BIconButton {
            visible: !Theme.narrow
            iconName: "panel-right"
            label: AppController.sidePanelVisible ? qsTr("Hide side panel") : qsTr("Show side panel")
            shortcut: "Ctrl+\\"
            checkable: true
            checked: AppController.sidePanelVisible
            onClicked: AppController.sidePanelVisible = !AppController.sidePanelVisible
        }
        BIconButton {
            id: moreButton
            iconName: "ellipsis-vertical"
            label: qsTr("More")
            onClicked: moreMenu.popup(moreButton, moreButton.width - moreMenu.width, moreButton.height)
        }
    }

    BMenu {
        id: moreMenu

        BMenuItem {
            visible: bar.onPage
            height: visible ? implicitHeight : 0
            text: bar.favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
            iconName: "star"
            onTriggered: Workspace.toggleFavorite(bar.pagePath)
        }
        BMenuItem {
            visible: bar.onPage && bar.route.view === "page"
            height: visible ? implicitHeight : 0
            text: qsTr("Rename…")
            iconName: "pencil"
            shortcut: "F2"
            onTriggered: bar.renameRequested(bar.pagePath)
        }
        BMenuItem {
            visible: bar.onPage
            height: visible ? implicitHeight : 0
            text: qsTr("Page history")
            iconName: "history"
            onTriggered: AppController.navigate({ view: "history", path: bar.pagePath })
        }
        BMenuItem {
            visible: bar.inEditor
            height: visible ? implicitHeight : 0
            // A picture: its two views (PageView switches them for this action too).
            text: !bar.picture ? qsTr("Markdown source")
                : bar.pictureAsText ? qsTr("Show picture") : qsTr("Show as text")
            iconName: bar.picture && bar.pictureAsText ? "image" : "code"
            shortcut: "Ctrl+E"
            onTriggered: AppController.requestUiAction("toggleSourceMode")
        }
        BMenuItem {
            // Document (like Obsidian) / outline (like Logseq): the page shown, in the journal
            // stream the day being written in (docs/UI.md "Note modes"). A picture is no note.
            visible: bar.inEditor && !bar.picture
            height: visible ? implicitHeight : 0
            text: qsTr("Toggle note mode")
            iconName: "file-text"
            onTriggered: AppController.requestUiAction("toggleNoteMode")
        }
        BMenuItem {
            visible: bar.onPage && !Theme.narrow
            height: visible ? implicitHeight : 0
            text: qsTr("Open in side panel")
            iconName: "panel-right"
            onTriggered: AppController.openPage(bar.pagePath, -1, true)
        }
        BMenuItem {
            visible: bar.onPage
            height: visible ? implicitHeight : 0
            text: qsTr("Move to trash")
            iconName: "trash-2"
            danger: true
            onTriggered: bar.deleteRequested(bar.pagePath)
        }
        BMenuSeparator {
            visible: bar.inEditor
            height: visible ? implicitHeight : 0
        }
        BMenuItem {
            text: qsTr("New page")
            iconName: "square-pen"
            shortcut: "Ctrl+N"
            onTriggered: bar.newPageRequested()
        }
        BMenuItem {
            text: qsTr("Command palette")
            iconName: "command"
            shortcut: "Ctrl+K"
            onTriggered: bar.paletteRequested()
        }
        BMenuSeparator {}
        // Places without a button of their own: on phones this menu is the only way there
        // besides the command palette.
        BMenuItem {
            text: qsTr("Sync center")
            iconName: "folder-sync"
            onTriggered: AppController.navigate({ view: "sync" })
        }
        BMenuItem {
            // Straight to the import tab: notes from Obsidian, Logseq, Notion… (docs/UI.md).
            objectName: "menuImportNotes"
            text: qsTr("Import notes")
            iconName: "download"
            onTriggered: AppController.navigate({ view: "importExport", tab: "import" })
        }
        BMenuItem {
            text: qsTr("Import and export")
            iconName: "import"
            onTriggered: AppController.navigate({ view: "importExport" })
        }
        BMenuItem {
            text: Workspace.trashCount > 0 ? qsTr("Trash (%1)").arg(Workspace.trashCount) : qsTr("Trash")
            iconName: "trash-2"
            onTriggered: AppController.navigate({ view: "trash" })
        }
        BMenuItem {
            text: qsTr("Settings")
            iconName: "settings"
            shortcut: "Ctrl+,"
            onTriggered: AppController.navigate({ view: "settings" })
        }
        BMenuItem {
            text: qsTr("About Brulion")
            iconName: "info"
            onTriggered: bar.aboutRequested()
        }
    }
}
