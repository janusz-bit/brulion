pragma ComponentBehavior: Bound

import QtQuick
import Brulion

// CalendarAgendaAtramentList — the Calendar view's agenda drawn by Atrament (AppSettings.viewsAtrament,
// Settings → Editor, or `--atrament-views`; docs/ATRAMENT.md "The views in Atrament (M3)",
// Calendar): the days of CalendarView's CalendarModel in "agenda" mode as rows of an Atrament list
// (views/atrament/agendaview.h), in CalendarAgendaList's place. What a row asks (viewAction) goes to
// CalendarView's atramentAgendaAction(), which does what CalendarAgendaList's signals do; the list
// scrolls to the selected day (else today) when the model's range changes, as CalendarAgendaList.
AtramentView {
    id: list

    /// CalendarView (an Item: its function is called by name).
    property Item calendarView
    /// The model (CalendarView's `cal`), for the range and the rows' dates.
    property CalendarModel calendar
    /// A day of the list is being picked: the selection moves, the list does not scroll to it.
    property bool picking: false

    view: "agenda"
    focus: true
    backgroundColor: Theme.bg
    viewLook: ({
        background: Theme.bg,
        text: Theme.fg,
        muted: Theme.fgMuted,
        faint: Theme.fgFaint,
        hover: Theme.bgHover,
        today: Theme.accentSolid,
        onToday: Theme.fgOnAccent,
        todayText: Theme.accentText,
        selected: Theme.primary,
        onSelected: Theme.primaryFg,
        disc: Theme.bgSunken,
        border: Theme.border,
        separator: Theme.borderSubtle,
        focus: Theme.focusRing,
        box: Theme.borderStrong,
        accent: Theme.accent,
        reminder: Theme.markColor("reminder"),
        textSize: Theme.textBase,
        smallSize: Theme.textSm,
        dayTextSize: Theme.textMd,
        numberSize: Theme.textLg,
        chipSize: Theme.textXs,
        lineHeight: Theme.lineHeightPx(Theme.textBase, 1.35),
        maxWidth: Theme.measureList,
        sideMargin: Theme.space4,
        radius: Theme.radius,
        marker: Theme.touchUi ? 40 : 36,
        entryMarker: Theme.touchUi ? Theme.touchTarget : 24,
        rowHeight: Theme.listItemHeight,
        uiFamily: Theme.fontUi,
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
    Accessible.role: Accessible.List
    Accessible.name: list.calendar !== null ? list.calendar.title : ""
    onViewAction: (action, args) => QmlCall.call2(list.calendarView, "atramentAgendaAction", action, args)

    /// The selected day's row (else today's) at the top, as CalendarAgendaList.scrollToAnchor.
    function scrollToAnchor(): void {
        if (list.calendar === null)
            return
        let row = list.calendar.rowForDate(list.calendar.anchorDate)
        if (row < 0)
            row = list.calendar.rowForDate(DatesHelper.today)
        if (row >= 0)
            list.viewShowRow(row)
    }
    Connections {
        target: list.calendar
        function onRangeChanged(): void {
            if (!list.picking)
                scrollLater.restart()
        }
    }
    Component.onCompleted: scrollLater.restart()
    Later {
        id: scrollLater
        onTriggered: list.scrollToAnchor()
    }
}
