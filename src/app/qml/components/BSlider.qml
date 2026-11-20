pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BSlider — a number picked on a line: a track filled to the value in the link colour, a round
// handle with the focus ring, the value shown beside it (`valueText`, e.g. "3 / 5" or "70 kg").
//
//   BSlider { from: 1; to: 5; stepSize: 1; value: mood; valueText: "" + value
//             onCommitted: (v) => save(v) }
//
// committed(value) comes once a change is over: the finger or the mouse let go, or a key moved it
// (← → ↓ ↑ one step, Page Up / Page Down ten, Home / End the ends) — not for every pixel of a drag.
// The handle is 20 px (28 px on touch UIs, inside a 44-px target).
T.Slider {
    id: control

    /// The value as shown beside the track ("" none).
    property string valueText
    property string accessibleName
    signal committed(real value)

    readonly property int handleSize: Theme.touchUi ? 28 : 20
    readonly property int trackHeight: 4

    implicitWidth: 200
    implicitHeight: Theme.touchUi ? Theme.touchTarget : Theme.controlHeight
    leftPadding: control.handleSize / 2
    rightPadding: control.handleSize / 2 + (valueLabel.visible ? valueLabel.implicitWidth + Theme.space2 : 0)
    focusPolicy: Qt.StrongFocus
    snapMode: T.Slider.SnapAlways
    live: true
    wheelEnabled: false

    Accessible.role: Accessible.Slider
    Accessible.name: control.accessibleName
    Accessible.description: control.valueText

    onPressedChanged: if (!control.pressed) control.committed(control.value)
    // A key moves it without a press: committed at once.
    onMoved: if (!control.pressed) control.committed(control.value)
    Keys.onPressed: (event) => {
        const step = control.stepSize > 0 ? control.stepSize : (control.to - control.from) / 10
        let next = control.value
        if (event.key === Qt.Key_PageUp)
            next = Math.min(control.to, control.value + 10 * step)
        else if (event.key === Qt.Key_PageDown)
            next = Math.max(control.from, control.value - 10 * step)
        else if (event.key === Qt.Key_Home)
            next = control.from
        else if (event.key === Qt.Key_End)
            next = control.to
        else
            return
        event.accepted = true
        if (next !== control.value) {
            control.value = next
            control.committed(next)
        }
    }

    background: Item {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - control.trackHeight) / 2
        width: control.availableWidth
        height: control.trackHeight
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Theme.borderControl
        }
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: height / 2
            color: control.enabled ? Theme.link : Theme.fgFaint
        }
    }

    handle: Rectangle {
        id: knob
        x: control.leftPadding + control.visualPosition * control.availableWidth - width / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        width: control.handleSize
        height: control.handleSize
        radius: width / 2
        color: control.pressed ? Theme.bgSunken : Theme.bgElevated
        border.width: 2
        border.color: control.enabled ? Theme.link : Theme.fgFaint
        antialiasing: true
        BFocusRing {
            visible: control.visualFocus
            targetRadius: knob.radius
        }
    }

    Text {
        id: valueLabel
        visible: control.valueText !== ""
        x: control.width - width
        anchors.verticalCenter: parent.verticalCenter
        text: control.valueText
        textFormat: Text.PlainText
        font.family: Theme.fontUi
        font.pixelSize: Theme.textSm
        font.weight: Theme.weightMedium
        color: Theme.fg
    }
}
