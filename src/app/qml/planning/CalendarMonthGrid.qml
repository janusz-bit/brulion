pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CalendarMonthGrid — the month view: 6 weeks × 7 days of a CalendarModel (weekday names from
// DatesHelper, starting on the model's first weekday). Wide cells list the day's entries as
// rows without a fill — a dot in the kind's colour, "09:00 Spotkanie", "+3 więcej" (a click
// opens the entry on mouse UIs, the row under the pointer highlighted); narrow cells (phones)
// show coloured dots per kind. Today has a red disc, the selected day a tinted cell, days of
// other months are muted, a daily note shows a small book.
//
//   CalendarMonthGrid { calendar: cal; onDayTapped: (date) => …; onItemOpened: (entry) => … }
//
// Keyboard (when focused): arrows move the selected day (±1 / ±7), PageUp/PageDown change the
// month, Home/End go to the start / end of the week, Enter emits dayTapped (the view opens the
// day), Ctrl+Enter dayOpened (the daily note). Touch: horizontal swipe = previous / next month.
FocusScope {
    id: root

    required property CalendarModel calendar
    /// The horizontal swipe is on (touch UIs); the view turns it off under a modal panel (the
    /// shell's drawer), whose own swipe the handler would take.
    property bool swipeEnabled: true

    readonly property real headerHeight: Theme.touchUi ? 30 : 26
    readonly property real cellWidth: width / 7
    readonly property real cellHeight: Math.max(1, (height - headerHeight) / 6)
    /// Dots instead of entry rows (narrow cells).
    readonly property bool dots: cellWidth < 96
    readonly property int pillHeight: Theme.touchUi ? 20 : 18
    /// The entry rows start 4 px under the day number's disc (the 4-px grid).
    readonly property int pillsTop: Theme.touchUi ? 36 : 32
    readonly property int maxPills: Math.max(0, Math.floor((cellHeight - pillsTop - 2) / (pillHeight + 2)))
    // (The locale passed for real: a method call is not tracked, and a bare `void DatesHelper.locale`
    // is compiled away with the dependency.)
    readonly property var weekdayNames: {
        const names = DatesHelper.weekdayNames(dots ? "short" : "short", DatesHelper.locale)
        const start = calendar.effectiveWeekStart
        const out = []
        for (let i = 0; i < 7; ++i)
            out.push(names[(start - 1 + i) % 7])
        return out
    }

    signal dayTapped(string date)
    signal dayOpened(string date)
    signal itemOpened(var entry)

    /// The keyboard was used last (the focus ring shows only then).
    property bool keyboardActive: false

    activeFocusOnTab: true
    onActiveFocusChanged: if (!activeFocus) keyboardActive = false
    Accessible.role: Accessible.Table
    Accessible.name: calendar.title

    Keys.onPressed: (event) => {
        const ctrl = event.modifiers & Qt.ControlModifier
        switch (event.key) {
        case Qt.Key_Left: calendar.moveAnchor(Qt.application.layoutDirection === Qt.RightToLeft ? 1 : -1); break
        case Qt.Key_Right: calendar.moveAnchor(Qt.application.layoutDirection === Qt.RightToLeft ? -1 : 1); break
        case Qt.Key_Up: calendar.moveAnchor(-7); break
        case Qt.Key_Down: calendar.moveAnchor(7); break
        case Qt.Key_PageUp: calendar.previous(); break
        case Qt.Key_PageDown: calendar.next(); break
        case Qt.Key_Home: calendar.moveAnchor(-((DatesHelper.dayOfWeek(calendar.anchorDate) - calendar.effectiveWeekStart + 7) % 7)); break
        case Qt.Key_End: calendar.moveAnchor(6 - ((DatesHelper.dayOfWeek(calendar.anchorDate) - calendar.effectiveWeekStart + 7) % 7)); break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (ctrl)
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

    // Weekday names.
    Row {
        width: parent.width
        height: root.headerHeight
        Repeater {
            model: root.weekdayNames
            delegate: Item {
                id: weekday
                required property string modelData
                required property int index
                width: root.cellWidth
                height: root.headerHeight
                readonly property int dayOfWeek: (root.calendar.effectiveWeekStart - 1 + index) % 7 + 1
                Text {
                    anchors.fill: parent
                    anchors.leftMargin: root.dots ? 0 : 8
                    horizontalAlignment: root.dots ? Text.AlignHCenter : Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    text: weekday.modelData
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textXs
                    font.weight: Theme.weightSemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.5
                    color: weekday.dayOfWeek >= 6 ? Theme.fgFaint : Theme.fgMuted
                }
            }
        }
    }

    // Cells. While swiping the grid follows the finger; a committed swipe slides the new month in.
    Item {
        id: cells
        y: root.headerHeight
        width: parent.width
        height: root.cellHeight * 6
        clip: true

        Item {
            id: slider
            width: parent.width
            height: parent.height
            x: swipe.active ? swipe.translation.x : 0
            Behavior on x {
                enabled: !swipe.active
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            Repeater {
                model: root.calendar
                delegate: Rectangle {
                    id: cell

                    required property int index
                    required property string date
                    required property int day
                    required property bool inMonth
                    required property bool isToday
                    required property bool isPast
                    required property bool isWeekend
                    required property bool isSelected
                    required property bool hasJournal
                    required property var items
                    required property int itemCount
                    required property var kinds
                    required property string longLabel

                    readonly property int column: index % 7
                    readonly property int row: Math.floor(index / 7)
                    readonly property bool overflow: items.length > root.maxPills
                    readonly property int shownPills: overflow ? Math.max(0, root.maxPills - 1) : items.length
                    /// The entry row under the mouse pointer (-1: none): its hover fill.
                    readonly property int hoveredRow: {
                        if (!rowHover.hovered)
                            return -1
                        const y = rowHover.point.position.y
                        const k = Math.floor((y - root.pillsTop) / (root.pillHeight + 2))
                        return y >= root.pillsTop && k >= 0 && k < shownPills ? k : -1
                    }

                    x: column * root.cellWidth
                    y: row * root.cellHeight
                    width: root.cellWidth
                    height: root.cellHeight
                    color: isSelected ? Theme.bgSelected
                         : !inMonth ? Theme.alpha(Theme.bgSunken, Theme.dark ? 0.55 : 0.7)
                         : isWeekend ? Theme.alpha(Theme.bgSunken, Theme.dark ? 0.3 : 0.4)
                         : "transparent"

                    Accessible.role: Accessible.Cell
                    Accessible.name: {
                        const parts = [longLabel]
                        if (isToday)
                            parts.push(qsTr("today"))
                        if (itemCount > 0)
                            parts.push(qsTr("%n entries", "", itemCount))
                        if (hasJournal)
                            parts.push(qsTr("journal"))
                        return parts.join(", ")
                    }
                    Accessible.selected: isSelected

                    // Grid lines (right and bottom edges).
                    Rectangle {
                        anchors.right: parent.right
                        width: 1
                        height: parent.height
                        color: Theme.borderSubtle
                        visible: cell.column < 6
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.borderSubtle
                    }

                    // Day number (today: a red disc).
                    Rectangle {
                        id: dayBadge
                        x: root.dots ? (parent.width - width) / 2 : 6
                        y: root.dots ? 4 : 4
                        width: Theme.touchUi ? 28 : 24
                        height: width
                        radius: width / 2
                        color: cell.isToday ? Theme.accentSolid : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: cell.day
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textSm
                            font.weight: cell.isToday || cell.isSelected ? Theme.weightBold : Theme.weightMedium
                            font.features: { "tnum": 1 }
                            color: cell.isToday ? Theme.fgOnAccent : cell.inMonth ? Theme.fg : Theme.fgFaint
                        }
                    }
                    BIcon {
                        visible: cell.hasJournal && !root.dots
                        anchors.verticalCenter: dayBadge.verticalCenter
                        x: dayBadge.x + dayBadge.width + 4
                        name: "book-open"
                        size: 12
                        color: Theme.fgFaint
                    }

                    // Entries as rows (wide cells): a dot in the kind's colour (an alarm clock or
                    // repeat icon for alarms and recurring entries), the time in semibold and the
                    // title — no fill, so the colour stays in the small marks. Done and cancelled:
                    // a hollow dot and the title struck out in fgMuted; overdue: the title in
                    // dangerText; days of other months: fgMuted (never fgFaint: below 4.5:1 on a
                    // selected cell). Mouse UIs: the row under the pointer gets a hover fill.
                    Column {
                        visible: !root.dots
                        x: 4
                        y: root.pillsTop
                        width: parent.width - 9
                        spacing: 2
                        Repeater {
                            model: cell.shownPills
                            delegate: Item {
                                id: entryRow
                                required property int index
                                readonly property var entry: cell.items[index]
                                readonly property bool closed: entry.variant === "done" || entry.closed === true
                                readonly property color markColor: Theme.markColor(closed ? "done"
                                                                                   : entry.variant === "neutral" ? "scheduled"
                                                                                   : entry.variant)
                                readonly property color textColor: closed || !cell.inMonth ? Theme.fgMuted
                                                                 : entry.variant === "overdue" ? Theme.dangerText
                                                                 : Theme.fg
                                readonly property bool iconMark: entry.variant === "alarm" || entry.recurring === true
                                width: parent ? parent.width : 0 // (null while the Repeater drops it: a new month)
                                height: root.pillHeight

                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.radiusSm
                                    color: Theme.bgHover
                                    visible: cell.hoveredRow === entryRow.index
                                }
                                // The text 16 px in (20 px from the cell's edge: the 4-px grid).
                                Item {
                                    id: marker
                                    x: 3
                                    width: 10
                                    height: parent.height
                                    Rectangle {
                                        visible: !entryRow.iconMark
                                        anchors.centerIn: parent
                                        width: 6
                                        height: 6
                                        radius: 3
                                        color: entryRow.closed ? "transparent" : entryRow.markColor
                                        border.width: entryRow.closed ? 1.5 : 0
                                        border.color: entryRow.markColor
                                        antialiasing: true
                                    }
                                    BIcon {
                                        visible: entryRow.iconMark
                                        anchors.centerIn: parent
                                        name: entryRow.entry.variant === "alarm" ? "alarm-clock" : "repeat"
                                        size: 10
                                        color: entryRow.markColor
                                    }
                                }
                                Text {
                                    id: timeText
                                    visible: entryRow.entry.time !== ""
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: marker.x + marker.width + 3
                                    text: entryRow.entry.time
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.textSm
                                    font.weight: Theme.weightSemiBold
                                    font.features: { "tnum": 1 }
                                    color: entryRow.textColor
                                }
                                Text {
                                    readonly property real textStart: timeText.visible ? timeText.x + timeText.implicitWidth + 4
                                                                                       : marker.x + marker.width + 3
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: textStart
                                    width: Math.max(0, parent.width - textStart - 3)
                                    text: entryRow.entry.title
                                    textFormat: Text.PlainText
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.textSm
                                    font.strikeout: entryRow.closed
                                    color: entryRow.textColor
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }
                            }
                        }
                        Text {
                            visible: cell.overflow
                            leftPadding: cell.shownPills > 0 ? 16 : 5 // under the rows' text
                            text: qsTr("+%n more", "", cell.items.length - cell.shownPills)
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textXs
                            font.weight: Theme.weightMedium
                            color: Theme.fgMuted
                        }
                    }

                    // Entries as dots (narrow cells): one per kind, the journal in grey.
                    Row {
                        visible: root.dots && cell.kinds.length > 0
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: dayBadge.y + dayBadge.height + 4
                        spacing: 3
                        Repeater {
                            model: cell.kinds.slice(0, 4)
                            delegate: Rectangle {
                                required property string modelData
                                width: 6
                                height: 6
                                radius: 3
                                color: Theme.markColor(modelData)
                            }
                        }
                    }

                    BFocusRing {
                        visible: cell.isSelected && root.activeFocus && root.keyboardActive
                        inset: true
                        targetRadius: 2
                    }

                    // The cue that a click opens an entry (mouse UIs; the same rows as the click).
                    HoverHandler {
                        id: rowHover
                        enabled: !Theme.touchUi && !root.dots
                        cursorShape: cell.hoveredRow >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: (eventPoint) => {
                            root.forceActiveFocus(Qt.MouseFocusReason)
                            root.keyboardActive = false
                            root.calendar.anchorDate = cell.date
                            // A click on an entry row opens that entry (mouse UIs); elsewhere it selects the day.
                            if (!Theme.touchUi && !root.dots) {
                                const p = eventPoint.position
                                const k = Math.floor((p.y - root.pillsTop) / (root.pillHeight + 2))
                                if (p.y >= root.pillsTop && k >= 0 && k < cell.shownPills) {
                                    root.itemOpened(cell.items[k])
                                    return
                                }
                            }
                            root.dayTapped(cell.date)
                        }
                        onDoubleTapped: root.dayOpened(cell.date)
                    }
                }
            }
        }

        DragHandler {
            id: swipe
            target: null
            enabled: Theme.touchUi && root.swipeEnabled
            xAxis.enabled: true
            yAxis.enabled: false
            // The drag's length is read from persistentTranslation: when activeChanged reports
            // the end, the active translation is already back at 0 (Qt 6.11), so a swipe slower
            // than the velocity limit did nothing.
            property real startX: 0
            onActiveChanged: {
                if (active) {
                    startX = persistentTranslation.x
                    return
                }
                const dx = persistentTranslation.x - startX
                if (Math.abs(dx) > Math.min(120, root.width * 0.18) || Math.abs(centroid.velocity.x) > 700) {
                    if (dx < 0)
                        root.calendar.next()
                    else
                        root.calendar.previous()
                    // Slide the new month in from the side it came from.
                    slider.x = dx < 0 ? root.width * 0.25 : -root.width * 0.25
                    slider.x = Qt.binding(() => swipe.active ? swipe.translation.x : 0)
                }
            }
        }
    }
}
