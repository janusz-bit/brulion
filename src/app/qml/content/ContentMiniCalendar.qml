pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ContentMiniCalendar — the side panel's "Calendar": a month (BDatePicker) with a dot on every
// day that has a journal and on days with dates or reminders; picking a day lists its entries
// (calendar items: due / scheduled tasks, reminders) and "Open journal" opens that day
// (created when you write in it). Today and the open page's day are marked by the picker.
// Also the tray calendar's content (ShellTrayCalendar): `activated()` after an entry or the
// journal was opened; with a `placeholder` (no notebook open) only the month and that text — no
// marks, no entries, no Workspace queries.
//
//   ContentMiniCalendar { selectedDate: "2026-10-14" }
Item {
    id: root

    property string selectedDate: DatesHelper.today
    property string placeholder: ""
    /// The day's journal or one of its entries was opened (the tray calendar then hides).
    signal activated()

    /// Select `date` and show its month (the tray calendar opens on today's month every time).
    function showDay(date: string): void {
        root.selectedDate = date
        picker.showMonthOf(date)
        picker.focusDate = date
        flick.contentY = 0
    }

    PageTools { id: tools; workspace: Workspace }
    function journalDates(date: string): list<string> {
        return Workspace.journalDates(date, date)
    }

    /// The picker's month grid ({ date } cells) and the date of its first or last cell.
    function cells(): list<var> {
        return picker.cells
    }
    function cellDate(last: bool): string {
        const c = root.cells()
        if (c.length === 0)
            return ""
        const cell = c[last ? c.length - 1 : 0]
        return AppController.routeText(cell, "date")
    }

    // Dots for the six weeks shown: journals + calendar entries (Workspace.revision: follows edits;
    // only while the tab is shown — the side panel keeps it while another tab is open, and every
    // save while typing bumps the revision). PageTools.calendarMarks.
    readonly property var marks: {
        // (Read for real: a bare `void Workspace.revision` is compiled away with the dependency.)
        const from = root.cellDate(false)
        if (!root.visible || root.placeholder !== "" || from === "" || Workspace.revision < 0)
            return ({})
        return tools.calendarMarks(from, root.cellDate(true), DatesHelper.today)
    }
    readonly property list<var> dayItems: !root.visible || root.placeholder !== "" || Workspace.revision < 0
                                          ? [] : tools.calendarEntries(root.selectedDate)
    readonly property bool hasJournal: {
        return root.visible && root.placeholder === "" && Workspace.revision >= 0
               && root.journalDates(root.selectedDate).length > 0
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: column.implicitHeight + 2 * Theme.space3
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}

        ColumnLayout {
            id: column
            x: Theme.space3
            y: Theme.space3
            width: flick.width - 2 * Theme.space3
            spacing: Theme.space2

            BDatePicker {
                id: picker
                Layout.fillWidth: true
                showNaturalInput: false
                selectedDate: root.selectedDate
                marks: root.marks
                onPicked: (value) => root.selectedDate = Js.slice(value, 0, 10)
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space2
                spacing: Theme.space2
                BText {
                    Layout.fillWidth: true
                    text: DatesHelper.formatLong(root.selectedDate)
                    variant: "label"
                    wrapMode: Text.Wrap
                }
            }
            BText {
                visible: root.placeholder !== ""
                Layout.fillWidth: true
                text: root.placeholder
                variant: "muted"
                wrapMode: Text.Wrap
            }
            BButton {
                objectName: "miniCalendarOpenJournal"
                visible: root.placeholder === ""
                Layout.fillWidth: true
                iconName: "book-open"
                text: root.hasJournal ? qsTr("Open journal") : qsTr("Write in this day's journal")
                onClicked: {
                    AppController.navigate({ view: "journal", date: root.selectedDate })
                    root.activated()
                }
            }

            Repeater {
                model: root.dayItems
                delegate: BListItem {
                    id: entry
                    // Workspace.calendarItems() rows (every key is always there).
                    required property string title
                    required property string path
                    required property string time
                    required property string kind
                    required property string status
                    required property bool alarm
                    required property int line
                    Layout.fillWidth: true
                    dense: true
                    text: entry.title
                    readonly property string day: Workspace.journalDate(entry.path)
                    readonly property string where: (entry.time !== "" ? entry.time + " · " : "")
                                                    + (day !== "" ? DatesHelper.formatShort(day) : Workspace.pageTitle(entry.path))
                    subtitle: where // plain text (BListItem)
                    iconName: entry.kind === "reminder" ? (entry.alarm ? "alarm-clock" : "bell")
                              : entry.status === "done" ? "circle-check"
                              : entry.kind === "scheduled" ? "hourglass" : "calendar-check"
                    iconColor: entry.kind === "reminder" ? Theme.warningText : Theme.fgMuted
                    onClicked: {
                        AppController.openPage(entry.path, entry.line)
                        root.activated()
                    }
                }
            }
            BText {
                visible: root.placeholder === "" && root.dayItems.length === 0
                Layout.fillWidth: true
                text: qsTr("No tasks or reminders on this day.")
                variant: "muted"
                wrapMode: Text.Wrap
            }
        }
    }
}
