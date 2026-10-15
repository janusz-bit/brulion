pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// PlanningItemRow — one calendar entry (a CalendarModel entry map) in the day panel, the agenda
// and the overdue list: the task checkbox (tasks) or the entry's icon, the time ("" = all day),
// the title (plain text, two lines at most), where it is written (page title, or the day of a
// daily note) and the kind of date as a small chip.
//
//   PlanningItemRow { entry: modelData; onTaskToggled: actions.toggleTask(entry.path, entry.line, entry.title)
//                     onOpened: AppController.openPage(entry.path, entry.line) }
//
// Click / Enter opens the note at the entry; the checkbox (Space when focused) toggles the task.
T.ItemDelegate {
    id: control

    required property var entry
    /// Show the entry's date in the subtitle (lists spanning several days: overdue tasks).
    property bool showDate: false

    signal taskToggled()
    signal opened()

    readonly property bool isTask: entry.isTask === true
    readonly property bool closed: entry.closed === true
    readonly property string variant: entry.variant || "neutral"
    readonly property var chipColors: Theme.chip(variant)
    readonly property string journalDay: entry.isJournalPage ? Workspace.journalDate(entry.path || "") : ""
    readonly property string kindLabel: {
        switch (variant) {
        case "overdue": return qsTr("Overdue")
        case "alarm": return qsTr("Alarm")
        case "reminder": return qsTr("Reminder")
        case "scheduled": return qsTr("Scheduled")
        case "done": return entry.kind === "done" ? qsTr("Done") : qsTr("Finished")
        }
        switch (entry.kind) {
        case "due": return qsTr("Due")
        case "start": return qsTr("Starts")
        case "journal": return qsTr("Journal")
        }
        return ""
    }
    readonly property string subtitle: {
        const where = journalDay !== "" ? qsTr("Journal · %1").arg(DatesHelper.formatShort(journalDay))
                                        : (entry.pageTitle || "")
        if (showDate && entry.dueLabel)
            return entry.dueLabel + (where !== "" ? " · " + where : "")
        return where
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(Theme.listItemHeight, implicitContentHeight + topPadding + bottomPadding)
    leftPadding: Theme.touchUi ? 4 : 6
    rightPadding: Theme.touchUi ? 12 : 8
    topPadding: Theme.touchUi ? 6 : 5
    bottomPadding: topPadding
    focusPolicy: Qt.StrongFocus
    hoverEnabled: !Theme.touchUi

    Accessible.role: Accessible.ListItem
    Accessible.name: [entry.time || "", entry.title || "", kindLabel, subtitle].filter(s => s !== "").join(", ")

    onClicked: control.opened()
    Keys.onReturnPressed: control.opened()
    Keys.onEnterPressed: control.opened()
    Keys.onSpacePressed: (event) => {
        if (control.isTask) {
            control.taskToggled()
            event.accepted = true
        }
    }

    contentItem: RowLayout {
        spacing: Theme.touchUi ? 6 : 8

        // Checkbox for tasks, the entry's icon otherwise (both in the same column width).
        Item {
            Layout.preferredWidth: Theme.touchUi ? Theme.touchTarget : 24
            Layout.preferredHeight: Theme.touchUi ? Theme.touchTarget : 24
            Layout.alignment: Qt.AlignTop
            TaskBox {
                anchors.centerIn: parent
                visible: control.isTask
                status: control.entry.status || "todo"
                Accessible.name: qsTr("Done: %1").arg(control.entry.title || "")
                onToggled: control.taskToggled()
            }
            BIcon {
                anchors.centerIn: parent
                visible: !control.isTask
                name: control.entry.icon || "calendar"
                size: Theme.iconSizeSm
                color: control.variant === "alarm" || control.variant === "reminder" ? Theme.markColor(control.variant)
                                                                                     : Theme.fgMuted
            }
        }

        Text {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: Theme.touchUi ? 12 : 4
            Layout.preferredWidth: timeMetrics.width
            visible: (control.entry.time || "") !== ""
            text: control.entry.time || ""
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            font.weight: Theme.weightSemiBold
            font.features: { "tnum": 1 }
            color: control.closed ? Theme.fgFaint : Theme.fg
            TextMetrics {
                id: timeMetrics
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                font.weight: Theme.weightSemiBold
                text: "00:00"
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 1

            Text {
                Layout.fillWidth: true
                text: control.entry.title || ""
                font.family: Theme.fontUi
                font.pixelSize: Theme.textBase
                font.strikeout: control.closed
                color: control.closed ? Theme.fgMuted : Theme.fg
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.lineHeightPx(font.pixelSize, 1.35)
            }
            Text {
                Layout.fillWidth: true
                visible: text !== ""
                text: control.subtitle
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: Theme.fgMuted
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }

        BChip {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: Theme.touchUi ? 10 : 2
            visible: control.kindLabel !== "" && control.width > 300
            size: "small"
            variant: control.variant
            showIcon: control.entry.recurring === true
            iconName: "repeat"
            text: control.kindLabel
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.down ? Theme.bgPressed : control.hovered || control.visualFocus ? Theme.bgHover : "transparent"
        BFocusRing {
            visible: control.visualFocus
            targetRadius: Theme.radius
            inset: true
        }
    }
}
