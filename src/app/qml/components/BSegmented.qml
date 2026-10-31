pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BSegmented — a small set of mutually exclusive options with a sliding selection.
//
//   BSegmented {
//       model: [{ value: "light", text: qsTr("Jasny"), icon: "sun" },
//               { value: "dark", text: qsTr("Ciemny"), icon: "moon" }]
//       currentValue: Theme.mode
//       onActivated: (value) => Theme.mode = value
//   }
//
// model: array of { value, text, icon } (or plain strings: value = text). One tab stop;
// Left/Right/Home/End move the selection (activated(value) is emitted for user changes).
// Bind currentValue to your state and update the state in onActivated (the binding is kept).
// `fill: true` stretches segments to the control's width, as evenly as their labels allow: the
// narrow ones widen to one shared width, a wider label keeps its own (none is cut); when their
// natural widths do not fit, the icons go first (the labels stay), then the labels share and elide.
// `iconOnly: true` shows icons with the text as accessible name/tooltip.
// `controlled: true`: the selection only ever follows currentValue's binding — for a segment
// that may start an action instead of changing the state ("Other…" opening a picker that can
// be cancelled), so the selection never shows a value the state does not have.
T.Control {
    id: control

    property var model: []
    property var currentValue
    property bool fill: false
    property bool iconOnly: false
    property bool controlled: false
    property string size: "normal"   // "small" | "normal"
    property string accessibleName

    signal activated(var value)

    readonly property var items: {
        const out = []
        for (let i = 0; i < (model ? model.length : 0); ++i) {
            const m = model[i]
            out.push(typeof m === "object" ? m : { value: m, text: String(m), icon: "" })
        }
        return out
    }
    readonly property int currentIndex: {
        for (let i = 0; i < items.length; ++i)
            if (items[i].value === currentValue)
                return i
        return -1
    }
    /// The segments' natural widths (for `fill`; repeater.count: re-evaluated once they exist).
    readonly property var _naturalWidths: {
        const out = []
        for (let i = 0; i < repeater.count; ++i) {
            const segment = repeater.itemAt(i)
            out.push(segment ? segment.implicitWidth : 0)
        }
        return out
    }
    /// Their sum.
    readonly property real _implicitSum: _naturalWidths.reduce((sum, w) => sum + w, 0)
    /// Filling: the width every narrower segment widens to, so that Σ max(natural, width) is the
    /// available width (water-filling: the widest labels keep theirs, the rest share the room
    /// left); -1 when the natural widths do not fit.
    readonly property real _fillWidth: {
        const widths = _naturalWidths.slice().sort((a, b) => b - a)
        let room = availableWidth
        let left = widths.length
        if (left === 0 || _implicitSum > room)
            return -1
        for (let i = 0; i < widths.length && widths[i] > room / left; ++i) {
            room -= widths[i]
            --left
        }
        return room / left
    }
    /// The same with every segment's icon (whether it is shown or not).
    readonly property real _withIconsSum: {
        let sum = 0
        for (let i = 0; i < repeater.count; ++i) {
            const segment = repeater.itemAt(i)
            if (segment)
                sum += segment.withIconWidth
        }
        return sum
    }
    /// The icons are shown: always, except a filling control whose segments would not fit with
    /// them (a narrow phone) — the labels say the same.
    readonly property bool iconsShown: iconOnly || !fill || _withIconsSum <= availableWidth
    readonly property int visualHeight: size === "small" ? Theme.controlHeightSm : Theme.controlHeight
    readonly property int hitHeight: Math.max(visualHeight, Theme.touchUi ? Theme.touchTarget : 0)

    function select(index) {
        if (index < 0 || index >= items.length || index === currentIndex)
            return
        const value = items[index].value
        // Controlled use (`currentValue: model.x; onActivated: model.x = value`): the handler
        // updates the source and the binding follows — assigning here would break that
        // binding, and later changes of the source (another conflict choice, a reopened
        // dialog) would no longer show. Uncontrolled use: keep the selection ourselves.
        activated(value)
        if (!controlled && currentValue !== value)
            currentValue = value
    }

    implicitWidth: Math.max(implicitBackgroundWidth, row.implicitWidth + leftPadding + rightPadding)
    implicitHeight: hitHeight
    topInset: (hitHeight - visualHeight) / 2
    bottomInset: topInset
    padding: 3
    topPadding: topInset + 3
    bottomPadding: bottomInset + 3
    focusPolicy: Qt.StrongFocus
    hoverEnabled: !Theme.touchUi

    Accessible.role: Accessible.PageTabList
    Accessible.name: accessibleName

    Keys.onLeftPressed: select(Math.max(0, currentIndex - 1))
    Keys.onRightPressed: select(Math.min(items.length - 1, currentIndex + 1))
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Home) {
            select(0)
            event.accepted = true
        } else if (event.key === Qt.Key_End) {
            select(items.length - 1)
            event.accepted = true
        }
    }

    background: Rectangle {
        implicitHeight: control.visualHeight
        radius: Theme.radius
        color: Theme.bgSunken
        border.width: 1
        border.color: Theme.border
    }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        // Sliding thumb under the selected segment.
        Rectangle {
            id: thumb
            // repeater.count: re-evaluate once the segments exist (itemAt is not notifiable).
            readonly property Item target: repeater.count > 0 && control.currentIndex >= 0
                                           ? repeater.itemAt(control.currentIndex) : null
            visible: target !== null
            x: target ? target.x : 0
            width: target ? target.width : 0
            height: parent.height
            radius: Theme.radius - 2
            // Dark: the raised surface lightened by 12 % of the text colour (slate ≈ #424858,
            // ≈ 1.9:1 against the track instead of ≈ 1.36:1), so the choice reads at a glance as
            // light's white thumb does; from tokens, so every surface and accent follows.
            color: Theme.dark ? Qt.tint(Theme.bgElevated2, Theme.alpha(Theme.fg, 0.12)) : Theme.bgElevated
            border.width: 1
            // Dark: a state border too (≥ 3:1 against the track, WCAG 1.4.11).
            border.color: Theme.dark ? Theme.borderControl : Theme.border
            antialiasing: true
            Behavior on x { NumberAnimation { duration: Theme.duration; easing.type: Theme.easing } }
            Behavior on width { NumberAnimation { duration: Theme.duration; easing.type: Theme.easing } }

            BFocusRing {
                visible: control.visualFocus
                targetRadius: thumb.radius
            }
        }

        // The segments take the control's whole hit height (44 px on touch UIs: the insets and
        // the padding above and below the track belong to them), while the thumb keeps the
        // visual, padded rectangle.
        Row {
            id: row
            y: -control.topPadding
            height: control.height

            Repeater {
                id: repeater
                model: control.items
                delegate: T.AbstractButton {
                    id: segment
                    required property var modelData
                    required property int index
                    readonly property bool isCurrent: index === control.currentIndex

                    height: row.height
                    // Filling the width: every segment at least the shared width (_fillWidth), so
                    // the short labels line up evenly and a long one ("Cały notatnik") is not cut;
                    // when they do not fit, each its own width minus an equal share of the excess.
                    width: !control.fill || control.items.length === 0 ? implicitWidth
                           : control._implicitSum <= 0 ? control.availableWidth / control.items.length
                           : control._fillWidth >= 0 ? Math.max(implicitWidth, control._fillWidth)
                           : Math.max(0, implicitWidth + (control.availableWidth - control._implicitSum) / control.items.length)
                    // At least a touch target wide on touch UIs (icon-only and short labels).
                    implicitWidth: Math.max(Theme.touchUi ? Theme.touchTarget : 0,
                                            (segmentIcon.width > 0 ? iconRoom : 0)
                                            + (control.iconOnly ? 0 : segmentLabel.implicitWidth) + (control.iconOnly ? 16 : 24))
                    /// The icon and the space after it (0 without an icon), shown or not.
                    readonly property real iconRoom: (modelData.icon || "") === "" ? 0
                        : (control.size === "small" ? Theme.iconSizeSm : Theme.iconSize) + (control.iconOnly ? 0 : segmentRow.spacing)
                    readonly property real withIconWidth: Math.max(Theme.touchUi ? Theme.touchTarget : 0,
                                                                   iconRoom + (control.iconOnly ? 0 : segmentLabel.implicitWidth)
                                                                   + (control.iconOnly ? 16 : 24))
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: !Theme.touchUi
                    text: modelData.text
                    Accessible.role: Accessible.PageTab
                    Accessible.name: modelData.text
                    Accessible.checkable: true
                    Accessible.checked: isCurrent
                    onClicked: {
                        control.forceActiveFocus(Qt.MouseFocusReason)
                        control.select(index)
                    }

                    contentItem: Item {
                        Row {
                            id: segmentRow
                            anchors.centerIn: parent
                            spacing: 6
                            BIcon {
                                id: segmentIcon
                                anchors.verticalCenter: parent.verticalCenter
                                name: control.iconsShown ? (segment.modelData.icon || "") : ""
                                size: control.size === "small" ? Theme.iconSizeSm : Theme.iconSize
                                color: segment.isCurrent ? Theme.fg : Theme.fgMuted
                            }
                            Text {
                                id: segmentLabel
                                anchors.verticalCenter: parent.verticalCenter
                                visible: !control.iconOnly
                                text: segment.modelData.text
                                color: segment.isCurrent ? Theme.fg : segment.hovered ? Theme.fg : Theme.fgMuted
                                font.family: Theme.fontUi
                                font.pixelSize: control.size === "small" ? Theme.textSm : Theme.textControl
                                font.weight: segment.isCurrent ? Theme.weightSemiBold : Theme.weightMedium
                                elide: Text.ElideRight
                                width: Math.min(implicitWidth, Math.max(0, segment.width - 12 - (segmentIcon.width > 0 ? segmentIcon.width + 6 : 0)))
                            }
                        }
                    }
                    Loader {
                        active: control.iconOnly && segment.hovered && !Theme.touchUi
                        sourceComponent: BToolTip {
                            parent: segment
                            visible: true
                            text: segment.modelData.text
                        }
                    }
                }
            }
        }
    }

    opacity: enabled ? 1 : Theme.disabledOpacity
}
