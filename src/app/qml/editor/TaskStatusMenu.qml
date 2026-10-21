pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// TaskStatusMenu — the menu that gives a task its status (docs/SYNTAX.md §5): Do zrobienia `[ ]`,
// W toku `[/]`, Zrobione `[x]`, Oczekujące `[>]`, Anulowane `[-]` and Bez statusu (the checkbox or
// Logseq keyword goes, the text stays), the current status checked. One menu everywhere a task
// checkbox is shown: a right-click or long press on a TaskBox (the editor, query results,
// embeds, backlinks and tags, the calendar, Tasks), the editor's Ctrl+Shift+Enter and the touch
// toolbar's task button held down; Tasks rows put "Otwórz" first (`showOpen`).
//
//   TaskStatusMenu { id: menu; status: task.status; onStatusChosen: (status) => setStatus(status) }
//   menu.popup(box, 0, box.height)
//
// Status names as in the models: "todo" | "doing" | "done" | "deferred" | "cancelled"; "" is no
// status (a block that is no task: nothing checked but "Bez statusu"). statusChosen() comes with
// the choice also when it is the current status; callers write only changes. React to it with
// the last statement of the handler: writing the status may destroy the checkbox that owns the
// menu (no status, a query result that leaves the query).
BMenu {
    id: menu

    /// The task's current status ("" = none).
    property string status
    /// "Otwórz" (openRequested()) above the statuses.
    property bool showOpen: false

    signal statusChosen(string status)
    signal openRequested()

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

    /// A checkable item toggles its own `checked` when clicked; give it its binding back first.
    function choose(item: BMenuItem, value: string) {
        item.checked = Qt.binding(() => menu.status === value)
        menu.statusChosen(value)
    }
    /// Opened from the keyboard: the current status highlighted, Up / Down move from there.
    function highlightStatus() {
        for (let i = 0; i < count; ++i) {
            const entry = itemAt(i)
            if (entry && entry.objectName === "taskStatus_" + (status === "" ? "none" : status)) {
                currentIndex = i
                return
            }
        }
    }

    /// A status: checked when it is the task's. It takes the keyboard focus from Up / Down (T.Menu
    /// moves only to items that do — BMenuItem itself takes none), and Enter chooses it.
    component StatusItem: BMenuItem {
        id: statusItem
        required property string value
        objectName: "taskStatus_" + (value === "" ? "none" : value)
        checkable: true
        checked: menu.status === value
        focusPolicy: Qt.TabFocus
        onTriggered: menu.choose(statusItem, value)
        Keys.onReturnPressed: {
            menu.close()
            menu.choose(statusItem, value)
        }
        Keys.onEnterPressed: {
            menu.close()
            menu.choose(statusItem, value)
        }
    }

    BMenuItem {
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
}
