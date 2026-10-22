pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// BTimePicker — a round 24-hour clock for choosing a time by tapping or dragging, like the
// clock in Android's time picker; the fields at the top switch between hours and minutes.
//
//   BTimePicker {
//       time: "18:30"
//       onPicked: (value) => model.time = value      // "HH:mm"
//   }
//
// * 24-hour dial: the hours 00–11 sit on the outer ring, 12–23 on the inner one; picking an
//   hour moves on to minutes (autoSwitchToMinutes) when the finger or the mouse lets go.
//   Minutes snap to one minute: the labels mark every five minutes (a tap on one gives it),
//   dragging anywhere on the face gives any minute. The face takes every press itself — a drag
//   that starts on a number, or on the chosen value's bubble, moves the hand too (the numbers
//   are drawn, not buttons; screen readers still press them: Accessible.onPressAction).
// * Keyboard (the dial focused): ↑/↓ change the current value by one, PageUp/PageDown by five,
//   ←/→ switch between hours and minutes, Home/End jump to the first/last value. The focus ring
//   shows for the keyboard only (Tab, or keys used on the dial), not after a tap.
// * `horizontal`: the hours / minutes fields beside the dial (BTimePickerDialog in a window too
//   short for them above it, a phone in landscape); `dialSize` shrinks the dial to fit.
// * The time is a string ("HH:mm") — no JavaScript Date. picked() comes before the property
//   changes, so a consumer binding (`time: model.time; onPicked: model.time = …`) stays intact.
T.Control {
    id: control

    property string time: "09:00"
    property string mode: "hour"   // "hour" | "minute"
    property bool autoSwitchToMinutes: true
    property int dialSize: Theme.touchUi ? 260 : 232
    /// The hours / minutes fields beside the dial instead of above it.
    property bool horizontal: false
    /// The focusable clock face (for a dialog's initialFocusItem).
    property alias focusItem: faceFocus
    /// Height of the fields above the dial, with the gap to it (BTimePickerDialog's fitting).
    readonly property real fieldsHeight: fields.implicitHeight + Theme.space2

    readonly property int selectedHour: {
        const h = parseInt(time.slice(0, 2), 10)
        return isNaN(h) || h < 0 || h > 23 ? 9 : h
    }
    readonly property int selectedMinute: {
        const m = parseInt(time.slice(3, 5), 10)
        return isNaN(m) || m < 0 || m > 59 ? 0 : m
    }
    readonly property string displayHour: pad(selectedHour)
    readonly property string displayMinute: pad(selectedMinute)
    readonly property string displayTime: displayHour + ":" + displayMinute

    readonly property real center: dialSize / 2
    readonly property real outerRadius: center - Math.round(dialSize * 0.115)
    readonly property real innerRadius: outerRadius * 0.62
    /// A number's circle where the dial has room for it: two digits with room around them.
    readonly property int labelRoom: Math.max(Theme.textSm * 2, Math.round(dialSize * 0.12))
    /// The inner ring's circles: the twelve hours sit closer together there (24 px apart on a
    /// 196 px dial, 22 on the 180 px the dialog shrinks to), so they are as large as the outer
    /// ones only where they do not reach the next hour (1 px between them), and take at most
    /// half the room between the rings.
    readonly property int innerLabelSize: Math.min(labelRoom,
                                                   Math.floor(2 * innerRadius * Math.sin(Math.PI / 12)) - 1,
                                                   Math.floor(outerRadius - innerRadius - 0.5))
    /// A number's circle on the outer ring (and the chosen value's bubble there): never reaching
    /// the next number nor the inner ring's (1 px between them). Sized after the inner ones, so
    /// larger text (Theme.fontScale) on a small dial does not squeeze the inner hours.
    readonly property int labelSize: Math.min(labelRoom,
                                              Math.floor(2 * outerRadius * Math.sin(Math.PI / 12)) - 1,
                                              Math.floor(2 * (outerRadius - innerRadius) - innerLabelSize) - 1)
    /// The numbers' font: the small one, smaller only where two digits would not fit their
    /// circle (the inner ring's on the smallest dials, like Android's clock).
    readonly property int labelFontSize: Math.min(Theme.textSm, Math.floor(labelSize * 0.62))
    readonly property int innerLabelFontSize: Math.min(Theme.textSm, Math.floor(innerLabelSize * 0.62))
    /// The chosen value's bubble: the size of the number it sits on.
    readonly property int selectionSize: mode === "hour" && selectedHour >= 12 ? innerLabelSize : labelSize
    // The hand points at the chosen value: 0° is up, 90° right (QML rotation of a vertical bar).
    readonly property real selectionRotation: mode === "minute" ? selectedMinute * 6
                                                                 : (selectedHour % 12) * 30
    readonly property real selectionRadius: mode === "hour" && selectedHour >= 12 ? innerRadius
                                                                                  : outerRadius
    readonly property real selectionX: center + Math.cos((selectionRotation - 90) * Math.PI / 180)
                                                   * selectionRadius
    readonly property real selectionY: center + Math.sin((selectionRotation - 90) * Math.PI / 180)
                                                   * selectionRadius

    signal picked(string time)

    function pad(value) {
        return String(value).padStart(2, "0")
    }
    function wrap(value, modulo) {
        return ((value % modulo) + modulo) % modulo
    }
    /// Emit the value first, then keep it unless the consumer's binding already followed.
    function commitTime(hourValue, minuteValue) {
        const value = pad(wrap(hourValue, 24)) + ":" + pad(wrap(minuteValue, 60))
        if (value === control.time)
            return
        control.picked(value)
        if (control.time !== value)
            control.time = value
    }
    function commitHour(value) {
        commitTime(value, selectedMinute)
    }
    function commitMinute(value) {
        commitTime(selectedHour, value)
    }
    /// The value of the number drawn at (x, y) of the face — an hour (0–23) or a minute label (a
    /// multiple of five) — or -1 when no number is there.
    function labelAt(x, y) {
        const count = mode === "hour" ? 24 : 12
        for (let i = 0; i < count; ++i) {
            const inner = mode === "hour" && i >= 12
            const radius = inner ? innerRadius : outerRadius
            const r = (inner ? innerLabelSize : labelSize) / 2
            const angle = (i % 12) * Math.PI / 6 - Math.PI / 2
            const dx = x - center - Math.cos(angle) * radius
            const dy = y - center - Math.sin(angle) * radius
            if (dx * dx + dy * dy <= r * r)
                return mode === "hour" ? i : i * 5
        }
        return -1
    }
    /// A number pressed (a tap on it, a screen reader's press): that value; an hour moves on to
    /// the minutes.
    function pickLabel(value) {
        if (mode === "hour") {
            commitHour(value)
            if (autoSwitchToMinutes)
                mode = "minute"
        } else {
            commitMinute(value)
        }
    }
    /// A tap or a drag on the face: the nearest number for hours, the angle for minutes. False
    /// on the hub (no value there).
    function pickAt(x, y) {
        const dx = x - center
        const dy = y - center
        if (dx * dx + dy * dy < 144)   // the hub is not a value
            return false
        if (mode === "minute") {
            const fromTop = (Math.atan2(dy, dx) + Math.PI / 2 + 2 * Math.PI) % (2 * Math.PI)
            commitMinute(Math.round(fromTop / (2 * Math.PI) * 60) % 60)
            return true
        }
        let best = 0
        let bestDistance = Number.POSITIVE_INFINITY
        for (let value = 0; value < 24; ++value) {
            const radius = value < 12 ? outerRadius : innerRadius
            const angle = (value % 12) * Math.PI / 6 - Math.PI / 2
            const ddx = dx - Math.cos(angle) * radius
            const ddy = dy - Math.sin(angle) * radius
            const distance = ddx * ddx + ddy * ddy
            if (distance < bestDistance) {
                bestDistance = distance
                best = value
            }
        }
        commitHour(best)
        return true
    }

    implicitWidth: Math.max(contentItem.implicitWidth + leftPadding + rightPadding, dialSize)
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    padding: 0

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Clock: %1").arg(displayTime)
    Accessible.description: qsTr("Up and down change the time; left and right switch between hours and minutes.")

    contentItem: GridLayout {
        columns: control.horizontal ? 2 : 1
        rowSpacing: Theme.space2
        columnSpacing: Theme.space5

        // ---- Hours / minutes (also switch the dial), above the dial or beside it -----------
        // Both parts fill their column, kept at their own width: centred in it.
        ColumnLayout {
            id: fields
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.maximumWidth: implicitWidth
            spacing: Theme.space2

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 0

                T.AbstractButton {
                    id: hourTab
                    readonly property bool active: control.mode === "hour"
                    implicitWidth: hourLabel.implicitWidth + 2 * Theme.space3
                    implicitHeight: Math.max(Theme.controlHeightLg, hourLabel.implicitHeight + Theme.space2)
                    padding: 0
                    focusPolicy: Qt.StrongFocus
                    hoverEnabled: !Theme.touchUi
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Hours: %1").arg(control.displayHour)
                    Accessible.description: qsTr("Choose the hour")
                    onClicked: control.mode = "hour"

                    contentItem: Text {
                        id: hourLabel
                        text: control.displayHour
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.text3xl
                        font.weight: Theme.weightSemiBold
                        font.features: { "tnum": 1 }
                        color: hourTab.active ? Theme.fg : Theme.fgFaint
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Theme.radius
                        color: hourTab.active ? Theme.bgSelected
                             : hourTab.hovered ? Theme.bgHover : "transparent"
                        BFocusRing {
                            visible: hourTab.visualFocus
                            targetRadius: Theme.radius
                        }
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: ":"
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.text3xl
                    font.weight: Theme.weightSemiBold
                    font.features: { "tnum": 1 }
                    color: Theme.fgFaint
                }

                T.AbstractButton {
                    id: minuteTab
                    readonly property bool active: control.mode === "minute"
                    implicitWidth: minuteLabel.implicitWidth + 2 * Theme.space3
                    implicitHeight: Math.max(Theme.controlHeightLg, minuteLabel.implicitHeight + Theme.space2)
                    padding: 0
                    focusPolicy: Qt.StrongFocus
                    hoverEnabled: !Theme.touchUi
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Minutes: %1").arg(control.displayMinute)
                    Accessible.description: qsTr("Choose the minutes")
                    onClicked: control.mode = "minute"

                    contentItem: Text {
                        id: minuteLabel
                        text: control.displayMinute
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.text3xl
                        font.weight: Theme.weightSemiBold
                        font.features: { "tnum": 1 }
                        color: minuteTab.active ? Theme.fg : Theme.fgFaint
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Theme.radius
                        color: minuteTab.active ? Theme.bgSelected
                              : minuteTab.hovered ? Theme.bgHover : "transparent"
                        BFocusRing {
                            visible: minuteTab.visualFocus
                            targetRadius: Theme.radius
                        }
                    }
                }
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: control.mode === "hour" ? qsTr("Hour") : qsTr("Minutes")
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                font.weight: Theme.weightMedium
                color: Theme.fgMuted
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }

        // ---- The clock face ----------------------------------------------------------------
        // A Control for its visualFocus: the ring shows for keyboard focus only (focus that came
        // with Tab), or once keys were used on it — not when a dialog focuses it, and not after a
        // tap (visualFocus alone would stay: focus forced on the focused face keeps its reason).
        T.Control {
            id: faceFocus

            /// The keyboard has the dial: it came with Tab, or keys were used, since the last press.
            property bool keyboardActive: false
            readonly property bool focusRingShown: activeFocus && keyboardActive

            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.maximumWidth: control.dialSize
            Layout.preferredWidth: control.dialSize
            Layout.preferredHeight: control.dialSize
            implicitWidth: control.dialSize
            implicitHeight: control.dialSize
            padding: 0
            focusPolicy: Qt.StrongFocus
            onActiveFocusChanged: if (!activeFocus) keyboardActive = false
            onVisualFocusChanged: if (visualFocus) keyboardActive = true

            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Clock face, %1").arg(control.displayTime)
            Accessible.description: qsTr("Up and down change the time; left and right switch between hours and minutes.")

            Keys.onPressed: (event) => {
                let handled = true
                const five = event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown
                const step = event.key === Qt.Key_PageUp || event.key === Qt.Key_Up ? 1
                           : event.key === Qt.Key_PageDown || event.key === Qt.Key_Down ? -1 : 0
                switch (event.key) {
                case Qt.Key_Left:
                    control.mode = "hour"
                    break
                case Qt.Key_Right:
                    control.mode = "minute"
                    break
                case Qt.Key_Up:
                case Qt.Key_Down:
                case Qt.Key_PageUp:
                case Qt.Key_PageDown:
                    if (control.mode === "hour")
                        control.commitHour(control.selectedHour + step * (five ? 5 : 1))
                    else
                        control.commitMinute(control.selectedMinute + step * (five ? 5 : 1))
                    break
                case Qt.Key_Home:
                    if (control.mode === "hour")
                        control.commitHour(0)
                    else
                        control.commitMinute(0)
                    break
                case Qt.Key_End:
                    if (control.mode === "hour")
                        control.commitHour(23)
                    else
                        control.commitMinute(59)
                    break
                default:
                    handled = false
                }
                if (handled)
                    faceFocus.keyboardActive = true
                event.accepted = handled
            }

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.bgSunken
                border.width: 1
                border.color: Theme.borderSubtle
            }

            // Every press and drag on the face, numbers included (they are drawn on top but take
            // no input: a drag that starts on one — or on the chosen value's bubble — moves the
            // hand). preventStealing: a scrolling dialog never takes the drag over.
            MouseArea {
                id: faceInput

                /// The number under the mouse (hover highlight), -1 for none.
                property int hoverValue: -1
                /// The press picked a value (not the hub): letting go moves on to the minutes.
                property bool valuePicked: false
                /// The number the press landed on (-1: none) and where: it stays that number
                /// until the pointer moves farther than a drag — a finger that shakes a pixel or
                /// two while tapping "45" still gives 45, not the 46 of its angle.
                property int pressedLabel: -1
                property point pressPoint

                anchors.fill: parent
                hoverEnabled: !Theme.touchUi
                preventStealing: true
                cursorShape: Qt.PointingHandCursor
                onPressed: (mouse) => {
                    faceFocus.keyboardActive = false
                    faceFocus.forceActiveFocus(Qt.MouseFocusReason)
                    faceInput.hoverValue = -1
                    // A number under the finger is that value (a tap on "45" is 45, even a little
                    // off its centre); elsewhere the nearest hour or the minute of the angle.
                    const label = control.labelAt(mouse.x, mouse.y)
                    faceInput.pressedLabel = label
                    faceInput.pressPoint = Qt.point(mouse.x, mouse.y)
                    if (label >= 0) {
                        if (control.mode === "hour")
                            control.commitHour(label)
                        else
                            control.commitMinute(label)
                        faceInput.valuePicked = true
                    } else {
                        faceInput.valuePicked = control.pickAt(mouse.x, mouse.y)
                    }
                }
                onPositionChanged: (mouse) => {
                    if (!faceInput.pressed) {
                        faceInput.hoverValue = control.labelAt(mouse.x, mouse.y)
                        return
                    }
                    if (faceInput.pressedLabel >= 0) {
                        const dx = mouse.x - faceInput.pressPoint.x
                        const dy = mouse.y - faceInput.pressPoint.y
                        const slop = Application.styleHints.startDragDistance
                        if (dx * dx + dy * dy <= slop * slop)
                            return
                        faceInput.pressedLabel = -1 // a drag: the hand follows the pointer now
                    }
                    if (control.pickAt(mouse.x, mouse.y))
                        faceInput.valuePicked = true
                }
                onReleased: {
                    if (faceInput.valuePicked && control.mode === "hour" && control.autoSwitchToMinutes)
                        control.mode = "minute"
                    faceInput.valuePicked = false
                }
                onCanceled: faceInput.valuePicked = false
                onExited: faceInput.hoverValue = -1
            }

            // Minute ticks, a little thicker every five minutes.
            Repeater {
                model: 60
                delegate: Rectangle {
                    required property int index
                    readonly property bool major: index % 5 === 0
                    width: major ? 6 : 3
                    height: major ? 2 : 1
                    radius: height / 2
                    color: Theme.borderStrong
                    x: control.center
                       + Math.cos((index * 6 - 90) * Math.PI / 180) * (control.center - 8) - width / 2
                    y: control.center
                       + Math.sin((index * 6 - 90) * Math.PI / 180) * (control.center - 8) - height / 2
                    rotation: index * 6 - 90
                }
            }

            // The hand under the numbers.
            Rectangle {
                x: control.center - width / 2
                y: control.center - height
                width: 2
                height: control.selectionRadius
                radius: 1
                color: Theme.primary
                transformOrigin: Item.Bottom
                rotation: control.selectionRotation
            }
            Rectangle {
                anchors.centerIn: parent
                width: 6
                height: 6
                radius: 3
                color: Theme.primary
            }

            // Hours 00–11 outside, 12–23 inside (drawn only: the face's MouseArea picks them).
            Repeater {
                model: 24
                delegate: Item {
                    id: hourNumber
                    required property int index
                    readonly property real angle: (index % 12) * Math.PI / 6 - Math.PI / 2
                    readonly property real radius: index < 12 ? control.outerRadius : control.innerRadius
                    readonly property bool selected: control.mode === "hour" && control.selectedHour === index
                    width: index < 12 ? control.labelSize : control.innerLabelSize
                    height: width
                    x: control.center + Math.cos(angle) * radius - width / 2
                    y: control.center + Math.sin(angle) * radius - height / 2
                    visible: control.mode === "hour"

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Hour %1").arg(control.pad(index))
                    Accessible.checkable: true
                    Accessible.checked: selected
                    Accessible.onPressAction: control.pickLabel(index)

                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: faceInput.hoverValue === hourNumber.index ? Theme.bgHover : "transparent"
                    }
                    Text {
                        anchors.centerIn: parent
                        text: control.pad(hourNumber.index)
                        font.family: Theme.fontUi
                        font.pixelSize: hourNumber.index < 12 ? control.labelFontSize : control.innerLabelFontSize
                        font.weight: hourNumber.selected ? Theme.weightSemiBold : Theme.weightNormal
                        font.features: { "tnum": 1 }
                        color: Theme.fg
                    }
                }
            }

            // Minutes: a label every five, the hand and the bubble give the exact minute.
            Repeater {
                model: 12
                delegate: Item {
                    id: minuteNumber
                    required property int index
                    readonly property int value: index * 5
                    readonly property real angle: index * Math.PI / 6 - Math.PI / 2
                    width: control.labelSize
                    height: control.labelSize
                    x: control.center + Math.cos(angle) * control.outerRadius - width / 2
                    y: control.center + Math.sin(angle) * control.outerRadius - height / 2
                    visible: control.mode === "minute"

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Minute %1").arg(control.pad(value))
                    Accessible.onPressAction: control.pickLabel(minuteNumber.value)

                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: faceInput.hoverValue === minuteNumber.value ? Theme.bgHover : "transparent"
                    }
                    Text {
                        anchors.centerIn: parent
                        text: control.pad(minuteNumber.value)
                        font.family: Theme.fontUi
                        font.pixelSize: control.labelFontSize
                        font.weight: Theme.weightNormal
                        font.features: { "tnum": 1 }
                        color: Theme.fg
                    }
                }
            }

            // The chosen value sits on top of its number (the exact minute may lie between labels).
            Rectangle {
                width: control.selectionSize
                height: control.selectionSize
                radius: width / 2
                color: Theme.primary
                x: control.selectionX - width / 2
                y: control.selectionY - height / 2
                Text {
                    anchors.centerIn: parent
                    text: control.mode === "hour" ? control.displayHour : control.displayMinute
                    font.family: Theme.fontUi
                    font.pixelSize: Math.min(Math.round(control.labelSize * 0.45), Math.floor(control.selectionSize * 0.6))
                    font.weight: Theme.weightSemiBold
                    font.features: { "tnum": 1 }
                    color: Theme.primaryFg
                }
            }

            BFocusRing {
                objectName: "clockFocusRing" // (the editor's self-test)
                visible: faceFocus.focusRingShown
                target: faceFocus
                targetRadius: faceFocus.width / 2
            }
        }
    }
}
