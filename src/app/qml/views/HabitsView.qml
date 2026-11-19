pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// HabitsView — route "habits" ({month?, open?}, docs/UI.md "Habits"): the built-in habits extension
// (src/core/extensions/builtin/nawyki.luau, docs/SYNTAX.md §5 "Habits") through its view "month",
// drawn by ShellExtensionView: a summary of today, then a card per habit — today's check (a tap
// ticks it, again opens it; a right-click or a long press: done / skipped / no check), its name,
// its schedule, the "⋯" menu, the streaks and the month grid, whose past days are ticked the same
// way. Every check is written into the day's journal by the extension's action "set" (a task
// tagged #nawyk); its toast has Undo. A tick written in the journal itself shows here as well (the
// view is drawn again when notes change). The month row moves every grid; "Add habit" is the
// extension's command "add" (a form in ShellExtensionQuestion).
//
// Route parameters: month ("YYYY-MM"; none: this month), open ("add" = "Add habit" at once —
// screenshots).
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Habits")
    readonly property int maximumWidth: 720
    readonly property WorkspaceExtensions extensions: Workspace.extensions
    /// The month shown ("YYYY-MM"); "" is this month.
    property string month: ""
    readonly property string thisMonth: Js.slice(DatesHelper.today, 0, 7)
    readonly property string shownMonth: root.month !== "" ? root.month : root.thisMonth
    readonly property int shownYear: Js.parseInt(Js.slice(root.shownMonth, 0, 4))
    readonly property int shownMonthNumber: Js.parseInt(Js.slice(root.shownMonth, 5, 7))
    readonly property string monthLabel: DatesHelper.monthTitle(root.shownYear, root.shownMonthNumber)

    function applyRoute(): void {
        const month = AppController.routeText(root.route, "month")
        root.showMonth(month)
        root.addPending = AppController.routeText(root.route, "open") === "add"
        if (root.addPending)
            addLater.restart()
    }
    /// The month "YYYY-MM" ("" or not one: this month).
    function showMonth(month: string): void {
        root.month = DatesHelper.isValidDate(month + "-01") && month !== root.thisMonth ? Js.slice(month, 0, 7) : ""
    }
    /// Another month: `delta` months from the one shown.
    function moveMonth(delta: int): void {
        root.showMonth(Js.slice(DatesHelper.addMonths(root.shownMonth + "-01", delta), 0, 7))
    }
    function addHabit(): void {
        if (AppSettings.habitsEnabled)
            root.extensions.runCommand("nawyki", "add", "", -1)
    }
    /// open: "add" — as soon as the notebook is read (a new habit goes to the habits' page).
    property bool addPending: false
    Timer {
        id: addLater
        interval: 300 // the view draws the habits first
        onTriggered: {
            if (!root.addPending || Workspace.indexing)
                return
            root.addPending = false
            root.addHabit()
        }
    }
    Connections {
        target: Workspace
        function onIndexingChanged() {
            if (root.addPending)
                addLater.restart()
        }
    }

    onRouteChanged: applyRoute()
    Component.onCompleted: {
        applyRoute()
        lines.forceActiveFocus()
    }
    focus: true

    Keys.onPressed: (event) => {
        if (event.modifiers !== Qt.NoModifier || !AppSettings.habitsEnabled)
            return
        if (event.key === Qt.Key_N) {
            root.addHabit()
            event.accepted = true
        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_PageUp) {
            root.moveMonth(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Right || event.key === Qt.Key_PageDown) {
            root.moveMonth(1)
            event.accepted = true
        }
    }

    ExtensionView {
        id: habits
        objectName: "habitsModel"
        extensions: root.extensions
        extension: "nawyki"
        view: "month"
        paramKey: "month"
        paramValue: root.month
        // A habit's definition (its page at its line), a link's page: opened, or made when missing.
        onPageRequested: (page, line) => AppController.openNamedPage(page, line)
        onJournalRequested: (date) => AppController.openJournal(date)
        // The view's own month links ({ month }; none: this month).
        onViewRequested: (view, params) => root.showMonth(AppController.routeText(params, "month"))
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ShellExtensionView {
        id: lines
        objectName: "habitsList"
        anchors.fill: parent
        visible: AppSettings.habitsEnabled
        focus: true
        lines: habits
        maximumWidth: root.maximumWidth
        Accessible.name: qsTr("Habits")

        header: Item {
            width: lines.width
            height: headerColumn.y + headerColumn.height + Theme.space3

            Column {
                id: headerColumn
                x: lines.inset
                y: Theme.narrow ? Theme.space3 : Theme.space5
                width: lines.lineWidth
                spacing: Theme.space3

                Item {
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
                        text: qsTr("Recurring points of the day, ticked in the journal")
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
                        onClicked: root.addHabit()
                    }
                }

                Item {
                    id: monthRow
                    width: parent.width
                    height: prevButton.height

                    BIconButton {
                        id: prevButton
                        objectName: "habitsPrevMonth"
                        label: qsTr("Previous month")
                        iconName: "chevron-left"
                        onClicked: root.moveMonth(-1)
                    }
                    BText {
                        id: monthText
                        objectName: "habitsMonth"
                        x: prevButton.width + Theme.space1
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.monthLabel
                        variant: "label"
                    }
                    BIconButton {
                        x: monthText.x + monthText.width + Theme.space1
                        objectName: "habitsNextMonth"
                        label: qsTr("Next month")
                        iconName: "chevron-right"
                        onClicked: root.moveMonth(1)
                    }
                    BButton {
                        objectName: "habitsThisMonth"
                        x: parent.width - width
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.month !== ""
                        size: "small"
                        variant: "ghost"
                        text: qsTr("This month")
                        onClicked: root.month = ""
                    }
                }

                BText {
                    visible: habits.error !== ""
                    width: parent.width
                    text: qsTr("The view could not be drawn: %1").arg(habits.error)
                    color: Theme.danger
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    BEmptyState {
        objectName: "habitsOff"
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.space6, 440)
        visible: !AppSettings.habitsEnabled
        iconName: "flame"
        title: qsTr("Habits are off")
        description: qsTr("Recurring points of the day ticked in the journal (“- [x] Reading #habit”), with streaks and a month grid. Switch them on in Settings → Extensions.")
        actionText: qsTr("Switch on")
        onActionTriggered: AppSettings.habitsEnabled = true
    }
}
