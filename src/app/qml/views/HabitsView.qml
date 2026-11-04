pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// HabitsView — route "habits" ({month?}, docs/UI.md "Habits"): every habit of the notebook
// (HabitListModel over PlanningContext.vaultIndex; a habit is a block or page with `habit::`,
// docs/SYNTAX.md §5 "Nawyki / Habits") as a HabitCard: today's check to tick (written as a task
// tagged #nawyk into today's journal by Workspace.setHabitCheck — undo in the toast), the streaks,
// the completion rate and a month grid whose past days can be ticked too. The month arrows move
// every grid; "Dodaj nawyk" appends a definition to the habits' page (HabitAddDialog). A tick
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

    function applyRoute() {
        if (!route)
            return
        habitModel.month = route.month || ""
        if (route.open === "add")
            Qt.callLater(addDialog.open)
    }
    /// Set habit `habit` (a model row) on `date` to `state`, with undo in the toast.
    function setCheck(habit, date, state) {
        const result = Workspace.setHabitCheck(habit.name, date, state, habit.isPage)
        if (!result.ok) {
            toasts.show(qsTr("The journal could not be saved."), { kind: "error" })
            return
        }
        if (!result.changed)
            return
        const before = result.before === "" ? "open" : result.before
        const message = state === "done" ? (date === DatesHelper.today ? qsTr("Done for today: %1").arg(habit.name)
                                                                         : qsTr("Marked done: %1").arg(habit.name))
                      : state === "skipped" ? qsTr("Day skipped: %1").arg(habit.name)
                      : qsTr("Check removed: %1").arg(habit.name)
        toasts.show(message, { kind: state === "done" ? "success" : "info", actionText: qsTr("Undo"),
                               onAction: () => Workspace.setHabitCheck(habit.name, date, before, habit.isPage) })
    }
    function addHabit(name, schedule) {
        const path = Workspace.addHabit(name, schedule, habitModel.habitsPage)
        if (path === "") {
            toasts.show(qsTr("The habit could not be saved."), { kind: "error" })
            return
        }
        toasts.show(qsTr("Habit added to “%1”").arg(Workspace.pageTitle(path)),
                    { kind: "success", actionText: qsTr("Open"), onAction: () => AppController.openPage(path) })
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
            height: headerColumn.height + Theme.space3

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
                            const parts = [qsTr("%1 of %2 done today").arg(habitModel.doneTodayCount)
                                           .arg(habitModel.doneTodayCount + habitModel.dueCount)]
                            if (habitModel.problemCount > 0)
                                parts.push(qsTr("%n with a problem", "", habitModel.problemCount))
                            return parts.join(" · ")
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

        delegate: Item {
            id: row
            required property int index
            required property var model
            width: ListView.view.width
            height: habitCard.height

            HabitCard {
                id: habitCard
                width: Math.min(parent.width - 2 * (Theme.narrow ? Theme.space3 : Theme.space6), root.maximumWidth)
                x: (parent.width - width) / 2
                habit: row.model
                today: DatesHelper.today
                weekdayNames: habitModel.weekdayNames
                monthOffset: habitModel.monthOffset
                onCheckRequested: (date, state) => root.setCheck(row.model, date, state)
                onOpenRequested: AppController.openPage(row.model.path, row.model.isPage ? -1 : row.model.line)
                onDayOpenRequested: (date) => AppController.openJournal(date)
            }
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
        description: qsTr("Add one here, or give any point the property “habit:: codziennie” (or “pn, śr, pt”, “3 razy w tygodniu”). Tick it here or in the journal: “- [x] Name #nawyk”.")
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
