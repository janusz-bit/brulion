pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CalendarWeekGrid — the week view: 7 day columns of a CalendarModel in "week" mode with a row
// of all-day entries (due / scheduled dates without a time) and 24 hour rows where timed entries
// (reminders, alarms, tasks with a time) sit at their time (drawn 45 minutes long); entries
// starting within 45 minutes of each other share the width (cascade on narrow columns). A red line marks the current time in today's column. The hours
// scroll to the first entry of the week (or 7:00).
//
//   CalendarWeekGrid { calendar: cal; onDayTapped: (date) => …; onItemOpened: (entry) => … }
//
// Keyboard as in the month grid (arrows, PageUp/PageDown, Home/End, Enter); touch: swipe for
// the previous / next week.
FocusScope {
    id: root

    required property CalendarModel calendar

    readonly property real gutter: Theme.touchUi ? 44 : 50
    readonly property real columnWidth: Math.max(1, (width - gutter) / 7)
    readonly property real hourHeight: Theme.touchUi ? 44 : 40
    readonly property real headerHeight: Theme.touchUi ? 52 : 46
    readonly property int pillHeight: Theme.touchUi ? 20 : 18
    readonly property bool narrowColumns: columnWidth < 80
    /// Rows of the all-day strip (0–3). (`calendar.revision` read for real: the model's methods are
    /// not tracked, and a bare `void calendar.revision` is compiled away with the dependency.)
    readonly property int allDayRows: {
        if (calendar.revision < 0)
            return 0
        let rows = 0
        for (let i = 0; i < calendar.rowCount(); ++i) {
            const items = calendar.data(calendar.index(i, 0), CalendarModel.ItemsRole) || []
            rows = Math.max(rows, items.filter(e => e.minutes < 0).length)
        }
        return Math.min(rows, 3)
    }
    readonly property real allDayHeight: allDayRows > 0 ? allDayRows * (pillHeight + 2) + 6 : 0
    property string nowSpec: DatesHelper.now()

    signal dayTapped(string date)
    signal dayOpened(string date)
    signal itemOpened(var entry)

    /// The keyboard was used last (the focus ring shows only then).
    property bool keyboardActive: false

    activeFocusOnTab: true
    onActiveFocusChanged: if (!activeFocus) keyboardActive = false
    Accessible.role: Accessible.Table
    Accessible.name: calendar.title

    function scrollToFirstEntry() {
        const first = calendar.itemCount > 0 ? calendar.firstHour : 7
        hours.contentY = Math.max(0, Math.min(hours.contentHeight - hours.height, (first - 0.6) * hourHeight))
    }
    Connections {
        target: root.calendar
        function onRangeChanged() { Qt.callLater(root.scrollToFirstEntry) }
    }
    Component.onCompleted: Qt.callLater(scrollToFirstEntry)
    Timer {
        interval: 60 * 1000
        running: root.visible
        repeat: true
        onTriggered: root.nowSpec = DatesHelper.now()
    }

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Left: calendar.moveAnchor(-1); break
        case Qt.Key_Right: calendar.moveAnchor(1); break
        case Qt.Key_Up: calendar.moveAnchor(-7); break
        case Qt.Key_Down: calendar.moveAnchor(7); break
        case Qt.Key_PageUp: calendar.previous(); break
        case Qt.Key_PageDown: calendar.next(); break
        case Qt.Key_Home: calendar.anchorDate = calendar.rangeStart; break
        case Qt.Key_End: calendar.anchorDate = calendar.rangeEnd; break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (event.modifiers & Qt.ControlModifier)
                root.dayOpened(calendar.anchorDate)
            else
                root.dayTapped(calendar.anchorDate)
            break
        default:
            return
        }
        keyboardActive = true
        event.accepted = true
    }

    /// Timed entries of a day with their lane: [{ entry, lane, lanes }].
    function layoutTimed(items) {
        const timed = (items || []).filter(e => e.minutes >= 0)
        const out = []
        let group = []
        const flush = () => {
            for (let k = 0; k < group.length; ++k)
                out.push({ entry: group[k], lane: k, lanes: group.length })
            group = []
        }
        for (const e of timed) {
            if (group.length > 0 && e.minutes - group[0].minutes >= 45)
                flush()
            group.push(e)
        }
        flush()
        return out
    }

    // ---- Day headers + all-day strip --------------------------------------------------------
    Item {
        id: top
        width: parent.width
        height: root.headerHeight + root.allDayHeight

        Repeater {
            model: root.calendar
            delegate: Item {
                id: dayHead
                required property int index
                required property string date
                required property int day
                required property int dayOfWeek
                required property bool isToday
                required property bool isSelected
                required property bool hasJournal
                required property var items
                required property string longLabel
                readonly property var allDay: items.filter(e => e.minutes < 0)

                x: root.gutter + index * root.columnWidth
                width: root.columnWidth
                height: top.height

                Rectangle {
                    anchors.fill: parent
                    color: dayHead.isSelected ? Theme.bgSelected : "transparent"
                }
                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 4
                    spacing: 1
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: DatesHelper.weekdayNames("short")[dayHead.dayOfWeek - 1]
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textXs
                        font.weight: Theme.weightSemiBold
                        font.capitalization: Font.AllUppercase
                        color: dayHead.isToday ? Theme.accentText : Theme.fgMuted
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Theme.touchUi ? 28 : 24
                        height: width
                        radius: width / 2
                        color: dayHead.isToday ? Theme.accentSolid : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: dayHead.day
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textMd
                            font.weight: Theme.weightSemiBold
                            color: dayHead.isToday ? Theme.fgOnAccent : Theme.fg
                        }
                    }
                }
                BIcon {
                    visible: dayHead.hasJournal && !root.narrowColumns
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    y: 6
                    name: "book-open"
                    size: 12
                    color: Theme.fgFaint
                }
                // All-day entries.
                Column {
                    x: 2
                    y: root.headerHeight + 2
                    width: parent.width - 5
                    spacing: 2
                    Repeater {
                        model: Math.min(dayHead.allDay.length, root.allDayRows)
                        delegate: Rectangle {
                            id: allDayPill
                            required property int index
                            readonly property bool more: index === root.allDayRows - 1 && dayHead.allDay.length > root.allDayRows
                            readonly property var entry: dayHead.allDay[index]
                            readonly property var colors: Theme.chip(more ? "neutral" : entry.variant)
                            width: parent.width
                            height: root.pillHeight
                            radius: Theme.radiusSm
                            color: colors.bg
                            opacity: !more && entry.closed ? 0.75 : 1
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 4
                                anchors.rightMargin: 2
                                verticalAlignment: Text.AlignVCenter
                                text: allDayPill.more ? qsTr("+%n more", "", dayHead.allDay.length - root.allDayRows + 1)
                                                      : allDayPill.entry.title
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textXs
                                font.strikeout: !allDayPill.more && allDayPill.entry.closed
                                color: allDayPill.colors.fg
                                elide: Text.ElideRight
                            }
                            TapHandler {
                                // Exclusive: the day header's handler must not also react.
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: {
                                    root.calendar.anchorDate = dayHead.date
                                    if (allDayPill.more || Theme.touchUi)
                                        root.dayTapped(dayHead.date)
                                    else
                                        root.itemOpened(allDayPill.entry)
                                }
                            }
                        }
                    }
                }
                TapHandler {
                    onTapped: {
                        root.forceActiveFocus(Qt.MouseFocusReason)
                            root.keyboardActive = false
                        root.calendar.anchorDate = dayHead.date
                        root.dayTapped(dayHead.date)
                    }
                    onDoubleTapped: root.dayOpened(dayHead.date)
                }
                Accessible.role: Accessible.ColumnHeader
                Accessible.name: longLabel
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
        Text {
            visible: root.allDayRows > 0
            x: 4
            y: root.headerHeight + 3
            width: root.gutter - 8
            text: qsTr("all day")
            font.family: Theme.fontUi
            font.pixelSize: Theme.textXs
            color: Theme.fgFaint
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignRight
        }
    }

    // ---- Hours ---------------------------------------------------------------------------------
    Flickable {
        id: hours
        TouchFlick.enabled: true
        anchors.top: top.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        contentWidth: width
        contentHeight: 24 * root.hourHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Item {
            id: slider
            width: hours.width
            height: hours.contentHeight
            x: swipe.active ? swipe.translation.x : 0
            Behavior on x {
                enabled: !swipe.active
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            // Hour lines and labels.
            Repeater {
                model: 24
                delegate: Item {
                    id: hourRow
                    required property int index
                    y: index * root.hourHeight
                    width: slider.width
                    height: root.hourHeight
                    Rectangle {
                        x: root.gutter
                        width: parent.width - root.gutter
                        height: 1
                        color: Theme.borderSubtle
                    }
                    Text {
                        visible: hourRow.index > 0
                        x: 0
                        width: root.gutter - 8
                        y: -height / 2
                        horizontalAlignment: Text.AlignRight
                        text: (hourRow.index < 10 ? "0" : "") + hourRow.index + ":00"
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textXs
                        font.features: { "tnum": 1 }
                        color: Theme.fgFaint
                    }
                }
            }

            // Day columns with their timed entries.
            Repeater {
                model: root.calendar
                delegate: Item {
                    id: dayColumn
                    required property int index
                    required property string date
                    required property bool isToday
                    required property bool isSelected
                    required property bool isWeekend
                    required property var items
                    readonly property var timed: root.layoutTimed(items)

                    x: root.gutter + index * root.columnWidth
                    width: root.columnWidth
                    height: slider.height

                    Rectangle {
                        anchors.fill: parent
                        color: dayColumn.isSelected ? Theme.alpha(Theme.bgSelected, Theme.bgSelected.a * 0.5)
                             : dayColumn.isWeekend ? Theme.alpha(Theme.bgSunken, 0.45) : "transparent"
                    }
                    Rectangle {
                        width: 1
                        height: parent.height
                        color: Theme.borderSubtle
                    }
                    TapHandler {
                        onTapped: {
                            root.forceActiveFocus(Qt.MouseFocusReason)
                            root.keyboardActive = false
                            root.calendar.anchorDate = dayColumn.date
                            root.dayTapped(dayColumn.date)
                        }
                    }

                    Repeater {
                        model: dayColumn.timed
                        delegate: Rectangle {
                            id: event
                            required property var modelData
                            readonly property var entry: modelData.entry
                            readonly property var colors: Theme.chip(entry.variant)
                            // Wide columns: side by side; narrow ones (phones): cascaded, so
                            // every time stays readable.
                            readonly property real laneWidth: (dayColumn.width - 4) / modelData.lanes
                            x: root.narrowColumns ? 2 + modelData.lane * 6 : 2 + modelData.lane * laneWidth
                            y: entry.minutes / 60 * root.hourHeight + 1 + (root.narrowColumns ? modelData.lane * 10 : 0)
                            width: root.narrowColumns ? dayColumn.width - 4 - modelData.lane * 6 : laneWidth - 2
                            z: modelData.lane
                            height: Math.max(2 * root.pillHeight - 2, root.hourHeight * 0.75 - 2)
                            radius: Theme.radiusSm
                            color: colors.bg
                            border.width: 1
                            border.color: Theme.alpha(colors.fg, 0.25)
                            opacity: entry.closed ? 0.75 : 1
                            clip: true
                            Accessible.role: Accessible.Button
                            Accessible.name: entry.time + ", " + entry.title

                            Column {
                                x: 4
                                y: 2
                                width: parent.width - 6
                                Text {
                                    width: parent.width
                                    text: (event.entry.variant === "alarm" ? "⏰ " : "") + event.entry.time
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.textXs
                                    font.weight: Theme.weightSemiBold
                                    font.features: { "tnum": 1 }
                                    color: event.colors.fg
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    visible: event.height > 2 * root.pillHeight - 6
                                    text: event.entry.title
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.textXs
                                    font.strikeout: event.entry.closed
                                    color: event.colors.fg
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }
                            }
                            TapHandler {
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: {
                                    root.calendar.anchorDate = dayColumn.date
                                    if (Theme.touchUi)
                                        root.dayTapped(dayColumn.date)
                                    else
                                        root.itemOpened(event.entry)
                                }
                            }
                        }
                    }

                    // Now.
                    Rectangle {
                        readonly property int minutes: {
                            const t = root.nowSpec.slice(11, 16).split(":")
                            return t.length === 2 ? parseInt(t[0]) * 60 + parseInt(t[1]) : -1
                        }
                        visible: dayColumn.isToday && minutes >= 0
                        y: minutes / 60 * root.hourHeight - 1
                        width: parent.width
                        height: 2
                        color: Theme.accent
                        Rectangle {
                            x: -4
                            y: -3
                            width: 8
                            height: 8
                            radius: 4
                            color: Theme.accent
                        }
                    }
                }
            }
        }

        DragHandler {
            id: swipe
            target: null
            enabled: Theme.touchUi
            xAxis.enabled: true
            yAxis.enabled: false
            onActiveChanged: {
                if (active)
                    return
                const dx = translation.x
                if (Math.abs(dx) > Math.min(120, root.width * 0.18) || Math.abs(centroid.velocity.x) > 700) {
                    if (dx < 0)
                        root.calendar.next()
                    else
                        root.calendar.previous()
                    slider.x = dx < 0 ? root.width * 0.25 : -root.width * 0.25
                    slider.x = Qt.binding(() => swipe.active ? swipe.translation.x : 0)
                }
            }
        }
    }

    BFocusRing {
        visible: root.activeFocus && root.keyboardActive
        inset: true
        targetRadius: 2
    }
}
