pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQml.Models
import QtQuick.Templates as T
import Brulion

// TasksView — route "tasks" ({query?}): every task of the vault (TaskListModel over
// PlanningContext.vaultIndex) in sections Overdue / Today / Tomorrow / This week / Later / No
// date / Done (collapsed): tick them off inline (undo in the toast), see their dates,
// reminders, recurrence and priority as chips (a right-click or long press on one: its menu,
// PlanningTokenMenu — the change goes to the task's file, undo in the toast), give a task without
// a date one from its "Add date" chip (QuickDateMenu), open them at their line, set any status from the context menu. Filters (status, tag, page, folder, priority, text) and the sort order sit in
// TasksFilterBar; "Show as query" turns them into the equivalent ```query block (SYNTAX §10);
// route.query pre-sets them. Quick add with natural-language dates writes into today's journal.
//
// Keys: Up/Down move through the tasks, Space toggles, Enter opens, Ctrl+N… stays with the shell;
// "/" focuses the filter field, "n" the quick-add field.
//
// Route parameters: query (filters, e.g. "tag:work status:open"), open ("query" = the query
// dialog, "add:<text>" = the quick-add field filled — screenshots).
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Tasks")
    /// Text width of the list on wide windows.
    readonly property int maximumWidth: 860
    /// The rows' inset on each side in the full-width delegates: centred, 4 px wider than the
    /// header's column (the hover fill around the text).
    readonly property real rowMargin: Math.max(Theme.narrow ? Theme.space2 : Theme.space5,
                                               (list.width - root.maximumWidth - Theme.space2) / 2)

    /// The list's rows drawn by Atrament (Settings → Advanced, AppSettings.viewsAtrament, or
    /// `--atrament-views`; where Atrament is built): TasksAtramentList under the ListView's header in
    /// place of its rows (docs/ATRAMENT.md "The views in Atrament (M3)").
    readonly property bool usesAtrament: DeviceInfo.atrament && (AppSettings.viewsAtrament || DevTools.atramentViews)
    /// What a row drawn by Atrament asks (views/atrament/tasksview.h's actions): what TasksRow's
    /// signals do.
    MapHolder { id: actionInfo }
    function atramentAction(action: string, args: var): void {
        // (The map through a MapHolder, every value read into a plain value before any call:
        // compiled QML reads nothing a call could have changed.)
        actionInfo.map = args
        const path = "" + AppController.routeText(actionInfo.map, "path")
        const line = 0 + AppController.routeInt(actionInfo.map, "line", 0)
        const text = "" + AppController.routeText(actionInfo.map, "text")
        const status = "" + AppController.routeText(actionInfo.map, "status")
        const x = 0 + AppController.routeInt(actionInfo.map, "x", 0)
        const y = 0 + AppController.routeInt(actionInfo.map, "y", 0)
        const section = "" + AppController.routeText(actionInfo.map, "section")
        const key = "" + AppController.routeText(actionInfo.map, "key")
        const url = "" + AppController.routeText(actionInfo.map, "url")
        const kind = "" + AppController.routeText(actionInfo.map, "kind")
        const value = "" + AppController.routeText(actionInfo.map, "value")
        const alarm = AppController.routeFlag(actionInfo.map, "alarm") === true
        const rule = "" + AppController.routeText(actionInfo.map, "rule")
        const priority = "" + AppController.routeText(actionInfo.map, "priority")
        const running = AppController.routeFlag(actionInfo.map, "clockRunning") === true
        if (action === "toggle")
            root.toggle(path, line, text, status)
        else if (action === "open")
            AppController.openPage(path, line)
        else if (action === "toggleSection")
            taskModel.toggleSection(section)
        else if (action === "fold")
            taskModel.toggleSubtasks(key)
        else if (action === "link")
            root.activateLink(url, path)
        else if (action === "linkMenu")
            root.openAtramentLinkMenu(url, path, x, y)
        else if (action === "tokenMenu")
            root.openAtramentTokenMenu(path, line, text, kind, value, alarm, rule, priority, x, y)
        else if (action === "statusMenu" || action === "menu")
            root.openAtramentMenu(path, line, text, status, running, x, y)
    }
    /// A link's menu and a chip's token menu of a row drawn by Atrament, at (x, y) of the list.
    function openAtramentLinkMenu(url: string, path: string, x: int, y: int): void {
        tokenMenu.openLink(url, path, atramentList.item as Item, x, y, true)
    }
    function openAtramentTokenMenu(path: string, line: int, text: string, kind: string, value: string, alarm: bool,
                                   rule: string, priority: string, x: int, y: int): void {
        root.openTokenMenu(path, line, text, kind, value, alarm, rule, priority, atramentList.item as Item, x, y)
    }
    /// The status menu of a task drawn by Atrament (TasksRow's TaskStatusMenu), made when first
    /// opened, and the task it is for.
    property TaskStatusMenu atramentMenu: null
    property string menuPath
    property int menuLine: 0
    property string menuText
    property string menuStatus
    property bool menuClockRunning: false
    function openAtramentMenu(path: string, line: int, text: string, status: string, running: bool, x: int, y: int): void {
        root.menuPath = path
        root.menuLine = line
        root.menuText = text
        root.menuStatus = status !== "" ? status : "todo"
        root.menuClockRunning = running
        if (!root.atramentMenu)
            root.atramentMenu = atramentMenuComponent.createObject(root) as TaskStatusMenu
        root.atramentMenu.status = root.menuStatus
        root.atramentMenu.clockRunning = root.menuClockRunning
        root.atramentMenu.popup(atramentList.item as Item, x, y)
    }
    Component {
        id: atramentMenuComponent
        TaskStatusMenu {
            showOpen: true
            showTaskTools: true
            onOpenRequested: AppController.openPage(root.menuPath, root.menuLine)
            onClockRequested: root.toggleClock(root.menuPath, root.menuLine, root.menuText, root.menuClockRunning)
            onDependencyRequested: root.openDependencyPicker(root.menuPath, root.menuLine, root.menuText)
            onStatusChosen: (status) => {
                if (status !== root.menuStatus)
                    root.setStatus(root.menuPath, root.menuLine, root.menuText, status)
            }
        }
    }

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
            fillAddLater.addText = Js.sliceFrom(open, 4)
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
    /// "Add dependency…": the picker of the task it will wait for.
    function openDependencyPicker(path: string, line: int, text: string): void {
        root.dependencyHasTask = true
        root.dependencyPath = path
        root.dependencyLine = line
        root.dependencyText = text
        if (!root.dependencyDialog) {
            // (TaskDependencyDialog, loaded from its file when first opened: docs/PERFORMANCE.md "QML
            // files loaded when first used".)
            const component = Qt.createComponent(Qt.resolvedUrl("../editor/TaskDependencyDialog.qml"))
            root.dependencyDialog = component.createObject(root) as T.Popup
            QmlCall.connect(root.dependencyDialog, "searchRequested", root, "dependencySearch")
            QmlCall.connect(root.dependencyDialog, "chosen", root, "dependencyChosen")
        }
        root.dependencyDialog.open()
    }
    /// The dependency picker and the task that will wait (dependencyHasTask false: none).
    property T.Popup dependencyDialog: null
    property bool dependencyHasTask: false
    property string dependencyPath
    property int dependencyLine
    property string dependencyText
    readonly property list<var> noDependencies: []
    function dependencySearch(text: string): void {
        if (root.dependencyHasTask)
            QmlCall.set(root.dependencyDialog, "items", planningActions.dependencyCandidates(root.dependencyPath, root.dependencyLine, text))
        else
            QmlCall.set(root.dependencyDialog, "items", root.noDependencies)
    }
    function dependencyChosen(path: string, line: int): void {
        if (root.dependencyHasTask && planningActions.addDependency(root.dependencyPath, root.dependencyLine, root.dependencyText, path, line))
            root.undoToast(qsTr("Dependency added"), "success")
    }
    /// The menu of a task's chip: its token in the task's file (PlanningTokenMenu). `value`: the
    /// token's value as the model gives it.
    function openTokenMenu(path: string, line: int, text: string, kind: string, value: string, alarm: bool,
                           rule: string, priority: string, item: Item, x: real, y: real): void {
        tokenMenu.openToken({ path: path, line: line, text: text, kind: kind, value: value,
                              alarm: alarm, rule: rule, priority: priority,
                              isTask: true, occurrence: false }, item, x, y)
    }
    /// A click on a task's date chip: the date dialog on its token (PlanningTokenMenu).
    function openTokenDialog(path: string, line: int, text: string, kind: string, value: string): void {
        tokenMenu.openTokenDialog({ path: path, line: line, text: text, kind: kind, value: value,
                                    isTask: true, occurrence: false })
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
    Later {
        id: openQueryLater
        onTriggered: queryDialog.open()
    }
    Later {
        id: fillAddLater
        /// The quick-add field's text from the route ("add:<text>").
        property string addText
        onTriggered: {
            if (quickAdd.field)
                quickAdd.field.text = fillAddLater.addText
        }
    }
    Later {
        id: toTopLater
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
        if (visible)
            takeFocus()
    }
    /// The keyboard to the list (made shown, or shown the first time after being built ahead).
    function takeFocus(): void {
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
        // What the tasks' macros of extensions show ({{ext:…}}), as the editor shows them.
        host: PlanningContext.host
        // Hidden (kept by ShellRouteView): index changes only mark the list stale, it is computed when shown.
        active: root.visible
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
        model: root.usesAtrament ? null : taskModel
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
            implicitHeight: headerColumn.y + headerColumn.implicitHeight + Theme.space2
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
                    textFormat: Text.PlainText
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
                        width: parent.width - 2 * root.rowMargin
                        x: root.rowMargin
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
                    // Full width (ListView places delegates at x 0); the row itself is inset.
                    width: ListView.view.width
                    sideMargin: root.rowMargin
                    focus: ListView.isCurrentItem
                    onTaskToggled: root.toggle(taskRow.path, taskRow.line, taskRow.text, taskRow.status)
                    onFoldToggled: taskModel.toggleSubtasks(taskRow.key)
                    onOpened: AppController.openPage(taskRow.path, taskRow.line)
                    onLinkActivated: (url) => root.activateLink(url, taskRow.path)
                    onTagPageRequested: (tag) => AppController.openTag(tag)
                    onStatusRequested: (status) => root.setStatus(taskRow.path, taskRow.line, taskRow.text, status)
                    onClockRequested: root.toggleClock(taskRow.path, taskRow.line, taskRow.text, taskRow.clockRunning)
                    onDependencyRequested: root.openDependencyPicker(taskRow.path, taskRow.line, taskRow.text)
                    onTokenMenuRequested: (kind, item, x, y) =>
                        root.openTokenMenu(taskRow.path, taskRow.line, taskRow.text, kind, taskRow.tokenValue(kind),
                                           taskRow.reminderAlarm, taskRow.recurrence, taskRow.priorityName, item, x, y)
                    onTokenDialogRequested: (kind) =>
                        root.openTokenDialog(taskRow.path, taskRow.line, taskRow.text, kind, taskRow.tokenValue(kind))
                    onDateMenuRequested: (item) => tokenMenu.openAddDate(taskRow.path, taskRow.line, taskRow.text,
                                                                        item, 0, item.height)
                    // "Filter by tag" first: PlanningTokenMenu.filterRequested.
                    onLinkMenuRequested: (url, item, x, y) => tokenMenu.openLink(url, taskRow.path, item, x, y, true)
                }
            }
        }
    }

    // Seams to the header's items (the header is created by the ListView).
    /// The list (its header; its rows unless they are Atrament's) — self-tests.
    readonly property ListView listView: list
    // Dev: the self-test of the rows drawn by Atrament (ctest tst_views_qml_atrament), loaded from its
    // file in developer builds only.
    Loader {
        id: atramentSelfTest
        active: DevTools.editorScript === "tasks-atrament-selftest" && atramentList.item !== null
        onActiveChanged: {
            if (!atramentSelfTest.active)
                return
            root.selfTestValues = { tasksView: root, model: taskModel, list: atramentList.item }
            atramentSelfTest.setSource(Qt.resolvedUrl("../dev/TasksAtramentSelfTest.qml"), root.selfTestValues)
        }
    }
    property var selfTestValues: ({})
    // The rows drawn by Atrament (usesAtrament): under the list's header, loaded by file name — it
    // exists only where Atrament is built.
    Loader {
        id: atramentList
        active: root.usesAtrament
        anchors.fill: parent
        anchors.topMargin: list.headerItem ? list.headerItem.height : 0
        source: active ? "../planning/TasksAtramentList.qml" : ""
        onLoaded: {
            QmlCall.set(atramentList.item, "tasksView", root)
            QmlCall.set(atramentList.item, "viewModel", taskModel)
        }
        Binding {
            target: atramentList.item
            property: "sideMargin"
            value: root.rowMargin
            when: atramentList.item !== null
        }
    }

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
