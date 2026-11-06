pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BToolTip — short help text above (or below) a control, with an optional shortcut hint.
//
//   BIconButton { ... }            // shows its own tooltip from `label` + `shortcut`
//   BToolTip { parent: someItem; visible: hover.hovered; text: qsTr("Paste") }
//
// Inverse colours (ink on paper, paper on ink), small radius, fades in after `delay`.
// `below: true` places it under the parent (for controls at the top edge), `beside: true` to its
// right (a column of buttons at the window's left edge: the sidebar's rail).
T.ToolTip {
    id: control

    property string shortcut
    property bool below: false
    property bool beside: false

    x: !parent ? 0 : beside ? parent.width + 6 : Math.round((parent.width - implicitWidth) / 2)
    y: !parent ? 0 : beside ? Math.round((parent.height - implicitHeight) / 2)
                            : below ? parent.height + 6 : -implicitHeight - 6
    margins: 6
    delay: Theme.touchUi ? 0 : 550
    timeout: Theme.touchUi ? 2500 : -1

    implicitWidth: Math.min(320, Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                          implicitContentWidth + leftPadding + rightPadding))
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    topPadding: 5
    bottomPadding: 5
    leftPadding: 8
    rightPadding: 8
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    font.family: Theme.fontUi
    font.pixelSize: Theme.textSm

    contentItem: Row {
        spacing: 8
        Text {
            id: tipText
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, 300 - (kbd.visible ? kbd.width + 8 : 0))
            text: control.text
            font: control.font
            color: Theme.fgInverse
            wrapMode: Text.Wrap
        }
        BKbd {
            id: kbd
            anchors.verticalCenter: parent.verticalCenter
            sequence: control.shortcut
            inverse: true
            small: true
        }
    }

    background: Rectangle {
        color: Theme.bgInverse
        radius: Theme.radiusSm
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easing }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationInstant }
    }
}
