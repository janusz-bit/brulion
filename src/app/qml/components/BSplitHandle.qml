pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BSplitHandle — SplitView handle: a 1 px divider with a wider invisible grab area that
// highlights on hover and while dragging.
//
//   SplitView {
//       handle: BSplitHandle { orientation: Qt.Horizontal }
//       ...
//   }
//
// orientation must match the SplitView's (the handle cannot see its view).
Item {
    id: handle

    property int orientation: Qt.Horizontal
    readonly property bool horizontal: orientation === Qt.Horizontal
    readonly property bool pressed: T.SplitHandle.pressed
    readonly property bool active: pressed || T.SplitHandle.hovered
    readonly property int grab: Theme.touchUi ? 24 : 9

    implicitWidth: horizontal ? 1 : 0
    implicitHeight: horizontal ? 0 : 1

    // The grab area extends beyond the 1 px the handle occupies in the layout.
    containmentMask: Item {
        x: handle.horizontal ? (handle.width - width) / 2 : 0
        y: handle.horizontal ? 0 : (handle.height - height) / 2
        width: handle.horizontal ? handle.grab : handle.width
        height: handle.horizontal ? handle.height : handle.grab
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.border
    }

    Rectangle {
        readonly property int thickness: handle.pressed ? 3 : 2
        x: handle.horizontal ? (handle.width - thickness) / 2 : 0
        y: handle.horizontal ? 0 : (handle.height - thickness) / 2
        width: handle.horizontal ? thickness : handle.width
        height: handle.horizontal ? handle.height : thickness
        color: handle.pressed ? Theme.focusRing : Qt.rgba(Theme.focusRing.r, Theme.focusRing.g, Theme.focusRing.b, 0.7)
        opacity: handle.active ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }
}
