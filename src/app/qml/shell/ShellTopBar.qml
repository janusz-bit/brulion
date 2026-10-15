pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellTopBar — the bar above the current view: sidebar/drawer button, back/forward, the title
// (a breadcrumb for pages in folders), search, command palette, quick capture, side panel toggle
// and the "more" menu with page actions. On narrow windows it keeps only what fits a phone.
// `topInset` extends the bar under the Android status bar (edge-to-edge).
Rectangle {
    id: bar

    property string title
    /// Folder parts above the title (pages in folders): [{ title, path }].
    property var crumbs: []
    property real topInset: 0
    property bool sidebarVisible: true

    signal menuClicked()
    signal sidebarToggled()
    signal paletteRequested()
    signal captureRequested()
    signal newPageRequested()
    signal renameRequested(string path)
    signal deleteRequested(string path)
    signal aboutRequested()

    readonly property var route: AppController.currentRoute
    readonly property bool onPage: route.view === "page"
    /// The view shows a page in the editor (a page or a daily note).
    readonly property bool inEditor: onPage || route.view === "journal" || route.view === "journals"
    readonly property string pagePath: onPage ? route.path : ""
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
            visible: bar.onPage
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
            text: qsTr("Markdown source")
            iconName: "code"
            shortcut: "Ctrl+E"
            onTriggered: AppController.requestUiAction("toggleSourceMode")
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
