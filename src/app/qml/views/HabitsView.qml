pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// HabitsView — route "habits" ({month?}, docs/UI.md "Habits"): every habit of the notebook
// (HabitListModel over PlanningContext.vaultIndex; a habit is a block or page with `habit::`,
// docs/SYNTAX.md §5 "Habits") as a HabitCard: today's check to tick (written as a task tagged
// #nawyk (Polish: habit) into today's journal by Workspace.setHabitCheck — undo in the toast), the
// streaks, the completion rate and a month grid whose past days can be ticked too. The month arrows
// move every grid; "Add habit" appends a definition to the habits' page (HabitAddDialog). A tick
// written in the journal itself shows here as well (the index follows the note).
//
// Route parameters: month ("YYYY-MM"), open ("add" = the add dialog — screenshots).
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Habits")
    readonly property int maximumWidth: 720

    function applyRoute(): void {
        habitModel.month = AppController.routeText(route, "month")
        if (AppController.routeText(route, "open") === "add")
            openAddLater.restart()
    }
    /// Set habit `name` (`isPage`: a page habit) on `date` to `state`, with undo in the toast.
    MapHolder { id: checkResult }
    function setCheck(name: string, isPage: bool, date: string, state: string): void {
        // { ok, changed, path, line, before }
        checkResult.map = Workspace.setHabitCheck(name, date, state, isPage)
        if (checkResult.map.ok !== true) {
            toasts.showToast(qsTr("The journal could not be saved."), "error", "", -1)
            return
        }
        if (checkResult.map.changed !== true)
            return
        const before = "" + checkResult.map.before
        const message = state === "done" ? (date === DatesHelper.today ? qsTr("Done for today: %1").arg(name)
                                                                         : qsTr("Marked done: %1").arg(name))
                      : state === "skipped" ? qsTr("Day skipped: %1").arg(name)
                      : qsTr("Check removed: %1").arg(name)
        const toastId = toasts.showToast(message, state === "done" ? "success" : "info", qsTr("Undo"), -1)
        undo.remember(toastId, name, isPage, date, before === "" ? "open" : before)
    }
    function addHabit(name: string, schedule: string): void {
        const path = "" + Workspace.addHabit(name, schedule, habitModel.habitsPage)
        if (path === "") {
            toasts.showToast(qsTr("The habit could not be saved."), "error", "", -1)
            return
        }
        const toastId = toasts.showToast(qsTr("Habit added to “%1”").arg(Workspace.pageTitle(path)),
                                         "success", qsTr("Open"), -1)
        undo.rememberOpen(toastId, path)
    }

    // What the toasts' buttons do, by toast id: undo a check (ids → the check before it), open
    // the page of a habit just added (openIds → openPaths).
    QtObject {
        id: undo
        property list<int> ids
        property list<string> names
        property list<bool> pages
        property list<string> dates
        property list<string> befores
        property list<int> openIds
        property list<string> openPaths
        function remember(toastId: int, name: string, isPage: bool, date: string, before: string): void {
            const at = ids.length
            ids[at] = toastId
            names[at] = name
            pages[at] = isPage
            dates[at] = date
            befores[at] = before
        }
        function rememberOpen(toastId: int, path: string): void {
            const at = openIds.length
            openIds[at] = toastId
            openPaths[at] = path
        }
    }
    Connections {
        target: toasts
        function onActionTriggered(toastId: int) {
            const at = undo.ids.indexOf(toastId)
            if (at >= 0)
                Workspace.setHabitCheck(undo.names[at], undo.dates[at], undo.befores[at], undo.pages[at])
            const open = undo.openIds.indexOf(toastId)
            if (open >= 0)
                AppController.openPage(undo.openPaths[open])
        }
    }
    Later {
        id: openAddLater
        onTriggered: addDialog.open()
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
        if (event.key === Qt.Key_N) {
            addDialog.open()
            event.accepted = true
        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_PageUp) {
            habitModel.previousMonth()
            event.accepted = true
        } else if (event.key === Qt.Key_Right || event.key === Qt.Key_PageDown) {
            habitModel.nextMonth()
            event.accepted = true
        }
    }

    HabitListModel {
        id: habitModel
        objectName: "habitModel"
        vaultIndex: PlanningContext.vaultIndex
        today: DatesHelper.today
        weekStart: DatesHelper.weekStart
        locale: DatesHelper.locale
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ListView {
        id: list
        objectName: "habitsList"
        TouchFlick.enabled: true
        anchors.fill: parent
        focus: true
        clip: true
        model: habitModel
        spacing: Theme.space3
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: Theme.space8
        BRowScrollBar { list: list }
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Habits")

        header: Item {
            width: list.width
            height: headerColumn.y + headerColumn.height + Theme.space3

            Item {
                id: headerColumn
                width: Math.min(parent.width - 2 * (Theme.narrow ? Theme.space3 : Theme.space6), root.maximumWidth)
                x: (parent.width - width) / 2
                y: Theme.narrow ? Theme.space3 : Theme.space5
                height: titleRow.height + Theme.space3 + monthRow.height

                Item {
                    id: titleRow
                    width: parent.width
                    height: Math.max(titleText.height + 2 + summaryText.height, addButton.height)

                    BText {
                        id: titleText
                        width: addButton.x - Theme.space3
                        text: qsTr("Habits")
                        variant: "heading"
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    BText {
                        id: summaryText
                        y: titleText.height + 2
                        width: addButton.x - Theme.space3
                        variant: "muted"
                        elide: Text.ElideRight
                        text: {
                            if (habitModel.count === 0)
                                return qsTr("Recurring points of the day, ticked in the journal")
                            const done = qsTr("%1 of %2 done today").arg(habitModel.doneTodayCount)
                                           .arg(habitModel.doneTodayCount + habitModel.dueCount)
                            return habitModel.problemCount > 0
                                    ? done + " · " + qsTr("%n with a problem", "", habitModel.problemCount) : done
                        }
                    }
                    BButton {
                        id: addButton
                        objectName: "habitAdd"
                        x: parent.width - width
                        y: (parent.height - height) / 2
                        text: Theme.narrow ? "" : qsTr("Add habit")
                        iconName: "plus"
                        variant: "primary"
                        Accessible.name: qsTr("Add habit")
                        onClicked: addDialog.open()
                    }
                }

                Item {
                    id: monthRow
                    y: titleRow.height + Theme.space3
                    width: parent.width
                    height: prevButton.height
                    visible: habitModel.count > 0

                    BIconButton {
                        id: prevButton
                        objectName: "habitsPrevMonth"
                        label: qsTr("Previous month")
                        iconName: "chevron-left"
                        onClicked: habitModel.previousMonth()
                    }
                    BText {
                        id: monthText
                        x: prevButton.width + Theme.space1
                        anchors.verticalCenter: parent.verticalCenter
                        text: habitModel.monthLabel
                        variant: "label"
                    }
                    BIconButton {
                        x: monthText.x + monthText.width + Theme.space1
                        objectName: "habitsNextMonth"
                        label: qsTr("Next month")
                        iconName: "chevron-right"
                        onClicked: habitModel.nextMonth()
                    }
                    BButton {
                        x: parent.width - width
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !habitModel.currentMonth
                        size: "small"
                        variant: "ghost"
                        text: qsTr("This month")
                        onClicked: habitModel.thisMonth()
                    }
                }
            }
        }

        // A habit row of habitModel (HabitCard takes its roles).
        delegate: HabitCard {
            id: habitCard
            // Full width (ListView places delegates at x 0); the card itself is inset.
            width: ListView.view.width
            inset: Math.max(Theme.narrow ? Theme.space3 : Theme.space6, (ListView.view.width - root.maximumWidth) / 2)
            today: DatesHelper.today
            weekdayNames: habitModel.weekdayNames
            monthOffset: habitModel.monthOffset
            onCheckRequested: (date, state) => root.setCheck(habitCard.name, habitCard.isPage, date, state)
            onOpenRequested: AppController.openPage(habitCard.path, habitCard.isPage ? -1 : habitCard.line)
            onDayOpenRequested: (date) => AppController.openJournal(date)
        }
    }

    BEmptyState {
        objectName: "habitsEmpty"
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(list.headerItem ? list.headerItem.height + Theme.space8 : 0, (parent.height - height) / 2)
        width: Math.min(parent.width - 2 * Theme.space6, 440)
        visible: habitModel.count === 0 && !Workspace.indexing
        iconName: "flame"
        title: qsTr("No habits yet")
        description: qsTr("Add one here, or give any point the property “habit:: every day” (or “mon, wed, fri”, “3 times a week”). Tick it here or in the journal: “- [x] Name #habit”.")
        actionText: qsTr("Add habit")
        onActionTriggered: addDialog.open()
    }

    HabitAddDialog {
        id: addDialog
        habits: habitModel
        onAddRequested: (name, schedule) => root.addHabit(name, schedule)
    }

    BToastHost {
        id: toasts
        anchors.fill: parent
        z: 100
        bottomMargin: Theme.space4
    }
}
