pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// CalendarDayPanel — one day of the calendar: its date, the daily note (open / create), the
// overdue tasks (on today), the day's entries as PlanningItemRows (open ones first; tasks can be
// ticked off), and "Add task" / "Add reminder" (both written into that day's journal by the view)
// — the day's actions as one even list of full-width rows under the date. Shown as a side column
// on wide views and inside a bottom sheet on phones.
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
    /// Empty space after the content.
    property real bottomMargin: Theme.space4

    signal itemOpened(var entry)
    signal itemToggled(var entry)
    /// A right-click or long press on an entry: its token menu (PlanningTokenMenu.openToken).
    signal itemMenuRequested(var entry, Item item, real x, real y)
    /// A task's status menu: `status` "todo" … "cancelled", "" = none.
    signal itemStatusRequested(var entry, string status)
    signal journalOpened(string date)
    signal addTaskRequested(string date)
    signal addReminderRequested(string date)
    signal allOverdueRequested()

    // (`calendar.revision` read for real: the calls are not tracked, and a bare
    // `void calendar.revision` is compiled away with the dependency.)
    /// The day has a daily note.
    readonly property bool journal: date !== "" && calendar.revision >= 0 && calendar.dayHasJournal(date)
    /// The day's entries without the journal, open ones first (a stable partition: times keep
    /// their order).
    readonly property list<var> orderedItems: date !== "" && calendar.revision >= 0 ? calendar.dayEntries(date) : []
    readonly property list<var> none: []
    readonly property string heading: date !== "" ? root.capitalized(DatesHelper.formatLong(date)) : ""
    readonly property string relative: date !== "" ? root.relativeOf(DatesHelper.formatHuman(date)) : ""

    /// `human` (DatesHelper.formatHuman()) capitalized when it is a relative word ("Dziś"), else "".
    function relativeOf(human: string): string {
        return Js.indexOf(human, " ") < 0 ? root.capitalized(human) : ""
    }

    /// `s` with its first letter upper case (the locale's: "dziś" → "Dziś").
    function capitalized(s: string): string {
        return s.length > 0 ? Js.toUpperCase(Js.charAt(s, 0)) + Js.sliceFrom(s, 1) : ""
    }
    /// The overdue tasks listed (at most overdueLimit).
    function overdueShown(): list<var> {
        return root.showOverdue && root.overdueExpanded ? root.calendar.overdue.slice(0, root.overdueLimit) : root.none
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

            // ---- Actions: one even list of full-width rows (icon and label), the icons in
            // line with the date heading; 30 px rows with a mouse, 44 px on touch -------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                BListItem {
                    objectName: "calendarDayJournal"
                    Layout.fillWidth: true
                    dense: true
                    leftPadding: Theme.space2
                    iconName: "book-open"
                    text: root.journal ? qsTr("Open journal") : qsTr("Write in journal")
                    Accessible.role: Accessible.Button
                    onClicked: root.journalOpened(root.date)
                }
                BListItem {
                    objectName: "calendarDayAddTask"
                    Layout.fillWidth: true
                    dense: true
                    leftPadding: Theme.space2
                    iconName: "plus"
                    text: qsTr("Add task")
                    Accessible.role: Accessible.Button
                    onClicked: root.addTaskRequested(root.date)
                }
                BListItem {
                    objectName: "calendarDayAddReminder"
                    Layout.fillWidth: true
                    dense: true
                    leftPadding: Theme.space2
                    iconName: "bell"
                    text: qsTr("Add reminder")
                    Accessible.role: Accessible.Button
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
                model: root.overdueShown()
                delegate: PlanningItemRow {
                    required property var modelData
                    Layout.fillWidth: true
                    entry: modelData
                    showDate: true
                    onOpened: root.itemOpened(modelData)
                    onTaskToggled: root.itemToggled(modelData)
                    onStatusRequested: (status) => root.itemStatusRequested(modelData, status)
                    onTokenMenuRequested: (item, x, y) => root.itemMenuRequested(modelData, item, x, y)
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
                id: dayHeader
                Layout.fillWidth: true
                Layout.topMargin: Theme.space2
                text: qsTr("Plan of the day")
                count: root.orderedItems.length
            }
            Repeater {
                model: root.orderedItems
                delegate: PlanningItemRow {
                    required property var modelData
                    Layout.fillWidth: true
                    entry: modelData
                    onOpened: root.itemOpened(modelData)
                    onTaskToggled: root.itemToggled(modelData)
                    onStatusRequested: (status) => root.itemStatusRequested(modelData, status)
                    onTokenMenuRequested: (item, x, y) => root.itemMenuRequested(modelData, item, x, y)
                }
            }
            // Nothing planned: a short note under "Plan dnia", in line with it (no picture: the
            // panel stays a list, not an empty page).
            BEmptyState {
                Layout.fillWidth: true
                Layout.leftMargin: dayHeader.leftPadding
                Layout.rightMargin: Theme.space2
                visible: root.orderedItems.length === 0
                compact: true
                alignStart: true
                iconName: ""
                title: qsTr("Nothing planned")
                description: root.journal ? qsTr("This day has a journal entry, but no dates or reminders.")
                                          : qsTr("Tasks and reminders you add here go into this day's journal.")
            }
            Item { Layout.preferredHeight: root.bottomMargin }
        }
    }
}
