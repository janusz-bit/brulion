pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// QuickDateMenu — the "Add date" menu of a task without a date: Today, Tomorrow, Next week (the
// first day of the next week, DatesHelper.weekStart) with their dates on the right, and "Pick a
// date…" for the date dialog. It opens from the calendar button at the end of a task's first line
// in the editor (BlockDelegate, BlockEditor.openQuickDateMenu) and from the "Add date" chip of a
// Tasks row (TasksRow, PlanningTokenMenu.openAddDate); the date becomes the task's 📅 due date.
//
//   QuickDateMenu { onDateChosen: (date) => setDue(date); onPickRequested: openDateDialog() }
//   menu.popup(button, 0, button.height)
//
// dateChosen(date): "YYYY-MM-DD" (the days follow DatesHelper.today, also past midnight).
BMenu {
    id: menu

    signal dateChosen(string date)
    signal pickRequested()

    title: qsTr("Add date")

    readonly property string today: DatesHelper.today
    readonly property string tomorrow: DatesHelper.addDays(today, 1)
    readonly property string nextWeek: DatesHelper.addDays(DatesHelper.startOfWeek(today), 7)

    BMenuItem {
        objectName: "quickDateToday"
        text: qsTr("Today")
        iconName: "sun"
        hint: DatesHelper.formatShort(menu.today)
        onTriggered: menu.dateChosen(menu.today)
    }
    BMenuItem {
        objectName: "quickDateTomorrow"
        text: qsTr("Tomorrow")
        iconName: "calendar-days"
        hint: DatesHelper.formatShort(menu.tomorrow)
        onTriggered: menu.dateChosen(menu.tomorrow)
    }
    BMenuItem {
        objectName: "quickDateNextWeek"
        text: qsTr("Next week")
        iconName: "calendar-plus"
        hint: DatesHelper.formatShort(menu.nextWeek)
        onTriggered: menu.dateChosen(menu.nextWeek)
    }
    BMenuSeparator {}
    BMenuItem {
        objectName: "quickDatePick"
        text: qsTr("Pick a date…")
        iconName: "calendar"
        onTriggered: menu.pickRequested()
    }
}
