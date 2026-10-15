pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// ShellCaptureButton — the floating quick-capture button of narrow windows (56 px, round, ink
// fill like primary buttons). Adds a note to today's journal.
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
            color: Theme.primaryFg
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
            color: control.down ? Theme.primaryPressed : control.hovered ? Theme.primaryHover : Theme.primary
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }
        BFocusRing {
            target: control
            visible: control.visualFocus
            targetRadius: control.width / 2
        }
    }
}
