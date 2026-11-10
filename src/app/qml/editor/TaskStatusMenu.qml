pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// TaskStatusMenu — the menu that gives a task its status (docs/SYNTAX.md §5): To do `[ ]`,
// In progress `[/]`, Done `[x]`, Waiting `[>]`, Cancelled `[-]` and No status (the checkbox or
// Logseq keyword goes, the text stays), the current status checked. One menu everywhere a task
// checkbox is shown: a right-click or long press on a TaskBox (the editor, query results,
// embeds, backlinks and tags, the calendar, Tasks), the editor's Ctrl+Shift+Enter and the touch
// toolbar's task button held down; Tasks rows put "Open" first (`showOpen`).
//
//   TaskStatusMenu { id: menu; status: task.status; onStatusChosen: (status) => setStatus(status) }
//   menu.popup(box, 0, box.height)
//
// Status names as in the models: "todo" | "doing" | "done" | "deferred" | "cancelled"; "" is no
// status (a block that is no task: nothing checked but "No status"). statusChosen() comes with
// the choice also when it is the current status; callers write only changes. React to it with
// the last statement of the handler: writing the status may destroy the checkbox that owns the
// menu (no status, a query result that leaves the query).
//
// With `showTaskTools` (the editor, Tasks) the menu also starts or stops the task's timer
// (clockRequested(); `clockRunning` names the action) and adds a dependency
// (dependencyRequested(): the picker), docs/SYNTAX.md §5.
BMenu {
    id: menu

    /// The task's current status ("" = none).
    property string status
    /// "Open" (openRequested()) above the statuses.
    property bool showOpen: false

    /// The timer and dependency items below the statuses.
    property bool showTaskTools: false
    /// The task's timer runs ("Stop timer" instead of "Start timer").
    property bool clockRunning: false

    signal statusChosen(string status)
    signal openRequested()
    signal clockRequested()
    signal dependencyRequested()

    title: qsTr("Task status")

    /// Its height once open, known before it is shown (to place it above something).
    function openHeight(): real {
        let h = topPadding + bottomPadding
        for (let i = showOpen ? 0 : 2; i < count; ++i) {
            const entry = itemAt(i)
            if (entry)
                h += entry.implicitHeight
        }
        return h
    }

    /// A checkable item toggles its own `checked` when clicked; the status items' bindings read
    /// this: a click bumps it to give them their state back (an assignment would end the binding).
    property int checkRevision: 0
    /// Opened from the keyboard: the current status highlighted, Up / Down move from there.
    function highlightStatus(): void {
        for (let i = 0; i < count; ++i) {
            const entry = itemAt(i)
            if (entry && entry.objectName === "taskStatus_" + (status === "" ? "none" : status)) {
                currentIndex = i
                return
            }
        }
    }

    /// A status: checked when it is the task's.
    component StatusItem: BMenuItem {
        required property string value
        objectName: "taskStatus_" + (value === "" ? "none" : value)
        checkable: true
        checked: menu.checkRevision >= 0 && menu.status === value
        onTriggered: {
            ++menu.checkRevision
            menu.statusChosen(value)
        }
    }

    BMenuItem {
        objectName: "taskStatusOpen"
        visible: menu.showOpen
        height: visible ? implicitHeight : 0
        text: qsTr("Open")
        iconName: "file-text"
        onTriggered: menu.openRequested()
    }
    BMenuSeparator {
        visible: menu.showOpen
        height: visible ? implicitHeight : 0
    }
    StatusItem { value: "todo"; text: qsTr("To do") }
    StatusItem { value: "doing"; text: qsTr("In progress") }
    StatusItem { value: "done"; text: qsTr("Done") }
    StatusItem { value: "deferred"; text: qsTr("Waiting") }
    StatusItem { value: "cancelled"; text: qsTr("Cancelled") }
    BMenuSeparator {}
    StatusItem { value: ""; text: qsTr("No status") }

    /// An action of the task tools: shown with showTaskTools.
    component ToolItem: BMenuItem {
        visible: menu.showTaskTools
        height: visible ? implicitHeight : 0
    }
    BMenuSeparator {
        visible: menu.showTaskTools
        height: visible ? implicitHeight : 0
    }
    ToolItem {
        objectName: "taskClockToggle"
        text: menu.clockRunning ? qsTr("Stop timer") : qsTr("Start timer")
        iconName: menu.clockRunning ? "square" : "play"
        onTriggered: menu.clockRequested()
    }
    ToolItem {
        objectName: "taskAddDependency"
        text: qsTr("Add dependency…")
        iconName: "link"
        onTriggered: menu.dependencyRequested()
    }
}
