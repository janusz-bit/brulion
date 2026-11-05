pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// TasksFilterBar — the filters of TasksView as chips: status, tag, page, folder, priority and the
// sort order (menus; tags, pages and folders through a searchable list — a big vault has many),
// a text search, "Clear" and "Show as query". A chosen filter is a selected chip with an × to
// remove it. The chips wrap on narrow windows.
//
//   TasksFilterBar { tasks: taskModel; onQueryRequested: queryDialog.open() }
ColumnLayout {
    id: root

    required property TaskListModel tasks

    signal queryRequested()
    /// "Czas pracy": the time summary of the listed tasks (TasksTimeDialog).
    signal timeRequested()

    spacing: Theme.space2

    readonly property list<var> statusChoices: [
        { value: "all", text: qsTr("All tasks") },
        { value: "open", text: qsTr("Open") },
        { value: "todo", text: qsTr("To do") },
        { value: "doing", text: qsTr("In progress") },
        { value: "deferred", text: qsTr("Waiting") },
        { value: "done", text: qsTr("Done") },
        { value: "cancelled", text: qsTr("Cancelled") }
    ]
    readonly property list<var> priorityChoices: [
        { value: "", text: qsTr("Any priority") },
        { value: "highest", text: qsTr("Highest") },
        { value: "high", text: qsTr("High") },
        { value: "medium", text: qsTr("Medium") },
        { value: "none", text: qsTr("Normal (none)") },
        { value: "low", text: qsTr("Low") },
        { value: "lowest", text: qsTr("Lowest") }
    ]
    readonly property list<var> sortChoices: [
        { value: "due", text: qsTr("By date") },
        { value: "priority", text: qsTr("By priority") },
        { value: "page", text: qsTr("By page") },
        { value: "created", text: qsTr("Newest first") }
    ]
    /// The text of the choice `value` of statusChoices, priorityChoices or sortChoices.
    function statusLabel(value: string): string {
        switch (value) {
        case "all": return qsTr("All tasks")
        case "open": return qsTr("Open")
        case "todo": return qsTr("To do")
        case "doing": return qsTr("In progress")
        case "deferred": return qsTr("Waiting")
        case "done": return qsTr("Done")
        case "cancelled": return qsTr("Cancelled")
        default: return ""
        }
    }
    function priorityLabel(value: string): string {
        switch (value) {
        case "": return qsTr("Any priority")
        case "highest": return qsTr("Highest")
        case "high": return qsTr("High")
        case "medium": return qsTr("Medium")
        case "none": return qsTr("Normal (none)")
        case "low": return qsTr("Low")
        case "lowest": return qsTr("Lowest")
        default: return ""
        }
    }
    function sortLabel(value: string): string {
        switch (value) {
        case "due": return qsTr("By date")
        case "priority": return qsTr("By priority")
        case "page": return qsTr("By page")
        case "created": return qsTr("Newest first")
        default: return ""
        }
    }
    function focusSearch(): void { search.forceActiveFocus() }
    function pageTitle(path: string): string {
        return root.withoutMd(Js.sliceFrom(path, Js.lastIndexOf(path, "/") + 1))
    }
    function withoutMd(name: string): string {
        return Js.endsWith(name, ".md") ? Js.slice(name, 0, -3) : name
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space2

        BSearchField {
            id: search
            Layout.fillWidth: true
            Layout.maximumWidth: Theme.compact ? -1 : 360
            size: "small"
            placeholderText: qsTr("Filter tasks…")
            text: root.tasks.textFilter
            onSearch: (query) => root.tasks.textFilter = query
        }
        Item { Layout.fillWidth: !Theme.compact; visible: !Theme.compact }
        BButton {
            objectName: "tasksTimeButton"
            size: "small"
            variant: "ghost"
            iconName: "clock"
            text: Theme.compact ? "" : qsTr("Time spent")
            Accessible.name: qsTr("Time spent")
            onClicked: root.timeRequested()
        }
        BButton {
            size: "small"
            variant: "ghost"
            iconName: "code"
            text: Theme.compact ? "" : qsTr("Show as query")
            Accessible.name: qsTr("Show as query")
            onClicked: root.queryRequested()
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: 6

        BChip {
            id: statusChip
            interactive: true
            selected: root.tasks.statusFilter !== "all"
            iconName: "circle-check"
            text: root.statusLabel(root.tasks.statusFilter)
            onClicked: statusMenu.popup(statusChip, 0, statusChip.height)
        }
        BChip {
            id: tagChip
            interactive: true
            removable: root.tasks.tagFilter !== ""
            selected: root.tasks.tagFilter !== ""
            iconName: "hash"
            text: root.tasks.tagFilter !== "" ? root.tasks.tagFilter : qsTr("Tag")
            onClicked: {
                tagPalette.items = root.tasks.paletteChoices("tag")
                tagPalette.open()
            }
            onRemoveClicked: root.tasks.tagFilter = ""
        }
        BChip {
            interactive: true
            removable: root.tasks.pageFilter !== ""
            selected: root.tasks.pageFilter !== ""
            iconName: "file-text"
            text: root.tasks.pageFilter !== "" ? root.pageTitle(root.tasks.pageFilter) : qsTr("Page")
            onClicked: {
                pagePalette.items = root.tasks.paletteChoices("page")
                pagePalette.open()
            }
            onRemoveClicked: root.tasks.pageFilter = ""
        }
        BChip {
            interactive: true
            removable: root.tasks.folderFilter !== ""
            selected: root.tasks.folderFilter !== ""
            iconName: "folder"
            text: root.tasks.folderFilter !== "" ? root.tasks.folderFilter : qsTr("Folder")
            onClicked: {
                folderPalette.items = root.tasks.paletteChoices("folder")
                folderPalette.open()
            }
            onRemoveClicked: root.tasks.folderFilter = ""
        }
        BChip {
            id: priorityChip
            interactive: true
            removable: root.tasks.priorityFilter !== ""
            selected: root.tasks.priorityFilter !== ""
            iconName: "arrow-up"
            text: root.tasks.priorityFilter !== "" ? root.priorityLabel(root.tasks.priorityFilter) : qsTr("Priority")
            onClicked: priorityMenu.popup(priorityChip, 0, priorityChip.height)
            onRemoveClicked: root.tasks.priorityFilter = ""
        }
        BChip {
            id: sortChip
            interactive: true
            iconName: "chevrons-up-down"
            text: root.sortLabel(root.tasks.sortKey)
            Accessible.name: qsTr("Sort: %1").arg(text)
            onClicked: sortMenu.popup(sortChip, 0, sortChip.height)
        }
        BButton {
            visible: root.tasks.filtered
            size: "small"
            variant: "ghost"
            iconName: "x"
            text: qsTr("Clear filters")
            onClicked: root.tasks.clearFilters()
        }
    }

    BMenu {
        id: statusMenu
    }
    Instantiator {
        model: root.statusChoices
        delegate: BMenuItem {
            id: choiceItem
            required text
            required property string value
            checkable: true
            checked: root.tasks.statusFilter === choiceItem.value
            onTriggered: root.tasks.statusFilter = choiceItem.value
        }
        onObjectAdded: (index, object) => statusMenu.insertItem(index, object as Item)
        onObjectRemoved: (index, object) => statusMenu.removeItem(object as Item)
    }
    BMenu {
        id: priorityMenu
    }
    Instantiator {
        model: root.priorityChoices
        delegate: BMenuItem {
            id: choiceItem
            required text
            required property string value
            checkable: true
            checked: root.tasks.priorityFilter === choiceItem.value
            onTriggered: root.tasks.priorityFilter = choiceItem.value
        }
        onObjectAdded: (index, object) => priorityMenu.insertItem(index, object as Item)
        onObjectRemoved: (index, object) => priorityMenu.removeItem(object as Item)
    }
    BMenu {
        id: sortMenu
    }
    Instantiator {
        model: root.sortChoices
        delegate: BMenuItem {
            id: choiceItem
            required text
            required property string value
            checkable: true
            checked: root.tasks.sortKey === choiceItem.value
            onTriggered: root.tasks.sortKey = choiceItem.value
        }
        onObjectAdded: (index, object) => sortMenu.insertItem(index, object as Item)
        onObjectRemoved: (index, object) => sortMenu.removeItem(object as Item)
    }

    BPalette {
        id: tagPalette
        placeholderText: qsTr("Filter by tag…")
        emptyTitle: qsTr("No tags on tasks")
        defaultIcon: "hash"
        onItemActivated: (item) => root.tasks.tagFilter = AppController.routeText(item, "value")
    }
    BPalette {
        id: pagePalette
        placeholderText: qsTr("Filter by page…")
        emptyTitle: qsTr("No pages with tasks")
        defaultIcon: "file-text"
        onItemActivated: (item) => root.tasks.pageFilter = AppController.routeText(item, "value")
    }
    BPalette {
        id: folderPalette
        placeholderText: qsTr("Filter by folder…")
        emptyTitle: qsTr("No folders with tasks")
        defaultIcon: "folder"
        onItemActivated: (item) => root.tasks.folderFilter = AppController.routeText(item, "value")
    }
}
