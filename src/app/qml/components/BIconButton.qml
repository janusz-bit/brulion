pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BIconButton — square icon-only button. `label` is required: it is the accessible name and
// the tooltip text (with `shortcut` shown as key caps).
//
//   BIconButton { iconName: "search"; label: qsTr("Szukaj"); shortcut: "Ctrl+Shift+F" }
//   BIconButton { iconName: "pin"; label: qsTr("Przypnij"); checkable: true }
//
// variant: "ghost" (default) | "secondary" (outlined) | "primary" | "danger"
// size:    "small" | "normal" (default) | "large"
// A checked (toggle) button gets a tinted background and the link colour.
// On touch UIs the hit area is at least Theme.touchTarget (44 px) square.
T.Button {
    id: control

    required property string label
    property string iconName
    property string variant: "ghost"
    property string size: "normal"
    property string shortcut
    property bool showToolTip: true
    property bool toolTipBelow: false
    property color iconColor: {
        if (!enabled)
            return Theme.fgMuted
        switch (variant) {
        case "primary": return Theme.primaryFg
        case "danger": return Theme.dangerFg
        default: return control.checked ? Theme.link : control.hovered || control.down ? Theme.fg : Theme.fgMuted
        }
    }

    readonly property int visualSize: size === "small" ? Theme.controlHeightSm
                                    : size === "large" ? Theme.controlHeightLg
                                    : Theme.controlHeight
    readonly property int hitSize: Math.max(visualSize, Theme.touchUi ? Theme.touchTarget : 0)

    implicitWidth: hitSize
    implicitHeight: hitSize
    topInset: (hitSize - visualSize) / 2
    bottomInset: topInset
    leftInset: topInset
    rightInset: topInset
    padding: 0

    focusPolicy: Qt.StrongFocus
    hoverEnabled: !Theme.touchUi

    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.description: shortcut

    contentItem: Item {
        BIcon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.size === "small" ? Theme.iconSizeSm : control.size === "large" ? Theme.iconSizeLg : Theme.iconSize
            color: control.iconColor
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }
    }

    background: Rectangle {
        radius: Theme.radius
        antialiasing: true
        color: {
            switch (control.variant) {
            case "primary":
                return control.down ? Theme.primaryPressed : control.hovered ? Theme.primaryHover : Theme.primary
            case "danger":
                return control.hovered || control.down ? Theme.dangerHover : Theme.danger
            case "secondary":
                return Theme.bgElevated
            default:
                return control.checked ? Theme.bgSelected
                     : control.down ? Theme.bgPressed
                     : control.hovered ? Theme.bgHover : "transparent"
            }
        }
        border.width: control.variant === "secondary" ? 1 : 0
        border.color: control.hovered ? Theme.borderControl : Theme.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            visible: control.variant === "secondary"
            color: control.checked ? Theme.bgSelected : control.down ? Theme.bgPressed : control.hovered ? Theme.bgHover : "transparent"
        }

        BFocusRing {
            visible: control.visualFocus
            targetRadius: Theme.radius
        }
    }

    opacity: enabled ? 1 : Theme.disabledOpacity

    // Touch: a long press shows the label (there is no hover).
    property bool _touchTip: false
    onPressAndHold: if (Theme.touchUi) _touchTip = true

    // Created on demand: lists may hold hundreds of icon buttons.
    Loader {
        active: control.showToolTip && control.label !== ""
                && (control.hovered || control.visualFocus || control._touchTip)
        sourceComponent: BToolTip {
            parent: control
            text: control.label
            shortcut: control.shortcut
            below: control.toolTipBelow
            visible: ((control.hovered && !control.down) || control.visualFocus) && !Theme.touchUi || control._touchTip
            onClosed: control._touchTip = false
        }
    }
}
