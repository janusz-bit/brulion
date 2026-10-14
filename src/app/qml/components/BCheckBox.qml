pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BCheckBox — check box with label and optional description (box on the left).
//
//   BCheckBox { text: qsTr("Pokaż ukończone"); checked: settings.showDone }
//   BCheckBox { tristate: true; checkState: Qt.PartiallyChecked }   // "some selected"
//
// Keyboard: Space toggles; visible focus ring around the box.
T.CheckBox {
    id: control

    property string description

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding,
                             Theme.touchUi ? Theme.touchTarget : 28)
    padding: 4
    leftPadding: 2 + (indicator ? indicator.width + spacing : 0)
    spacing: 10
    focusPolicy: Qt.StrongFocus
    hoverEnabled: !Theme.touchUi

    font.family: Theme.fontUi
    font.pixelSize: Theme.textBase

    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.description: description

    indicator: Rectangle {
        id: box
        readonly property int boxSize: Theme.touchUi ? 22 : 18
        implicitWidth: boxSize
        implicitHeight: boxSize
        x: 2
        y: control.topPadding + content.firstLineCenter - height / 2
        radius: Theme.radiusSm - 1
        color: control.checkState !== Qt.Unchecked ? Theme.link : Theme.bgElevated
        border.width: control.checkState !== Qt.Unchecked ? 0 : 1.5
        border.color: control.hovered ? Theme.fgMuted : Theme.borderControl
        antialiasing: true
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        BIcon {
            anchors.centerIn: parent
            name: control.checkState === Qt.PartiallyChecked ? "minus" : "check"
            size: box.boxSize - 4
            color: Theme.dark ? Theme.bg : "#ffffff"
            visible: control.checkState !== Qt.Unchecked
            scale: visible ? 1 : 0.6
            Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutBack } }
        }

        BFocusRing {
            visible: control.visualFocus
            targetRadius: box.radius
        }
    }

    // Text block centred vertically; the box (indicator) lines up with its first line.
    contentItem: Item {
        id: content
        implicitWidth: Math.max(titleText.implicitWidth, descriptionText.visible ? descriptionText.implicitWidth : 0)
        implicitHeight: textColumn.implicitHeight
        // Centre of the title's first line (its real line height: fonts differ).
        readonly property real firstLineCenter: textColumn.y + titleText.contentHeight / Math.max(1, titleText.lineCount) / 2

        Column {
            id: textColumn
            y: Math.max(0, (parent.height - implicitHeight) / 2)
            width: parent.width
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
