pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellBottomNav — the phone's bottom navigation: four views (AppSettings.bottomNavTabs, by
// default Dziennik, Kalendarz, Szukaj, Zadania; Ustawienia → Wygląd → Układ) and Menu (opens the
// drawer). Each tab is a ≥ 48 px touch target with icon and label; the bar extends
// under the Android navigation bar (`bottomInset`).
Rectangle {
    id: nav

    property real bottomInset: 0

    signal menuClicked()

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

    implicitHeight: 60 + bottomInset
    color: Theme.bgElevated

    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Navigation")

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.bottomMargin: nav.bottomInset
        spacing: 0

        Repeater {
            model: AppSettings.bottomNavTabs.map(v => nav.tabs[v]).filter(t => t !== undefined)
                       .concat([{ id: "menu", title: qsTr("Menu"), icon: "menu" }])
            delegate: T.AbstractButton {
                id: tab
                required property var modelData
                readonly property bool current: modelData.id === nav.view
                                                || (modelData.id === "journals" && nav.view === "journal")
                                                || (modelData.id === "tags" && nav.view === "tag")

                objectName: "bottomNav_" + modelData.id
                Layout.fillWidth: true
                Layout.fillHeight: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.title
                Accessible.selected: current

                onClicked: {
                    if (modelData.id === "menu")
                        nav.menuClicked()
                    else if (modelData.id === "journals" && current)
                        AppController.openToday()
                    else
                        AppController.navigate({ view: modelData.id })
                }

                contentItem: Column {
                    spacing: 3
                    topPadding: 7

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 56
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
                    BText {
                        id: label
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: tab.modelData.title
                        font.pixelSize: Theme.textXs
                        font.weight: tab.current ? Theme.weightSemiBold : Theme.weightMedium
                        color: tab.current ? Theme.fg : Theme.fgMuted
                        // A long name (a view chosen in the settings) on a narrow tab: elided.
                        readonly property bool tooWide: implicitWidth > tab.width - 4 && tab.width > 4
                        width: tooWide ? tab.width - 4 : implicitWidth
                        elide: tooWide ? Text.ElideRight : Text.ElideNone
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
        }
    }
}
