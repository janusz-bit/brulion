pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CalendarAgendaList — the agenda: the days of a CalendarModel in "agenda" mode (four weeks from
// the start of the selected week) that have entries, plus today, each with its entries as
// PlanningItemRows; a day with a daily note offers to open it. Past days say so with colours, not
// a fade (at 72 % opacity faint text fell to 2.9:1, under WCAG AA): the date's circle outlined
// (Theme.border) with its number and the open entries' titles in fgMuted; the rest as on other
// days. Scrolls to today (or the selected day) when the range changes.
// A day's header is a button (a tap or click, Enter on the list's current day — Up / Down move a
// focus ring from the selected day): dayTapped, the view selects the day (the side panel follows,
// phones open its sheet). The selected day's number is on a dark disc (Theme.primary), as in the
// month grid; today keeps its red one. The list stays where it is (the model keeps the range: only
// the selection moves).
//
//   CalendarAgendaList { calendar: cal; onItemOpened: …; onItemToggled: …; onItemStatusRequested: …
//                        onJournalOpened: …; onDayTapped: (date) => … }
FocusScope {
    id: root

    required property CalendarModel calendar
    /// Its width on wide windows (Theme.measureList: wider than the editor's text measure, a row
    /// holds a time, a title and its page).
    property int maximumWidth: Theme.measureList

    signal itemOpened(var entry)
    signal itemToggled(var entry)
    /// A right-click or long press on an entry: its token menu (PlanningTokenMenu.openToken).
    signal itemMenuRequested(var entry, Item item, real x, real y)
    /// A task's status menu: `status` "todo" … "cancelled", "" = none.
    signal itemStatusRequested(var entry, string status)
    signal journalOpened(string date)
    signal dayTapped(string date)

    readonly property bool empty: list.count === 0 || (list.count === 1 && calendar.itemCount === 0
                                                       && calendar.data(calendar.index(0, 0), CalendarModel.HasJournalRole) !== true)
    /// The keyboard was used last (the current day's focus ring shows only then).
    property bool keyboardActive: false
    /// A day of the list is being picked: the selection moves, the list does not scroll to it.
    property bool picking: false

    /// Pick day `date` (its header tapped, Enter): the view selects it.
    function pickDay(date: string): void {
        root.picking = true
        root.dayTapped(date)
        root.picking = false
    }
    /// The list's row of the selected day (-1 when it has none).
    function selectedRow(): int {
        return calendar.rowForDate(calendar.anchorDate)
    }

    /// `s` with its first letter upper case ("today" → "Today").
    function capitalized(s: string): string {
        return s.length > 0 ? Js.toUpperCase(Js.charAt(s, 0)) + Js.sliceFrom(s, 1) : ""
    }
    function scrollToAnchor(): void {
        let row = calendar.rowForDate(calendar.anchorDate)
        if (row < 0)
            row = calendar.rowForDate(DatesHelper.today)
        if (row >= 0)
            list.positionViewAtIndex(row, ListView.Beginning)
    }
    Connections {
        target: root.calendar
        function onRangeChanged() {
            if (!root.picking)
                scrollLater.restart()
        }
    }
    Component.onCompleted: scrollLater.restart()
    // Qt.callLater(scrollToAnchor): once, after this turn's changes.
    Later {
        id: scrollLater
        onTriggered: root.scrollToAnchor()
    }

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
        BRowScrollBar { list: list }
        Accessible.role: Accessible.List
        Accessible.name: root.calendar.title
        onActiveFocusChanged: if (!activeFocus) root.keyboardActive = false
        // Up / Down move the current day (the list's own keys; the first one from the selected
        // day), Enter picks it (before any arrow: the selected day). A tap leaves the current
        // index alone: setting it scrolls the whole day into view, moving the list under the
        // pointer.
        Keys.onPressed: (event) => {
            const key = event.key
            const plain = event.modifiers === Qt.NoModifier
            if ((key === Qt.Key_Return || key === Qt.Key_Enter) && plain) {
                const row = root.keyboardActive ? list.currentIndex : root.selectedRow()
                if (row >= 0) {
                    root.pickDay("" + root.calendar.data(root.calendar.index(row, 0), CalendarModel.DateRole))
                    event.accepted = true
                }
                return
            }
            if ((key === Qt.Key_Up || key === Qt.Key_Down) && !root.keyboardActive) {
                root.keyboardActive = true
                const selected = root.selectedRow()
                if (selected >= 0)
                    list.currentIndex = selected
            }
            event.accepted = false
        }

        delegate: Item {
            id: day

            required property int index
            required property string date
            required property int day
            required property bool isToday
            required property bool isPast
            required property bool isSelected
            required property bool hasJournal
            required property list<var> items
            required property string longLabel

            /// A day before today.
            readonly property bool pastDay: isPast && !isToday

            width: ListView.view.width
            implicitHeight: column.height + Theme.space2

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

                // Day header: number, weekday and month, "Today", the daily note. A button that
                // picks the day (the journal button keeps its own click).
                Item {
                    id: header
                    objectName: "agendaDay_" + day.date
                    y: Theme.space2
                    width: parent.width
                    height: Math.max(circle.height, labels.height, journalButton.visible ? journalButton.implicitHeight : 0)
                    Accessible.role: Accessible.Button
                    Accessible.name: root.capitalized(day.longLabel) + (day.isToday ? ", " + qsTr("today") : "")
                    Accessible.selected: day.isSelected
                    Accessible.onPressAction: root.pickDay(day.date)

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: {
                            list.forceActiveFocus(Qt.MouseFocusReason)
                            root.keyboardActive = false
                            root.pickDay(day.date)
                        }
                    }
                    HoverHandler {
                        enabled: !Theme.touchUi
                        cursorShape: Qt.PointingHandCursor
                    }
                    BFocusRing {
                        visible: day.ListView.isCurrentItem && list.activeFocus && root.keyboardActive
                        targetRadius: Theme.radius
                    }

                    // Today on a red disc, the selected day on a dark one (only the red one when
                    // today is selected), as in the month grid; a past day only outlined.
                    Rectangle {
                        id: circle
                        readonly property bool outlined: day.pastDay && !day.isSelected
                        width: column.markerWidth
                        height: width
                        y: (header.height - height) / 2
                        radius: width / 2
                        color: day.isToday ? Theme.accentSolid : day.isSelected ? Theme.primary
                             : outlined ? "transparent" : Theme.bgSunken
                        border.width: outlined ? 1 : 0
                        border.color: Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: "" + day.day
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textLg
                            font.weight: Theme.weightBold
                            font.features: { "tnum": 1 }
                            color: day.isToday ? Theme.fgOnAccent : day.isSelected ? Theme.primaryFg
                                 : circle.outlined ? Theme.fgMuted : Theme.fg
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
                            text: root.capitalized(day.longLabel)
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
                                return root.capitalized(day.isToday ? qsTr("Today") : DatesHelper.formatHuman(day.date))
                            }
                            // Only the relative words (today, tomorrow, yesterday).
                            visible: day.isToday || Js.indexOf(DatesHelper.formatHuman(day.date), " ") < 0
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
                            past: day.pastDay
                            onOpened: root.itemOpened(modelData)
                            onTaskToggled: root.itemToggled(modelData)
                            onStatusRequested: (status) => root.itemStatusRequested(modelData, status)
                            onTokenMenuRequested: (item, x, y) => root.itemMenuRequested(modelData, item, x, y)
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
        description: qsTr("Dates (📅, ⏳) and reminders (⏰) from your notes appear here. Choose a day to add a task or a reminder to it.")
    }
}
