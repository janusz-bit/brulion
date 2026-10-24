pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// DateChipMenu — the menu of a date chip in the rendered text of a block: 📅 due, ⏳ scheduled,
// 🛫 start, ➕ created, ✅ done, ❌ cancelled and ⏰ reminders (alarms too). It opens with a
// right-click (mouse, touchpad, stylus) or a long press on the chip (BlockText) and changes that
// token only — its date, its time, the alarm, or removes it (PageController.dateChip and the
// …DateTokenAt edits, one undo step each).
//
//   DateChipMenu { kind: chip.kind; alarm: chip.alarm; onChosen: (action, a, b) => … }
//   menu.popup(item, x, y)
//
// chosen(action, a, b): "change" (the date dialog or, for a reminder, the reminder dialog),
// "move" (to today + a days, its time kept), "shift" (a days and b minutes later), "alarm" (a: on
// or off), "remove". React to it with the last statement of the handler: the change rebuilds the
// block's text, the chip included.
BMenu {
    id: menu

    /// "due" | "scheduled" | "start" | "created" | "done" | "cancelled" | "reminder"
    property string kind: "due"
    /// A reminder that is an alarm (`⏰!`).
    property bool alarm: false
    readonly property bool reminder: kind === "reminder"

    signal chosen(string action, var a, var b)

    title: reminder ? (alarm ? qsTr("Alarm") : qsTr("Reminder"))
                    : kind === "scheduled" ? qsTr("Scheduled")
                    : kind === "start" ? qsTr("Start date")
                    : kind === "created" ? qsTr("Created")
                    : kind === "done" ? qsTr("Done date")
                    : kind === "cancelled" ? qsTr("Cancelled date")
                    : qsTr("Due date")

    /// An item shown only for reminders (`forReminder`) or only for other dates.
    component KindItem: BMenuItem {
        property bool forReminder: false
        visible: menu.reminder === forReminder
        height: visible ? implicitHeight : 0
    }

    BMenuItem {
        objectName: "dateChipChange"
        text: menu.reminder ? qsTr("Change date and time…") : qsTr("Change date…")
        iconName: menu.reminder ? "calendar-clock" : "calendar"
        onTriggered: menu.chosen("change", 0, 0)
    }
    BMenuSeparator {}
    KindItem {
        objectName: "dateChipToday"
        text: qsTr("Today")
        iconName: "sun"
        onTriggered: menu.chosen("move", 0, 0)
    }
    KindItem {
        objectName: "dateChipTomorrow"
        text: qsTr("Tomorrow")
        iconName: "calendar-days"
        onTriggered: menu.chosen("move", 1, 0)
    }
    KindItem {
        objectName: "dateChipDayLater"
        text: qsTr("A day later")
        iconName: "arrow-right"
        onTriggered: menu.chosen("shift", 1, 0)
    }
    KindItem {
        objectName: "dateChipHourLater"
        forReminder: true
        text: qsTr("An hour later")
        iconName: "clock"
        onTriggered: menu.chosen("shift", 0, 60)
    }
    KindItem {
        objectName: "dateChipTomorrowSameTime"
        forReminder: true
        text: qsTr("Tomorrow at this time")
        iconName: "calendar-days"
        onTriggered: menu.chosen("move", 1, 0)
    }
    BMenuItem {
        objectName: "dateChipWeekLater"
        text: qsTr("A week later")
        iconName: "calendar-plus"
        onTriggered: menu.chosen("shift", 7, 0)
    }
    BMenuSeparator {
        visible: menu.reminder
        height: visible ? implicitHeight : 0
    }
    KindItem {
        objectName: "dateChipAlarm"
        forReminder: true
        text: qsTr("Alarm")
        iconName: "bell-ring"
        checkable: true
        checked: menu.alarm
        onTriggered: {
            const on = !menu.alarm
            checked = Qt.binding(() => menu.alarm) // (a checkable item toggles itself)
            menu.chosen("alarm", on, 0)
        }
    }
    BMenuSeparator {}
    BMenuItem {
        objectName: "dateChipRemove"
        text: menu.reminder ? qsTr("Delete reminder") : qsTr("Remove date")
        iconName: "trash-2"
        danger: true
        onTriggered: menu.chosen("remove", 0, 0)
    }
}
