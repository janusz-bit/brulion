pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentDayTasks — the side panel's "Tasks" of a journal day (docs/UI.md "Side panel"): the
// notebook's open tasks as the Tasks view files them seen from `date` (TaskListModel with
// `today` = the day) — overdue (their date in red), that day's, the next day's —, at most `limit`;
// each row: a dot in the task's colour, its text, its date in the mono font; a click opens the
// task's block, "All tasks" the Tasks view.
//
//   ContentDayTasks { date: "2026-10-14" }
Item {
    id: root

    property string date
    property int limit: 12
    /// The sections listed (TaskListModel's keys).
    readonly property list<string> shownSections: ["overdue", "today", "tomorrow"]
    /// The tasks of those sections.
    readonly property int count: {
        let n = 0
        for (let i = 0; i < root.sections.length; ++i) {
            const key = AppController.routeText(root.sections[i], "key")
            if (root.shownSections.indexOf(key) >= 0)
                n += AppController.routeInt(root.sections[i], "count", 0)
        }
        return n
    }
    readonly property list<var> sections: tasks.sections

    implicitHeight: column.implicitHeight

    TaskListModel {
        id: tasks
        vaultIndex: PlanningContext.vaultIndex
        today: root.date !== "" ? root.date : DatesHelper.today
        weekStart: DatesHelper.weekStart
        locale: DatesHelper.locale
        statusFilter: "open"
        nestSubtasks: false
        // Only the sections shown make rows (the others keep a header, not listed here).
        collapsedSections: ["thisWeek", "later", "noDate", "done"]
    }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 2

        BText {
            visible: root.count === 0
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space2
            text: qsTr("No open tasks due by this day.")
            variant: "muted"
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }

        Repeater {
            model: tasks
            delegate: BListItem {
                id: row
                required property int index
                required property string rowType
                required property string section
                required property string path
                required property int line
                required property string dueLabel
                required property string scheduledLabel
                required property bool overdue
                required property string status
                required text
                // Rows of the sections shown, up to the limit (headers and the rest: hidden, so the
                // layout gives them no height).
                readonly property bool shown: row.rowType === "task" && root.shownSections.indexOf(row.section) >= 0
                                              && row.index <= root.limit + root.shownSections.length
                objectName: "dayTaskRow"
                visible: row.shown
                Layout.fillWidth: true
                dense: true
                leftPadding: Theme.space2
                Accessible.name: row.text + (row.dueLabel !== "" ? ", " + row.dueLabel : "")
                onClicked: AppController.openPage(row.path, row.line, false)
                contentItem: RowLayout {
                    spacing: Theme.space2
                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: "transparent"
                        border.width: 1.5
                        border.color: row.overdue ? Theme.dangerText : row.status === "doing" ? Theme.accent : Theme.borderControl
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.text
                        textFormat: Text.PlainText
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
                        color: Theme.fg
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }
                    Text {
                        readonly property string label: row.dueLabel !== "" ? row.dueLabel : row.scheduledLabel
                        visible: label !== ""
                        text: label
                        textFormat: Text.PlainText
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.textXs
                        color: row.overdue ? Theme.dangerText : Theme.fgMuted
                        Accessible.ignored: true
                    }
                }
            }
        }

        BButton {
            Layout.leftMargin: Theme.space1
            variant: "ghost"
            size: "small"
            iconName: "list-todo"
            text: qsTr("All tasks")
            onClicked: AppController.navigate({ view: "tasks" })
        }
    }
}
