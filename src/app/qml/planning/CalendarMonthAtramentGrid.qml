pragma ComponentBehavior: Bound

import QtQuick
import Brulion

// CalendarMonthAtramentGrid — the Calendar view's month drawn by Atrament (AppSettings.viewsAtrament,
// Settings → Editor, or `--atrament-views`; docs/ATRAMENT.md "The views in Atrament (M3)", Calendar):
// the 42 days of CalendarView's CalendarModel in "month" mode as Atrament's grid
// (views/atrament/monthview.h), in CalendarMonthGrid's place. What a cell asks (viewAction) goes to
// CalendarView's atramentMonthAction(), which does what CalendarMonthGrid's signals and keys do.
AtramentView {
    id: grid

    /// CalendarView (an Item: its function is called by name).
    property Item calendarView
    property CalendarModel calendar
    /// The ISO week numbers on the left (CalendarMonthGrid.showWeekNumbers).
    property bool showWeekNumbers: true

    /// Mondays of the shown weeks that have a weekly note (the numbers' accent): asked only while
    /// shown (a walk over the notebook's files).
    readonly property list<string> weekNotes: {
        if (!grid.visible || !grid.showWeekNumbers || grid.calendar === null || grid.calendar.rangeStart === ""
                || Workspace.namesRevision < 0)
            return []
        return Workspace.periodicNoteStarts("week", grid.calendar.rangeStart, grid.calendar.rangeEnd)
    }

    view: "month"
    focus: true
    backgroundColor: Theme.bg
    viewLook: ({
        background: Theme.bg,
        text: Theme.fg,
        muted: Theme.fgMuted,
        faint: Theme.fgFaint,
        grid: Theme.borderSubtle,
        today: Theme.accentSolid,
        onToday: Theme.fgOnAccent,
        selected: Theme.primary,
        onSelected: Theme.primaryFg,
        focus: Theme.focusRing,
        weekNote: Theme.accentText,
        smallSize: Theme.textSm,
        tinySize: Theme.textXs,
        headerHeight: Theme.touchUi ? 30 : 26,
        weekColumn: grid.showWeekNumbers ? (Theme.touchUi ? 26 : 28) : 0,
        pillHeight: Theme.touchUi ? 20 : 18,
        pillsTop: Theme.touchUi ? 34 : 30,
        disc: Theme.touchUi ? 28 : 24,
        radiusSm: Theme.radiusSm,
        dotsBelow: Theme.touchUi ? 96 : 72,
        timesFrom: 104,
        uiFamily: Theme.fontUi,
        weekdays: DatesHelper.weekdayNames("short", DatesHelper.locale),
        weekNotes: grid.weekNotes,
        chips: {
            due: { background: Theme.chipBg("due"), text: Theme.chipFg("due") },
            overdue: { background: Theme.chipBg("overdue"), text: Theme.chipFg("overdue") },
            scheduled: { background: Theme.chipBg("scheduled"), text: Theme.chipFg("scheduled") },
            reminder: { background: Theme.chipBg("reminder"), text: Theme.chipFg("reminder") },
            done: { background: Theme.chipBg("done"), text: Theme.chipFg("done") },
            neutral: { background: Theme.chipBg("neutral"), text: Theme.chipFg("neutral") }
        },
        marks: {
            overdue: Theme.markColor("overdue"),
            reminder: Theme.markColor("reminder"),
            scheduled: Theme.markColor("scheduled"),
            due: Theme.markColor("due"),
            event: Theme.markColor("event")
        }
    })
    Accessible.role: Accessible.Table
    Accessible.name: grid.calendar !== null ? grid.calendar.title : ""
    onViewAction: (action, args) => QmlCall.call2(grid.calendarView, "atramentMonthAction", action, args)
}
