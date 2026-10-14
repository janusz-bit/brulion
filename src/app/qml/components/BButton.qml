pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BButton — text button with optional icon.
//
//   BButton { text: qsTr("Zapisz"); variant: "primary"; onClicked: save() }
//   BButton { text: qsTr("Usuń"); variant: "danger"; iconName: "trash-2"; busy: deleting }
//
// variant: "primary" (ink; one per view) | "secondary" (default, outlined) | "ghost"
//          (toolbar/text button) | "danger" (destructive confirmation)
// size:    "small" | "normal" (default) | "large"
// iconName: Lucide icon before the text (after it with `iconRight`, e.g. "chevron-down")
// busy:    shows a spinner instead of the icon and swallows clicks (keeps its size)
// On touch UIs the hit area is at least Theme.touchTarget high even for small buttons
// (the visual part stays compact, the rest is inset).
T.Button {
    id: control

    property string variant: "secondary"
    property string size: "normal"
    property string iconName: ""
    property bool iconRight: false
    property bool busy: false
    property color textColor: {
        switch (variant) {
        case "primary": return Theme.primaryFg
        case "danger": return Theme.dangerFg
        default: return Theme.fg
        }
    }

    readonly property int visualHeight: size === "small" ? Theme.controlHeightSm
                                      : size === "large" ? Theme.controlHeightLg
                                      : Theme.controlHeight
    readonly property int hitHeight: Math.max(visualHeight, Theme.touchUi ? Theme.touchTarget : 0)
    readonly property bool iconOnly: text === "" && (iconName !== "" || busy)

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: hitHeight
    topInset: (hitHeight - visualHeight) / 2
    bottomInset: (hitHeight - visualHeight) / 2
    // Icon-only: square visual, hit area at least a touch target wide (like BIconButton).
    leftInset: iconOnly ? (hitHeight - visualHeight) / 2 : 0
    rightInset: leftInset
    leftPadding: iconOnly ? leftInset + (visualHeight - iconItem.size) / 2
                          : (size === "small" ? 10 : size === "large" ? 18 : 14) - (iconName !== "" && !iconRight ? 2 : 0)
    rightPadding: iconOnly ? leftPadding
                           : (size === "small" ? 10 : size === "large" ? 18 : 14) - (iconName !== "" && iconRight ? 2 : 0)
    topPadding: topInset
    bottomPadding: bottomInset

    focusPolicy: Qt.StrongFocus
    hoverEnabled: !Theme.touchUi
    font.family: Theme.fontUi
    font.pixelSize: size === "small" ? Theme.textSm : Theme.textControl
    font.weight: Theme.weightMedium

    Accessible.role: Accessible.Button
    Accessible.name: text

    // While busy: no clicks, no key activation, but keep focus and appearance.
    Keys.onPressed: (event) => {
        if (control.busy && (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter))
            event.accepted = true
    }
    MouseArea {
        anchors.fill: parent
        enabled: control.busy
        visible: control.busy
        acceptedButtons: Qt.AllButtons
        cursorShape: Qt.BusyCursor
    }

    contentItem: Item {
        id: content
        // From the label's implicit width (not the Row's): the label is elided to the
        // available width, which must not feed back into the implicit size.
        readonly property bool hasIcon: control.iconName !== "" || control.busy
        implicitWidth: (hasIcon ? iconItem.size : 0) + (hasIcon && control.text !== "" ? row.spacing : 0)
                       + (control.text !== "" ? label.implicitWidth : 0)
        implicitHeight: Math.max(iconItem.size, label.implicitHeight)

        Row {
            id: row
            anchors.centerIn: parent
            spacing: control.size === "small" ? 5 : 7
            layoutDirection: control.iconRight ? Qt.RightToLeft : Qt.LeftToRight

            Item {
                width: iconItem.width
                height: iconItem.height
                visible: control.iconName !== "" || control.busy
                anchors.verticalCenter: parent.verticalCenter

                BIcon {
                    id: iconItem
                    name: control.iconName
                    size: control.size === "small" ? Theme.iconSizeSm : Theme.iconSize
                    collapseWhenEmpty: !control.busy
                    color: control.textColor
                    opacity: control.busy ? 0 : 1
                }
                BSpinner {
                    anchors.centerIn: parent
                    visible: control.busy
                    size: iconItem.size
                    color: control.textColor
                }
            }

            Text {
                id: label
                visible: control.text !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: control.font
                color: control.textColor
                elide: Text.ElideRight
                width: Math.min(implicitWidth, Math.max(0, control.availableWidth - (content.hasIcon ? iconItem.size + row.spacing : 0)))
            }
        }
    }

    background: Rectangle {
        implicitWidth: control.iconOnly ? control.visualHeight : 64
        implicitHeight: control.visualHeight
        radius: Theme.radius
        antialiasing: true
        color: {
            switch (control.variant) {
            case "primary":
                return control.down ? Theme.primaryPressed : control.hovered ? Theme.primaryHover : Theme.primary
            case "danger":
                return control.down ? Qt.darker(Theme.dangerHover, 1.08) : control.hovered ? Theme.dangerHover : Theme.danger
            case "ghost":
                return control.down ? Theme.bgPressed : control.hovered || control.checked ? Theme.bgHover : "transparent"
            default:
                return Theme.bgElevated
            }
        }
        border.width: control.variant === "secondary" ? 1 : 0
        border.color: control.hovered ? Theme.borderControl : Theme.borderStrong

        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        // State overlay for the outlined variant (keeps the elevated surface colour).
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            visible: control.variant === "secondary"
            color: control.down ? Theme.bgPressed : control.hovered || control.checked ? Theme.bgHover : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }

        BFocusRing {
            visible: control.visualFocus
            targetRadius: Theme.radius
        }
    }

    opacity: enabled ? 1 : Theme.disabledOpacity
}
