pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// ShellCaptureButton — the floating quick-capture button of narrow windows (56 px, round): a
// raised neutral button — the popup surface (bgElevated2) with a 1-px ring and a level-2 shadow,
// a "+" in the text colour — so it stays obvious without being the loudest thing on the screen
// (no ink / cream primary fill). Adds a note to today's journal.
T.AbstractButton {
    id: control

    implicitWidth: 56
    implicitHeight: 56
    focusPolicy: Qt.StrongFocus
    hoverEnabled: !Theme.touchUi

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Quick capture")

    contentItem: Item {
        BIcon {
            anchors.centerIn: parent
            name: "plus"
            size: Theme.iconSizeLg
            color: Theme.fg
        }
    }

    background: Item {
        BShadow {
            target: fill
            level: 2
        }
        Rectangle {
            id: fill
            anchors.fill: parent
            radius: width / 2
            antialiasing: true
            color: Theme.bgElevated2
            // The ring carries the edge where the surface is as light as the page (white paper).
            border.width: 1
            border.color: Theme.dark ? Theme.borderStrong : Theme.border

            // Hover and press (as BChip): an overlay keeps the raised surface's colour.
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                antialiasing: true
                color: control.down ? Theme.bgPressed : control.hovered ? Theme.bgHover : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            }
        }
        BFocusRing {
            target: control
            visible: control.visualFocus
            targetRadius: control.width / 2
        }
    }
}
