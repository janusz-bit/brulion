pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BFocusRing — the visible keyboard focus indicator shared by all components: a 2 px ring
// in Theme.focusRing drawn just outside the target (or inside with `inset: true`).
//
//   BFocusRing { target: control; visible: control.visualFocus; radius: Theme.radius }
//
// Place it as a child of the control's background (or any item filling the control).
Rectangle {
    id: ring

    property Item target: parent
    property bool inset: false
    property real targetRadius: Theme.radius

    readonly property real gap: inset ? -Theme.focusRingWidth : Theme.focusRingWidth
    x: target ? -gap : 0
    y: target ? -gap : 0
    width: target ? target.width + 2 * gap : 0
    height: target ? target.height + 2 * gap : 0
    radius: targetRadius + gap
    color: "transparent"
    border.width: Theme.focusRingWidth
    border.color: Theme.focusRing
    antialiasing: true
    z: 10
}
