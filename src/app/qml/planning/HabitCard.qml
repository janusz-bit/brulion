pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// HabitCard — one habit of HabitsView (a HabitListModel row): today's check (a click ticks it,
// again opens it; the menu also skips the day), its name and schedule, the streaks, the completion
// rate and the period's progress, and the month grid of the model's month: a click on a past day
// ticks it or opens it again, a right-click or a long press offers done / skipped / no check for
// that day. A definition that is not understood (or a second one of a name) shows the problem.
//
// Laid out with bindings (docs/UI.md "Scrolling performance"): the card has its final height when
// it is created.
//
//   HabitCard { habit: model; weekdayNames: habitModel.weekdayNames; monthOffset: habitModel.monthOffset
//               onCheckRequested: (date, state) => …; onOpenRequested: … }
Item {
    id: card

    /// The row's roles (name, key, scheduleLabel, valid, problem, todayMark, dueToday, streaks,
    /// rate, period, days, …).
    required property var habit
    property var weekdayNames: []
    property int monthOffset: 0
    /// Today ("YYYY-MM-DD").
    property string today: ""

    /// Set the habit's day `date` to `state` ("done" | "skipped" | "open").
    signal checkRequested(string date, string state)
    /// Open the note that defines the habit.
    signal openRequested()
    /// Open the daily note of `date`.
    signal dayOpenRequested(string date)

    readonly property bool valid: habit.valid === true
    readonly property var days: habit.days || []
    readonly property int cell: Theme.touchUi ? 34 : 26
    readonly property int gap: 4
    readonly property int gridRows: days.length > 0 ? Math.ceil((monthOffset + days.length) / 7) : 0
    readonly property int headerHeight: Math.max(toggle.height, titleBlock.height)
    readonly property int statsHeight: valid ? Theme.textSm + Theme.space3 : 0
    readonly property int gridHeight: gridRows > 0 ? Theme.textXs + Theme.space2 + gridRows * (cell + gap) : 0
    readonly property int pad: Theme.narrow ? Theme.space3 : Theme.space4

    implicitHeight: pad + headerHeight + (statsHeight > 0 ? Theme.space2 + statsHeight : 0)
                    + (gridHeight > 0 ? Theme.space3 + gridHeight : 0) + pad
    height: implicitHeight

    function stateLabel(state) {
        switch (state) {
        case "done": return qsTr("done")
        case "skipped": return qsTr("skipped")
        case "missed": return qsTr("missed")
        case "due": return qsTr("due today")
        case "future": return qsTr("future")
        case "outside": return qsTr("before the start")
        }
        return qsTr("nothing due")
    }
    function nextState(mark) {
        return mark === "done" || mark === "skipped" ? "open" : "done"
    }
    function openDayMenu(date, mark, item, x, y) {
        dayMenuLoader.date = date
        dayMenuLoader.mark = mark
        dayMenuLoader.active = true
        const p = item.mapToItem(card, x, y)
        dayMenuLoader.item.popup(card, p.x, p.y)
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusLg
        color: Theme.bgElevated
        border.width: 1
        border.color: Theme.borderSubtle
    }

    // ---- Today's check ----------------------------------------------------------------------
    Rectangle {
        id: toggle
        objectName: "habitToggle_" + card.habit.key
        readonly property string mark: card.habit.todayMark || "open"
        readonly property int size: Theme.touchUi ? 40 : 32
        x: card.pad
        y: card.pad + (card.headerHeight - height) / 2
        width: size
        height: size
        radius: size / 2
        color: mark === "done" ? Theme.successBg : mark === "skipped" ? Theme.chipDoneBg : "transparent"
        border.width: mark === "open" ? 2 : 0
        border.color: card.habit.dueToday ? Theme.accent : Theme.borderControl
        Accessible.role: Accessible.CheckBox
        Accessible.checkable: true
        Accessible.checked: mark === "done"
        Accessible.name: qsTr("%1 today: %2").arg(card.habit.name)
                         .arg(mark === "done" ? qsTr("done") : mark === "skipped" ? qsTr("skipped") : qsTr("not done"))
        activeFocusOnTab: true
        Keys.onSpacePressed: card.checkRequested(card.today, card.nextState(mark))

        BIcon {
            anchors.centerIn: parent
            name: toggle.mark === "done" ? "check" : toggle.mark === "skipped" ? "minus" : ""
            size: toggle.size - 14
            color: toggle.mark === "done" ? Theme.successText : Theme.fgMuted
        }
        BFocusRing {
            visible: toggle.activeFocus
            targetRadius: toggle.radius
        }
        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: card.checkRequested(card.today, card.nextState(toggle.mark))
            onLongPressed: card.openDayMenu(card.today, toggle.mark, toggle, toggle.width / 2, toggle.height)
        }
        TapHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            acceptedButtons: Qt.RightButton
            onTapped: (eventPoint) => card.openDayMenu(card.today, toggle.mark, toggle, eventPoint.position.x,
                                                       eventPoint.position.y)
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }

    // ---- Name, schedule, problem ------------------------------------------------------------
    Item {
        id: titleBlock
        x: toggle.x + toggle.width + Theme.space3
        y: card.pad
        width: menuButton.x - x - Theme.space2
        height: nameText.height + 2 + subText.height

        BText {
            id: nameText
            width: parent.width
            text: card.habit.name
            variant: "title"
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        BText {
            id: subText
            y: nameText.height + 2
            width: parent.width
            variant: card.valid ? "muted" : "small"
            color: card.valid ? Theme.fgMuted : Theme.warningText
            elide: Text.ElideRight
            text: card.habit.problem === "duplicate" ? qsTr("Defined twice — this definition is not used")
                : card.habit.problem === "schedule" ? qsTr("Schedule not understood: “%1”").arg(card.habit.schedule)
                : card.habit.periodLabel !== "" ? card.habit.scheduleLabel + " · " + card.habit.periodLabel
                : card.habit.scheduleLabel
        }
    }

    BIconButton {
        id: menuButton
        objectName: "habitMenu_" + card.habit.key
        x: card.width - width - card.pad + Theme.space1
        y: card.pad + (card.headerHeight - height) / 2
        label: qsTr("More")
        iconName: "ellipsis"
        onClicked: {
            menuLoader.active = true
            menuLoader.item.popup(menuButton, 0, menuButton.height)
        }
    }

    // ---- Streaks and rate ------------------------------------------------------------------
    Item {
        id: stats
        visible: card.valid
        x: card.pad
        y: card.pad + card.headerHeight + Theme.space2
        width: card.width - 2 * card.pad
        height: card.statsHeight

        BIcon {
            id: flame
            name: card.valid ? "flame" : ""
            size: Theme.iconSizeSm
            color: card.habit.currentStreak > 0 ? Theme.warningText : Theme.fgMuted
            anchors.verticalCenter: parent.verticalCenter
        }
        BText {
            anchors.left: flame.right
            anchors.leftMargin: Theme.space1
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            variant: "small"
            color: Theme.fgMuted
            elide: Text.ElideRight
            text: {
                const parts = [qsTr("Streak: %1").arg(card.habit.streakLabel),
                               qsTr("longest: %1").arg(card.habit.longestLabel)]
                if (card.habit.rateLabel !== "")
                    parts.push(qsTr("completed: %1").arg(card.habit.rateLabel))
                return parts.join(" · ")
            }
        }
    }

    // ---- The month grid --------------------------------------------------------------------
    Item {
        id: grid
        visible: card.gridRows > 0
        x: card.pad
        y: card.pad + card.headerHeight + (card.statsHeight > 0 ? Theme.space2 + card.statsHeight : 0) + Theme.space3
        width: 7 * card.cell + 6 * card.gap
        height: card.gridHeight

        Repeater {
            model: card.gridRows > 0 ? card.weekdayNames : []
            delegate: Text {
                required property int index
                required property string modelData
                x: index * (card.cell + card.gap)
                width: card.cell
                horizontalAlignment: Text.AlignHCenter
                text: modelData
                font.family: Theme.fontUi
                font.pixelSize: Theme.textXs
                color: Theme.fgMuted
            }
        }
        Repeater {
            model: card.days
            delegate: Rectangle {
                id: dayCell
                required property int index
                required property var modelData
                readonly property string state_: modelData.state
                readonly property int slot: card.monthOffset + index
                objectName: "habitDay_" + card.habit.key + "_" + modelData.date
                x: (slot % 7) * (card.cell + card.gap)
                y: Theme.textXs + Theme.space2 + Math.floor(slot / 7) * (card.cell + card.gap)
                width: card.cell
                height: card.cell
                radius: Theme.radiusSm
                color: state_ === "done" ? Theme.successBg : state_ === "missed" ? Theme.dangerBg
                     : state_ === "skipped" ? Theme.chipDoneBg : "transparent"
                border.width: modelData.today ? 2 : state_ === "due" ? 1 : 0
                border.color: Theme.accent
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("%1: %2").arg(modelData.date).arg(card.stateLabel(state_))

                Text {
                    anchors.centerIn: parent
                    text: dayCell.modelData.day
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textXs
                    font.strikeout: dayCell.state_ === "skipped"
                    color: dayCell.state_ === "done" ? Theme.successText : dayCell.state_ === "missed" ? Theme.dangerText
                         : dayCell.state_ === "due" || dayCell.modelData.today ? Theme.fg : Theme.fgMuted
                }
                TapHandler {
                    enabled: dayCell.state_ !== "future"
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: card.checkRequested(dayCell.modelData.date,
                                                  dayCell.state_ === "done" || dayCell.state_ === "skipped" ? "open" : "done")
                    onLongPressed: card.openDayMenu(dayCell.modelData.date, dayCell.state_, dayCell, dayCell.width / 2,
                                                    dayCell.height)
                }
                TapHandler {
                    enabled: dayCell.state_ !== "future"
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                    acceptedButtons: Qt.RightButton
                    onTapped: (eventPoint) => card.openDayMenu(dayCell.modelData.date, dayCell.state_, dayCell,
                                                               eventPoint.position.x, eventPoint.position.y)
                }
            }
        }
    }

    // ---- Menus (created when they open) ------------------------------------------------------
    Loader {
        id: menuLoader
        active: false
        sourceComponent: BMenu {
            BMenuItem {
                objectName: "habitMenuDone"
                text: qsTr("Done today")
                iconName: "check"
                enabled: card.valid || card.habit.problem === "schedule"
                onTriggered: card.checkRequested(card.today, "done")
            }
            BMenuItem {
                objectName: "habitMenuSkip"
                text: qsTr("Skip today")
                iconName: "minus"
                enabled: card.valid || card.habit.problem === "schedule"
                onTriggered: card.checkRequested(card.today, "skipped")
            }
            BMenuItem {
                text: qsTr("No check today")
                iconName: "circle"
                enabled: card.habit.todayRecorded === true
                onTriggered: card.checkRequested(card.today, "open")
            }
            BMenuSeparator {}
            BMenuItem {
                objectName: "habitMenuOpen"
                text: qsTr("Open its definition")
                iconName: "file-text"
                onTriggered: card.openRequested()
            }
            BMenuItem {
                text: qsTr("Open today's journal")
                iconName: "book-open"
                onTriggered: card.dayOpenRequested(card.today)
            }
        }
    }
    Loader {
        id: dayMenuLoader
        property string date
        property string mark
        active: false
        sourceComponent: BMenu {
            BMenuItem {
                text: qsTr("Done")
                iconName: "check"
                onTriggered: card.checkRequested(dayMenuLoader.date, "done")
            }
            BMenuItem {
                text: qsTr("Skipped (no streak lost)")
                iconName: "minus"
                onTriggered: card.checkRequested(dayMenuLoader.date, "skipped")
            }
            BMenuItem {
                text: qsTr("No check")
                iconName: "circle"
                enabled: dayMenuLoader.mark === "done" || dayMenuLoader.mark === "skipped"
                onTriggered: card.checkRequested(dayMenuLoader.date, "open")
            }
            BMenuSeparator {}
            BMenuItem {
                text: qsTr("Open the journal of this day")
                iconName: "book-open"
                onTriggered: card.dayOpenRequested(dayMenuLoader.date)
            }
        }
    }
}
