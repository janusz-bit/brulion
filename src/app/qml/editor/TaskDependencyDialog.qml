pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// TaskDependencyDialog — "Dodaj zależność…" of a task's menu (docs/SYNTAX.md §5 "Dependencies"):
// a search over the notebook's open tasks; the chosen one gets a `🆔` when it has none, and the
// task a `⛔ id` (the caller writes them: PageController.addTaskDependency in the editor,
// PlanningActions.addDependency in Tasks).
//
//   TaskDependencyDialog {
//       candidates: (text) => controller.dependencyCandidates(row, text)
//       onChosen: (path, line) => controller.addTaskDependency(row, path, line)
//   }
//
// `candidates(text)` gives [{ path, line, text, title, id, status }] (notebook::dependencyCandidates:
// words of the text, diacritics ignored).
BDialog {
    id: dialog
    objectName: "taskDependencyDialog"

    /// (text) => the tasks to offer.
    property var candidates: (text) => []
    property var items: []
    property int current: -1

    signal chosen(string path, int line)

    title: qsTr("Add dependency")
    subtitle: qsTr("This task waits until the chosen task is done.")
    iconName: "link"
    acceptText: qsTr("Add")
    acceptEnabled: current >= 0 && current < items.length
    initialFocusItem: field

    function refresh() {
        items = candidates(field.text) || []
        current = items.length > 0 ? 0 : -1
    }

    onAboutToShow: {
        field.text = ""
        refresh()
    }
    onAccepted: {
        const item = items[current]
        if (item)
            chosen(item.path, item.line)
    }

    BSearchField {
        id: field
        objectName: "taskDependencySearch"
        Layout.fillWidth: true
        placeholderText: qsTr("Search open tasks")
        debounce: 0
        onTextChanged: dialog.refresh()
        Keys.onDownPressed: dialog.current = Math.min(dialog.items.length - 1, dialog.current + 1)
        Keys.onUpPressed: dialog.current = Math.max(0, dialog.current - 1)
        Keys.onReturnPressed: dialog.tryAccept()
        Keys.onEnterPressed: dialog.tryAccept()
    }
    ListView {
        id: list
        objectName: "taskDependencyList"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 6 * Theme.listItemHeightTwoLine)
        clip: true
        model: dialog.items
        currentIndex: dialog.current
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        delegate: BListItem {
            required property var modelData
            required property int index
            objectName: "taskDependencyItem_" + index
            width: ListView.view.width
            iconName: modelData.status === "doing" ? "clock" : "square"
            text: modelData.text
            subtitle: modelData.title + (modelData.id ? "  🆔 " + modelData.id : "")
            selected: index === dialog.current
            onClicked: {
                dialog.current = index
                dialog.tryAccept()
            }
        }
    }
    BText {
        visible: dialog.items.length === 0
        Layout.fillWidth: true
        variant: "muted"
        wrapMode: Text.Wrap
        text: field.text === "" ? qsTr("The notebook has no other open tasks.") : qsTr("No open task matches.")
    }
}
