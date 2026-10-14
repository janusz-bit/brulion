pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BKbd — keyboard shortcut hint drawn as key caps: BKbd { sequence: "Ctrl+Shift+F" }.
// Keys are split on "+" ("Ctrl++" works). `inverse` for use on dark tooltips/toasts.
// Hidden automatically on touch-only UIs unless `alwaysVisible`.
Row {
    id: kbd

    property string sequence
    property bool inverse: false
    property bool alwaysVisible: false
    property bool small: false

    readonly property var keys: {
        if (!sequence)
            return []
        const parts = sequence.split("+")
        const out = []
        for (let i = 0; i < parts.length; ++i) {
            if (parts[i] === "" && i + 1 < parts.length && parts[i + 1] === "") {
                out.push("+")
                ++i
            } else if (parts[i] !== "") {
                out.push(parts[i])
            }
        }
        return out
    }

    spacing: 3
    visible: keys.length > 0 && (alwaysVisible || !Theme.touchUi)

    Accessible.role: Accessible.StaticText
    Accessible.name: sequence

    Repeater {
        model: kbd.keys
        delegate: Rectangle {
            id: cap
            required property string modelData
            implicitHeight: kbd.small ? 16 : 18
            implicitWidth: Math.max(implicitHeight, keyLabel.implicitWidth + (kbd.small ? 8 : 10))
            radius: Theme.radiusXs + 1
            color: kbd.inverse ? Qt.rgba(Theme.fgInverse.r, Theme.fgInverse.g, Theme.fgInverse.b, 0.14) : Theme.bgElevated
            border.width: kbd.inverse ? 0 : 1
            border.color: Theme.borderStrong

            // A slightly heavier bottom edge reads as a key cap.
            Rectangle {
                visible: !kbd.inverse
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 1
                anchors.rightMargin: 1
                height: 1
                color: Theme.borderStrong
            }

            Text {
                id: keyLabel
                anchors.centerIn: parent
                anchors.verticalCenterOffset: kbd.inverse ? 0 : -0.5
                text: cap.modelData
                color: kbd.inverse ? Theme.fgInverse : Theme.fgMuted
                font.family: Theme.fontUi
                font.pixelSize: kbd.small ? Theme.textXs - 1 : Theme.textXs
                font.weight: Theme.weightMedium
            }
        }
    }
}
