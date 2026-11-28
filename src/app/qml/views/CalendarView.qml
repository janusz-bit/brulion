pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CalendarView — route "calendar" ({date?}): the vault's dates in a month grid, a week with hour
// rows or an agenda (Month / Week / Agenda, remembered on this device), from the core's
// calendar entries (CalendarModel over PlanningContext.vaultIndex): daily notes, task due /
// scheduled / start / done dates and reminders / alarms with their times, recurring series
// expanded. Month and weekday names in the UI language (DatesHelper), the vault's first weekday.
//
// The selected day's panel (CalendarDayPanel) is a column on the right of wide views and a
// bottom sheet on phones (tap a day): its entries (tick tasks off — undo in the toast — or open
// them at their line; a right-click or long press: the menu of the entry's date, reminder or
// series, PlanningTokenMenu), the overdue tasks (today), the daily note, and "Add task" / "Add
// reminder", written into that day's journal (CalendarAddDialog, BReminderDialog for more
// options).
//
// Keys: arrows / PageUp / PageDown / Home / End move through days and periods (the grid has the
// focus), T = today, Enter opens the day, Ctrl+Enter its journal. Weekly and monthly notes: a click
// on a week number (the month grid's left column, the week grid's corner) or on the title (the
// month's / week's note); W / M open the selected day's week / month note. Touch: swipe for the previous /
// next period.
//
// Other calendars (Workspace.calendars: ICS subscriptions, CalDAV — docs/UI.md "Calendars"): their
// events in their colours among the notes' entries; the header's calendars button lists them with
// a tick each (shown or hidden), "New event…" and "Manage calendars…" (the sync center's tab); an
// event opens CalendarEventDialog (details, edit, delete, "Note in the journal").
//
// Route parameters: date (the selected day), mode ("month" | "week" | "agenda"), open ("day" =
// the day's sheet on phones, "task" / "reminder" = the add dialog for that day, "event" = a new
// event that day — also used by tests/app/planning/screenshots.sh).
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, `covered`, optional `title`;
// offers `edgeSwipe`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Calendar")
    /// The week and month grids reach the left edge and swipe sideways from there to the previous
    /// period (touch UIs): the shell's drawer leaves that edge to them (the bottom navigation's
    /// Menu opens it).
    readonly property bool edgeSwipe: Theme.touchUi && mode !== "agenda"
    /// A modal panel of the shell lies over the view (the drawer): the grids' swipe makes way for
    /// the panel's own (set by ShellRouteView).
    property bool covered: false
    /// The grids swipe to the previous / next period unless something modal lies over them.
    readonly property bool gridSwipe: !covered && !daySheet.visible && !addDialog.visible

    /// The day panel sits next to the calendar.
    readonly property bool sideBySide: width >= 860
    /// Narrow month view: a compact grid with the selected day's panel below it (phones); the
    /// week view opens the day as a bottom sheet instead.
    readonly property bool stackedDay: !sideBySide && mode === "month"
    /// The agenda drawn by Atrament (Settings → Editor, AppSettings.viewsAtrament, or
    /// `--atrament-views`; where Atrament is built): CalendarAgendaAtramentList in
    /// CalendarAgendaList's place (docs/ATRAMENT.md "The views in Atrament (M3)").
    readonly property bool agendaAtrament: DeviceInfo.atrament && (AppSettings.viewsAtrament || DevTools.atramentViews)
    /// The agenda drawn by Atrament once loaded (an AtramentView: the type exists only where
    /// Atrament is built), and the self-test's values.
    property Item atramentAgenda: null
    property var selfTestValues: ({})
    /// The agenda has nothing to show (CalendarAgendaList.empty): only today, without entries.
    readonly property bool agendaEmpty: cal.revision >= 0 && cal.rangeStart !== "-"
                                        && (cal.rowCount() === 0 || (cal.rowCount() === 1 && cal.itemCount === 0
                                            && cal.data(cal.index(0, 0), CalendarModel.HasJournalRole) !== true))
    property string mode: {
        const m = AppSettings.value("calendar/mode", "month")
        return m === "week" || m === "agenda" ? m : "month"
    }

    function setMode(m: string): void {
        mode = m
        AppSettings.setValue("calendar/mode", m)
        body.forceActiveFocus()
    }
    function applyRoute(): void {
        const date = AppController.routeText(route, "date")
        if (date !== "" && DatesHelper.isValidDate(date))
            cal.anchorDate = date
        const routeMode = AppController.routeText(route, "mode")
        if (routeMode === "month" || routeMode === "week" || routeMode === "agenda")
            mode = routeMode
        const open = AppController.routeText(route, "open")
        if (open === "day" && !sideBySide && !stackedDay) {
            openDayLater.restart()
        } else if (open === "task" || open === "reminder") {
            later.addKind = open
            openAddLater.restart()
        } else if (open === "event") {
            openEventLater.restart()
        }
    }
    // An entry (the planning components' signals): a CalendarModel item { key, kind, title, path,
    // line, isTask, status, closed, alarm, date, time, spec, occurrence, rule, priority, … }, read
    // through a typed map.
    MapHolder { id: entryInfo }
    /// What the agenda drawn by Atrament asks (views/atrament/agendaview.h's actions): what
    /// CalendarAgendaList's signals do.
    MapHolder { id: agendaInfo }
    function atramentAgendaAction(action: string, args: var): void {
        // (The map through a MapHolder, every value read into a plain value before any call.)
        agendaInfo.map = args
        const date = "" + AppController.routeText(agendaInfo.map, "date")
        const x = 0 + AppController.routeInt(agendaInfo.map, "x", 0)
        const y = 0 + AppController.routeInt(agendaInfo.map, "y", 0)
        const entry = agendaInfo.map.entry
        if (action === "day")
            root.pickAgendaDay(date)
        else if (action === "journal")
            root.openJournal(date)
        else if (action === "open")
            root.openEntry(entry)
        else if (action === "toggle")
            root.toggleEntry(entry)
        else if (action === "menu")
            root.openEntryMenu(entry, root.atramentAgenda, x, y)
        else if (action === "statusMenu")
            root.openAgendaStatusMenu(entry, x, y)
    }
    /// A day picked in the agenda drawn by Atrament: selected, the list not scrolled to it.
    function pickAgendaDay(date: string): void {
        QmlCall.set(root.atramentAgenda, "picking", true)
        root.showDay(date)
        QmlCall.set(root.atramentAgenda, "picking", false)
    }
    /// The status menu of a task in the agenda drawn by Atrament (TaskBox's TaskStatusMenu), made
    /// when first opened, and the entry it is for.
    property TaskStatusMenu agendaStatusMenu: null
    property var agendaMenuEntry: ({})
    property string agendaMenuStatus
    function openAgendaStatusMenu(entry: var, x: int, y: int): void {
        root.agendaMenuEntry = entry
        entryInfo.map = entry
        const status = "" + AppController.routeText(entryInfo.map, "status")
        root.agendaMenuStatus = status !== "" ? status : "todo"
        if (root.agendaStatusMenu === null)
            root.agendaStatusMenu = agendaStatusMenuComponent.createObject(root) as TaskStatusMenu
        root.agendaStatusMenu.status = root.agendaMenuStatus
        root.agendaStatusMenu.popup(root.atramentAgenda, x, y)
    }
    Component {
        id: agendaStatusMenuComponent
        TaskStatusMenu {
            onStatusChosen: (status) => {
                if (status !== root.agendaMenuStatus)
                    root.setEntryStatus(root.agendaMenuEntry, status)
            }
        }
    }
    function openEntry(entry: var): void {
        entryInfo.map = entry
        if (daySheet.opened)
            daySheet.close()
        if (AppController.routeText(entryInfo.map, "kind") === "event") {
            eventDialog.openFor(AppController.routeText(entryInfo.map, "eventKey"))
            return
        }
        AppController.openPage(entryInfo.map.path, entryInfo.map.line)
    }
    /// A toast whose button undoes the last change of `actions`.
    function undoToast(text: string, kind: string): void {
        toasts.showToast(text, kind, qsTr("Undo"), -1)
    }
    function toggleEntry(entry: var): void {
        entryInfo.map = entry
        const wasClosed = entryInfo.map.closed === true
        if (actions.toggleTask(entryInfo.map.path, entryInfo.map.line, entryInfo.map.title))
            undoToast(wasClosed ? qsTr("Task reopened") : qsTr("Task done"), "success")
    }
    function setEntryStatus(entry: var, status: string): void {
        entryInfo.map = entry
        if (actions.setTaskStatus(entryInfo.map.path, entryInfo.map.line, status, entryInfo.map.title))
            undoToast(qsTr("Status changed"), "")
    }
    /// The menu of an entry's date token (PlanningTokenMenu): the token as written (`spec`); a series'
    /// other dates change the whole series, a reminder's one time is snoozed.
    function openEntryMenu(entry: var, item: Item, x: real, y: real): void {
        entryInfo.map = entry
        const e = entryInfo.map
        if (AppController.routeText(entryInfo.map, "kind") === "event") {
            eventDialog.openFor(AppController.routeText(entryInfo.map, "eventKey"))
            return
        }
        tokenMenu.openToken({ path: e.path, line: e.line, text: e.title, kind: e.kind, value: e.spec,
                              alarm: e.alarm, rule: e.rule, priority: e.priority,
                              isTask: e.isTask === true && e.status !== "", occurrence: e.occurrence,
                              date: e.date, time: e.time }, item, x, y)
    }
    function openJournal(date: string): void {
        if (daySheet.opened)
            daySheet.close()
        AppController.openJournal(date)
    }
    /// The weekly or monthly note of the period holding `date` (docs/SYNTAX.md §1): opened, created
    /// by its first change from the period's template.
    function openPeriodNote(period: string, date: string): void {
        if (daySheet.opened)
            daySheet.close()
        AppController.openPeriodicNote(period, date, 0)
    }
    /// The note of the period in the header's title (the anchor's month or week) exists.
    readonly property bool periodHasNote: {
        // Hidden: nothing is asked (the file check) until shown. (Read for real: a bare
        // `void Workspace.namesRevision` is compiled away with the dependency; only the set of
        // files matters, not an edit of a note.)
        if (!root.visible || mode === "agenda" || cal.anchorDate === "" || Workspace.namesRevision < 0)
            return false
        return AppController.routeFlag(Workspace.periodicNote(mode === "week" ? "week" : "month", cal.anchorDate), "exists")
    }
    function showDay(date: string): void {
        cal.anchorDate = date
        if (!sideBySide && !stackedDay)
            daySheet.open()
    }
    function appendToJournal(date: string, line: string, message: string): void {
        const path = Workspace.ensureJournal(date)
        if (path === "" || !actions.appendBlock(path, line)) {
            toasts.showToast(qsTr("The journal could not be saved."), "error", "", -1)
            return
        }
        const toastId = toasts.showToast(message, "success", qsTr("Open"), -1)
        const at = later.openIds.length
        later.openIds[at] = toastId
        later.openDates[at] = date
    }
    /// A reminder written into the journal of its day (the add dialog, the reminder dialog).
    function addReminder(text: string, at: string, alarm: bool, recurrence: string): void {
        const line = actions.reminderLine(text, at, alarm, recurrence)
        const human = DatesHelper.formatHuman(at)
        root.appendToJournal(Js.slice(at, 0, 10), line, qsTr("Reminder set for %1").arg(human))
    }
    /// `s` with its first letter in upper case.
    function capitalized(s: string): string {
        return s.length > 0 ? Js.toUpperCase(Js.charAt(s, 0)) + Js.sliceFrom(s, 1) : ""
    }

    /// The add dialog for `kind` ("task" | "reminder"); the day sheet makes way for it.
    function addFor(kind: string, date: string): void {
        daySheet.close()
        addDialog.openFor(kind, date)
    }
    /// A day's panel wired to this view (the side column, below the month, the phone's sheet).
    component DayPanel: CalendarDayPanel {
        calendar: cal
        date: cal.anchorDate
        onItemOpened: (entry) => root.openEntry(entry)
        onItemToggled: (entry) => root.toggleEntry(entry)
        onItemStatusRequested: (entry, status) => root.setEntryStatus(entry, status)
        onItemMenuRequested: (entry, item, x, y) => root.openEntryMenu(entry, item, x, y)
        onJournalOpened: (date) => root.openJournal(date)
        onAddTaskRequested: (date) => root.addFor("task", date)
        onAddReminderRequested: (date) => root.addFor("reminder", date)
        onAllOverdueRequested: {
            daySheet.close()
            AppController.navigate({ view: "tasks" })
        }
    }

    // Deferred calls (instead of Qt.callLater) and what the toasts' buttons do.
    QtObject {
        id: later
        /// The add dialog's kind from the route ("task" | "reminder").
        property string addKind
        /// Toasts whose button opens a journal (toast id → its date); the others' undo.
        property list<int> openIds
        property list<string> openDates
    }
    Later {
        id: openDayLater
        onTriggered: daySheet.open()
    }
    Later {
        id: openAddLater
        onTriggered: addDialog.openFor(later.addKind, cal.anchorDate)
    }
    Later {
        id: openEventLater
        onTriggered: eventDialog.openNew(cal.anchorDate)
    }
    Connections {
        target: toasts
        function onActionTriggered(toastId: int) {
            const at = later.openIds.indexOf(toastId)
            if (at >= 0)
                AppController.openJournal(later.openDates[at])
            else
                actions.undo()
        }
    }

    onRouteChanged: applyRoute()
    Component.onCompleted: {
        applyRoute()
        if (visible)
            takeFocus()
    }
    /// The keyboard to the grids (made shown, or shown the first time after being built ahead).
    function takeFocus(): void {
        body.forceActiveFocus()
    }
    focus: true

    Keys.onPressed: (event) => {
        if (event.modifiers !== Qt.NoModifier)
            return
        if (event.key === Qt.Key_T) {
            cal.goToday()
            event.accepted = true
        } else if (event.key === Qt.Key_W) {
            root.openPeriodNote("week", cal.anchorDate)
            event.accepted = true
        } else if (event.key === Qt.Key_M) {
            root.openPeriodNote("month", cal.anchorDate)
            event.accepted = true
        }
    }

    CalendarModel {
        id: cal
        vaultIndex: PlanningContext.vaultIndex
        // Hidden (kept by ShellRouteView): index changes only mark the days stale, computed when shown.
        active: root.visible
        mode: root.mode
        today: DatesHelper.today
        weekStart: DatesHelper.weekStart
        locale: DatesHelper.locale
        events: Workspace.calendars
    }
    PlanningTokenMenu {
        id: tokenMenu
        actions: actions
        onChanged: (text) => root.undoToast(text, "")
        onMessage: (text, kind) => toasts.showToast(text, kind, "", -1)
    }
    PlanningActions {
        id: actions
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        CalendarHeader {
            id: header
            Layout.fillWidth: true
            Layout.leftMargin: Theme.narrow ? Theme.space2 : Theme.space4
            Layout.rightMargin: Layout.leftMargin
            title: cal.title
            shortTitle: cal.shortTitle
            mode: root.mode
            showingToday: cal.showingToday
            overdueCount: cal.overdueCount
            busy: Workspace.indexing
            onPrevious: cal.previous()
            onNext: cal.next()
            onToday: cal.goToday()
            onModeRequested: (m) => root.setMode(m)
            onOverdueRequested: root.showDay(DatesHelper.today)
            titleHasNote: root.periodHasNote
            onTitleClicked: root.openPeriodNote(root.mode === "week" ? "week" : "month", cal.anchorDate)
            onCalendarsRequested: (button) => sourcesMenu.popup(button, 0, button.height)
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ColumnLayout {
                id: mainColumn
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Loader {
                    id: body
                    Layout.fillWidth: true
                    Layout.fillHeight: !root.stackedDay
                    // Phones: six compact weeks, the rest for the day below.
                    Layout.preferredHeight: root.stackedDay
                                            ? Math.min((root.height - header.height) * 0.56, 6 * (Theme.touchUi ? 62 : 56) + 30) : -1
                    Layout.leftMargin: root.mode === "agenda" ? 0 : (Theme.narrow ? 0 : Theme.space2)
                    Layout.rightMargin: root.sideBySide ? 0 : Layout.leftMargin
                    Layout.bottomMargin: root.mode === "agenda" || root.stackedDay ? 0 : (Theme.narrow ? 0 : Theme.space2)
                    focus: true
                    sourceComponent: root.mode === "week" ? weekView
                                   : root.mode === "agenda" ? (root.agendaAtrament ? atramentAgendaView : agendaView) : monthView
                }
                Rectangle {
                    visible: root.stackedDay
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.border
                }
                DayPanel {
                    visible: root.stackedDay
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: Theme.space2
                    Layout.rightMargin: Theme.space2
                    Layout.topMargin: Theme.space3
                    bottomMargin: Theme.space4
                }
            }
            Rectangle {
                visible: root.sideBySide
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.border
            }
            Rectangle {
                visible: root.sideBySide
                Layout.preferredWidth: Math.min(400, Math.max(300, root.width * 0.3))
                Layout.fillHeight: true
                color: Theme.bgElevated

                DayPanel {
                    anchors.fill: parent
                    anchors.margins: Theme.space3
                }
            }
        }
    }

    Component {
        id: monthView
        CalendarMonthGrid {
            calendar: cal
            swipeEnabled: root.gridSwipe
            focus: true
            onDayTapped: (date) => root.showDay(date)
            onDayOpened: (date) => root.openJournal(date)
            onItemOpened: (entry) => root.openEntry(entry)
            onWeekOpened: (monday) => root.openPeriodNote("week", monday)
        }
    }
    Component {
        id: weekView
        CalendarWeekGrid {
            onWeekOpened: (monday) => root.openPeriodNote("week", monday)
            onItemMenuRequested: (entry, item, x, y) => root.openEntryMenu(entry, item, x, y)
            calendar: cal
            swipeEnabled: root.gridSwipe
            focus: true
            onDayTapped: (date) => root.showDay(date)
            onDayOpened: (date) => root.openJournal(date)
            onItemOpened: (entry) => root.openEntry(entry)
        }
    }
    // Drawn by Atrament (agendaAtrament): loaded by file name — it exists only where Atrament is
    // built —, CalendarAgendaList's empty state over it.
    Component {
        id: atramentAgendaView
        FocusScope {
            focus: true
            Loader {
                id: atramentAgendaLoader
                anchors.fill: parent
                focus: true
                visible: !root.agendaEmpty
                source: "../planning/CalendarAgendaAtramentList.qml"
                onLoaded: {
                    QmlCall.set(atramentAgendaLoader.item, "calendarView", root)
                    QmlCall.set(atramentAgendaLoader.item, "calendar", cal)
                    QmlCall.set(atramentAgendaLoader.item, "viewModel", cal)
                    root.atramentAgenda = atramentAgendaLoader.item as Item
                }
                Component.onDestruction: root.atramentAgenda = null
            }
            BEmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space6, 420)
                visible: root.agendaEmpty
                iconName: "calendar"
                title: qsTranslate("CalendarAgendaList", "Nothing planned in these four weeks")
                description: qsTranslate("CalendarAgendaList", "Dates (📅, ⏳) and reminders (⏰) from your notes appear here. Choose a day to add a task or a reminder to it.")
            }
            // Dev: the self-test of the agenda drawn by Atrament (ctest tst_views_qml_atrament_agenda),
            // loaded from its file in developer builds only.
            Loader {
                id: agendaSelfTest
                active: DevTools.editorScript === "agenda-atrament-selftest" && root.atramentAgenda !== null
                onActiveChanged: {
                    if (!agendaSelfTest.active)
                        return
                    root.selfTestValues = { calendarView: root, calendar: cal, list: root.atramentAgenda }
                    agendaSelfTest.setSource(Qt.resolvedUrl("../dev/AgendaAtramentSelfTest.qml"), root.selfTestValues)
                }
            }
        }
    }
    Component {
        id: agendaView
        CalendarAgendaList {
            calendar: cal
            focus: true
            onItemOpened: (entry) => root.openEntry(entry)
            onItemToggled: (entry) => root.toggleEntry(entry)
            onItemStatusRequested: (entry, status) => root.setEntryStatus(entry, status)
            onItemMenuRequested: (entry, item, x, y) => root.openEntryMenu(entry, item, x, y)
            onJournalOpened: (date) => root.openJournal(date)
            onDayTapped: (date) => root.showDay(date)
        }
    }

    // The day on phones: a bottom sheet.
    BDialog {
        id: daySheet
        objectName: "calendarDaySheet"
        title: root.capitalized(DatesHelper.formatLong(cal.anchorDate))
        // A one-word day ("Tomorrow") only.
        subtitle: {
            const h = "" + DatesHelper.formatHuman(cal.anchorDate)
            return Js.indexOf(h, " ") < 0 ? root.capitalized(h) : ""
        }
        iconName: "calendar"
        showFooter: false
        preferredWidth: Theme.dialogWidth

        DayPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            scrollable: false
            showHeading: false
        }
    }

    CalendarAddDialog {
        id: addDialog
        defaultTime: Workspace.config.defaultReminderTime || "09:00"
        onTaskAdded: (text, date) => {
            const label = "" + text
            const day = "" + date
            const line = actions.taskLine(label, day)
            const shortDay = DatesHelper.formatShort(day)
            root.appendToJournal(day, line, qsTr("Task added to the journal of %1").arg(shortDay))
        }
        onReminderAdded: (text, at, alarm) => root.addReminder(text, at, alarm === true, "")
        onMoreOptionsRequested: (text, at, alarm) => {
            reminderDialog.draftText = text
            reminderDialog.editWith(at, alarm, "", text)
            reminderDialog.editing = false
        }
    }

    BReminderDialog {
        id: reminderDialog
        property string draftText
        readonly property string marksFrom: DatesHelper.addDays(cal.rangeStart, -31)
        readonly property string marksTo: DatesHelper.addDays(cal.rangeEnd, 62)
        marks: cal.marks(marksFrom, marksTo)
        onReminderChosen: (at, alarm, recurrence) => root.addReminder(draftText !== "" ? draftText : qsTr("Reminder"),
                                                                     at, alarm, recurrence)
    }

    // Other calendars: shown or hidden, a new event, the sync center's tab.
    BMenu {
        id: sourcesMenu
        objectName: "calendarSourcesMenu"
        Repeater {
            model: Workspace.calendars !== null ? Workspace.calendars.calendars : root.noRows
            delegate: BMenuItem {
                required property var modelData
                readonly property string sourceId: AppController.routeText(modelData, "id")
                readonly property bool shown: AppController.routeFlag(modelData, "visible")
                text: AppController.routeText(modelData, "name")
                hint: AppController.routeText(modelData, "error") !== "" ? qsTr("Problem") : ""
                readonly property string swatchText: AppController.routeText(modelData, "color")
                swatch: swatchText
                checkable: true
                checked: shown
                onTriggered: Workspace.calendars.setCalendarVisible(sourceId, !shown)
            }
        }
        BMenuSeparator { visible: Workspace.calendars !== null && Workspace.calendars.hasCalendars }
        BMenuItem {
            objectName: "calendarNewEvent"
            visible: Workspace.calendars !== null && Workspace.calendars.writableCalendars.length > 0
            text: qsTr("New event…")
            iconName: "calendar-plus"
            onTriggered: eventDialog.openNew(cal.anchorDate)
        }
        BMenuItem {
            visible: Workspace.calendars !== null && Workspace.calendars.hasCalendars
            text: qsTr("Refresh calendars")
            iconName: "refresh-cw"
            onTriggered: Workspace.calendars.refresh("")
        }
        BMenuItem {
            objectName: "calendarManage"
            text: Workspace.calendars !== null && Workspace.calendars.hasCalendars ? qsTr("Manage calendars…")
                                                                                   : qsTr("Add a calendar…")
            iconName: "settings"
            onTriggered: AppController.navigate({ view: "sync", tab: "calendars" })
        }
    }
    readonly property list<var> noRows: []

    CalendarEventDialog {
        id: eventDialog
        onMessage: (text, kind) => toasts.showToast(text, kind, "", -1)
        onJournalRequested: (date, line) => root.appendToJournal(date, line, qsTr("Written in the journal of %1")
                                                                                .arg(DatesHelper.formatShort(date)))
    }

    // Actionable toasts of this view ("Task done · Undo").
    BToastHost {
        id: toasts
        anchors.fill: parent
        z: 100
        bottomMargin: Theme.space4
    }
}
