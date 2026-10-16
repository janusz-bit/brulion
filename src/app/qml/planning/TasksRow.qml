pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// TasksRow — one task of TasksView: the checkbox, the task text (escaped rich text from the core
// InlineRenderer: [[links]] and #tags clickable), and its metadata as chips — due date (red when
// overdue), scheduled, start, the next reminder / alarm, recurrence in words, priority — plus
// where it is written (page, or the day of a daily note).
//
//   TasksRow { task: model; onTaskToggled: …; onOpened: …; onLinkActivated: (url) => …
//              onStatusRequested: (status) => … }
//
// Click / Enter opens the note at the task's line; Space toggles; the context menu (right
// click, long press, Menu key) sets any status.
T.ItemDelegate {
    id: control

    /// The row's roles (TaskListModel), e.g. the delegate's `model`.
    required property var task

    signal taskToggled()
    signal opened()
    signal linkActivated(string url)
    signal statusRequested(string status)

    readonly property bool closed: task.status === "done" || task.status === "cancelled"
    readonly property string journalDay: task.isJournal ? Workspace.journalDate(task.path || "") : ""
    readonly property string where: journalDay !== "" ? qsTr("Journal · %1").arg(DatesHelper.formatShort(journalDay))
                                                      : (task.folder ? task.pageTitle + " · " + task.folder : task.pageTitle)
    readonly property string priorityLabel: {
        switch (task.priorityName) {
        case "highest": return qsTr("Highest")
        case "high": return qsTr("High")
        case "medium": return qsTr("Medium")
        case "low": return qsTr("Low")
        case "lowest": return qsTr("Lowest")
        }
        return ""
    }
    readonly property string statusLabel: {
        switch (task.status) {
        case "doing": return qsTr("In progress")
        case "deferred": return qsTr("Waiting")
        case "cancelled": return qsTr("Cancelled")
        }
        return ""
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    leftPadding: Theme.touchUi ? 2 : 4
    rightPadding: Theme.touchUi ? 10 : 8
    topPadding: Theme.touchUi ? 4 : 5
    bottomPadding: Theme.touchUi ? 6 : 6
    focusPolicy: Qt.StrongFocus
    hoverEnabled: !Theme.touchUi

    Accessible.role: Accessible.ListItem
    Accessible.name: [task.text, statusLabel, task.dueLabel ? qsTr("due %1").arg(task.dueLabel) : "",
                      task.overdue ? qsTr("overdue") : "", priorityLabel, where].filter(s => !!s).join(", ")

    onClicked: control.opened()
    onPressAndHold: menu.popup()
    Keys.onReturnPressed: control.opened()
    Keys.onEnterPressed: control.opened()
    Keys.onSpacePressed: control.taskToggled()
    Keys.onMenuPressed: menu.popup(control, Theme.space8, control.height)

    TapHandler {
        // Right-click only: Qt gives a finger (no buttons) to every TapHandler, so on a phone a
        // tap opened the (modal) menu, which swallowed the release: the task never opened.
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        acceptedButtons: Qt.RightButton
        onTapped: (eventPoint) => menu.popup(eventPoint.position.x, eventPoint.position.y)
    }

    contentItem: RowLayout {
        spacing: Theme.touchUi ? 4 : 6

        TaskBox {
            Layout.alignment: Qt.AlignTop
            status: control.task.status || "todo"
            Accessible.name: qsTr("Done: %1").arg(control.task.text || "")
            onToggled: control.taskToggled()
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.touchUi ? 10 : 2
            spacing: Theme.space1

            Text {
                id: label
                Layout.fillWidth: true
                // Escaped by the core InlineRenderer: notes never inject markup.
                text: control.task.html || ""
                textFormat: Text.RichText
                font.family: Theme.fontUi
                font.pixelSize: Theme.textBase
                font.strikeout: control.task.status === "cancelled"
                color: control.closed ? Theme.fgMuted : Theme.fg
                linkColor: Theme.link
                wrapMode: Text.Wrap
                maximumLineCount: 4
                elide: Text.ElideRight
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.lineHeightPx(font.pixelSize, 1.4)
                onLinkActivated: (link) => control.linkActivated(link)
                HoverHandler {
                    cursorShape: label.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: Theme.space1
                visible: children.length > 0

                BChip {
                    visible: control.statusLabel !== ""
                    size: "small"
                    variant: control.task.status === "doing" ? "reminder" : "neutral"
                    iconName: control.task.status === "doing" ? "clock" : control.task.status === "deferred" ? "hourglass" : "x"
                    text: control.statusLabel
                }
                BChip {
                    visible: (control.task.dueLabel || "") !== ""
                    size: "small"
                    variant: control.closed ? "done" : control.task.overdue ? "overdue" : "due"
                    text: control.task.overdue ? qsTr("%1, overdue").arg(control.task.dueLabel) : control.task.dueLabel
                }
                BChip {
                    visible: (control.task.scheduledLabel || "") !== ""
                    size: "small"
                    variant: control.closed ? "done" : "scheduled"
                    text: control.task.scheduledLabel
                }
                BChip {
                    visible: (control.task.startLabel || "") !== ""
                    size: "small"
                    variant: "neutral"
                    iconName: "calendar-clock"
                    text: qsTr("from %1").arg(control.task.startLabel)
                }
                BChip {
                    visible: (control.task.reminderLabel || "") !== "" && !control.closed
                    size: "small"
                    variant: control.task.reminderAlarm ? "alarm" : "reminder"
                    text: control.task.reminderLabel
                }
                BChip {
                    visible: (control.task.recurrenceLabel || "") !== ""
                    size: "small"
                    variant: "recurrence"
                    text: control.task.recurrenceLabel
                }
                BChip {
                    visible: control.priorityLabel !== ""
                    size: "small"
                    variant: control.task.priority >= 4 && !control.closed ? "overdue" : "neutral"
                    iconName: control.task.priority >= 3 ? "arrow-up" : "arrow-down"
                    text: control.priorityLabel
                }
                Text {
                    height: Theme.touchUi ? 24 : 20
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 2
                    text: control.where
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textXs
                    color: Theme.fgFaint
                    elide: Text.ElideMiddle
                    width: Math.min(implicitWidth, control.availableWidth - 40)
                }
            }
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

    BMenu {
        id: menu
        BMenuItem {
            text: qsTr("Open")
            iconName: "file-text"
            onTriggered: control.opened()
        }
        BMenuSeparator {}
        BMenuItem {
            text: qsTr("To do")
            checkable: true
            checked: control.task.status === "todo"
            onTriggered: control.statusRequested("todo")
        }
        BMenuItem {
            text: qsTr("In progress")
            checkable: true
            checked: control.task.status === "doing"
            onTriggered: control.statusRequested("doing")
        }
        BMenuItem {
            text: qsTr("Done")
            checkable: true
            checked: control.task.status === "done"
            onTriggered: control.statusRequested("done")
        }
        BMenuItem {
            text: qsTr("Waiting")
            checkable: true
            checked: control.task.status === "deferred"
            onTriggered: control.statusRequested("deferred")
        }
        BMenuItem {
            text: qsTr("Cancelled")
            checkable: true
            checked: control.task.status === "cancelled"
            onTriggered: control.statusRequested("cancelled")
        }
    }
}
