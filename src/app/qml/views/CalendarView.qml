pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CalendarView — route "calendar" ({date?}): the vault's dates in a month grid, a week with hour
// rows or an agenda (Miesiąc / Tydzień / Agenda, remembered on this device), from the core's
// calendar entries (CalendarModel over PlanningContext.vaultIndex): daily notes, task due /
// scheduled / start / done dates and reminders / alarms with their times, recurring series
// expanded. Polish month and weekday names (DatesHelper), the vault's first weekday.
//
// The selected day's panel (CalendarDayPanel) is a column on the right of wide views and a
// bottom sheet on phones (tap a day): its entries (tick tasks off — undo in the toast — or open
// them at their line), the overdue tasks (today), the daily note, and "Add task" / "Add
// reminder", written into that day's journal (CalendarAddDialog, BReminderDialog for more
// options).
//
// Keys: arrows / PageUp / PageDown / Home / End move through days and periods (the grid has the
// focus), T = today, Enter opens the day, Ctrl+Enter its journal. Touch: swipe for the previous /
// next period.
//
// Route parameters: date (the selected day), mode ("month" | "week" | "agenda"), open ("day" =
// the day's sheet on phones, "task" / "reminder" = the add dialog for that day — also used by
// tests/app/planning/screenshots.sh).
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Calendar")

    /// The day panel sits next to the calendar.
    readonly property bool sideBySide: width >= 860
    /// Narrow month view: a compact grid with the selected day's panel below it (phones); the
    /// week view opens the day as a bottom sheet instead.
    readonly property bool stackedDay: !sideBySide && mode === "month"
    property string mode: {
        const m = AppSettings.value("calendar/mode", "month")
        return m === "week" || m === "agenda" ? m : "month"
    }

    function setMode(m) {
        mode = m
        AppSettings.setValue("calendar/mode", m)
        body.forceActiveFocus()
    }
    function applyRoute() {
        if (!route)
            return
        if (route.date && DatesHelper.isValidDate(route.date))
            cal.anchorDate = route.date
        if (route.mode === "month" || route.mode === "week" || route.mode === "agenda")
            mode = route.mode
        if (route.open === "day" && !sideBySide && !stackedDay)
            Qt.callLater(daySheet.open)
        else if (route.open === "task" || route.open === "reminder")
            Qt.callLater(() => addDialog.openFor(route.open, cal.anchorDate))
    }
    function openEntry(entry) {
        if (daySheet.opened)
            daySheet.close()
        AppController.openPage(entry.path, entry.line)
    }
    function toggleEntry(entry) {
        const wasClosed = entry.closed === true
        if (actions.toggleTask(entry.path, entry.line, entry.title))
            toasts.show(wasClosed ? qsTr("Task reopened") : qsTr("Task done"),
                        { kind: "success", actionText: qsTr("Undo"), onAction: () => actions.undo() })
    }
    function openJournal(date) {
        if (daySheet.opened)
            daySheet.close()
        AppController.openJournal(date)
    }
    function showDay(date) {
        cal.anchorDate = date
        if (!sideBySide && !stackedDay)
            daySheet.open()
    }
    function appendToJournal(date, line, message) {
        const path = Workspace.ensureJournal(date)
        if (path === "" || !actions.appendBlock(path, line)) {
            toasts.show(qsTr("The journal could not be saved."), { kind: "error" })
            return
        }
        toasts.show(message, { kind: "success", actionText: qsTr("Open"), onAction: () => AppController.openJournal(date) })
    }

    onRouteChanged: applyRoute()
    Component.onCompleted: {
        applyRoute()
        body.forceActiveFocus()
    }
    focus: true

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_T && event.modifiers === Qt.NoModifier) {
            cal.goToday()
            event.accepted = true
        }
    }

    CalendarModel {
        id: cal
        vaultIndex: PlanningContext.vaultIndex
        mode: root.mode
        today: DatesHelper.today
        weekStart: DatesHelper.weekStart
        locale: DatesHelper.locale
    }
    PlanningActions {
        id: actions
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
                    sourceComponent: root.mode === "week" ? weekView : root.mode === "agenda" ? agendaView : monthView
                }
                Rectangle {
                    visible: root.stackedDay
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.border
                }
                CalendarDayPanel {
                    visible: root.stackedDay
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: Theme.space2
                    Layout.rightMargin: Theme.space2
                    Layout.topMargin: Theme.space3
                    // Room for the shell's floating capture button.
                    bottomMargin: Theme.space16 + Theme.space4
                    calendar: cal
                    date: cal.anchorDate
                    onItemOpened: (entry) => root.openEntry(entry)
                    onItemToggled: (entry) => root.toggleEntry(entry)
                    onJournalOpened: (date) => root.openJournal(date)
                    onAddTaskRequested: (date) => addDialog.openFor("task", date)
                    onAddReminderRequested: (date) => addDialog.openFor("reminder", date)
                    onAllOverdueRequested: AppController.navigate({ view: "tasks" })
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

                CalendarDayPanel {
                    anchors.fill: parent
                    anchors.margins: Theme.space3
                    calendar: cal
                    date: cal.anchorDate
                    onItemOpened: (entry) => root.openEntry(entry)
                    onItemToggled: (entry) => root.toggleEntry(entry)
                    onJournalOpened: (date) => root.openJournal(date)
                    onAddTaskRequested: (date) => addDialog.openFor("task", date)
                    onAddReminderRequested: (date) => addDialog.openFor("reminder", date)
                    onAllOverdueRequested: AppController.navigate({ view: "tasks" })
                }
            }
        }
    }

    Component {
        id: monthView
        CalendarMonthGrid {
            calendar: cal
            focus: true
            onDayTapped: (date) => root.showDay(date)
            onDayOpened: (date) => root.openJournal(date)
            onItemOpened: (entry) => root.openEntry(entry)
        }
    }
    Component {
        id: weekView
        CalendarWeekGrid {
            calendar: cal
            focus: true
            onDayTapped: (date) => root.showDay(date)
            onDayOpened: (date) => root.openJournal(date)
            onItemOpened: (entry) => root.openEntry(entry)
        }
    }
    Component {
        id: agendaView
        CalendarAgendaList {
            calendar: cal
            focus: true
            onItemOpened: (entry) => root.openEntry(entry)
            onItemToggled: (entry) => root.toggleEntry(entry)
            onJournalOpened: (date) => root.openJournal(date)
            onDayTapped: (date) => root.showDay(date)
        }
    }

    // The day on phones: a bottom sheet.
    BDialog {
        id: daySheet
        objectName: "calendarDaySheet"
        title: {
            const s = DatesHelper.formatLong(cal.anchorDate)
            return s.length > 0 ? s.charAt(0).toLocaleUpperCase() + s.slice(1) : ""
        }
        subtitle: {
            const h = DatesHelper.formatHuman(cal.anchorDate)
            return h.indexOf(" ") < 0 ? h.charAt(0).toLocaleUpperCase() + h.slice(1) : ""
        }
        iconName: "calendar"
        showFooter: false
        preferredWidth: Theme.dialogWidth

        CalendarDayPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            scrollable: false
            showHeading: false
            calendar: cal
            date: cal.anchorDate
            onItemOpened: (entry) => root.openEntry(entry)
            onItemToggled: (entry) => root.toggleEntry(entry)
            onJournalOpened: (date) => root.openJournal(date)
            onAddTaskRequested: (date) => {
                daySheet.close()
                addDialog.openFor("task", date)
            }
            onAddReminderRequested: (date) => {
                daySheet.close()
                addDialog.openFor("reminder", date)
            }
            onAllOverdueRequested: {
                daySheet.close()
                AppController.navigate({ view: "tasks" })
            }
        }
    }

    CalendarAddDialog {
        id: addDialog
        defaultTime: Workspace.config.defaultReminderTime || "09:00"
        onTaskAdded: (text, date) => root.appendToJournal(date, actions.taskLine(text, date),
                                                           qsTr("Task added to the journal of %1").arg(DatesHelper.formatShort(date)))
        onReminderAdded: (text, at, alarm) => {
            const date = at.slice(0, 10)
            root.appendToJournal(date, actions.reminderLine(text, at, alarm, ""),
                                 qsTr("Reminder set for %1").arg(DatesHelper.formatHuman(at)))
        }
        onMoreOptionsRequested: (text, at, alarm) => {
            reminderDialog.draftText = text
            reminderDialog.edit({ at: at, alarm: alarm, recurrence: "", context: text })
            reminderDialog.editing = false
        }
    }

    BReminderDialog {
        id: reminderDialog
        property string draftText
        marks: cal.marks(DatesHelper.addDays(cal.rangeStart, -31), DatesHelper.addDays(cal.rangeEnd, 62))
        onReminderAccepted: (r) => {
            const date = r.at.slice(0, 10)
            const text = draftText !== "" ? draftText : qsTr("Reminder")
            root.appendToJournal(date, actions.reminderLine(text, r.at, r.alarm, r.recurrence),
                                 qsTr("Reminder set for %1").arg(DatesHelper.formatHuman(r.at)))
        }
    }

    // Actionable toasts of this view ("Task done · Undo").
    BToastHost {
        id: toasts
        anchors.fill: parent
        z: 100
    }
}
