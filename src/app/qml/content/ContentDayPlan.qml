pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentDayPlan — the side panel's "Day plan" of a journal day (docs/UI.md "Side panel"): the
// blocks with a due date, a scheduled date or reminders on `date` (Workspace.dayPlan(),
// reminders/dayplan.h: one row per block), timed rows first. Each row: the time in the mono font
// (a dash without one), a coloured bar — amber for reminders and alarms (whatever the
// accent), the link blue for due dates, violet for scheduled ones, red for a due date already
// past, faint for a done task — and the block's text; a click opens the block.
//
//   ContentDayPlan { date: "2026-10-14" }
Item {
    id: root

    property string date
    /// The rows (Workspace.dayPlan), followed through Workspace.revision.
    readonly property list<var> rows: root.date === "" || Workspace.revision < 0 ? [] : UiModels.withDefaults(
            Workspace.dayPlan(root.date), {
        time: "", title: "", path: "", line: 0, status: "", due: false, scheduled: false, reminder: false,
        alarm: false, recurring: false
    })
    readonly property int count: rows.length

    implicitHeight: count > 0 ? column.implicitHeight : emptyText.implicitHeight

    /// The bar's colour of a row.
    function barColor(status: string, due: bool, scheduled: bool, reminder: bool): color {
        if (status === "done" || status === "cancelled")
            return Theme.fgFaint
        if (reminder)
            return Theme.markColor("reminder")
        if (due && DatesHelper.compare(root.date, DatesHelper.today) < 0)
            return Theme.dangerText
        if (due)
            return Theme.link
        return scheduled ? Theme.chipScheduledFg : Theme.fgMuted
    }

    BText {
        id: emptyText
        visible: root.count === 0
        x: Theme.space2
        width: parent.width - 2 * Theme.space2
        text: qsTr("Nothing planned for this day.")
        variant: "muted"
        font.pixelSize: Theme.textSm
        wrapMode: Text.Wrap
    }

    Column {
        id: column
        width: parent.width
        visible: root.count > 0
        spacing: 2

        Repeater {
            model: root.rows
            delegate: BListItem {
                id: row
                required property var modelData
                readonly property string time: AppController.routeText(row.modelData, "time")
                readonly property string status: AppController.routeText(row.modelData, "status")
                readonly property bool finished: row.status === "done" || row.status === "cancelled"
                readonly property string path: AppController.routeText(row.modelData, "path")
                readonly property int line: AppController.routeInt(row.modelData, "line", 0)
                readonly property bool due: AppController.routeFlag(row.modelData, "due")
                readonly property bool scheduled: AppController.routeFlag(row.modelData, "scheduled")
                readonly property bool reminder: AppController.routeFlag(row.modelData, "reminder")
                objectName: "dayPlanRow"
                width: column.width
                dense: true
                leftPadding: Theme.space2
                text: AppController.routeText(row.modelData, "title")
                Accessible.name: (row.time !== "" ? row.time : qsTr("All day", "day plan")) + ", " + row.text
                onClicked: AppController.openPage(row.path, row.line, false)
                contentItem: RowLayout {
                    spacing: Theme.space2
                    Text {
                        Layout.preferredWidth: timeMetrics.advanceWidth
                        text: row.time !== "" ? row.time : "–"
                        textFormat: Text.PlainText
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.textXs
                        color: row.time !== "" ? Theme.fgMuted : Theme.fgFaint
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }
                    Rectangle {
                        Layout.preferredWidth: 3
                        Layout.fillHeight: true
                        Layout.topMargin: 3
                        Layout.bottomMargin: 3
                        radius: 1.5
                        color: root.barColor(row.status, row.due, row.scheduled, row.reminder)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.text
                        textFormat: Text.PlainText
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
                        font.strikeout: row.finished
                        color: row.finished ? Theme.fgFaint : Theme.fg
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }
                    BIcon {
                        visible: AppController.routeFlag(row.modelData, "alarm")
                        name: "alarm-clock"
                        size: Theme.iconSizeSm - 2
                        color: Theme.markColor("alarm")
                    }
                }
            }
        }
    }
    TextMetrics {
        id: timeMetrics
        font.family: Theme.fontMono
        font.pixelSize: Theme.textXs
        text: "00:00"
    }
}
