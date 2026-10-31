pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CalendarMonthGrid — the month view: 6 weeks × 7 days of a CalendarModel (weekday names from
// DatesHelper, starting on the model's first weekday). Wide cells list the day's entries as
// coloured pills ("09:00 Spotkanie", "+3 więcej"; an alarm in the reminder's tint with its
// alarm clock, not a solid amber pill); narrow cells (phones) show coloured dots per kind. The
// cells are one surface (no weekend or other-month tint): today has a red disc on its number, the
// selected day a dark disc (Theme.primary, as in BDatePicker; ≥ 3:1, WCAG 1.4.11) — today
// selected shows only the red one —, days of other months have faint numbers, a daily note
// shows a small book.
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
    /// Dots instead of pills (narrow cells).
    readonly property bool dots: cellWidth < 96
    readonly property int pillHeight: Theme.touchUi ? 20 : 18
    readonly property int pillsTop: Theme.touchUi ? 34 : 30
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

                    x: column * root.cellWidth
                    y: row * root.cellHeight
                    width: root.cellWidth
                    height: root.cellHeight
                    // One surface: the selection is the disc on the day's number, not a fill.
                    color: "transparent"

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

                    // Day number: today on a red disc, the selected day on a dark one (only the red
                    // one when today is selected), another month's day faint.
                    Rectangle {
                        id: dayBadge
                        x: root.dots ? (parent.width - width) / 2 : 6
                        y: root.dots ? 4 : 4
                        width: Theme.touchUi ? 28 : 24
                        height: width
                        radius: width / 2
                        color: cell.isToday ? Theme.accentSolid : cell.isSelected ? Theme.primary : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: cell.day
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textSm
                            font.weight: cell.isToday || cell.isSelected ? Theme.weightBold
                                       : cell.inMonth ? Theme.weightMedium : Theme.weightNormal
                            font.features: { "tnum": 1 }
                            color: cell.isToday ? Theme.fgOnAccent : cell.isSelected ? Theme.primaryFg
                                 : cell.inMonth ? Theme.fg : Theme.fgFaint
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

                    // Entries as pills (wide cells).
                    Column {
                        visible: !root.dots
                        x: 4
                        y: root.pillsTop
                        width: parent.width - 9
                        spacing: 2
                        Repeater {
                            model: cell.shownPills
                            delegate: Rectangle {
                                id: pill
                                required property int index
                                readonly property var entry: cell.items[index]
                                // An alarm in the reminder's tint: its alarm clock tells it apart (a
                                // solid amber pill was the loudest thing on the month).
                                readonly property var colors: Theme.chip(entry.variant === "alarm" ? "reminder" : entry.variant)
                                width: parent ? parent.width : 0 // (null while the Repeater drops it: a new month)
                                height: root.pillHeight
                                radius: Theme.radiusSm
                                // (Done and cancelled entries: struck out in the "done" chip's
                                // colours, at full opacity — readable text, AA.)
                                color: colors.bg
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 5
                                    width: parent.width - 8
                                    spacing: 3
                                    BIcon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: pill.entry.variant === "alarm" || pill.entry.recurring
                                        name: pill.entry.variant === "alarm" ? "alarm-clock" : "repeat"
                                        size: 10
                                        color: pill.colors.fg
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - (x > 0 ? 13 : 0)
                                        text: (pill.entry.time !== "" ? "<b>" + pill.entry.time + "</b> " : "")
                                              + TextHelper.escapeHtml(pill.entry.title)
                                        textFormat: Text.StyledText
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.textXs
                                        font.strikeout: pill.entry.closed
                                        color: pill.colors.fg
                                        elide: Text.ElideRight
                                        maximumLineCount: 1
                                    }
                                }
                            }
                        }
                        Text {
                            visible: cell.overflow
                            leftPadding: 5
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

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: (eventPoint) => {
                            root.forceActiveFocus(Qt.MouseFocusReason)
                            root.keyboardActive = false
                            root.calendar.anchorDate = cell.date
                            // A click on a pill opens that entry (mouse UIs); elsewhere it selects the day.
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
