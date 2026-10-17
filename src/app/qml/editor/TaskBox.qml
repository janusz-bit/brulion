pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// TaskBox — the checkbox of a task block. status: "todo" (empty), "doing" (half), "done"
// (filled with a check), "cancelled" (struck), "deferred" (arrow). A click emits toggled();
// the hit area is a touch target on touch UIs while the box stays small.
T.AbstractButton {
    id: control

    property string status: "todo"
    readonly property int boxSize: Theme.touchUi ? 18 : 16
    readonly property bool closed: status === "done" || status === "cancelled"

    implicitWidth: Theme.touchUi ? Theme.touchTarget : 24
    implicitHeight: Theme.touchUi ? Theme.touchTarget : 24
    padding: 0
    focusPolicy: Qt.NoFocus
    hoverEnabled: !Theme.touchUi

    Accessible.role: Accessible.CheckBox
    Accessible.name: qsTr("Task")
    Accessible.checkable: true
    Accessible.checked: status === "done"

    onClicked: toggled()

    contentItem: Item {
        Rectangle {
            id: box
            anchors.centerIn: parent
            width: control.boxSize
            height: control.boxSize
            radius: Theme.radiusXs + 1
            color: control.status === "done" ? Theme.success
                 : control.status === "doing" ? Theme.alpha(Theme.warning, 0.18)
                 : control.hovered ? Theme.bgHover : "transparent"
            border.width: control.status === "done" ? 0 : 1.5
            border.color: control.status === "doing" ? Theme.warningText
                        : control.closed ? Theme.fgFaint
                        : control.hovered ? Theme.fg : Theme.borderControl
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }

            // Icons only for the states that show one (an icon is an image to load: a box is
            // created with every task row and block, also while lists scroll).
            BIcon {
                anchors.centerIn: parent
                name: control.status === "done" ? "check" : ""
                size: control.boxSize - 4
                color: Theme.dark ? Theme.bg : "#ffffff"
            }
            // Doing: the lower half filled.
            Rectangle {
                visible: control.status === "doing"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 3
                height: (parent.height - 6) / 2
                radius: 1
                color: Theme.warningText
            }
            Rectangle {
                visible: control.status === "cancelled"
                anchors.centerIn: parent
                width: parent.width - 6
                height: 1.5
                color: Theme.fgFaint
            }
            BIcon {
                anchors.centerIn: parent
                name: control.status === "deferred" ? "chevron-right" : ""
                size: control.boxSize - 4
                color: Theme.fgMuted
            }
        }
    }
}
