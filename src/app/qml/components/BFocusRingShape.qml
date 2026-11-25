pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BFocusRingShape — the ring a BFocusRing (src/app/ui/focusring.h) draws: made by it, with itself as
// `ring`, the first time it is visible. A Rectangle 2 px (Theme.focusRingWidth) wide in
// Theme.focusRing just outside its target (inside with `inset`), in the BFocusRing's coordinates,
// which are its parent's.
Rectangle {
    required property BFocusRing ring

    readonly property real gap: ring.inset ? -Theme.focusRingWidth : Theme.focusRingWidth
    x: ring.target ? -gap : 0
    y: ring.target ? -gap : 0
    width: ring.target ? ring.target.width + 2 * gap : 0
    height: ring.target ? ring.target.height + 2 * gap : 0
    radius: (ring.hasTargetRadius ? ring.targetRadius : Theme.radius) + gap
    color: "transparent"
    border.width: Theme.focusRingWidth
    border.color: Theme.focusRing
    antialiasing: true
}
