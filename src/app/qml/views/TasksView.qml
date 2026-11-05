pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQml.Models
import Brulion

// TasksView — route "tasks" ({query?}): every task of the vault (TaskListModel over
// PlanningContext.vaultIndex) in sections Zaległe / Dziś / Jutro / Ten tydzień / Później / Bez
// terminu / Zrobione (collapsed): tick them off inline (undo in the toast), see their dates,
// reminders, recurrence and priority as chips (a right-click or long press on one: its menu,
// PlanningTokenMenu — the change goes to the task's file, undo in the toast), open them at their
// line, set any status from the context menu. Filters (status, tag, page, folder, priority, text) and the sort order sit in
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

    MapHolder { id: routeInfo }
    function applyRoute(): void {
        routeInfo.map = route
        const query = "" + AppController.routeText(routeInfo.map, "query")
        const open = "" + AppController.routeText(routeInfo.map, "open")
        if (query !== "")
            taskModel.applyQuery(query)
        if (open === "query") {
            openQueryLater.restart()
        } else if (Js.startsWith(open, "add:")) {
            later.addText = Js.sliceFrom(open, 4)
            fillAddLater.restart()
        }
    }
    /// A toast whose button undoes the last change of planningActions.
    function undoToast(text: string, kind: string): void {
        toasts.showToast(text, kind, qsTr("Undo"), -1)
    }
    function toggle(path: string, line: int, text: string, status: string): void {
        const wasClosed = status === "done" || status === "cancelled"
        if (planningActions.toggleTask(path, line, text))
            undoToast(wasClosed ? qsTr("Task reopened") : qsTr("Task done"), "success")
    }
    function setStatus(path: string, line: int, text: string, status: string): void {
        if (planningActions.setTaskStatus(path, line, status, text))
            undoToast(qsTr("Status changed"), "")
    }
    /// The menu's timer (docs/SYNTAX.md §5): one at a time — another one stops.
    function toggleClock(path: string, line: int, text: string, running: bool): void {
        if (planningActions.toggleClock(path, line, text))
            undoToast(running ? qsTr("Timer stopped") : qsTr("Timer started"), "success")
    }
    /// "Dodaj zależność…": the picker of the task it will wait for.
    function openDependencyPicker(path: string, line: int, text: string): void {
        dependencyDialog.hasTask = true
        dependencyDialog.taskPath = path
        dependencyDialog.taskLine = line
        dependencyDialog.taskText = text
        dependencyDialog.open()
    }
    /// The menu of a task's chip: its token in the task's file (PlanningTokenMenu). `value`: the
    /// token's value as the model gives it.
    function openTokenMenu(path: string, line: int, text: string, kind: string, value: string, alarm: bool,
                           rule: string, priority: string, item: Item, x: real, y: real): void {
        tokenMenu.openToken({ path: path, line: line, text: text, kind: kind, value: value,
                              alarm: alarm, rule: rule, priority: priority,
                              isTask: true, occurrence: false }, item, x, y)
    }
    function activateLink(link: string, path: string): void {
        // A tag filters this list (Ctrl+click or a long press opens its page: TasksRow); everything
        // else navigates.
        if (Js.startsWith(link, "tag:")) {
            taskModel.tagFilter = Js.decodeURIComponent(Js.sliceFrom(link, 4))
            return
        }
        planningActions.openLink(link, path)
    }

    // Deferred calls (instead of Qt.callLater).
    QtObject {
        id: later
        /// The quick-add field's text from the route ("add:<text>").
        property string addText
    }
    Timer {
        id: openQueryLater
        interval: 0
        onTriggered: queryDialog.open()
    }
    Timer {
        id: fillAddLater
        interval: 0
        onTriggered: {
            if (quickAdd.field)
                quickAdd.field.text = later.addText
        }
    }
    Timer {
        id: toTopLater
        interval: 0
        onTriggered: list.positionViewAtBeginning()
    }
    // Every toast button of this view is "Undo".
    Connections {
        target: toasts
        function onActionTriggered(toastId: int) {
            planningActions.undo()
        }
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
    PlanningTokenMenu {
        id: tokenMenu
        actions: planningActions
        onChanged: (text) => root.undoToast(text, "")
        onMessage: (text, kind) => toasts.showToast(text, kind, "", -1)
        onFilterRequested: (tag) => taskModel.tagFilter = tag
    }
    PlanningActions {
        id: planningActions
        host: PlanningContext.host
        vaultIndex: PlanningContext.vaultIndex
        today: DatesHelper.today
        now: DatesHelper.fixedNow
        weekStart: DatesHelper.weekStart
        locale: DatesHelper.locale
        onFailed: (message) => toasts.showToast(message, "warning", "", -1)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ListView {
        id: list
        TouchFlick.enabled: true
        anchors.fill: parent
        focus: true
        clip: true
        model: taskModel
        reuseItems: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: Theme.space8
        currentIndex: -1
        BRowScrollBar { list: list }
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
            onHeightChanged: if (!list.userScrolled) toTopLater.restart()
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
                        // (The signal's values first: later calls could change them.)
                        const block = "" + line
                        const when = "" + AppController.routeText(parsed, "when")
                        const path = "" + Workspace.ensureJournal(DatesHelper.today)
                        if (path === "" || !planningActions.appendBlock(path, block)) {
                            toasts.showToast(qsTr("The journal could not be saved."), "error", "", -1)
                            return
                        }
                        root.undoToast(when !== "" ? qsTr("Task added: %1").arg(when) : qsTr("Task added to today's journal"), "success")
                    }
                    Component.onCompleted: quickAdd.field = quickAddField
                }
                TasksFilterBar {
                    id: filterBarItem
                    Layout.fillWidth: true
                    tasks: taskModel
                    onQueryRequested: queryDialog.open()
                    onTimeRequested: timeDialog.open()
                    Component.onCompleted: filterBar.bar = filterBarItem
                }
                Text {
                    Layout.fillWidth: true
                    visible: taskModel.totalCount > 0
                    text: qsTr("%n open", "", taskModel.openCount)
                          + (taskModel.overdueCount > 0 ? " · " + qsTr("%n overdue", "", taskModel.overdueCount) : "")
                          + (taskModel.todayCount > 0 ? " · " + qsTr("%n for today", "", taskModel.todayCount) : "")
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgMuted
                    elide: Text.ElideRight
                }
            }
        }

        // Rows of taskModel: a section header or a task (role rowType).
        delegate: DelegateChooser {
            role: "rowType"
            DelegateChoice {
                roleValue: "header"
                Item {
                    id: headerRow
                    required property string section
                    required property string sectionLabel
                    required property int sectionCount
                    required property bool collapsed
                    width: ListView.view.width
                    height: sectionHeader.implicitHeight
                    BSectionHeader {
                        id: sectionHeader
                        width: Math.min(parent.width - 2 * (Theme.narrow ? Theme.space2 : Theme.space5), root.maximumWidth + Theme.space2)
                        x: (parent.width - width) / 2
                        topPadding: Theme.space4
                        text: headerRow.sectionLabel
                        count: headerRow.sectionCount
                        collapsible: true
                        expanded: !headerRow.collapsed
                        onExpandedToggled: taskModel.toggleSection(headerRow.section)
                    }
                }
            }
            DelegateChoice {
                roleValue: "task"
                TasksRow {
                    id: taskRow
                    /// The value of a chip's token as the model gives it.
                    function tokenValue(kind: string): string {
                        return kind === "due" ? due : kind === "scheduled" ? scheduled : kind === "start" ? start
                             : kind === "reminder" ? reminderSpec : kind === "recurrence" ? recurrence : priorityName
                    }
                    width: Math.min(ListView.view.width - 2 * (Theme.narrow ? Theme.space2 : Theme.space5), root.maximumWidth + Theme.space2)
                    x: (ListView.view.width - width) / 2
                    focus: ListView.isCurrentItem
                    onTaskToggled: root.toggle(taskRow.path, taskRow.line, taskRow.text, taskRow.status)
                    onOpened: AppController.openPage(taskRow.path, taskRow.line)
                    onLinkActivated: (url) => root.activateLink(url, taskRow.path)
                    onTagPageRequested: (tag) => AppController.openTag(tag)
                    onStatusRequested: (status) => root.setStatus(taskRow.path, taskRow.line, taskRow.text, status)
                    onClockRequested: root.toggleClock(taskRow.path, taskRow.line, taskRow.text, taskRow.clockRunning)
                    onDependencyRequested: root.openDependencyPicker(taskRow.path, taskRow.line, taskRow.text)
                    onTokenMenuRequested: (kind, item, x, y) =>
                        root.openTokenMenu(taskRow.path, taskRow.line, taskRow.text, kind, taskRow.tokenValue(kind),
                                           taskRow.reminderAlarm, taskRow.recurrence, taskRow.priorityName, item, x, y)
                    // "Filtruj po tagu" first: PlanningTokenMenu.filterRequested.
                    onLinkMenuRequested: (url, item, x, y) => tokenMenu.openLink(url, taskRow.path, item, x, y, true)
                }
            }
        }
    }

    // Seams to the header's items (the header is created by the ListView).
    QtObject {
        id: quickAdd
        property TasksQuickAdd field: null
        function focusField(): void {
            list.positionViewAtBeginning()
            if (field)
                field.focusField()
        }
    }
    QtObject {
        id: filterBar
        property TasksFilterBar bar: null
        function focusSearch(): void {
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

    TasksTimeDialog {
        id: timeDialog
        tasks: taskModel
        onTaskOpened: (path, line) => AppController.openPage(path, line)
    }
    TaskDependencyDialog {
        id: dependencyDialog
        // The task that will wait (hasTask false: none).
        property bool hasTask: false
        property string taskPath
        property int taskLine
        property string taskText
        onSearchRequested: (text) => {
            if (hasTask)
                items = planningActions.dependencyCandidates(taskPath, taskLine, text)
            else
                items = []
        }
        onChosen: (path, line) => {
            if (hasTask && planningActions.addDependency(taskPath, taskLine, taskText, path, line))
                root.undoToast(qsTr("Dependency added"), "success")
        }
    }
    TasksQueryDialog {
        id: queryDialog
        query: taskModel.queryText
        onCopied: toasts.showToast(qsTr("Query copied — paste it into a page"), "success", "", -1)
    }

    BToastHost {
        id: toasts
        anchors.fill: parent
        z: 100
        bottomMargin: Theme.space4
    }
}
