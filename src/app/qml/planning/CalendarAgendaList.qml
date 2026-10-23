pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// CalendarAgendaList — the agenda: the days of a CalendarModel in "agenda" mode (four weeks from
// the start of the selected week) that have entries, plus today, each with its entries as
// PlanningItemRows; a day with a daily note offers to open it. Past days are muted. Scrolls to
// today (or the selected day) when the range changes.
//
//   CalendarAgendaList { calendar: cal; onItemOpened: …; onItemToggled: …; onItemStatusRequested: …
//                        onJournalOpened: … }
FocusScope {
    id: root

    required property CalendarModel calendar
    /// Text width on wide windows.
    property int maximumWidth: Theme.measure

    signal itemOpened(var entry)
    signal itemToggled(var entry)
    /// A task's status menu: `status` "todo" … "cancelled", "" = none.
    signal itemStatusRequested(var entry, string status)
    signal journalOpened(string date)
    signal dayTapped(string date)

    readonly property bool empty: list.count === 0 || (list.count === 1 && calendar.itemCount === 0
                                                       && calendar.data(calendar.index(0, 0), CalendarModel.HasJournalRole) !== true)

    function scrollToAnchor() {
        let row = calendar.rowForDate(calendar.anchorDate)
        if (row < 0)
            row = calendar.rowForDate(DatesHelper.today)
        if (row >= 0)
            list.positionViewAtIndex(row, ListView.Beginning)
    }
    Connections {
        target: root.calendar
        function onRangeChanged() { Qt.callLater(root.scrollToAnchor) }
    }
    Component.onCompleted: Qt.callLater(scrollToAnchor)

    ListView {
        id: list
        TouchFlick.enabled: true
        anchors.fill: parent
        visible: !root.empty
        focus: true
        clip: true
        model: root.calendar
        spacing: Theme.space2
        topMargin: Theme.space2
        bottomMargin: Theme.space16
        reuseItems: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: root.calendar.title

        delegate: Item {
            id: day

            required property int index
            required property string date
            required property int day
            required property bool isToday
            required property bool isPast
            required property bool hasJournal
            required property var items
            required property string longLabel

            width: ListView.view.width
            implicitHeight: column.height + Theme.space2
            opacity: isPast && !isToday ? 0.72 : 1

            // Laid out with bindings, not Layouts: a day has its final height when the list creates
            // it (a Layout's size arrives only with its next polish, and a day growing after the
            // list had placed it pushed the days below it while the agenda scrolled up — docs/UI.md
            // "Scrolling performance").
            Item {
                id: column
                readonly property real markerWidth: Theme.touchUi ? 40 : 36
                width: Math.min(parent.width - 2 * Theme.space4, root.maximumWidth)
                x: (parent.width - width) / 2
                height: separator.y + separator.height

                // Day header: number, weekday and month, "Today", the daily note.
                Item {
                    id: header
                    y: Theme.space2
                    width: parent.width
                    height: Math.max(circle.height, labels.height, journalButton.visible ? journalButton.implicitHeight : 0)

                    Rectangle {
                        id: circle
                        width: column.markerWidth
                        height: width
                        y: (header.height - height) / 2
                        radius: width / 2
                        color: day.isToday ? Theme.accentSolid : Theme.bgSunken
                        Text {
                            anchors.centerIn: parent
                            text: day.day
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textLg
                            font.weight: Theme.weightBold
                            font.features: { "tnum": 1 }
                            color: day.isToday ? Theme.fgOnAccent : Theme.fg
                        }
                    }
                    Item {
                        id: labels
                        x: circle.width + Theme.space3
                        width: Math.max(0, header.width - x - (journalButton.visible ? journalButton.implicitWidth + Theme.space3 : 0))
                        height: dayLabel.implicitHeight + (relativeLabel.visible ? relativeLabel.implicitHeight : 0)
                        y: (header.height - height) / 2
                        Text {
                            id: dayLabel
                            width: parent.width
                            text: day.longLabel.charAt(0).toLocaleUpperCase() + day.longLabel.slice(1)
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textMd
                            font.weight: Theme.weightSemiBold
                            color: Theme.fg
                            elide: Text.ElideRight
                            Accessible.role: Accessible.Heading
                            Accessible.name: text
                        }
                        Text {
                            id: relativeLabel
                            y: dayLabel.implicitHeight
                            width: parent.width
                            text: {
                                const h = day.isToday ? qsTr("Today") : DatesHelper.formatHuman(day.date)
                                return h.charAt(0).toLocaleUpperCase() + h.slice(1)
                            }
                            // Only the relative words (today, tomorrow, yesterday).
                            visible: day.isToday || DatesHelper.formatHuman(day.date).indexOf(" ") < 0
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textSm
                            color: day.isToday ? Theme.accentText : Theme.fgMuted
                            elide: Text.ElideRight
                        }
                    }
                    BButton {
                        id: journalButton
                        anchors.right: parent.right
                        y: (header.height - height) / 2
                        visible: day.hasJournal
                        size: "small"
                        variant: "ghost"
                        iconName: "book-open"
                        text: Theme.compact ? "" : qsTr("Journal")
                        Accessible.name: qsTr("Open the journal of %1").arg(day.longLabel)
                        onClicked: root.journalOpened(day.date)
                    }
                }

                Column {
                    id: entries
                    y: header.y + header.height + Theme.space1
                    width: parent.width
                    // (`revision`: itemAt() notifies nothing, items may come after the count; read for
                    // real — qmlcachegen compiles a bare `void rows.revision` away with the dependency.)
                    height: {
                        if (rows.revision < 0)
                            return 0
                        let h = 0
                        for (let i = 0; i < rows.count; ++i) {
                            const row = rows.itemAt(i)
                            if (row)
                                h += row.height
                        }
                        return h
                    }
                    Repeater {
                        id: rows
                        property int revision: 0
                        onItemAdded: revision++
                        onItemRemoved: revision++
                        model: day.items
                        delegate: PlanningItemRow {
                            required property var modelData
                            width: entries.width
                            entry: modelData
                            onOpened: root.itemOpened(modelData)
                            onTaskToggled: root.itemToggled(modelData)
                            onStatusRequested: (status) => root.itemStatusRequested(modelData, status)
                        }
                    }
                }
                Text {
                    id: nothing
                    visible: day.items.length === 0
                    x: column.markerWidth + Theme.space3
                    y: entries.y + entries.height
                    width: Math.max(0, parent.width - x)
                    text: day.hasJournal ? qsTr("Only a journal entry.") : qsTr("Nothing planned.")
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgFaint
                    wrapMode: Text.Wrap
                }
                Rectangle {
                    id: separator
                    y: (nothing.visible ? nothing.y + nothing.implicitHeight + Theme.space1 : entries.y + entries.height)
                       + Theme.space2
                    width: parent.width
                    height: 1
                    color: Theme.borderSubtle
                }
            }
        }
    }

    BEmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.space6, 420)
        visible: root.empty
        iconName: "calendar"
        title: qsTr("Nothing planned in these four weeks")
        description: qsTr("Dates with 📅, ⏳ and reminders with ⏰ from your notes appear here. Add one to a journal with the buttons of a day.")
    }
}
