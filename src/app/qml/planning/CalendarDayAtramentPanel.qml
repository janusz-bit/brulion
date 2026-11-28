pragma ComponentBehavior: Bound

import QtQuick
import Brulion

// CalendarDayAtramentPanel — the Calendar's day panel drawn by Atrament (AppSettings.viewsAtrament,
// `--atrament-views`; mouse UIs; docs/ATRAMENT.md "The views in Atrament (M3)", Calendar): the
// selected day of CalendarView's CalendarModel (views/atrament/daypanelview.h) in the side column,
// in CalendarDayPanel's place. What it asks (viewAction) goes to CalendarView's
// atramentPanelAction(), which does what the QML panel's signals do.
AtramentView {
    id: panel

    /// CalendarView (an Item: its function is called by name).
    property Item calendarView

    view: "daypanel"
    backgroundColor: Theme.bgElevated
    viewLook: ({
        background: Theme.bgElevated,
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
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Day")
    onViewAction: (action, args) => QmlCall.call2(panel.calendarView, "atramentPanelAction", action, args)
}
