pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BSwitch — on/off setting with a label, optional icon and description. The track sits at
// the right edge (settings-row style), so a full-width switch reads label … [switch].
//
//   BSwitch { text: qsTr("Alarm (głośny, do wyłączenia)"); iconName: "alarm-clock"
//             description: qsTr("Dzwoni, dopóki go nie wyłączysz") }
//
// Keyboard: Space toggles. The whole row is the hit area (≥ 44 px high on touch UIs).
// The track: on, the link colour; off, solid borderControl (≥ 3:1 on bg, bgElevated and bgSunken,
// WCAG 1.4.11 — the off state is seen, not guessed), fgFaint under the mouse. The knob says the
// state too, not only the track's colour and the knob's side (WCAG 1.4.1): a check when on, a cross
// when off, in the track's colour (12 px in the 18-px touch knob, 10 px in the 14-px one).
T.Switch {
    id: control

    property string description
    property string iconName

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding,
                             Theme.touchUi ? Theme.touchTarget : 28)
    padding: 4
    // The title at the row's left edge, like every setting row's (the focus ring is on the track).
    leftPadding: 0
    rightPadding: indicator.width + 14
    spacing: 10
    focusPolicy: Qt.StrongFocus
    hoverEnabled: !Theme.touchUi

    font.family: Theme.fontUi
    font.pixelSize: Theme.textBase

    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.description: description
    Accessible.checkable: true
    Accessible.checked: checked

    indicator: Rectangle {
        id: track
        readonly property int trackWidth: Theme.touchUi ? 40 : 34
        readonly property int trackHeight: Theme.touchUi ? 24 : 20
        implicitWidth: trackWidth
        implicitHeight: trackHeight
        x: control.width - width - 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? Theme.link : control.hovered ? Theme.fgFaint : Theme.borderControl
        antialiasing: true
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            id: thumb
            readonly property int inset: 3
            width: track.height - 2 * inset
            height: width
            radius: width / 2
            y: inset
            x: Math.max(inset, Math.min(track.width - width - inset,
                       control.visualPosition * (track.width - width - 2 * inset) + inset))
            color: control.checked && Theme.dark ? Theme.bg : Theme.switchThumb
            antialiasing: true
            Behavior on x {
                enabled: !control.down
                NumberAnimation { duration: Theme.duration; easing.type: Theme.easing }
            }

            BIcon {
                objectName: "switchStateIcon"
                anchors.centerIn: parent
                name: control.checked ? "check" : "x"
                size: Theme.touchUi ? 12 : 10
                color: track.color
            }
        }

        BFocusRing {
            visible: control.visualFocus
            targetRadius: track.radius
        }
    }

    // Content block centred vertically; the icon lines up with the first line of the title.
    contentItem: Item {
        implicitWidth: (icon.width > 0 ? icon.width + 10 : 0) + Math.max(titleText.implicitWidth, descriptionText.visible ? descriptionText.implicitWidth : 0)
        implicitHeight: textColumn.implicitHeight

        BIcon {
            id: icon
            // Centred on the title's first line (its real line height: fonts differ).
            y: textColumn.y + Math.max(0, (titleText.contentHeight / Math.max(1, titleText.lineCount) - height) / 2)
            name: control.iconName
            size: Theme.iconSize
            color: control.checked ? Theme.fg : Theme.fgMuted
        }
        Column {
            id: textColumn
            x: icon.width > 0 ? icon.width + 10 : 0
            y: Math.max(0, (parent.height - implicitHeight) / 2)
            width: parent.width - x
            spacing: 1
            Text {
                id: titleText
                width: parent.width
                text: control.text
                font: control.font
                color: Theme.fg
                wrapMode: Text.Wrap
            }
            Text {
                id: descriptionText
                width: parent.width
                visible: control.description !== ""
                text: control.description
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: Theme.fgMuted
                wrapMode: Text.Wrap
            }
        }
    }

    opacity: enabled ? 1 : Theme.disabledOpacity
}
