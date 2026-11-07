pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Brulion

// ShellContextDrawer — the side panel of narrow windows (phones; docs/UI.md "Side panel"): the
// same accordion as the panel docked beside the text on wide windows (the view's `sidePanel`,
// ContentSidePanel), in a drawer that slides in from the right edge over about 85 % of the
// window, the page dimmed behind it (Theme.scrim). It opens from
//
//   * the handle, a small tab always on the right edge while the view has a context (a tap, or a
//     swipe to the left on it); the window keeps its rectangle from Android's back gesture
//     (GestureInsets.setExclusion: there a swipe from the edge is the handle's, everywhere else
//     the system's "back");
//   * the top bar's panel button (ShellTopBar.contextRequested), `open()`.
//
// A tap on the dimmed page, a swipe to the right on the drawer, Escape or the Back key close it.
// Every opening makes the panel anew: what was folded is not remembered. Wide windows (≥ 900 px)
// never show it: the panel stays docked there.
//
//   ShellContextDrawer { anchors.fill: parent; contextPanel: shell.routeView.viewSidePanel }
Item {
    id: root

    /// The view's context panel (ShellRouteView.viewSidePanel), or null: no handle, no drawer.
    property Component contextPanel: null
    /// The title in the drawer's header (the page's).
    property string title
    /// The handle may show (narrow windows, not in focus mode, not over a modal surface).
    property bool allowed: true
    /// The window's top and bottom room taken by the system bars (the drawer runs under them).
    property real topInset: 0
    property real bottomInset: 0
    /// The handle's top edge as a share of the height.
    property real handlePosition: 0.38

    readonly property bool available: Theme.narrow && root.contextPanel !== null && root.allowed
    readonly property bool opened: drawer.opened
    readonly property alias drawerItem: drawer

    function open(): void {
        if (root.available)
            drawer.open()
    }
    function close(): void {
        drawer.close()
    }
    function toggle(): void {
        if (drawer.visible)
            drawer.close()
        else
            root.open()
    }

    // ---- The handle --------------------------------------------------------------------------
    Item {
        id: handle
        objectName: "contextDrawerHandle"
        visible: root.available && !drawer.visible
        // A finger-sized target at the very edge; only its inner edge's tab is drawn.
        width: 44
        height: 64
        x: root.width - width - root.SafeArea.margins.right
        y: Math.round((root.height - height) * root.handlePosition)
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Context panel")
        Accessible.onPressAction: root.open()

        Rectangle {
            id: tab
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 56
            // Rounded on the inner side only: it comes out of the edge.
            radius: Theme.radiusSm
            color: handleTap.pressed ? Theme.bgPressed : Theme.bgElevated
            border.width: 1
            border.color: Theme.border
            Rectangle {
                // Squares off the outer corners (the edge side).
                anchors.right: parent.right
                width: tab.radius
                height: tab.height
                color: tab.color
                Rectangle { width: parent.width; height: 1; color: Theme.border }
                Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: Theme.border }
            }
            BIcon {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: -1
                name: "chevron-left"
                size: 12
                color: Theme.fgMuted
            }
        }
        TapHandler {
            id: handleTap
            onTapped: root.open()
        }
        // A swipe to the left on it opens too (the drawer's own edge swipe starts past the
        // system's zone; this one starts on the handle, which the system leaves to the app).
        DragHandler {
            target: null
            yAxis.enabled: false
            onActiveTranslationChanged: if (activeTranslation.x < -Theme.space5) root.open()
        }
    }
    // The handle's rectangle is the app's, not the system's back gesture (Android 10+).
    readonly property rect handleRect: handle.visible ? Qt.rect(handle.x, handle.y, handle.width, handle.height)
                                                      : Qt.rect(0, 0, 0, 0)
    onHandleRectChanged: GestureInsets.setExclusion(root.handleRect)
    Component.onDestruction: GestureInsets.setExclusion(Qt.rect(0, 0, 0, 0))

    // ---- The drawer --------------------------------------------------------------------------
    Drawer {
        id: drawer
        objectName: "contextDrawer"
        width: Math.min(420, root.width * 0.85)
        height: root.height
        edge: Qt.RightEdge
        interactive: root.available
        // The edge swipe starts beyond the system's own zone (gesture navigation's "back"), as
        // the left drawer's; the handle is the way in from the very edge.
        dragMargin: !Theme.touchUi ? Application.styleHints.startDragDistance
                    : Math.max(root.SafeArea.margins.right, GestureInsets.right) + 24
        dim: true
        focus: true
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle {
            color: Theme.bg
            Rectangle {
                width: 1
                height: parent.height
                color: Theme.border
            }
        }
        topPadding: 0
        bottomPadding: 0
        onClosed: root.closed()

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Item { Layout.preferredHeight: root.topInset }
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touchUi ? 56 : 46
                Layout.leftMargin: Theme.space4
                Layout.rightMargin: Theme.space1
                spacing: Theme.space1
                BText {
                    Layout.fillWidth: true
                    text: root.title !== "" ? root.title : qsTr("Context")
                    variant: "label"
                    elide: Text.ElideRight
                    Accessible.role: Accessible.Heading
                }
                BIconButton {
                    objectName: "contextDrawerClose"
                    iconName: "x"
                    label: qsTr("Close side panel")
                    onClicked: drawer.close()
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.borderSubtle
            }
            // Made anew on every opening (nothing folded is remembered), gone once closed.
            Loader {
                objectName: "contextDrawerPanel"
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: drawer.visible && root.contextPanel !== null
                sourceComponent: root.contextPanel
            }
            Item { Layout.preferredHeight: root.bottomInset }
        }
    }

    /// The drawer closed (the shell gives the focus back to the view).
    signal closed()
}
