pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CalendarMonthGrid — the month view: 6 weeks × 7 days of a CalendarModel (weekday names from
// DatesHelper, starting on the model's first weekday). Cells list the day's entries as
// coloured pills ("09:00 Meeting", "+3 more"; an alarm in the reminder's tint with its
// alarm clock, not a solid amber pill; open entries first, so "+N more" hides finished ones; a
// done entry is muted with a check, not struck out — as in Tasks —, a cancelled one struck out
// with an x; under ~104-px cells the title alone: the time stays in the pill's accessible name,
// its tooltip — also for an elided title — and the day panel). Narrow cells on touch UIs (phones,
// touch tablets: under 96 px; with a mouse only under 72 px) show a small icon in its colour per
// kind of open entry (overdue: an alert, reminder: a bell, due: a calendar, scheduled: an
// hourglass — none for done entries) and a short faint line under the number for a daily note. The
// cells are one surface (no weekend or other-month tint): today has a red disc on its number, the
// selected day a dark disc (Theme.primary, as in BDatePicker; ≥ 3:1, WCAG 1.4.11) — today
// selected shows only the red one —, days of other months have faint numbers, a daily note
// shows a small book (wide cells) or the line (narrow cells).
//
//   CalendarMonthGrid { calendar: cal; onDayTapped: (date) => …; onItemOpened: (entry) => … }
//
// Keyboard (when focused): arrows move the selected day (±1 / ±7), PageUp/PageDown change the
// month, Home/End go to the start / end of the week, Enter emits dayTapped (the view opens the
// day), Ctrl+Enter dayOpened (the daily note). Touch: horizontal swipe = previous / next month.
// The ISO week numbers on the left (`showWeekNumbers`) open the week's note (weekOpened).
FocusScope {
    id: root

    required property CalendarModel calendar
    /// The horizontal swipe is on (touch UIs); the view turns it off under a modal panel (the
    /// shell's drawer), whose own swipe the handler would take.
    property bool swipeEnabled: true

    readonly property real headerHeight: Theme.touchUi ? 30 : 26
    /// The ISO week numbers' column on the left (CalendarWeekNumber: a click opens the week's note).
    property bool showWeekNumbers: true
    readonly property real weekColumn: showWeekNumbers ? (Theme.touchUi ? 26 : 28) : 0
    readonly property real cellWidth: (width - weekColumn) / 7
    readonly property real cellHeight: Math.max(1, (height - headerHeight) / 6)
    /// Icons instead of pills: narrow cells, decided by the input as well — a finger needs the
    /// whole cell to pick the day, a mouse keeps the titled pills down to 72 px (a 1280-px
    /// desktop with the sidebar and the day panel has cells of ~91 px).
    readonly property bool dots: Theme.touchUi ? cellWidth < 96 : cellWidth < 72
    /// The pills start with their entry's time (narrower cells: the title alone).
    readonly property bool pillTimes: cellWidth >= 104
    readonly property int pillHeight: Theme.touchUi ? 20 : 18
    readonly property int pillsTop: Theme.touchUi ? 34 : 30
    readonly property int maxPills: Math.max(0, Math.floor((cellHeight - pillsTop - 2) / (pillHeight + 2)))
    // (The locale passed for real: a method call is not tracked, and a bare `void DatesHelper.locale`
    // is compiled away with the dependency.)
    readonly property list<string> weekdayNames: rotatedNames(shortNames(DatesHelper.locale), calendar.effectiveWeekStart)
    function shortNames(locale: string): list<string> {
        return DatesHelper.weekdayNames("short", locale)
    }
    /// `names` (Monday first) starting at the day `start` (1 = Monday).
    function rotatedNames(names: list<string>, start: int): list<string> {
        if (names.length !== 7)
            return names
        const at = (start - 1 + 7) % 7
        return Js.concatStrings(names.slice(at), names.slice(0, at))
    }

    /// The selected day's place in its week (0 = the first day of the week).
    function dayInWeek(): int {
        return (DatesHelper.dayOfWeek(calendar.anchorDate) - calendar.effectiveWeekStart + 7) % 7
    }

    /// An entry's accessible name and tooltip: its time (if any), its title and its state when
    /// finished (as the week grid's).
    function entryName(closed: bool, status: string, title: string, time: string): string {
        const state = !closed ? "" : status === "cancelled" ? qsTr("cancelled") : qsTr("done")
        let out = time
        if (title !== "")
            out = out === "" ? title : out + ", " + title
        if (state !== "")
            out = out === "" ? state : out + ", " + state
        return out
    }

    /// The icon of a narrow cell's mark (CalendarModel `openKinds`): as the date chips' icons
    /// (Theme.chipIcon), but an alert for overdue — a calendar both for it and for due told them
    /// apart by colour alone.
    function markIcon(kind: string): string {
        switch (kind) {
        case "overdue": return "circle-alert"
        case "reminder": return "bell"
        case "scheduled": return "hourglass"
        default: return "calendar"
        }
    }

    signal dayTapped(string date)
    signal dayOpened(string date)
    signal itemOpened(var entry)
    /// A week number was clicked: the week's Monday ("YYYY-MM-DD").
    signal weekOpened(string monday)

    /// Mondays of the shown weeks that have a weekly note (the numbers' accent).
    readonly property list<string> weekNotes: {
        // (Read for real: a bare `void Workspace.revision` is compiled away with the dependency.)
        if (!showWeekNumbers || calendar.rangeStart === "" || Workspace.revision < 0)
            return []
        return Workspace.periodicNoteStarts("week", calendar.rangeStart, calendar.rangeEnd)
    }

    /// The keyboard was used last (the focus ring shows only then).
    property bool keyboardActive: false

    activeFocusOnTab: true
    onActiveFocusChanged: if (!activeFocus) keyboardActive = false
    Accessible.role: Accessible.Table
    Accessible.name: calendar.title

    Keys.onPressed: (event) => {
        const ctrl = event.modifiers & Qt.ControlModifier
        const forward = Application.layoutDirection === Qt.RightToLeft ? -1 : 1
        switch (event.key) {
        case Qt.Key_Left: calendar.moveAnchor(-forward); break
        case Qt.Key_Right: calendar.moveAnchor(forward); break
        case Qt.Key_Up: calendar.moveAnchor(-7); break
        case Qt.Key_Down: calendar.moveAnchor(7); break
        case Qt.Key_PageUp: calendar.previous(); break
        case Qt.Key_PageDown: calendar.next(); break
        case Qt.Key_Home: calendar.moveAnchor(-dayInWeek()); break
        case Qt.Key_End: calendar.moveAnchor(6 - dayInWeek()); break
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
        x: root.weekColumn
        width: parent.width - root.weekColumn
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
            // (Set to a side and back to 0 after a swipe: the Behavior slides the new range in.)
            property real slideOffset: 0
            x: swipe.active ? swipe.translation.x : slideOffset
            Behavior on x {
                enabled: !swipe.active
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            // ISO week numbers: the week of each row's middle day (its Monday's week, also when the
            // week starts on a Sunday).
            Repeater {
                model: root.showWeekNumbers ? 6 : 0
                delegate: CalendarWeekNumber {
                    required property int index
                    readonly property string middle: root.calendar.rangeStart !== ""
                                                     ? DatesHelper.addDays(root.calendar.rangeStart, index * 7 + 3) : ""
                    objectName: "calendarWeekNumber" + index
                    y: index * root.cellHeight
                    width: root.weekColumn
                    height: root.cellHeight
                    week: DatesHelper.isoWeek(middle)
                    monday: middle !== "" ? DatesHelper.addDays(middle, 1 - DatesHelper.dayOfWeek(middle)) : ""
                    hasNote: root.weekNotes.indexOf(monday) >= 0
                    onOpened: (date) => root.weekOpened(date)
                }
            }
            Rectangle {
                visible: root.showWeekNumbers
                x: root.weekColumn - 1
                width: 1
                height: parent.height
                color: Theme.borderSubtle
            }

            Repeater {
                model: root.calendar
                delegate: Item {
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
                    /// The day's entries, open ones first (a stable partition: times keep their
                    /// order), so "+N more" hides finished ones first; the pills and a click
                    /// on one read this order.
                    required property list<var> orderedItems
                    required property int itemCount
                    /// The open entries' kinds for the dots, most urgent first (CalendarModel).
                    required property list<string> openKinds
                    required property string longLabel

                    readonly property int column: index % 7
                    readonly property int row: Math.floor(index / 7)
                    readonly property bool overflow: orderedItems.length > root.maxPills
                    readonly property int shownPills: overflow ? Math.max(0, root.maxPills - 1) : orderedItems.length

                    x: root.weekColumn + column * root.cellWidth
                    y: row * root.cellHeight
                    width: root.cellWidth
                    height: root.cellHeight
                    // One surface: the selection is the disc on the day's number, not a fill.

                    Accessible.role: Accessible.Cell
                    Accessible.name: {
                        let out = longLabel
                        if (isToday)
                            out += ", " + qsTr("today")
                        if (itemCount > 0)
                            out += ", " + qsTr("%n entries", "", itemCount)
                        if (hasJournal)
                            out += ", " + qsTr("journal")
                        return out
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
                        y: 4
                        width: Theme.touchUi ? 28 : 24
                        height: width
                        radius: width / 2
                        color: cell.isToday ? Theme.accentSolid : cell.isSelected ? Theme.primary : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: "" + cell.day
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
                                readonly property var entry: cell.orderedItems[index]
                                readonly property string variant: AppController.routeText(entry, "variant")
                                readonly property string status: AppController.routeText(entry, "status")
                                readonly property string time: AppController.routeText(entry, "time")
                                readonly property bool closed: AppController.routeFlag(entry, "closed")
                                readonly property bool recurring: AppController.routeFlag(entry, "recurring")
                                // An alarm in the reminder's tint: its alarm clock tells it apart (a
                                // solid amber pill was the loudest thing on the month).
                                readonly property string tint: variant === "alarm" ? "reminder" : variant
                                readonly property color fgColor: Theme.chipFg(tint)
                                readonly property string title: AppController.routeText(entry, "title")
                                width: parent ? parent.width : 0 // (null while the Repeater drops it: a new month)
                                height: root.pillHeight
                                radius: Theme.radiusSm
                                Accessible.role: Accessible.Button
                                Accessible.name: root.entryName(closed, status, title, time)
                                // (Done and cancelled entries: the "done" chip's colours at full
                                // opacity — readable text, AA —, with a check, or struck out with
                                // an x when cancelled, as in Tasks.)
                                color: Theme.chipBg(pill.tint)
                                Row {
                                    id: pillRow
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 5
                                    width: parent.width - 8
                                    spacing: 3
                                    BIcon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: pill.closed || pill.variant === "alarm" || pill.recurring
                                        name: pill.closed ? (pill.status === "cancelled" ? "x" : "check")
                                            : pill.variant === "alarm" ? "alarm-clock" : "repeat"
                                        size: 10
                                        color: pill.fgColor
                                    }
                                    Text {
                                        id: pillText
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - (x > 0 ? 13 : 0)
                                        text: (pill.time !== "" && root.pillTimes ? "<b>" + pill.time + "</b> " : "")
                                              + TextHelper.escapeHtml(pill.title)
                                        textFormat: Text.StyledText
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.textXs
                                        font.strikeout: pill.status === "cancelled"
                                        color: pill.fgColor
                                        elide: Text.ElideRight
                                        maximumLineCount: 1
                                    }
                                }
                                // What the pill leaves out — its time, the end of its title —
                                // when the mouse rests on it (created then).
                                HoverHandler {
                                    id: pillHover
                                    enabled: !Theme.touchUi
                                }
                                Loader {
                                    active: pillHover.hovered && (pillText.truncated || (pill.time !== "" && !root.pillTimes))
                                    sourceComponent: BToolTip {
                                        parent: pill
                                        text: root.entryName(pill.closed, pill.status, pill.title, pill.time)
                                        visible: true
                                    }
                                }
                            }
                        }
                        Text {
                            visible: cell.overflow
                            leftPadding: 5
                            text: qsTr("+%n more", "", cell.orderedItems.length - cell.shownPills)
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textXs
                            font.weight: Theme.weightMedium
                            color: Theme.fgMuted
                        }
                    }

                    // A daily note (narrow cells): a short faint line under the number — not a
                    // mark: nearly every day has one, a grey one drowned out the others.
                    Rectangle {
                        id: journalMark
                        visible: root.dots && cell.hasJournal
                        anchors.horizontalCenter: dayBadge.horizontalCenter
                        y: dayBadge.y + dayBadge.height + 2
                        width: 12
                        height: 2
                        radius: 1
                        color: Theme.fgFaint
                    }

                    // Open entries as small icons in their colours (narrow cells): one per kind,
                    // the most urgent first (markIcon()) — a shape, not only a colour, to tell
                    // them apart.
                    Row {
                        visible: root.dots && cell.openKinds.length > 0
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: journalMark.y + journalMark.height + 4
                        spacing: 2
                        Repeater {
                            model: cell.openKinds
                            delegate: BIcon {
                                required property string modelData
                                name: root.markIcon(modelData)
                                size: 14
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
                            // (The pill under the pointer found before the focus and the anchor change.)
                            const py = eventPoint.position.y
                            const k = Math.floor((py - root.pillsTop) / (root.pillHeight + 2))
                            const onPill = !Theme.touchUi && !root.dots && py >= root.pillsTop && k >= 0 && k < cell.shownPills
                            root.forceActiveFocus(Qt.MouseFocusReason)
                            root.keyboardActive = false
                            root.calendar.anchorDate = cell.date
                            // A click on a pill opens that entry (mouse UIs); elsewhere it selects the day.
                            if (onPill) {
                                root.itemOpened(cell.orderedItems[k])
                                return
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
                    slider.slideOffset = dx < 0 ? root.width * 0.25 : -root.width * 0.25
                    slider.slideOffset = 0
                }
            }
        }
    }
}
