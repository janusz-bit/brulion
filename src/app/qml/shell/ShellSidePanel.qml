pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellSidePanel — the optional right panel of wide windows. It shows either a pinned route
// (a page opened "in the side panel", AppController.sidePanelRoute) or the current view's
// context panel (`contextPanel`: backlinks, outline, mini calendar — provided by the views as
// their `sidePanel` component), else a short hint.
Rectangle {
    id: panel

    /// Title of the pinned route.
    property string pinnedTitle
    /// The main view's context panel (ShellRouteView.viewSidePanel), or null.
    property Component contextPanel: null

    readonly property var pinned: AppController.sidePanelRoute
    readonly property bool hasPinned: pinned && pinned.view !== undefined

    color: Theme.bg
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Side panel")

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: Theme.border
        z: 2
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.touchUi ? 56 : 46
            color: Theme.bg

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space4
                anchors.rightMargin: Theme.space2
                spacing: 2

                BText {
                    Layout.fillWidth: true
                    text: panel.hasPinned ? panel.pinnedTitle : qsTr("Context")
                    variant: "label"
                    elide: Text.ElideRight
                }
                BIconButton {
                    visible: panel.hasPinned
                    iconName: "arrow-left"
                    label: qsTr("Open in the main view")
                    size: "small"
                    onClicked: {
                        const route = panel.pinned
                        AppController.clearSidePanelRoute()
                        AppController.navigate(route)
                    }
                }
                BIconButton {
                    visible: panel.hasPinned
                    iconName: "pin"
                    label: qsTr("Unpin")
                    size: "small"
                    onClicked: AppController.clearSidePanelRoute()
                }
                BIconButton {
                    iconName: "x"
                    label: qsTr("Close side panel")
                    shortcut: "Ctrl+\\"
                    size: "small"
                    onClicked: AppController.sidePanelVisible = false
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.borderSubtle
            }
        }

        ShellRouteView {
            visible: panel.hasPinned
            Layout.fillWidth: true
            Layout.fillHeight: true
            route: panel.hasPinned ? panel.pinned : ({})
            inSidePanel: true
        }
        Loader {
            visible: !panel.hasPinned && panel.contextPanel !== null
            active: visible
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: panel.contextPanel
        }
        Item {
            visible: !panel.hasPinned && panel.contextPanel === null
            Layout.fillWidth: true
            Layout.fillHeight: true

            BEmptyState {
                anchors.centerIn: parent
                compact: true
                iconName: "panel-right"
                title: qsTr("Nothing to show here")
                description: qsTr("Backlinks, the outline and a calendar of the open page appear here. Pages opened in the side panel stay here too.")
            }
        }
    }
}
