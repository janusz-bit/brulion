pragma ComponentBehavior: Bound
import QtQuick
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
// Click / Enter opens the note at the task's line; Space toggles; the context menu — "Otwórz" and
// the statuses (TaskStatusMenu; "Bez statusu" makes it a plain list item) — opens with a right
// click or long press on the row or its checkbox, or the Menu key.
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
    onPressAndHold: control.openMenu()
    Keys.onReturnPressed: control.opened()
    Keys.onEnterPressed: control.opened()
    Keys.onSpacePressed: control.taskToggled()
    Keys.onMenuPressed: {
        control.openMenu(control, Theme.space8, control.height)
        control.statusMenu.highlightStatus() // the keyboard starts at the task's status
    }

    TapHandler {
        // Right-click only: Qt gives a finger (no buttons) to every TapHandler, so on a phone a
        // tap opened the (modal) menu, which swallowed the release: the task never opened.
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        acceptedButtons: Qt.RightButton
        onTapped: (eventPoint) => control.openMenu(eventPoint.position.x, eventPoint.position.y)
    }

    // Laid out with bindings, not Layouts: the row has its final height (a title on two lines,
    // chips on a second line) as soon as it is created — the list is lazy, and a row that grew
    // after the list had placed it (the Layouts' and the Flow's sizes arrive with their next
    // polish) pushed the rows below it while Tasks scrolled (docs/UI.md "Scrolling performance").
    contentItem: Item {
        id: content
        readonly property int spacing: Theme.touchUi ? 4 : 6
        readonly property int textTop: Theme.touchUi ? 10 : 2
        implicitWidth: box.implicitWidth + spacing + label.implicitWidth
        implicitHeight: Math.max(box.implicitHeight, textTop + textColumn.height)

        TaskBox {
            id: box
            objectName: "tasksRowTaskBox"
            status: control.task.status || "todo"
            Accessible.name: qsTr("Done: %1").arg(control.task.text || "")
            onToggled: control.taskToggled()
            ownMenu: false // the row's menu, "Otwórz" first
            onMenuRequested: control.openMenu(box, 0, box.height)
        }

        // The text and its chips, vertically centred below their top margin (as a Layout would).
        Item {
            id: textColumn
            x: box.implicitWidth + content.spacing
            y: content.textTop + Math.max(0, (content.height - content.textTop - height) / 2)
            width: Math.max(0, content.width - x)
            height: label.height + Theme.space1 + chips.height

            Text {
                id: label
                width: parent.width
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
                id: chips
                y: label.height + Theme.space1
                width: parent.width
                spacing: Theme.space1
                // The Flow's own height comes with its next polish: the same lines here (a child
                // goes to the next line when it does not fit; hidden chips take no room).
                height: {
                    let x = 0
                    let line = 0
                    let total = 0
                    for (let i = 0; i < chips.children.length; ++i) {
                        const c = chips.children[i]
                        if (!c.visible)
                            continue
                        if (x > 0 && x + c.width > chips.width) {
                            total += line + chips.spacing
                            x = 0
                            line = 0
                        }
                        x += c.width + chips.spacing
                        line = Math.max(line, c.height)
                    }
                    return total + line
                }

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

    /// The status menu, created when it is first opened: built with every row, it made each
    /// row several times as costly to create while scrolling (docs/UI.md "Scrolling performance").
    property TaskStatusMenu statusMenu: null
    /// Opens the menu like BMenu.popup() with the same arguments: (), (x, y) or (item, x, y).
    function openMenu(a, b, c) {
        if (!statusMenu)
            statusMenu = menuComponent.createObject(control) as TaskStatusMenu
        statusMenu.status = control.task.status || "todo"
        if (a === undefined)
            statusMenu.popup()
        else if (c === undefined)
            statusMenu.popup(a, b)
        else
            statusMenu.popup(a, b, c)
    }

    Component {
        id: menuComponent
        TaskStatusMenu {
            showOpen: true
            onOpenRequested: control.opened()
            onStatusChosen: (status) => {
                if (status !== (control.task.status || "todo"))
                    control.statusRequested(status)
            }
        }
    }
}
