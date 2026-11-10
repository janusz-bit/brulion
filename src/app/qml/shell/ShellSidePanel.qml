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
    property var _opening: ({})
    readonly property bool hasPinned: AppController.routeText(pinned, "view") !== ""
    /// The pinned view's editing toolbar is docked at its bottom edge (touch UIs; AppShell keeps
    /// the panel's bottom on the keyboard).
    readonly property bool toolbarDocked: visible && hasPinned && pinnedView.viewToolbarDocked

    color: Theme.bg
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Side panel")

    // No edge line of its own: the SplitView's handle (BSplitHandle) is the one hairline beside
    // the page, and it carries the hover and drag highlight.
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            implicitHeight: Theme.touchUi ? 56 : 46

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
                    // (Not an arrow back: the page opens in the main view, as "Open as page" does.)
                    iconName: "external-link"
                    label: qsTr("Open in the main view")
                    size: "small"
                    onClicked: {
                        // (A copy: clearing the panel's route changes `pinned`.)
                        panel._opening = panel.pinned
                        AppController.clearSidePanelRoute()
                        AppController.navigate(panel._opening)
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
                    shortcut: ShortcutMap.sequences["panel.toggle"] || ""
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
            id: pinnedView
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
