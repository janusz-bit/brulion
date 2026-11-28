pragma ComponentBehavior: Bound

import QtQuick
import Brulion

// CalendarWeekAtramentGrid — the Calendar view's week drawn by Atrament (AppSettings.viewsAtrament,
// Settings → Editor, or `--atrament-views`; docs/ATRAMENT.md "The views in Atrament (M3)", Calendar):
// the 7 days of CalendarView's CalendarModel in "week" mode as Atrament's grid
// (views/atrament/weekview.h), in CalendarWeekGrid's place. What a part asks (viewAction) goes to
// CalendarView's atramentWeekAction(), which does what CalendarWeekGrid's signals and keys do.
AtramentView {
    id: grid

    /// CalendarView (an Item: its function is called by name).
    property Item calendarView
    property CalendarModel calendar

    view: "week"
    focus: true
    backgroundColor: Theme.bg
    viewLook: ({
        background: Theme.bg,
        text: Theme.fg,
        muted: Theme.fgMuted,
        faint: Theme.fgFaint,
        grid: Theme.borderSubtle,
        border: Theme.border,
        today: Theme.accentSolid,
        onToday: Theme.fgOnAccent,
        selected: Theme.primary,
        onSelected: Theme.primaryFg,
        selectedColumn: Theme.bgSelected.alpha(Theme.bgSelected.a * 0.5),
        weekend: Theme.bgSunken.alpha(0.45),
        now: Theme.accentSolid,
        focus: Theme.focusRing,
        gutter: Theme.touchUi ? 44 : 50,
        hourHeight: Theme.touchUi ? 44 : 40,
        headerHeight: Theme.touchUi ? 52 : 46,
        pillHeight: Theme.touchUi ? 20 : 18,
        narrowBelow: 80,
        smallSize: Theme.textSm,
        tinySize: Theme.textXs,
        radiusSm: Theme.radiusSm,
        uiFamily: Theme.fontUi,
        weekdays: DatesHelper.weekdayNames("short", DatesHelper.locale),
        chips: {
            due: { background: Theme.chipBg("due"), text: Theme.chipFg("due") },
            overdue: { background: Theme.chipBg("overdue"), text: Theme.chipFg("overdue") },
            scheduled: { background: Theme.chipBg("scheduled"), text: Theme.chipFg("scheduled") },
            reminder: { background: Theme.chipBg("reminder"), text: Theme.chipFg("reminder") },
            alarm: { background: Theme.chipBg("alarm"), text: Theme.chipFg("alarm") },
            done: { background: Theme.chipBg("done"), text: Theme.chipFg("done") },
            neutral: { background: Theme.chipBg("neutral"), text: Theme.chipFg("neutral") }
        }
    })
    Accessible.role: Accessible.Table
    Accessible.name: grid.calendar !== null ? grid.calendar.title : ""
    onViewAction: (action, args) => QmlCall.call2(grid.calendarView, "atramentWeekAction", action, args)
}
