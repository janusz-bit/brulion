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
//   hour moves on to minutes (autoSwitchToMinutes). Minutes snap to one minute: the labels
//   mark every five minutes, dragging anywhere on the face gives any minute.
// * Keyboard (the dial focused): ↑/↓ change the current value by one, PageUp/PageDown by five,
//   ←/→ switch between hours and minutes, Home/End jump to the first/last value.
// * The time is a string ("HH:mm") — no JavaScript Date. picked() comes before the property
//   changes, so a consumer binding (`time: model.time; onPicked: model.time = …`) stays intact.
T.Control {
    id: control

    property string time: "09:00"
    property string mode: "hour"   // "hour" | "minute"
    property bool autoSwitchToMinutes: true
    property int dialSize: Theme.touchUi ? 260 : 232
    /// The focusable clock face (for a dialog's initialFocusItem).
    property alias focusItem: faceFocus

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
    readonly property int labelSize: Math.max(28, Math.round(dialSize * 0.12))
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
    /// A tap or a drag on the face: the nearest number for hours, the angle for minutes.
    function pickAt(x, y) {
        const dx = x - center
        const dy = y - center
        if (dx * dx + dy * dy < 144)   // the hub is not a value
            return
        if (mode === "minute") {
            const fromTop = (Math.atan2(dy, dx) + Math.PI / 2 + 2 * Math.PI) % (2 * Math.PI)
            commitMinute(Math.round(fromTop / (2 * Math.PI) * 60) % 60)
            return
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
    }

    implicitWidth: Math.max(contentItem.implicitWidth + leftPadding + rightPadding, dialSize)
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    padding: 0

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Clock: %1").arg(displayTime)
    Accessible.description: qsTr("Up and down change the time; left and right switch between hours and minutes.")

    contentItem: ColumnLayout {
        spacing: Theme.space2

        // ---- Hours / minutes at the top (also switch the dial) -----------------------------
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 0

            T.AbstractButton {
                id: hourTab
                readonly property bool active: control.mode === "hour"
                implicitWidth: Math.max(60, hourLabel.implicitWidth + 20)
                implicitHeight: Math.max(46, hourLabel.implicitHeight + 10)
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
                implicitWidth: Math.max(60, minuteLabel.implicitWidth + 20)
                implicitHeight: Math.max(46, minuteLabel.implicitHeight + 10)
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

        // ---- The clock face ----------------------------------------------------------------
        FocusScope {
            id: faceFocus
            Layout.alignment: Qt.AlignHCenter
            width: control.dialSize
            height: control.dialSize
            activeFocusOnTab: true

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
                event.accepted = handled
            }

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.bgSunken
                border.width: 1
                border.color: Theme.borderSubtle
            }

            MouseArea {
                id: faceInput
                anchors.fill: parent
                hoverEnabled: !Theme.touchUi
                cursorShape: Qt.PointingHandCursor
                onPressed: (mouse) => {
                    faceFocus.forceActiveFocus(Qt.MouseFocusReason)
                    control.pickAt(mouse.x, mouse.y)
                }
                onPositionChanged: (mouse) => {
                    if (faceInput.pressed)
                        control.pickAt(mouse.x, mouse.y)
                }
                onReleased: {
                    if (control.mode === "hour" && control.autoSwitchToMinutes)
                        control.mode = "minute"
                }
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

            // Hours 00–11 outside, 12–23 inside.
            Repeater {
                model: 24
                delegate: T.AbstractButton {
                    id: hourNumber
                    required property int index
                    readonly property real angle: (index % 12) * Math.PI / 6 - Math.PI / 2
                    readonly property real radius: index < 12 ? control.outerRadius : control.innerRadius
                    readonly property bool selected: control.mode === "hour" && control.selectedHour === index
                    width: control.labelSize
                    height: control.labelSize
                    x: control.center + Math.cos(angle) * radius - width / 2
                    y: control.center + Math.sin(angle) * radius - height / 2
                    padding: 0
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: !Theme.touchUi
                    visible: control.mode === "hour"
                    text: control.pad(index)

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Hour %1").arg(control.pad(index))
                    Accessible.checkable: true
                    Accessible.checked: selected
                    onClicked: {
                        faceFocus.forceActiveFocus(Qt.MouseFocusReason)
                        control.commitHour(index)
                        if (control.autoSwitchToMinutes)
                            control.mode = "minute"
                    }

                    contentItem: Text {
                        text: hourNumber.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
                        font.weight: hourNumber.selected ? Theme.weightSemiBold : Theme.weightNormal
                        font.features: { "tnum": 1 }
                        color: Theme.fg
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: width / 2
                        color: hourNumber.hovered ? Theme.bgHover : "transparent"
                    }
                }
            }

            // Minutes: a label every five, the hand and the bubble give the exact minute.
            Repeater {
                model: 12
                delegate: T.AbstractButton {
                    id: minuteNumber
                    required property int index
                    readonly property int value: index * 5
                    readonly property real angle: index * Math.PI / 6 - Math.PI / 2
                    width: control.labelSize
                    height: control.labelSize
                    x: control.center + Math.cos(angle) * control.outerRadius - width / 2
                    y: control.center + Math.sin(angle) * control.outerRadius - height / 2
                    padding: 0
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: !Theme.touchUi
                    visible: control.mode === "minute"
                    text: control.pad(value)

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Minute %1").arg(control.pad(value))
                    onClicked: {
                        faceFocus.forceActiveFocus(Qt.MouseFocusReason)
                        control.commitMinute(value)
                    }

                    contentItem: Text {
                        text: minuteNumber.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
                        font.weight: Theme.weightNormal
                        font.features: { "tnum": 1 }
                        color: Theme.fg
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: width / 2
                        color: minuteNumber.hovered ? Theme.bgHover : "transparent"
                    }
                }
            }

            // The chosen value sits on top of its number (the exact minute may lie between labels).
            Rectangle {
                width: control.labelSize
                height: control.labelSize
                radius: width / 2
                color: Theme.primary
                x: control.selectionX - width / 2
                y: control.selectionY - height / 2
                Text {
                    anchors.centerIn: parent
                    text: control.mode === "hour" ? control.displayHour : control.displayMinute
                    font.family: Theme.fontUi
                    font.pixelSize: Math.round(control.labelSize * 0.45)
                    font.weight: Theme.weightSemiBold
                    font.features: { "tnum": 1 }
                    color: Theme.primaryFg
                }
            }

            BFocusRing {
                visible: faceFocus.activeFocus
                target: faceFocus
                targetRadius: faceFocus.width / 2
            }
        }
    }
}
