pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// CalendarDayPanel — one day of the calendar: its date, the daily note (open / create), the
// overdue tasks (on today), the day's entries as PlanningItemRows (tasks can be ticked off), and
// "Add task" / "Add reminder" (both written into that day's journal by the view). Shown as a
// side column on wide views and inside a bottom sheet on phones.
//
//   CalendarDayPanel { calendar: cal; date: cal.anchorDate
//                      onItemOpened: (e) => AppController.openPage(e.path, e.line) … }
FocusScope {
    id: root

    required property CalendarModel calendar
    property string date
    /// Overdue tasks are listed on today's panel.
    readonly property bool showOverdue: date === DatesHelper.today && calendar.overdueCount > 0
    property bool overdueExpanded: true
    /// Overdue tasks listed here; more open the tasks view.
    property int overdueLimit: 5
    /// Scroll inside (side column); a sheet scrolls itself.
    property bool scrollable: true
    /// The date heading (a sheet shows it in its own title).
    property bool showHeading: true
    /// Empty space after the content (e.g. above a floating button).
    property real bottomMargin: Theme.space4

    signal itemOpened(var entry)
    signal itemToggled(var entry)
    signal journalOpened(string date)
    signal addTaskRequested(string date)
    signal addReminderRequested(string date)
    signal allOverdueRequested()

    readonly property var entries: {
        void calendar.revision
        return date !== "" ? calendar.dayItems(date) : []
    }
    readonly property var journal: entries.find(e => e.kind === "journal") || null
    readonly property var items: entries.filter(e => e.kind !== "journal")
    readonly property string heading: {
        const s = date !== "" ? DatesHelper.formatLong(date) : ""
        return s.length > 0 ? s.charAt(0).toLocaleUpperCase() + s.slice(1) : ""
    }
    readonly property string relative: {
        if (date === "")
            return ""
        const h = DatesHelper.formatHuman(date)
        return h.indexOf(" ") < 0 ? h.charAt(0).toLocaleUpperCase() + h.slice(1) : ""
    }

    implicitHeight: column.implicitHeight

    Flickable {
        id: flick
        TouchFlick.enabled: true
        anchors.fill: parent
        interactive: root.scrollable && contentHeight > height
        contentHeight: column.implicitHeight
        clip: root.scrollable
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}

        ColumnLayout {
            id: column
            width: flick.width
            spacing: Theme.space2

            // ---- Date --------------------------------------------------------------------
            ColumnLayout {
                visible: root.showHeading
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space2
                Layout.rightMargin: Theme.space2
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    visible: root.relative !== ""
                    text: root.relative
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    font.weight: Theme.weightSemiBold
                    color: root.date === DatesHelper.today ? Theme.accentText : Theme.fgMuted
                }
                Text {
                    Layout.fillWidth: true
                    text: root.heading
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textLg
                    font.weight: Theme.weightSemiBold
                    color: Theme.fg
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
            }

            // ---- Actions ---------------------------------------------------------------------
            Flow {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space1
                spacing: Theme.space1
                BButton {
                    size: "small"
                    iconName: "book-open"
                    text: root.journal ? qsTr("Open journal") : qsTr("Write in journal")
                    onClicked: root.journalOpened(root.date)
                }
                BButton {
                    size: "small"
                    iconName: "plus"
                    text: qsTr("Add task")
                    onClicked: root.addTaskRequested(root.date)
                }
                BButton {
                    size: "small"
                    iconName: "bell"
                    text: qsTr("Add reminder")
                    onClicked: root.addReminderRequested(root.date)
                }
            }

            // ---- Overdue (today) -------------------------------------------------------------
            BSectionHeader {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space2
                visible: root.showOverdue
                text: qsTr("Overdue")
                count: root.calendar.overdueCount
                collapsible: true
                expanded: root.overdueExpanded
                onExpandedToggled: (open) => root.overdueExpanded = open
            }
            Repeater {
                model: root.showOverdue && root.overdueExpanded ? root.calendar.overdue.slice(0, root.overdueLimit) : []
                delegate: PlanningItemRow {
                    required property var modelData
                    Layout.fillWidth: true
                    entry: modelData
                    showDate: true
                    onOpened: root.itemOpened(modelData)
                    onTaskToggled: root.itemToggled(modelData)
                }
            }

            BButton {
                visible: root.showOverdue && root.overdueExpanded && root.calendar.overdueCount > root.overdueLimit
                Layout.leftMargin: Theme.space1
                size: "small"
                variant: "ghost"
                iconName: "list-todo"
                text: qsTr("All %n overdue in Tasks", "", root.calendar.overdueCount)
                onClicked: root.allOverdueRequested()
            }

            // ---- The day -----------------------------------------------------------------
            BSectionHeader {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space2
                text: qsTr("Plan of the day")
                count: root.items.length
            }
            Repeater {
                model: root.items
                delegate: PlanningItemRow {
                    required property var modelData
                    Layout.fillWidth: true
                    entry: modelData
                    onOpened: root.itemOpened(modelData)
                    onTaskToggled: root.itemToggled(modelData)
                }
            }
            BEmptyState {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space2
                visible: root.items.length === 0
                compact: true
                iconName: root.journal ? "book-open" : "calendar"
                title: qsTr("Nothing planned")
                description: root.journal ? qsTr("This day has a journal entry, but no dates or reminders.")
                                          : qsTr("Tasks and reminders you add here go into this day's journal.")
            }
            Item { Layout.preferredHeight: root.bottomMargin }
        }
    }
}
