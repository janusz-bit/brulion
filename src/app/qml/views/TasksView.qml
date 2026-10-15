pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// TasksView — route "tasks" ({query?}): every task of the vault (TaskListModel over
// PlanningContext.vaultIndex) in sections Zaległe / Dziś / Jutro / Ten tydzień / Później / Bez
// terminu / Zrobione (collapsed): tick them off inline (undo in the toast), see their dates,
// reminders, recurrence and priority as chips, open them at their line, set any status from the
// context menu. Filters (status, tag, page, folder, priority, text) and the sort order sit in
// TasksFilterBar; "Show as query" turns them into the equivalent ```query block (SYNTAX §10);
// route.query pre-sets them. Quick add with natural-language dates writes into today's journal.
//
// Keys: Up/Down move through the tasks, Space toggles, Enter opens, Ctrl+N… stays with the shell;
// "/" focuses the filter field, "n" the quick-add field.
//
// Route parameters: query (filters, e.g. "tag:praca status:open"), open ("query" = the query
// dialog, "add:<text>" = the quick-add field filled — screenshots).
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Tasks")
    /// Text width of the list on wide windows.
    readonly property int maximumWidth: 860

    readonly property var rendererPalette: ({
        link: Theme.link, unresolved: Theme.linkMissing, muted: Theme.fgMuted,
        codeBackground: Theme.bgSunken, highlightBackground: Theme.mark,
        tagFg: Theme.tagFg, tagBg: Theme.tagBg,
        dueFg: Theme.chipDueFg, dueBg: Theme.chipDueBg,
        overdueFg: Theme.chipOverdueFg, overdueBg: Theme.chipOverdueBg,
        scheduledFg: Theme.chipScheduledFg, scheduledBg: Theme.chipScheduledBg,
        reminderFg: Theme.chipReminderFg, reminderBg: Theme.chipReminderBg,
        alarmFg: Theme.chipAlarmFg, alarmBg: Theme.chipAlarmBg,
        recurrenceFg: Theme.chipRecurrenceFg, recurrenceBg: Theme.chipRecurrenceBg,
        doneFg: Theme.chipDoneFg, doneBg: Theme.chipDoneBg,
        neutralFg: Theme.chipNeutralFg, neutralBg: Theme.chipNeutralBg
    })

    function applyRoute() {
        if (!route)
            return
        if (route.query)
            taskModel.applyQuery(route.query)
        if (route.open === "query")
            Qt.callLater(queryDialog.open)
        else if (typeof route.open === "string" && route.open.startsWith("add:"))
            Qt.callLater(() => {
                if (quickAdd.field)
                    quickAdd.field.text = route.open.slice(4)
            })
    }
    function toggle(task) {
        const wasClosed = task.status === "done" || task.status === "cancelled"
        if (planningActions.toggleTask(task.path, task.line, task.text))
            toasts.show(wasClosed ? qsTr("Task reopened") : qsTr("Task done"),
                        { kind: "success", actionText: qsTr("Undo"), onAction: () => planningActions.undo() })
    }
    function setStatus(task, status) {
        if (planningActions.setTaskStatus(task.path, task.line, status, task.text))
            toasts.show(qsTr("Status changed"), { actionText: qsTr("Undo"), onAction: () => planningActions.undo() })
    }
    function activateLink(link, task) {
        // A tag filters this list; everything else navigates.
        if (link.startsWith("tag:")) {
            taskModel.tagFilter = decodeURIComponent(link.slice(4))
            return
        }
        planningActions.openLink(link, task.path)
    }

    onRouteChanged: applyRoute()
    Component.onCompleted: {
        applyRoute()
        list.forceActiveFocus()
    }
    focus: true

    Keys.onPressed: (event) => {
        if (event.modifiers !== Qt.NoModifier)
            return
        if (event.text === "/") {
            filterBar.focusSearch()
            event.accepted = true
        } else if (event.key === Qt.Key_N) {
            quickAdd.focusField()
            event.accepted = true
        }
    }

    TaskListModel {
        id: taskModel
        vaultIndex: PlanningContext.vaultIndex
        today: DatesHelper.today
        weekStart: DatesHelper.weekStart
        locale: DatesHelper.locale
        palette: root.rendererPalette
    }
    PlanningActions {
        id: planningActions
        host: PlanningContext.host
        vaultIndex: PlanningContext.vaultIndex
        today: DatesHelper.today
        now: DatesHelper.fixedNow
        weekStart: DatesHelper.weekStart
        locale: DatesHelper.locale
        onFailed: (message) => toasts.show(message, { kind: "warning" })
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ListView {
        id: list
        anchors.fill: parent
        focus: true
        clip: true
        model: taskModel
        reuseItems: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: Theme.narrow ? Theme.space16 + Theme.space8 : Theme.space8
        currentIndex: -1
        T.ScrollBar.vertical: BScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Tasks")

        // The header grows while it lays out (wrapping chips): keep it fully visible until the
        // user scrolls.
        property bool userScrolled: false
        onMovementStarted: userScrolled = true

        // The quick-add field, the filters and the summary scroll with the list.
        header: Item {
            width: list.width
            implicitHeight: headerColumn.implicitHeight + Theme.space2
            height: implicitHeight
            onHeightChanged: if (!list.userScrolled) Qt.callLater(list.positionViewAtBeginning)
            ColumnLayout {
                id: headerColumn
                width: Math.min(parent.width - 2 * (Theme.narrow ? Theme.space3 : Theme.space6), root.maximumWidth)
                x: (parent.width - width) / 2
                y: Theme.narrow ? Theme.space3 : Theme.space5
                spacing: Theme.space3

                TasksQuickAdd {
                    id: quickAddField
                    Layout.fillWidth: true
                    actions: planningActions
                    onSubmitted: (line, parsed) => {
                        const path = Workspace.ensureJournal(DatesHelper.today)
                        if (path === "" || !planningActions.appendBlock(path, line)) {
                            toasts.show(qsTr("The journal could not be saved."), { kind: "error" })
                            return
                        }
                        toasts.show(parsed.when ? qsTr("Task added: %1").arg(parsed.when) : qsTr("Task added to today's journal"),
                                    { kind: "success", actionText: qsTr("Undo"), onAction: () => planningActions.undo() })
                    }
                    Component.onCompleted: quickAdd.field = quickAddField
                }
                TasksFilterBar {
                    id: filterBarItem
                    Layout.fillWidth: true
                    tasks: taskModel
                    onQueryRequested: queryDialog.open()
                    Component.onCompleted: filterBar.bar = filterBarItem
                }
                Text {
                    Layout.fillWidth: true
                    visible: taskModel.totalCount > 0
                    text: {
                        const parts = [qsTr("%n open", "", taskModel.openCount)]
                        if (taskModel.overdueCount > 0)
                            parts.push(qsTr("%n overdue", "", taskModel.overdueCount))
                        if (taskModel.todayCount > 0)
                            parts.push(qsTr("%n for today", "", taskModel.todayCount))
                        return parts.join(" · ")
                    }
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgMuted
                    elide: Text.ElideRight
                }
            }
        }

        delegate: Item {
            id: row

            required property int index
            required property var model
            required property string rowType

            width: ListView.view.width
            height: loader.item ? loader.item.implicitHeight : 0

            Loader {
                id: loader
                width: Math.min(parent.width - 2 * (Theme.narrow ? Theme.space2 : Theme.space5), root.maximumWidth + Theme.space2)
                x: (parent.width - width) / 2
                sourceComponent: row.rowType === "header" ? headerRow : taskRow
            }
            Component {
                id: headerRow
                BSectionHeader {
                    topPadding: Theme.space4
                    text: row.model.sectionLabel
                    count: row.model.sectionCount
                    collapsible: true
                    expanded: !row.model.collapsed
                    onExpandedToggled: taskModel.toggleSection(row.model.section)
                }
            }
            Component {
                id: taskRow
                TasksRow {
                    task: row.model
                    focus: row.ListView.isCurrentItem
                    onTaskToggled: root.toggle(row.model)
                    onOpened: AppController.openPage(row.model.path, row.model.line)
                    onLinkActivated: (url) => root.activateLink(url, row.model)
                    onStatusRequested: (status) => root.setStatus(row.model, status)
                }
            }
        }
    }

    // Seams to the header's items (the header is created by the ListView).
    QtObject {
        id: quickAdd
        property TasksQuickAdd field: null
        function focusField() {
            list.positionViewAtBeginning()
            if (field)
                field.focusField()
        }
    }
    QtObject {
        id: filterBar
        property TasksFilterBar bar: null
        function focusSearch() {
            list.positionViewAtBeginning()
            if (bar)
                bar.focusSearch()
        }
    }

    // Empty states (below the header, which stays usable).
    BEmptyState {
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(list.headerItem ? list.headerItem.height + Theme.space8 : 0, (parent.height - height) / 2)
        width: Math.min(parent.width - 2 * Theme.space6, 440)
        visible: taskModel.totalCount === 0 && !Workspace.indexing
        iconName: taskModel.vaultTaskCount === 0 ? "list-todo" : "filter"
        title: taskModel.vaultTaskCount === 0 ? qsTr("No tasks yet") : qsTr("No tasks match the filters")
        description: taskModel.vaultTaskCount === 0
                     ? qsTr("Write “- [ ] something” in any note, or type a task above — “tomorrow at 9” adds a reminder.")
                     : qsTr("%n tasks in the notebook are hidden by the filters.", "", taskModel.vaultTaskCount)
        actionText: taskModel.vaultTaskCount === 0 ? "" : qsTr("Clear filters")
        onActionTriggered: taskModel.clearFilters()
    }
    BSpinner {
        anchors.horizontalCenter: parent.horizontalCenter
        y: list.headerItem ? list.headerItem.height + Theme.space8 : Theme.space16
        size: Theme.iconSizeLg
        visible: taskModel.totalCount === 0 && Workspace.indexing
        running: visible
    }

    TasksQueryDialog {
        id: queryDialog
        query: taskModel.queryText
        onCopied: toasts.show(qsTr("Query copied — paste it into a page"), { kind: "success" })
    }

    BToastHost {
        id: toasts
        anchors.fill: parent
        z: 100
    }
}
