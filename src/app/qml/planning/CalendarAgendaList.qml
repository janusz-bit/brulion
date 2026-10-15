pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// CalendarAgendaList — the agenda: the days of a CalendarModel in "agenda" mode (four weeks from
// the start of the selected week) that have entries, plus today, each with its entries as
// PlanningItemRows; a day with a daily note offers to open it. Past days are muted. Scrolls to
// today (or the selected day) when the range changes.
//
//   CalendarAgendaList { calendar: cal; onItemOpened: …; onItemToggled: …; onJournalOpened: … }
FocusScope {
    id: root

    required property CalendarModel calendar
    /// Text width on wide windows.
    property int maximumWidth: Theme.measure

    signal itemOpened(var entry)
    signal itemToggled(var entry)
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
            implicitHeight: column.implicitHeight + Theme.space2
            opacity: isPast && !isToday ? 0.72 : 1

            ColumnLayout {
                id: column
                width: Math.min(parent.width - 2 * Theme.space4, root.maximumWidth)
                x: (parent.width - width) / 2
                spacing: 0

                // Day header: number, weekday and month, "Today", the daily note.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space2
                    Layout.bottomMargin: Theme.space1
                    spacing: Theme.space3

                    Rectangle {
                        Layout.preferredWidth: Theme.touchUi ? 40 : 36
                        Layout.preferredHeight: Layout.preferredWidth
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
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
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
                            Layout.fillWidth: true
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
                        visible: day.hasJournal
                        size: "small"
                        variant: "ghost"
                        iconName: "book-open"
                        text: Theme.compact ? "" : qsTr("Journal")
                        Accessible.name: qsTr("Open the journal of %1").arg(day.longLabel)
                        onClicked: root.journalOpened(day.date)
                    }
                }

                Repeater {
                    model: day.items
                    delegate: PlanningItemRow {
                        required property var modelData
                        Layout.fillWidth: true
                        entry: modelData
                        onOpened: root.itemOpened(modelData)
                        onTaskToggled: root.itemToggled(modelData)
                    }
                }
                Text {
                    visible: day.items.length === 0
                    Layout.fillWidth: true
                    Layout.leftMargin: (Theme.touchUi ? 40 : 36) + Theme.space3
                    Layout.bottomMargin: Theme.space1
                    text: day.hasJournal ? qsTr("Only a journal entry.") : qsTr("Nothing planned.")
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgFaint
                    wrapMode: Text.Wrap
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space2
                    implicitHeight: 1
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
