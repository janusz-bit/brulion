pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// TasksTimeDialog — "Czas pracy" of the Tasks view (docs/UI.md "Tasks: dependencies, subtasks and
// time"): the time clocked on the listed tasks (their `:LOGBOOK:` CLOCK lines, docs/SYNTAX.md §5;
// the view's filters apply) per day, page, tag or task, for today, this week, this month or
// everything (TaskListModel.timeSummary). A task row opens the task.
BDialog {
    id: dialog
    objectName: "tasksTimeDialog"

    /// The TaskListModel whose tasks are summed.
    property var tasks: null
    property string group: "day"
    property string range: "week"
    property var summary: ({ rows: [], totalLabel: "", tasks: 0 })

    signal taskOpened(string path, int line)

    title: qsTr("Time spent")
    iconName: "clock"
    showReject: false
    acceptText: qsTr("Close")

    function refresh() {
        summary = tasks ? tasks.timeSummary(group, range) : { rows: [], totalLabel: "", tasks: 0 }
    }
    onAboutToShow: refresh()
    onGroupChanged: refresh()
    onRangeChanged: refresh()

    BSegmented {
        objectName: "tasksTimeRange"
        Layout.fillWidth: true
        model: [{ value: "today", text: qsTr("Today") }, { value: "week", text: qsTr("This week") },
                { value: "month", text: qsTr("This month") }, { value: "all", text: qsTr("All") }]
        fill: true
        currentValue: dialog.range
        onActivated: (value) => dialog.range = value
    }
    BSegmented {
        objectName: "tasksTimeGroup"
        Layout.fillWidth: true
        model: [{ value: "day", text: qsTr("Days") }, { value: "page", text: qsTr("Pages") },
                { value: "tag", text: qsTr("Tags") }, { value: "task", text: qsTr("Tasks") }]
        fill: true
        currentValue: dialog.group
        onActivated: (value) => dialog.group = value
    }
    BText {
        objectName: "tasksTimeTotal"
        Layout.fillWidth: true
        variant: "label"
        text: dialog.summary.tasks > 0
              ? qsTr("Total: %1 · %n task(s)", "", dialog.summary.tasks).arg(dialog.summary.totalLabel)
              : qsTr("No time was logged on these tasks in this period. Start a timer from a task's menu.")
        wrapMode: Text.Wrap
    }
    Repeater {
        model: dialog.summary.rows
        delegate: BListItem {
            required property var modelData
            required property int index
            objectName: "tasksTimeRow_" + index
            Layout.fillWidth: true
            text: modelData.label !== "" ? modelData.label : qsTr("No tag")
            subtitle: dialog.group === "task" ? "" : qsTr("%n task(s)", "", modelData.tasks)
            trailing: BText { text: modelData.duration; variant: "label" }
            onClicked: {
                if (dialog.group === "task" && modelData.path) {
                    dialog.close()
                    dialog.taskOpened(modelData.path, modelData.line)
                }
            }
        }
    }
}
