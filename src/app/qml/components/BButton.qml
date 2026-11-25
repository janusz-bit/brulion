pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// BButton — text button with optional icon.
//
//   BButton { text: qsTr("Save"); variant: "primary"; onClicked: save() }
//   BButton { text: qsTr("Delete"); variant: "danger"; iconName: "trash-2"; busy: deleting }
//
// variant: "primary" (the accent's solid fill; one per view) | "secondary" (default, outlined) | "ghost"
//          (toolbar/text button) | "danger" (destructive confirmation)
// size:    "small" | "normal" (default) | "large"
// iconName: Lucide icon before the text (after it with `iconRight`, e.g. "chevron-down")
// busy:    shows a spinner instead of the icon and swallows clicks (keeps its size)
// fill:    takes the width a layout gives it (Layout.fillWidth), the content centred — a lone
//          framed button of a stacked ShellSettingRow, like the segmented controls there
//          (`naturalWidth`: the width without it)
// On touch UIs the hit area is at least Theme.touchTarget high even for small buttons
// (the visual part stays compact, the rest is inset).
T.Button {
    id: control

    property string variant: "secondary"
    property string size: "normal"
    property string iconName: ""
    property bool iconRight: false
    property bool busy: false
    property bool fill: false
    property color textColor: {
        switch (variant) {
        case "primary": return Theme.buttonPrimaryFg
        case "danger": return Theme.dangerFg
        default: return Theme.fg
        }
    }

    readonly property int visualHeight: size === "small" ? Theme.controlHeightSm
                                      : size === "large" ? Theme.controlHeightLg
                                      : Theme.controlHeight
    readonly property int hitHeight: Math.max(visualHeight, Theme.touchUi ? Theme.touchTarget : 0)
    readonly property bool iconOnly: text === "" && (iconName !== "" || busy)
    /// The width it takes unfilled (its implicit width, which `fill` never changes): what
    /// ShellSettingRow measures to put it beside its text.
    readonly property real naturalWidth: implicitWidth
    /// The padding beside the text (2 less on the icon's side).
    readonly property int sidePadding: size === "small" ? 10 : size === "large" ? 18 : 14

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: hitHeight
    topInset: (hitHeight - visualHeight) / 2
    bottomInset: topInset
    // Icon-only: square visual, hit area at least a touch target wide (like BIconButton).
    leftInset: iconOnly ? topInset : 0
    rightInset: leftInset
    leftPadding: iconOnly ? leftInset + (visualHeight - iconItem.size) / 2 : sidePadding - (iconName !== "" && !iconRight ? 2 : 0)
    rightPadding: iconOnly ? leftPadding : sidePadding - (iconName !== "" && iconRight ? 2 : 0)
    topPadding: topInset
    bottomPadding: bottomInset

    Layout.fillWidth: fill
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
            x: Math.round(parent.width / 2) - Math.round(width / 2)
            y: Math.round(parent.height / 2) - Math.round(height / 2)
            spacing: control.size === "small" ? 5 : 7
            layoutDirection: control.iconRight ? Qt.RightToLeft : Qt.LeftToRight

            Item {
                width: iconItem.width
                height: iconItem.height
                visible: content.hasIcon
                y: Math.round(parent.height / 2) - Math.round(height / 2)

                BIcon {
                    id: iconItem
                    name: control.iconName
                    size: control.size === "small" ? Theme.iconSizeSm : Theme.iconSize
                    collapseWhenEmpty: !control.busy
                    color: control.textColor
                    opacity: control.busy ? 0 : 1
                }
                // While busy (made then: a button is seldom busy, and its spinner is a Shape with
                // two paths, an animator and a timer): the spinner in the icon's place, and a
                // MouseArea over the whole button that takes the clicks, with the busy cursor.
                Loader {
                    x: Math.round(parent.width / 2) - Math.round(width / 2)
                    y: Math.round(parent.height / 2) - Math.round(height / 2)
                    active: control.busy
                    sourceComponent: BSpinner {
                        size: iconItem.size
                        color: control.textColor
                        MouseArea {
                            parent: control
                            anchors.fill: parent
                            acceptedButtons: Qt.AllButtons
                            cursorShape: Qt.BusyCursor
                        }
                    }
                }
            }

            Text {
                id: label
                visible: control.text !== ""
                y: Math.round(parent.height / 2) - Math.round(height / 2)
                textFormat: Text.PlainText
                text: control.text
                font: control.font
                color: control.textColor
                elide: Text.ElideRight
                width: Math.min(implicitWidth, Math.max(0, control.availableWidth - (content.hasIcon ? iconItem.size + row.spacing : 0)))
            }
        }
    }

    background: Rectangle {
        id: backgroundRect
        implicitWidth: control.iconOnly ? control.visualHeight : 64
        implicitHeight: control.visualHeight
        radius: Theme.radius
        color: {
            switch (control.variant) {
            case "primary":
                return control.down ? Theme.buttonPrimaryPressed : control.hovered ? Theme.buttonPrimaryHover : Theme.buttonPrimary
            case "danger":
                return control.down ? Theme.dangerHover.darker(1.08) : control.hovered ? Theme.dangerHover : Theme.danger
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
            // (Its size bound, not anchored: no QQuickAnchors object for each of hundreds.)
            width: parent.width
            height: parent.height
            radius: backgroundRect.radius
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
    scale: control.down ? Theme.pressScale : 1
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutBack } }
}
