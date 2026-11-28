pragma ComponentBehavior: Bound

import QtQuick
import Brulion

// TasksAtramentList — TasksView's list drawn by Atrament (AppSettings.viewsAtrament, Settings →
// Advanced, or `--atrament-views`; docs/ATRAMENT.md "The views in Atrament (M3)", Tasks): the
// sections and tasks of TasksView's TaskListModel as rows of an Atrament list (views/atrament/
// tasksview.h). TasksView loads it by file name where Atrament is built and shows it under its
// header in place of its ListView's rows; what a row asks (viewAction) goes to TasksView's
// atramentAction(), the same functions its TasksRow delegates call.
AtramentView {
    id: list

    /// TasksView (an Item: its function is called by name).
    property Item tasksView
    /// The rows' inset on each side (TasksView.rowMargin).
    property real sideMargin: Theme.space5

    view: "tasks"
    focus: true
    textColor: Theme.fg
    mutedColor: Theme.fgMuted
    linkColor: Theme.link
    backgroundColor: Theme.bg
    viewLook: ({
        background: Theme.bg,
        hover: Theme.bgHover,
        current: Theme.selection,
        header: Theme.fgMuted,
        faint: Theme.fgFaint,
        box: Theme.borderStrong,
        accent: Theme.accent,
        danger: Theme.chipOverdueFg,
        chip: Theme.chipNeutralBg,
        chipText: Theme.chipNeutralFg,
        dangerChip: Theme.chipOverdueBg,
        textSize: Theme.textMd,
        lineHeight: Theme.lineHeightPx(Theme.textMd, 1.45),
        smallSize: Theme.textXs,
        sideMargin: list.sideMargin,
        indent: Theme.space5,
        uiFamily: Theme.fontUi
    })
    Accessible.role: Accessible.List
    Accessible.name: qsTr("Tasks")
    onViewAction: (action, args) => QmlCall.call2(list.tasksView, "atramentAction", action, args)
}
