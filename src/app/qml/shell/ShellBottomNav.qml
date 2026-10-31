pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellBottomNav — the phone's bottom navigation: four views (AppSettings.bottomNavTabs, by
// default Dziennik, Kalendarz, Szukaj, Zadania; Ustawienia → Wygląd → Układ), quick capture
// ("+ Notatka", AppSettings.captureButtonShown) and Menu (opens the drawer), always last. Each
// tab is a ≥ 48 px touch target with icon and label, sharing the width; capture is a fixed
// 56 px item whose "+" sits in a small ringed pill (the raised surface, as a button), so it
// reads as an action, never as the current view. Nothing floats over the content. The bar
// extends under the Android navigation bar (`bottomInset`).
Rectangle {
    id: nav

    property real bottomInset: 0

    signal menuClicked()
    signal captureClicked()

    readonly property string view: AppController.currentRoute.view || ""
    /// The views a tab can show.
    readonly property var tabs: ({
        journals: { id: "journals", title: qsTr("Journal"), icon: "book-open" },
        calendar: { id: "calendar", title: qsTr("Calendar"), icon: "calendar" },
        search: { id: "search", title: qsTr("Search"), icon: "search" },
        tasks: { id: "tasks", title: qsTr("Tasks"), icon: "list-todo" },
        allPages: { id: "allPages", title: qsTr("Pages"), icon: "files" },
        tags: { id: "tags", title: qsTr("Tags"), icon: "tags" },
        graph: { id: "graph", title: qsTr("Graph"), icon: "waypoints" }
    })
    /// The capture item's width (the tabs share the rest).
    readonly property int captureWidth: 56

    implicitHeight: 60 + bottomInset
    color: Theme.bgElevated

    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Navigation")

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.border
    }

    /// A label under an icon, elided only when a long view name does not fit its item (1 px
    /// kept on each side: "Kalendarz" fits a 360-px phone's tab, about 61 px, whole).
    component NavLabel: BText {
        required property Item item
        anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
        font.pixelSize: Theme.textXs
        readonly property bool tooWide: implicitWidth > item.width - 2 && item.width > 2
        width: tooWide ? item.width - 2 : implicitWidth
        elide: tooWide ? Text.ElideRight : Text.ElideNone
    }

    function openView(id, current) {
        if (id === "journals" && current)
            AppController.openToday()
        else
            AppController.navigate({ view: id })
    }

    /// A view's tab, or Menu (`currentView`: the route's view; the instance acts on clicked()).
    component NavTab: T.AbstractButton {
        id: tab
        required property var modelData
        property string currentView
        readonly property bool current: modelData.id === currentView
                                        || (modelData.id === "journals" && currentView === "journal")
                                        || (modelData.id === "tags" && currentView === "tag")

        objectName: "bottomNav_" + modelData.id
        // One preferred width for every tab, independent of its pill and label (both follow the
        // tab's width): the tabs share the bar evenly, a long label elides.
        implicitWidth: 56
        Layout.fillWidth: true
        Layout.fillHeight: true
        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.PageTab
        Accessible.name: modelData.title
        Accessible.selected: current

        contentItem: Column {
            spacing: 3
            topPadding: 7

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                // Never wider than the tab: it would run into a neighbour on a narrow phone.
                width: Math.min(56, tab.width - 4)
                height: 28
                radius: 14
                color: tab.current ? Theme.bgSelected : tab.pressed ? Theme.bgPressed : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                BIcon {
                    anchors.centerIn: parent
                    name: tab.modelData.icon
                    size: Theme.iconSize
                    color: tab.current ? Theme.fg : Theme.fgMuted
                }
            }
            NavLabel {
                item: tab
                text: tab.modelData.title
                font.weight: tab.current ? Theme.weightSemiBold : Theme.weightMedium
                color: tab.current ? Theme.fg : Theme.fgMuted
            }
        }
        background: Item {
            BFocusRing {
                target: tab
                visible: tab.visualFocus
                inset: true
                targetRadius: Theme.radius
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.bottomMargin: nav.bottomInset
        spacing: 0

        Repeater {
            model: AppSettings.bottomNavTabs.map(v => nav.tabs[v]).filter(t => t !== undefined)
            delegate: NavTab {
                id: viewTab
                currentView: nav.view
                onClicked: nav.openView(viewTab.modelData.id, viewTab.current)
            }
        }

        // Quick capture: a note into today's journal (ShellQuickCapture). Hidden by the user
        // (Ustawienia → Wygląd → Układ): capture stays in the palette and under Ctrl+Shift+N.
        T.AbstractButton {
            id: capture
            objectName: "shellCaptureButton"
            visible: AppSettings.captureButtonShown
            Layout.preferredWidth: nav.captureWidth
            Layout.fillHeight: true
            focusPolicy: Qt.StrongFocus
            hoverEnabled: !Theme.touchUi
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Quick capture")

            onClicked: nav.captureClicked()

            contentItem: Column {
                spacing: 3
                topPadding: 7

                // The old floating button's look, scaled down: the popup surface with a 1-px ring
                // (it carries the edge where the surface is as light as the bar), hover and press
                // as overlays.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 40
                    height: 28
                    radius: 14
                    antialiasing: true
                    color: Theme.bgElevated2
                    border.width: 1
                    border.color: Theme.dark ? Theme.borderStrong : Theme.border
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        antialiasing: true
                        color: capture.down ? Theme.bgPressed : capture.hovered ? Theme.bgHover : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    }
                    BIcon {
                        anchors.centerIn: parent
                        name: "plus"
                        size: Theme.iconSize
                        color: Theme.fg
                    }
                }
                NavLabel {
                    item: capture
                    text: qsTr("Note")
                    font.weight: Theme.weightMedium
                    color: Theme.fgMuted
                }
            }
            background: Item {
                BFocusRing {
                    target: capture
                    visible: capture.visualFocus
                    inset: true
                    targetRadius: Theme.radius
                }
            }
        }

        NavTab {
            modelData: ({ id: "menu", title: qsTr("Menu"), icon: "menu" })
            currentView: nav.view
            onClicked: nav.menuClicked()
        }
    }
}
