pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// DateChipMenu — the menu of a token of a block's rendered text: a date chip (📅 due, ⏳ scheduled,
// 🛫 start, ➕ created, ✅ done, ❌ cancelled), a ⏰ reminder (alarms too), the 🔁 rule, a priority
// (🔺 ⏫ 🔼 🔽 ⏬, Logseq [#A]) or a link to a day of the journal ([[2026-10-20]]). It opens with a
// right-click (mouse, touchpad, stylus) or a long press on the token — in the editor's blocks
// (BlockText), query results, embeds, table cells, Tasks and the calendar — and changes that token
// only (markdown/tokenedit.h: PageController.applyTokenAction, editTokenIn, PlanningActions.editToken;
// one undo step each).
//
//   DateChipMenu { kind: chip.kind; alarm: chip.alarm; rule: chip.rule; onChosen: (action, a, b) => … }
//   menu.popup(item, x, y)
//
// chosen(action, a, b): "change" (the date dialog, for a reminder the reminder dialog, for a rule
// the recurrence dialog), "move" (to today + a days, its time kept), "shift" (a days and b minutes
// later), "alarm" (a: on or off), "remove", "recurrence" (a: the rule, "" = none), "priority" (a:
// "highest" … "lowest", "" = none); on a reminder "blockRecurrence" (a: the block's rule),
// "blockRecurrenceChange" (the recurrence dialog for it), "blockPriority" (a: the block's
// priority); on a day "open", "openSide", "copy". React to it with the last statement of the
// handler: the change rebuilds the text, the chip included.
BMenu {
    id: menu

    /// "due" | "scheduled" | "start" | "created" | "done" | "cancelled" | "reminder" |
    /// "recurrence" | "priority" | "day"
    property string kind: "due"
    /// A reminder that is an alarm (`⏰!`).
    property bool alarm: false
    /// The rule (canonical) of a 🔁 token, or of the reminder's block ("" = none).
    property string rule: ""
    /// The priority name of a priority token, or of the reminder's block's task ("" = none).
    property string priority: ""
    /// A Logseq priority ([#A] [#B] [#C]: three levels).
    property bool logseq: false
    /// The reminder is on a task (its "Priority" submenu).
    property bool isTask: false
    /// One of a series' dates in the calendar (not the date written in the note): it moves only
    /// as a whole series ("Change date…"), a reminder is snoozed for that one time.
    property bool occurrence: false
    /// "Open in side panel" offered for a day link (wide windows, a host with panels).
    property bool sidePanel: !Theme.narrow

    readonly property bool reminder: kind === "reminder"
    readonly property bool recurrence: kind === "recurrence"
    readonly property bool priorityKind: kind === "priority"
    readonly property bool day: kind === "day"
    readonly property bool dateKind: !reminder && !recurrence && !priorityKind && !day
    /// Moves to a day: dates and day links (not a series' occurrence).
    readonly property bool movable: (dateKind || day) && !occurrence
    readonly property bool whenDone: rule.endsWith(" when done")
    readonly property string baseRule: whenDone ? rule.slice(0, rule.length - " when done".length) : rule

    signal chosen(string action, var a, var b)

    title: reminder ? (alarm ? qsTr("Alarm") : qsTr("Reminder"))
                    : recurrence ? qsTr("Repeat")
                    : priorityKind ? qsTr("Priority")
                    : day ? qsTr("Day")
                    : kind === "scheduled" ? qsTr("Scheduled")
                    : kind === "start" ? qsTr("Start date")
                    : kind === "created" ? qsTr("Created")
                    : kind === "done" ? qsTr("Done date")
                    : kind === "cancelled" ? qsTr("Cancelled date")
                    : qsTr("Due date")

    /// An item shown only when `shown`.
    component ShownItem: BMenuItem {
        property bool shown: true
        visible: shown
        height: shown ? implicitHeight : 0
    }
    component ShownSeparator: BMenuSeparator {
        property bool shown: true
        visible: shown
        height: shown ? implicitHeight : 0
    }
    /// A quick rule of the "Repeat" items (the recurrence dialog's choices).
    component RuleItem: ShownItem {
        required property string ruleText
        /// "recurrence" (the 🔁 token) or "blockRecurrence" (the reminder's block).
        property string chosenAction: "recurrence"
        checkable: true
        checked: menu.baseRule === ruleText
        onTriggered: {
            checked = Qt.binding(() => menu.baseRule === ruleText) // (a checkable item toggles itself)
            menu.chosen(chosenAction, menu.whenDone ? ruleText + " when done" : ruleText, 0)
        }
    }
    component PriorityItem: ShownItem {
        required property string level
        property string chosenAction: "priority"
        checkable: true
        checked: menu.priority === level
        onTriggered: {
            checked = Qt.binding(() => menu.priority === level)
            menu.chosen(chosenAction, level, 0)
        }
    }

    // The Repeat and Priority submenus belong to reminders (and Priority to their tasks only).
    onAboutToShow: {
        for (let i = 0; i < count; ++i) {
            const item = itemAt(i) as BMenuItem
            if (!item || !item.subMenu)
                continue
            const shown = item.subMenu === repeatMenu ? reminder : item.subMenu === priorityMenu ? reminder && isTask : true
            item.visible = shown
            item.height = shown ? item.implicitHeight : 0
        }
    }

    // ---- A day of the journal: open it, copy the link ---------------------------------------------
    ShownItem {
        objectName: "dateChipOpenDay"
        shown: menu.day
        text: qsTr("Open day")
        iconName: "calendar-days"
        onTriggered: menu.chosen("open", 0, 0)
    }
    ShownItem {
        objectName: "dateChipOpenSide"
        shown: menu.day && menu.sidePanel
        text: qsTr("Open in side panel")
        iconName: "panel-right"
        onTriggered: menu.chosen("openSide", 0, 0)
    }
    ShownItem {
        objectName: "dateChipCopyLink"
        shown: menu.day
        text: qsTr("Copy link")
        iconName: "link"
        onTriggered: menu.chosen("copy", 0, 0)
    }
    ShownSeparator { shown: menu.day }

    // ---- The date, the rule: "Change…" --------------------------------------------------------------
    ShownItem {
        objectName: "dateChipChange"
        shown: !menu.priorityKind
        text: menu.reminder ? (menu.occurrence ? qsTr("Change the whole series…") : qsTr("Change date and time…"))
                            : menu.recurrence ? qsTr("Change rule…")
                            : menu.occurrence ? qsTr("Change the whole series…")
                            : qsTr("Change date…")
        iconName: menu.reminder ? "calendar-clock" : menu.recurrence ? "repeat" : "calendar"
        onTriggered: menu.chosen("change", 0, 0)
    }
    ShownSeparator { shown: !menu.priorityKind }
    ShownItem {
        objectName: "dateChipToday"
        shown: menu.movable
        text: qsTr("Today")
        iconName: "sun"
        onTriggered: menu.chosen("move", 0, 0)
    }
    ShownItem {
        objectName: "dateChipTomorrow"
        shown: menu.movable
        text: qsTr("Tomorrow")
        iconName: "calendar-days"
        onTriggered: menu.chosen("move", 1, 0)
    }
    ShownItem {
        objectName: "dateChipDayLater"
        shown: menu.movable
        text: qsTr("A day later")
        iconName: "arrow-right"
        onTriggered: menu.chosen("shift", 1, 0)
    }
    ShownItem {
        objectName: "dateChipHourLater"
        shown: menu.reminder
        text: qsTr("An hour later")
        iconName: "clock"
        onTriggered: menu.chosen("shift", 0, 60)
    }
    ShownItem {
        objectName: "dateChipTomorrowSameTime"
        shown: menu.reminder
        text: qsTr("Tomorrow at this time")
        iconName: "calendar-days"
        onTriggered: menu.chosen("move", 1, 0)
    }
    ShownItem {
        objectName: "dateChipWeekLater"
        shown: menu.movable || (menu.reminder && !menu.occurrence)
        text: qsTr("A week later")
        iconName: "calendar-plus"
        onTriggered: menu.chosen("shift", 7, 0)
    }

    // ---- 🔁: the quick rules --------------------------------------------------------------------------
    RuleItem { objectName: "dateChipRuleDaily"; shown: menu.recurrence; ruleText: "every day"; text: qsTr("Daily") }
    RuleItem { objectName: "dateChipRuleWeekdays"; shown: menu.recurrence; ruleText: "every weekday"; text: qsTr("Weekdays") }
    RuleItem { objectName: "dateChipRuleWeekly"; shown: menu.recurrence; ruleText: "every week"; text: qsTr("Weekly") }
    RuleItem { objectName: "dateChipRuleTwoWeeks"; shown: menu.recurrence; ruleText: "every 2 weeks"; text: qsTr("Every 2 weeks") }
    RuleItem { objectName: "dateChipRuleMonthly"; shown: menu.recurrence; ruleText: "every month"; text: qsTr("Monthly") }
    RuleItem { objectName: "dateChipRuleYearly"; shown: menu.recurrence; ruleText: "every year"; text: qsTr("Yearly") }
    ShownSeparator { shown: menu.recurrence }
    ShownItem {
        objectName: "dateChipWhenDone"
        shown: menu.recurrence
        text: qsTr("From the completion date")
        iconName: "check"
        checkable: true
        checked: menu.whenDone
        onTriggered: {
            checked = Qt.binding(() => menu.whenDone)
            menu.chosen("recurrence", menu.whenDone ? menu.baseRule : menu.baseRule + " when done", 0)
        }
    }

    // ---- Priorities ---------------------------------------------------------------------------------
    PriorityItem { objectName: "dateChipPriorityHighest"; shown: menu.priorityKind && !menu.logseq; level: "highest"; text: qsTr("Highest"); iconName: "arrow-up" }
    PriorityItem { objectName: "dateChipPriorityHigh"; shown: menu.priorityKind; level: "high"; text: qsTr("High"); iconName: "chevron-up" }
    PriorityItem { objectName: "dateChipPriorityMedium"; shown: menu.priorityKind; level: "medium"; text: qsTr("Medium"); iconName: "minus" }
    PriorityItem { objectName: "dateChipPriorityLow"; shown: menu.priorityKind; level: "low"; text: qsTr("Low"); iconName: "chevron-down" }
    PriorityItem { objectName: "dateChipPriorityLowest"; shown: menu.priorityKind && !menu.logseq; level: "lowest"; text: qsTr("Lowest"); iconName: "arrow-down" }

    // ---- A reminder: the alarm, the block's rule and priority ---------------------------------------
    ShownSeparator { shown: menu.reminder }
    ShownItem {
        objectName: "dateChipAlarm"
        shown: menu.reminder
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
    BMenu {
        id: repeatMenu
        objectName: "dateChipRepeatMenu"
        title: qsTr("Repeat")
        iconName: "repeat"
        BMenuItem {
            objectName: "dateChipRepeatChange"
            text: qsTr("Change rule…")
            iconName: "repeat"
            onTriggered: menu.chosen("blockRecurrenceChange", 0, 0)
        }
        BMenuSeparator {}
        RuleItem { objectName: "dateChipRepeatDaily"; chosenAction: "blockRecurrence"; ruleText: "every day"; text: qsTr("Daily") }
        RuleItem { objectName: "dateChipRepeatWeekdays"; chosenAction: "blockRecurrence"; ruleText: "every weekday"; text: qsTr("Weekdays") }
        RuleItem { objectName: "dateChipRepeatWeekly"; chosenAction: "blockRecurrence"; ruleText: "every week"; text: qsTr("Weekly") }
        RuleItem { objectName: "dateChipRepeatTwoWeeks"; chosenAction: "blockRecurrence"; ruleText: "every 2 weeks"; text: qsTr("Every 2 weeks") }
        RuleItem { objectName: "dateChipRepeatMonthly"; chosenAction: "blockRecurrence"; ruleText: "every month"; text: qsTr("Monthly") }
        RuleItem { objectName: "dateChipRepeatYearly"; chosenAction: "blockRecurrence"; ruleText: "every year"; text: qsTr("Yearly") }
        ShownSeparator { shown: menu.rule !== "" }
        ShownItem {
            objectName: "dateChipRepeatNone"
            shown: menu.rule !== ""
            text: qsTr("Don't repeat")
            iconName: "x"
            danger: true
            onTriggered: menu.chosen("blockRecurrence", "", 0)
        }
    }
    BMenu {
        id: priorityMenu
        objectName: "dateChipPriorityMenu"
        title: qsTr("Priority")
        iconName: "arrow-up"
        PriorityItem { objectName: "dateChipTaskPriorityHighest"; chosenAction: "blockPriority"; shown: !menu.logseq; level: "highest"; text: qsTr("Highest"); iconName: "arrow-up" }
        PriorityItem { objectName: "dateChipTaskPriorityHigh"; chosenAction: "blockPriority"; level: "high"; text: qsTr("High"); iconName: "chevron-up" }
        PriorityItem { objectName: "dateChipTaskPriorityMedium"; chosenAction: "blockPriority"; level: "medium"; text: qsTr("Medium"); iconName: "minus" }
        PriorityItem { objectName: "dateChipTaskPriorityLow"; chosenAction: "blockPriority"; level: "low"; text: qsTr("Low"); iconName: "chevron-down" }
        PriorityItem { objectName: "dateChipTaskPriorityLowest"; chosenAction: "blockPriority"; shown: !menu.logseq; level: "lowest"; text: qsTr("Lowest"); iconName: "arrow-down" }
        ShownSeparator { shown: menu.priority !== "" }
        ShownItem {
            objectName: "dateChipTaskPriorityNone"
            shown: menu.priority !== ""
            text: qsTr("No priority")
            iconName: "x"
            danger: true
            onTriggered: menu.chosen("blockPriority", "", 0)
        }
    }

    // ---- Remove ---------------------------------------------------------------------------------------
    ShownSeparator { shown: !menu.day }
    ShownItem {
        objectName: "dateChipRemove"
        shown: !menu.day
        text: menu.reminder ? qsTr("Delete reminder")
                            : menu.recurrence ? qsTr("Don't repeat")
                            : menu.priorityKind ? qsTr("No priority")
                            : qsTr("Remove date")
        iconName: "trash-2"
        danger: true
        onTriggered: menu.chosen("remove", 0, 0)
    }
}
