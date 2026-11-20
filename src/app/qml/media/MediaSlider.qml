pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// MediaSlider — the players' seek bar and volume (MediaControls): a thin track, the part played in
// the accent's ink, a round handle that grows under the pointer or the finger; `buffered`
// (0–1, optional) a lighter part. On touch UIs the hit area is Theme.touchTarget high.
//
//   MediaSlider { from: 0; to: duration; value: position; onMoved: MediaPlayback.seek(value) }
T.Slider {
    id: control

    /// What the slider means (its accessible name).
    property string label
    /// The value as text (its accessible value: "1:23").
    property string valueText

    implicitWidth: 120
    implicitHeight: Theme.touchUi ? Theme.touchTarget : 20
    padding: 0
    leftPadding: handleSize / 2
    rightPadding: handleSize / 2
    // (A click does not take the keyboard from the text being written; Tab reaches it.)
    focusPolicy: Qt.TabFocus
    hoverEnabled: !Theme.touchUi
    live: true

    readonly property real handleSize: control.pressed || control.hovered || control.visualFocus ? 14 : 10

    Accessible.role: Accessible.Slider
    Accessible.name: control.label
    Accessible.description: control.valueText

    background: Item {
        x: control.leftPadding
        y: (control.height - 4) / 2
        width: control.availableWidth
        height: 4
        Rectangle {
            anchors.fill: parent
            radius: 2
            color: Theme.border
        }
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: control.enabled ? Theme.fg : Theme.fgFaint
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * control.availableWidth - width / 2
        y: (control.height - height) / 2
        width: control.handleSize
        height: control.handleSize
        radius: width / 2
        color: control.enabled ? Theme.fg : Theme.fgFaint
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.focusRing
        Behavior on width { enabled: !Theme.reducedMotion; NumberAnimation { duration: 90 } }
    }
}
