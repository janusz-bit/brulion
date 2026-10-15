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

    spacing: Theme.space2

    readonly property var statusChoices: [
        { value: "all", text: qsTr("All tasks") },
        { value: "open", text: qsTr("Open") },
        { value: "todo", text: qsTr("To do") },
        { value: "doing", text: qsTr("In progress") },
        { value: "deferred", text: qsTr("Waiting") },
        { value: "done", text: qsTr("Done") },
        { value: "cancelled", text: qsTr("Cancelled") }
    ]
    readonly property var priorityChoices: [
        { value: "", text: qsTr("Any priority") },
        { value: "highest", text: qsTr("Highest") },
        { value: "high", text: qsTr("High") },
        { value: "medium", text: qsTr("Medium") },
        { value: "none", text: qsTr("Normal (none)") },
        { value: "low", text: qsTr("Low") },
        { value: "lowest", text: qsTr("Lowest") }
    ]
    readonly property var sortChoices: [
        { value: "due", text: qsTr("By date") },
        { value: "priority", text: qsTr("By priority") },
        { value: "page", text: qsTr("By page") },
        { value: "created", text: qsTr("Newest first") }
    ]
    function labelOf(choices, value) {
        for (const c of choices)
            if (c.value === value)
                return c.text
        return ""
    }
    function focusSearch() { search.forceActiveFocus() }
    function pageTitle(path) {
        const name = path.split("/").pop()
        return name.endsWith(".md") ? name.slice(0, -3) : name
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
            text: root.labelOf(root.statusChoices, root.tasks.statusFilter)
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
                tagPalette.items = root.tasks.tagChoices(0).map(c => ({ title: c.label, subtitle: qsTr("%n tasks", "", c.count), icon: "hash", value: c.value }))
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
                pagePalette.items = root.tasks.pageChoices(0).map(c => ({ title: c.label, subtitle: c.folder !== "" ? c.folder + " · " + qsTr("%n tasks", "", c.count) : qsTr("%n tasks", "", c.count), icon: "file-text", value: c.value }))
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
                folderPalette.items = root.tasks.folderChoices(0).map(c => ({ title: c.label, subtitle: qsTr("%n tasks", "", c.count), icon: "folder", value: c.value }))
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
            text: root.tasks.priorityFilter !== "" ? root.labelOf(root.priorityChoices, root.tasks.priorityFilter) : qsTr("Priority")
            onClicked: priorityMenu.popup(priorityChip, 0, priorityChip.height)
            onRemoveClicked: root.tasks.priorityFilter = ""
        }
        BChip {
            id: sortChip
            interactive: true
            iconName: "chevrons-up-down"
            text: root.labelOf(root.sortChoices, root.tasks.sortKey)
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
            required property var modelData
            text: modelData.text
            checkable: true
            checked: root.tasks.statusFilter === modelData.value
            onTriggered: root.tasks.statusFilter = modelData.value
        }
        onObjectAdded: (index, object) => statusMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => statusMenu.removeItem(object)
    }
    BMenu {
        id: priorityMenu
    }
    Instantiator {
        model: root.priorityChoices
        delegate: BMenuItem {
            required property var modelData
            text: modelData.text
            checkable: true
            checked: root.tasks.priorityFilter === modelData.value
            onTriggered: root.tasks.priorityFilter = modelData.value
        }
        onObjectAdded: (index, object) => priorityMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => priorityMenu.removeItem(object)
    }
    BMenu {
        id: sortMenu
    }
    Instantiator {
        model: root.sortChoices
        delegate: BMenuItem {
            required property var modelData
            text: modelData.text
            checkable: true
            checked: root.tasks.sortKey === modelData.value
            onTriggered: root.tasks.sortKey = modelData.value
        }
        onObjectAdded: (index, object) => sortMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => sortMenu.removeItem(object)
    }

    BPalette {
        id: tagPalette
        placeholderText: qsTr("Filter by tag…")
        emptyTitle: qsTr("No tags on tasks")
        defaultIcon: "hash"
        onItemActivated: (item) => root.tasks.tagFilter = item.value
    }
    BPalette {
        id: pagePalette
        placeholderText: qsTr("Filter by page…")
        emptyTitle: qsTr("No pages with tasks")
        defaultIcon: "file-text"
        onItemActivated: (item) => root.tasks.pageFilter = item.value
    }
    BPalette {
        id: folderPalette
        placeholderText: qsTr("Filter by folder…")
        emptyTitle: qsTr("No folders with tasks")
        defaultIcon: "folder"
        onItemActivated: (item) => root.tasks.folderFilter = item.value
    }
}
