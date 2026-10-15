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
// `fill: true` stretches segments to the control's width (equal widths).
// `iconOnly: true` shows icons with the text as accessible name/tooltip.
T.Control {
    id: control

    property var model: []
    property var currentValue
    property bool fill: false
    property bool iconOnly: false
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
    /// Sum of the segments' natural widths (for `fill`).
    readonly property real _implicitSum: {
        let sum = 0
        for (let i = 0; i < repeater.count; ++i) {
            const segment = repeater.itemAt(i)
            if (segment)
                sum += segment.implicitWidth
        }
        return sum
    }
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
        if (currentValue !== value)
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
            color: Theme.dark ? Theme.bgElevated2 : Theme.bgElevated
            border.width: 1
            border.color: Theme.dark ? Theme.borderStrong : Theme.border
            antialiasing: true
            Behavior on x { NumberAnimation { duration: Theme.duration; easing.type: Theme.easing } }
            Behavior on width { NumberAnimation { duration: Theme.duration; easing.type: Theme.easing } }

            BFocusRing {
                visible: control.visualFocus
                targetRadius: thumb.radius
            }
        }

        Row {
            id: row
            height: parent.height

            Repeater {
                id: repeater
                model: control.items
                delegate: T.AbstractButton {
                    id: segment
                    required property var modelData
                    required property int index
                    readonly property bool isCurrent: index === control.currentIndex

                    height: row.height
                    // Filling the width: every segment gets its own width plus an equal share of
                    // what is left (or minus an equal share when they do not fit), so a long label
                    // ("Cały notatnik") is not cut while short ones have room to spare.
                    width: control.fill && control.items.length > 0
                           ? (control._implicitSum > 0
                              ? Math.max(0, implicitWidth + (control.availableWidth - control._implicitSum) / control.items.length)
                              : control.availableWidth / control.items.length)
                           : implicitWidth
                    // At least a touch target wide on touch UIs (icon-only and short labels).
                    implicitWidth: Math.max(Theme.touchUi ? Theme.touchTarget : 0,
                                            (segmentIcon.width > 0 ? segmentIcon.width + (control.iconOnly ? 0 : segmentRow.spacing) : 0)
                                            + (control.iconOnly ? 0 : segmentLabel.implicitWidth) + (control.iconOnly ? 16 : 24))
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
                                name: segment.modelData.icon || ""
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
