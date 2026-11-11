pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellTabBar — the main window's tabs (wide windows; docs/UI.md "Tabs, split view and windows"),
// between the top bar and the view, shown while there are two tabs or more (or the view is split).
// Every tab has the same width (at most 220 px, at least 120: past that the bar scrolls with the
// wheel); the tab of the focused pane is drawn like the view under it with a 2-px accent edge along
// its top (the "Nocny" look: square tabs, hairlines between them), the one shown in the other pane
// of a split view like the view without the edge. A click shows a tab, a
// middle click or its × closes it, a drag along the bar moves it (AppController.moveTab), a
// right-click (or a long press) opens its menu: Close tab, Close other tabs, Show in split view /
// Close split view, Move to new window (desktop). The + at the end opens a new tab (Ctrl+T).
Rectangle {
    id: bar

    readonly property list<var> tabs: AppController.tabs
    readonly property real tabWidth: Math.max(minTabWidth, Math.min(maxTabWidth, list.width / Math.max(1, tabs.length)))
    readonly property real minTabWidth: 120
    readonly property real maxTabWidth: 220

    implicitHeight: Theme.touchUi ? 44 : 38
    color: Theme.bgSunken
    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Tabs")

    ShellRouteTitles { id: titles }

    /// The icon of a route's view (ShellRouteTitles.routeIcon).
    function iconOf(route: var): string {
        return titles.routeIcon(titles.field(route, "view"))
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.borderSubtle
    }

    RowLayout {
        anchors.fill: parent
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
                // (AppController.tabs: { tabId, route, … })
                required property int tabId
                required property var route
                required property int index
                readonly property bool focusedTab: tabId === AppController.focusedTab
                /// Shown in a pane (the main one or the split one).
                readonly property bool shown: tabId === AppController.mainTab || tabId === AppController.splitTab
                readonly property string title: titles.routeTitle(titles.field(tab.route, "view"),
                                                                 titles.field(tab.route, "path"),
                                                                 titles.field(tab.route, "date"),
                                                                 titles.field(tab.route, "tag"),
                                                                 titles.field(tab.route, "title"))
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

                // A shown tab runs into the view under it (its fill covers the bar's bottom line).
                Rectangle {
                    anchors.fill: parent
                    visible: tab.shown || hover.hovered
                    color: tab.shown ? Theme.bg : Theme.bgHover
                }
                Rectangle {
                    objectName: "shellTabAccent"
                    visible: tab.focusedTab && (AppController.tabCount > 1 || AppController.splitTab !== 0)
                    anchors.top: parent.top
                    width: parent.width
                    height: 2
                    color: Theme.accent
                }
                // Hairlines between the tabs, full height (IDE-like).
                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: Theme.borderSubtle
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space2
                    anchors.rightMargin: 2
                    spacing: Theme.space1

                    BIcon {
                        name: bar.iconOf(tab.route)
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
                        shortcut: tab.focusedTab ? ShortcutMap.sequences["tab.close"] || "" : ""
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
                    onLongPressed: tabMenu.openFor(tab, tab.tabId)
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
                    onTapped: tabMenu.openFor(tab, tab.tabId)
                }
                DragHandler {
                    id: drag
                    target: null
                    yAxis.enabled: false
                    onActiveTranslationChanged: {
                        const centre = tab.x + tab.width / 2 + drag.activeTranslation.x
                        tab.dropIndex = Math.max(0, Math.min(bar.tabs.length - 1, Math.floor(centre / bar.tabWidth)))
                    }
                    onActiveChanged: {
                        // (Nothing changes the tabs while dragging: the delegates would be made again.)
                        if (drag.active) {
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
            shortcut: ShortcutMap.sequences["tab.new"] || ""
            size: "small"
            onClicked: AppController.newTab()
        }
    }

    BMenu {
        id: tabMenu
        property int tabId: 0
        readonly property bool inSplit: tabId !== 0 && tabId === AppController.splitTab
        function openFor(tab: Item, id: int): void {
            tabMenu.tabId = id
            tabMenu.popup(tab, 0, tab.height)
        }
        BMenuItem {
            text: qsTr("Close tab")
            iconName: "x"
            // (Its key closes the focused tab.)
            shortcut: tabMenu.tabId === AppController.focusedTab ? ShortcutMap.sequences["tab.close"] || "" : ""
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
