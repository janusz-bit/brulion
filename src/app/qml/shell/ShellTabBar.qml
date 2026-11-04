pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellTabBar — the main window's tabs (wide windows; docs/UI.md "Tabs, split view and windows"),
// between the top bar and the view, shown while there are two tabs or more (or the view is split).
// Every tab has the same width (at most 220 px, at least 120: past that the bar scrolls with the
// wheel); the tab of the focused pane is drawn like the view under it with an accent line, the one
// shown in the other pane of a split view like the view without the line. A click shows a tab, a
// middle click or its × closes it, a drag along the bar moves it (AppController.moveTab), a
// right-click (or a long press) opens its menu: Zamknij, Zamknij pozostałe, Pokaż w podziale /
// Zamknij podział, Przenieś do nowego okna (desktop). The + at the end opens a new tab (Ctrl+T).
Rectangle {
    id: bar

    /// The title of a route (AppShell.routeTitle).
    property var titleOf: (route) => ""

    readonly property var tabs: AppController.tabs
    readonly property real tabWidth: Math.max(minTabWidth, Math.min(maxTabWidth, (list.width) / Math.max(1, tabs.length)))
    readonly property real minTabWidth: 120
    readonly property real maxTabWidth: 220

    implicitHeight: Theme.touchUi ? 44 : 34
    color: Theme.bgSunken
    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Tabs")

    /// The icon of a route's view.
    function iconOf(route) {
        switch (route.view) {
        case "journals": return "book-open"
        case "journal": return "notebook-pen"
        case "calendar": return "calendar"
        case "tasks": return "list-todo"
        case "habits": return "flame"
        case "graph": return "waypoints"
        case "search": return "search"
        case "allPages": return "files"
        case "tags":
        case "tag": return "tags"
        case "settings": return "settings"
        case "drawing": return "pencil"
        default: return "file-text"
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.borderSubtle
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space1
        anchors.rightMargin: Theme.space1
        spacing: 2

        ListView {
            id: list
            objectName: "shellTabList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: ListView.Horizontal
            clip: true
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            model: bar.tabs
            // (A drag in the bar moves a tab; the wheel scrolls a bar of many tabs.)
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => {
                    const delta = event.angleDelta.y !== 0 ? -event.angleDelta.y : -event.angleDelta.x
                    list.contentX = Math.max(0, Math.min(list.contentWidth - list.width, list.contentX + delta / 2))
                }
            }

            delegate: Item {
                id: tab
                objectName: "shellTab"
                required property var modelData
                required property int index
                readonly property int tabId: modelData.id
                readonly property bool focusedTab: tabId === AppController.focusedTab
                /// Shown in a pane (the main one or the split one).
                readonly property bool shown: tabId === AppController.mainTab || tabId === AppController.splitTab
                readonly property string title: bar.titleOf(modelData.route)
                /// While dragged: where it would go.
                property int dropIndex: index

                width: bar.tabWidth
                height: list.height
                z: drag.active ? 2 : 0
                Accessible.role: Accessible.PageTab
                Accessible.name: title
                Accessible.selected: focusedTab
                Accessible.onPressAction: AppController.selectTab(tab.tabId)

                transform: Translate { x: drag.active ? drag.activeTranslation.x : 0 }

                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: 3
                    anchors.bottomMargin: tab.shown ? 0 : 3
                    radius: Theme.radiusSm
                    color: tab.shown ? Theme.bg : hover.hovered ? Theme.bgHover : "transparent"
                    // The bottom corners of a shown tab run into the view.
                    Rectangle {
                        visible: tab.shown
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: Theme.radiusSm
                        color: Theme.bg
                    }
                    Rectangle {
                        visible: tab.focusedTab && (AppController.tabCount > 1 || AppController.splitTab !== 0)
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Theme.radiusSm
                        anchors.rightMargin: Theme.radiusSm
                        height: 2
                        color: Theme.accent
                    }
                }
                // Separators between tabs not shown.
                Rectangle {
                    visible: !tab.shown && tab.index > 0
                    x: 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: 1
                    height: parent.height / 2
                    color: Theme.borderSubtle
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space2
                    anchors.rightMargin: 2
                    spacing: Theme.space1

                    BIcon {
                        name: bar.iconOf(tab.modelData.route)
                        size: Theme.iconSizeSm
                        color: tab.focusedTab ? Theme.fg : Theme.fgMuted
                    }
                    BText {
                        Layout.fillWidth: true
                        text: tab.title
                        variant: "small"
                        color: tab.focusedTab ? Theme.fg : Theme.fgMuted
                        font.weight: tab.focusedTab ? Theme.weightSemiBold : Theme.weightNormal
                        elide: Text.ElideRight
                    }
                    BIconButton {
                        objectName: "shellTabClose"
                        iconName: "x"
                        label: qsTr("Close tab")
                        shortcut: tab.focusedTab ? "Ctrl+W" : ""
                        size: "small"
                        showToolTip: false
                        opacity: hover.hovered || tab.focusedTab || Theme.touchUi ? 1 : 0
                        onClicked: AppController.closeTab(tab.tabId)
                    }
                }

                HoverHandler { id: hover }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: AppController.selectTab(tab.tabId)
                    onLongPressed: tabMenu.openFor(tab)
                }
                TapHandler {
                    // A middle click closes it, as in browsers.
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedButtons: Qt.MiddleButton
                    onTapped: AppController.closeTab(tab.tabId)
                }
                TapHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                    acceptedButtons: Qt.RightButton
                    onTapped: tabMenu.openFor(tab)
                }
                DragHandler {
                    id: drag
                    target: null
                    yAxis.enabled: false
                    onActiveTranslationChanged: {
                        const centre = tab.x + tab.width / 2 + activeTranslation.x
                        tab.dropIndex = Math.max(0, Math.min(bar.tabs.length - 1, Math.floor(centre / bar.tabWidth)))
                    }
                    onActiveChanged: {
                        // (Nothing changes the tabs while dragging: the delegates would be made again.)
                        if (active) {
                            tab.dropIndex = tab.index
                        } else if (tab.dropIndex !== tab.index) {
                            AppController.moveTab(tab.index, tab.dropIndex)
                        }
                    }
                }
            }
        }
        BIconButton {
            objectName: "shellNewTab"
            iconName: "plus"
            label: qsTr("New tab")
            shortcut: "Ctrl+T"
            size: "small"
            onClicked: AppController.newTab()
        }
    }

    BMenu {
        id: tabMenu
        property int tabId: 0
        readonly property bool inSplit: tabId !== 0 && tabId === AppController.splitTab
        function openFor(item) {
            tabId = item.tabId
            popup(item, 0, item.height)
        }
        BMenuItem {
            text: qsTr("Close tab")
            iconName: "x"
            onTriggered: AppController.closeTab(tabMenu.tabId)
        }
        BMenuItem {
            text: qsTr("Close other tabs")
            iconName: "x"
            enabled: AppController.tabCount > 1
            onTriggered: AppController.closeOtherTabs(tabMenu.tabId)
        }
        BMenuSeparator {}
        BMenuItem {
            visible: !tabMenu.inSplit && tabMenu.tabId !== AppController.mainTab
            height: visible ? implicitHeight : 0
            text: qsTr("Show in split view")
            iconName: "columns-2"
            onTriggered: AppController.splitView(tabMenu.tabId)
        }
        BMenuItem {
            visible: AppController.splitTab !== 0
            height: visible ? implicitHeight : 0
            text: qsTr("Close split view")
            iconName: "square"
            onTriggered: AppController.closeSplit()
        }
        BMenuItem {
            visible: AppController.windowsAllowed
            height: visible ? implicitHeight : 0
            text: qsTr("Move to new window")
            iconName: "app-window"
            onTriggered: AppController.detachTab(tabMenu.tabId)
        }
    }
}
