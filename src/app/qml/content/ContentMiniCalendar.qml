pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ContentMiniCalendar — the side panel's "Kalendarz": a month (BDatePicker) with a dot on every
// day that has a journal and on days with dates or reminders; picking a day lists its entries
// (calendar items: due / scheduled tasks, reminders) and "Otwórz dziennik" opens that day
// (created when you write in it). Today and the open page's day are marked by the picker.
//
//   ContentMiniCalendar { selectedDate: "2026-10-14" }
Item {
    id: root

    property string selectedDate: DatesHelper.today

    // Dots for the six weeks shown: journals + calendar entries (Workspace.revision: follows edits).
    readonly property var marks: {
        void Workspace.revision
        const cells = picker.cells
        if (!cells || cells.length === 0)
            return ({})
        const from = cells[0].date
        const to = cells[cells.length - 1].date
        const out = {}
        for (const d of Workspace.journalDates(from, to))
            out[d] = ["journal"]
        for (const item of Workspace.calendarItems(from, to)) {
            if (item.kind === "journal")
                continue
            const kind = item.kind === "due" && item.date < DatesHelper.today && item.status !== "done" ? "overdue"
                       : item.kind === "reminder" ? (item.alarm ? "alarm" : "reminder") : item.kind
            const list = out[item.date] || []
            if (list.indexOf(kind) < 0 && list.length < 3)
                list.push(kind)
            out[item.date] = list
        }
        return out
    }
    readonly property var dayItems: {
        void Workspace.revision
        return Workspace.calendarItems(root.selectedDate, root.selectedDate).filter(i => i.kind !== "journal")
    }
    readonly property bool hasJournal: {
        void Workspace.revision
        return Workspace.journalDates(root.selectedDate, root.selectedDate).length > 0
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
                onPicked: (value) => root.selectedDate = value.slice(0, 10)
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
            BButton {
                Layout.fillWidth: true
                iconName: "book-open"
                text: root.hasJournal ? qsTr("Open journal") : qsTr("Write in this day's journal")
                onClicked: AppController.navigate({ view: "journal", date: root.selectedDate })
            }

            Repeater {
                model: root.dayItems
                delegate: BListItem {
                    id: entry
                    required property var modelData
                    Layout.fillWidth: true
                    dense: true
                    text: entry.modelData.title
                    readonly property string day: Workspace.journalDate(entry.modelData.path)
                    readonly property string where: (entry.modelData.time !== "" ? entry.modelData.time + " · " : "")
                                                    + (day !== "" ? DatesHelper.formatShort(day) : Workspace.pageTitle(entry.modelData.path))
                    // The subtitle is shown as AutoText: a "<" in a file name must stay text.
                    subtitle: where.indexOf("<") >= 0 ? TextHelper.escapeHtml(where) : where
                    iconName: entry.modelData.kind === "reminder" ? (entry.modelData.alarm ? "alarm-clock" : "bell")
                              : entry.modelData.status === "done" ? "circle-check"
                              : entry.modelData.kind === "scheduled" ? "hourglass" : "calendar-check"
                    iconColor: entry.modelData.kind === "reminder" ? Theme.warningText : Theme.fgMuted
                    onClicked: AppController.openPage(entry.modelData.path, entry.modelData.line)
                }
            }
            BText {
                visible: root.dayItems.length === 0
                Layout.fillWidth: true
                text: qsTr("No tasks or reminders on this day.")
                variant: "muted"
                wrapMode: Text.Wrap
            }
        }
    }
}
