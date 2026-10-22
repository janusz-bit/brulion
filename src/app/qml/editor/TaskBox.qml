pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// TaskBox — the checkbox of a task block. status: "todo" (empty), "doing" (half), "done"
// (filled with a check), "cancelled" (struck), "deferred" (arrow). A click emits toggled();
// the hit area is a touch target on touch UIs while the box stays small.
// A right-click (mouse, touchpad, stylus) or a long press opens the status menu
// (TaskStatusMenu, created when first opened): statusChosen(status) reports the choice ("" = no
// status). With `ownMenu: false` the box only emits menuRequested() for an owner whose menu it is
// (the block editor's, a Tasks row's). A long press never also toggles.
//
//   TaskBox { status: model.status; onToggled: toggle(); onStatusChosen: (status) => setStatus(status) }
T.AbstractButton {
    id: control

    property string status: "todo"
    /// Open its own TaskStatusMenu (else just emit menuRequested()).
    property bool ownMenu: true
    readonly property int boxSize: Theme.touchUi ? 18 : 16
    readonly property bool closed: status === "done" || status === "cancelled"

    // (toggled() is AbstractButton's own signal, emitted here on a click.)
    signal statusChosen(string status)
    signal menuRequested()

    implicitWidth: Theme.touchUi ? Theme.touchTarget : 24
    implicitHeight: Theme.touchUi ? Theme.touchTarget : 24
    padding: 0
    focusPolicy: Qt.NoFocus
    hoverEnabled: !Theme.touchUi

    Accessible.role: Accessible.CheckBox
    Accessible.name: qsTr("Task")
    Accessible.description: qsTr("Right-click or press and hold to choose the status")
    Accessible.checkable: true
    Accessible.checked: status === "done"

    onClicked: toggled()
    onPressAndHold: openMenu()

    /// The status menu (own) or menuRequested().
    function openMenu() {
        if (!ownMenu) {
            menuRequested()
            return
        }
        if (!statusMenu)
            statusMenu = menuComponent.createObject(control) as TaskStatusMenu
        statusMenu.status = control.status
        statusMenu.popup(control, 0, control.height)
    }
    property TaskStatusMenu statusMenu: null

    // Right-click only: Qt gives a finger (no buttons) to every TapHandler, so without the device
    // filter every tap on a phone would open the menu as well. Touch: the long press above.
    // WithinBounds: an exclusive grab, so a row's own right-click handler under the box (Tasks)
    // does not open its menu a second time.
    TapHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.WithinBounds
        onTapped: control.openMenu()
    }

    Component {
        id: menuComponent
        TaskStatusMenu {
            onStatusChosen: (status) => control.statusChosen(status)
        }
    }

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
