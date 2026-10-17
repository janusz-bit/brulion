pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BChip — compact pill for tags, dates, reminders and filters.
//
//   BChip { variant: "reminder"; text: "jutro 09:00" }
//   BChip { variant: "tag"; text: "projekt"; removable: true; onRemoveClicked: removeTag() }
//   BChip { text: qsTr("za 10 min"); interactive: true; selected: picked; onClicked: pick() }
//
// variant:  "tag" | "due" | "overdue" | "scheduled" | "reminder" | "alarm" | "recurrence" |
//           "done" | "neutral" (default). Each has its colours and default icon
//           (Theme.chip(variant)); `iconName` overrides ("" for none: set showIcon: false).
// interactive: hover/press feedback, focusable, emits clicked(); `selected` = filter state
// removable:   an × button (signal removeClicked(); also Delete/Backspace when focused)
// size:        "small" (inline in text) | "normal"
T.AbstractButton {
    id: control

    property string variant: "neutral"
    property string iconName: colors.icon
    property bool showIcon: true
    property bool interactive: false
    property bool selected: false
    property bool removable: false
    property string size: "normal"

    signal removeClicked()

    readonly property var colors: Theme.chip(variant)
    readonly property bool outlined: variant === "neutral" && interactive
    readonly property color fgColor: selected && variant === "neutral" ? Theme.primaryFg : colors.fg
    readonly property int visualHeight: size === "small" ? (Theme.touchUi ? 24 : 20) : (Theme.touchUi ? 32 : 26)
    readonly property int hitHeight: interactive || removable ? Math.max(visualHeight, Theme.touchUi ? Theme.touchTarget : 0) : visualHeight

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: hitHeight
    topInset: (hitHeight - visualHeight) / 2
    bottomInset: topInset
    topPadding: topInset
    bottomPadding: bottomInset
    leftPadding: size === "small" ? 7 : 10
    rightPadding: removable ? (Theme.touchUi ? 0 : 3) : (size === "small" ? 8 : 11)

    // A static chip must not swallow clicks meant for the row or card it sits in: disabled
    // items take no pointer events (and only disabled *interactive* chips are dimmed below).
    enabled: interactive || removable
    focusPolicy: interactive || removable ? Qt.StrongFocus : Qt.NoFocus
    hoverEnabled: interactive && !Theme.touchUi
    font.family: Theme.fontUi
    font.pixelSize: size === "small" ? Theme.textXs : Theme.textSm
    font.weight: Theme.weightMedium

    Accessible.role: interactive ? Accessible.Button : Accessible.StaticText
    Accessible.name: text
    Accessible.checked: selected

    Keys.onPressed: (event) => {
        if (control.removable && (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)) {
            control.removeClicked()
            event.accepted = true
        } else if (control.interactive && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            control.click()
            event.accepted = true
        }
    }

    // Icon, text and × placed with bindings, like a Row (spacing between the parts that have a
    // size), not by a Row: a Row's size arrives with its next polish, so a chip whose text changed
    // (a list row reused for another task) was too narrow for a moment and the chips around it
    // wrapped onto another line after the list had placed the row (docs/UI.md "Scrolling
    // performance").
    contentItem: Item {
        id: chipContent
        readonly property real spacing: control.size === "small" ? 4 : 5
        readonly property bool iconShown: chipIcon.width > 0 && chipIcon.height > 0
        readonly property bool labelShown: chipLabel.width > 0 && chipLabel.height > 0
        readonly property real labelX: iconShown ? chipIcon.width + spacing : 0
        readonly property real removeX: labelShown ? labelX + chipLabel.width + spacing : labelX
        implicitWidth: control.removable ? removeX + removeButton.width
                       : labelShown ? labelX + chipLabel.width
                       : iconShown ? chipIcon.width : 0
        implicitHeight: Math.max(iconShown ? chipIcon.height : 0, labelShown ? chipLabel.height : 0,
                                 control.removable ? removeButton.height : 0)

        BIcon {
            id: chipIcon
            anchors.verticalCenter: parent.verticalCenter
            name: control.showIcon ? control.iconName : ""
            size: control.size === "small" ? 12 : Theme.iconSizeSm
            color: control.fgColor
        }
        Text {
            id: chipLabel
            x: chipContent.labelX
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: control.font
            color: control.fgColor
            elide: Text.ElideRight
            width: Math.min(implicitWidth, 280)
        }
        T.AbstractButton {
            id: removeButton
            x: chipContent.removeX
            visible: control.removable
            anchors.verticalCenter: parent.verticalCenter
            // The × circle stays small; on touch UIs the hit area is a full touch target
            // (taller than the chip's visual part, which is fine: it is inside the chip's
            // own touch-sized hit area).
            readonly property real circle: control.visualHeight - 6
            width: Theme.touchUi ? Theme.touchTarget : circle
            height: Theme.touchUi ? control.hitHeight : circle
            focusPolicy: Qt.NoFocus
            hoverEnabled: !Theme.touchUi
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Remove %1").arg(control.text)
            onClicked: control.removeClicked()
            background: Item {}
            contentItem: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: removeButton.circle
                    height: removeButton.circle
                    radius: width / 2
                    color: removeButton.hovered || removeButton.down
                           ? Qt.rgba(control.fgColor.r, control.fgColor.g, control.fgColor.b, 0.16) : "transparent"
                }
                BIcon {
                    anchors.centerIn: parent
                    name: "x"
                    size: control.size === "small" ? 10 : 12
                    color: control.fgColor
                }
            }
        }
    }

    background: Rectangle {
        implicitHeight: control.visualHeight
        radius: height / 2
        antialiasing: true
        color: control.selected && control.variant === "neutral" ? Theme.primary
             : control.outlined ? Theme.bgElevated
             : control.colors.bg
        border.width: control.outlined && !control.selected ? 1 : (control.selected ? 1.5 : 0)
        border.color: control.selected ? (control.variant === "neutral" ? Theme.primary : control.colors.fg)
                    : control.hovered ? Theme.borderControl : Theme.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            visible: control.interactive
            color: control.down ? Theme.bgPressed : control.hovered && !control.selected ? Theme.bgHover : "transparent"
        }
        BFocusRing {
            visible: control.visualFocus
            targetRadius: parent.radius
        }
    }

    opacity: enabled || !(interactive || removable) ? 1 : Theme.disabledOpacity
}
