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
    /// "Time spent": the time summary of the listed tasks (TasksTimeDialog).
    signal timeRequested()

    spacing: Theme.space2

    // The values of the filter menus (their texts: statusLabel(), priorityLabel(), sortLabel()).
    readonly property list<string> statusChoices: ["all", "open", "todo", "doing", "deferred", "done", "cancelled"]
    readonly property list<string> priorityChoices: ["", "highest", "high", "medium", "none", "low", "lowest"]
    readonly property list<string> sortChoices: ["due", "priority", "page", "created"]
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

    /// A filter chip: selected with an × to remove it when `filter` is set (it shows `shown`), else
    /// the `fallback` name of the filter.
    component FilterChip: BChip {
        property string filter
        property string shown: filter
        property string fallback
        interactive: true
        removable: filter !== ""
        selected: filter !== ""
        text: filter !== "" ? shown : fallback
    }
    /// Fills `menu` with the items of the Instantiator.
    component MenuFiller: Instantiator {
        required property BMenu menu
        onObjectAdded: (index, object) => menu.insertItem(index, object as Item)
        onObjectRemoved: (index, object) => menu.removeItem(object as Item)
    }
    component ChoiceItem: BMenuItem {
        required property string modelData
        checkable: true
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
        FilterChip {
            id: tagChip
            filter: root.tasks.tagFilter
            fallback: qsTr("Tag")
            iconName: "hash"
            onClicked: {
                tagPalette.items = root.tasks.paletteChoices("tag")
                tagPalette.open()
            }
            onRemoveClicked: root.tasks.tagFilter = ""
        }
        FilterChip {
            filter: root.tasks.pageFilter
            shown: root.pageTitle(filter)
            fallback: qsTr("Page")
            iconName: "file-text"
            onClicked: {
                pagePalette.items = root.tasks.paletteChoices("page")
                pagePalette.open()
            }
            onRemoveClicked: root.tasks.pageFilter = ""
        }
        FilterChip {
            filter: root.tasks.folderFilter
            fallback: qsTr("Folder")
            iconName: "folder"
            onClicked: {
                folderPalette.items = root.tasks.paletteChoices("folder")
                folderPalette.open()
            }
            onRemoveClicked: root.tasks.folderFilter = ""
        }
        FilterChip {
            id: priorityChip
            filter: root.tasks.priorityFilter
            shown: root.priorityLabel(filter)
            fallback: qsTr("Priority")
            iconName: "arrow-up"
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
    MenuFiller {
        menu: statusMenu
        model: root.statusChoices
        delegate: ChoiceItem {
            text: root.statusLabel(modelData)
            checked: root.tasks.statusFilter === modelData
            onTriggered: root.tasks.statusFilter = modelData
        }
    }
    BMenu {
        id: priorityMenu
    }
    MenuFiller {
        menu: priorityMenu
        model: root.priorityChoices
        delegate: ChoiceItem {
            text: root.priorityLabel(modelData)
            checked: root.tasks.priorityFilter === modelData
            onTriggered: root.tasks.priorityFilter = modelData
        }
    }
    BMenu {
        id: sortMenu
    }
    MenuFiller {
        menu: sortMenu
        model: root.sortChoices
        delegate: ChoiceItem {
            text: root.sortLabel(modelData)
            checked: root.tasks.sortKey === modelData
            onTriggered: root.tasks.sortKey = modelData
        }
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
