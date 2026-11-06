pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// TasksTimeDialog — "Time spent" of the Tasks view (docs/UI.md "Tasks: dependencies, subtasks and
// time"): the time clocked on the listed tasks (their `:LOGBOOK:` CLOCK lines, docs/SYNTAX.md §5;
// the view's filters apply) per day, page, tag or task, for today, this week, this month or
// everything (TaskListModel.timeSummary). A task row opens the task.
BDialog {
    id: dialog
    objectName: "tasksTimeDialog"

    /// The TaskListModel whose tasks are summed.
    property TaskListModel tasks: null
    property string group: "day"
    property string range: "week"
    /// TaskListModel.timeSummary(): { rows: [{ label, duration, tasks, path, line, … }], totalLabel, tasks }.
    readonly property var summary: summaryHolder.map
    readonly property list<var> ranges: [{ value: "today", text: qsTr("Today") }, { value: "week", text: qsTr("This week") },
                                         { value: "month", text: qsTr("This month") }, { value: "all", text: qsTr("All") }]
    readonly property list<var> groups: [{ value: "day", text: qsTr("Days") }, { value: "page", text: qsTr("Pages") },
                                         { value: "tag", text: qsTr("Tags") }, { value: "task", text: qsTr("Tasks") }]

    signal taskOpened(string path, int line)

    title: qsTr("Time spent")
    iconName: "clock"
    showReject: false
    acceptText: qsTr("Close")

    function refresh(): void {
        summaryHolder.map = dialog.tasks ? dialog.tasks.timeSummary(dialog.group, dialog.range) : ({})
    }
    function rows(): list<var> {
        if (summaryHolder.map.rows === undefined)
            return []
        return summaryHolder.map.rows
    }

    MapHolder {
        id: summaryHolder
    }
    onAboutToShow: refresh()
    onGroupChanged: refresh()
    onRangeChanged: refresh()

    BSegmented {
        objectName: "tasksTimeRange"
        Layout.fillWidth: true
        model: dialog.ranges
        fill: true
        currentValue: dialog.range
        onActivated: (value) => dialog.range = value
    }
    BSegmented {
        objectName: "tasksTimeGroup"
        Layout.fillWidth: true
        model: dialog.groups
        fill: true
        currentValue: dialog.group
        onActivated: (value) => dialog.group = value
    }
    BText {
        objectName: "tasksTimeTotal"
        Layout.fillWidth: true
        variant: "label"
        text: summaryHolder.map.tasks > 0
              ? qsTr("Total: %1 · %n task(s)", "", summaryHolder.map.tasks).arg(summaryHolder.map.totalLabel)
              : qsTr("No time was logged on these tasks in this period. Start a timer from a task's menu.")
        wrapMode: Text.Wrap
    }
    Repeater {
        model: dialog.rows()
        delegate: BListItem {
            id: timeRow
            required property string label
            required property string duration
            required property int tasks
            required property string path
            required property int line
            required property int index
            objectName: "tasksTimeRow_" + index
            Layout.fillWidth: true
            text: label !== "" ? label : qsTr("No tag")
            subtitle: dialog.group === "task" ? "" : qsTr("%n task(s)", "", tasks)
            trailing: BText { text: timeRow.duration; variant: "label" }
            onClicked: {
                if (dialog.group === "task" && timeRow.path !== "") {
                    dialog.close()
                    dialog.taskOpened(timeRow.path, timeRow.line)
                }
            }
        }
    }
}
