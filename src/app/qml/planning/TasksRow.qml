pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// TasksRow — one task of TasksView: the checkbox, the task text (escaped rich text from the core
// InlineRenderer: [[links]] and #tags clickable), and its metadata as chips — due date (red when
// overdue), scheduled, start, the next reminder / alarm, recurrence in words, priority — plus
// where it is written (page, or the day of a daily note).
//
//   delegate: TasksRow { onTaskToggled: …; onOpened: …; onLinkActivated: (url) => …
//                        onTagPageRequested: (tag) => …; onStatusRequested: (status) => … }
//
// A TaskListModel task row's delegate: its roles are required properties (`text` is the task's
// plain text).
//
// Click / Enter opens the note at the task's line; Space toggles; the context menu — "Otwórz" and
// the statuses (TaskStatusMenu; "Bez statusu" makes it a plain list item) — opens with a right
// click or long press on the row or its checkbox, or the Menu key. A right-click or long press on a
// date, reminder, 🔁 or priority chip asks for that token's menu instead (tokenMenuRequested: the
// view's PlanningTokenMenu), on a #tag or a link of the text for the link's menu
// (linkMenuRequested: "Filtruj po tagu" first, "Otwórz stronę tagu", …). A click on a #tag is a
// link (TasksView filters by it); Ctrl+click (mouse) on it asks for the tag's page instead
// (tagPageRequested, docs/SYNTAX.md §3). The text's #tags and chips are rounded shapes and it ends
// with "…" after 4 lines (BRichText); the mouse pointer resting on a [[page]] or ((block)) link
// shows its preview (LinkPreviews, docs/UI.md "Link previews"). Task dependencies and time
// (docs/SYNTAX.md §5): "Czeka na: …" when it is blocked (a click opens the task it waits for: a
// taskdep: link), a missing id or a cycle, how many tasks wait for it, its subtasks "3/5" and its
// clocked time; the menu starts or stops its timer and adds a dependency.
T.ItemDelegate {
    id: control

    // The row's roles (TaskListModel).
    required text
    required property string status
    required property string html
    required property string path
    required property int line
    required property string pageTitle
    required property string folder
    required property bool isJournal
    required property string due
    required property string dueLabel
    required property bool overdue
    required property string scheduled
    required property string scheduledLabel
    required property string start
    required property string startLabel
    required property string recurrence
    required property string recurrenceLabel
    required property int priority
    required property string priorityName
    required property string reminderLabel
    required property bool reminderAlarm
    required property string reminderSpec
    required property bool blocked
    required property string blockedBy
    required property string blockedById
    required property int blockingCount
    required property string dependencyProblem
    required property string progress
    required property bool progressComplete
    required property string timeLabel
    required property bool clockRunning
    /// The status, "todo" for none.
    readonly property string statusOrTodo: status !== "" ? status : "todo"

    signal taskToggled()
    signal opened()
    signal linkActivated(string url)
    /// Ctrl+click on the #tag `tag`: open the page it names.
    signal tagPageRequested(string tag)
    signal statusRequested(string status)
    /// The menu's "Uruchom / Zatrzymaj licznik czasu" and "Dodaj zależność…" (docs/SYNTAX.md §5).
    signal clockRequested()
    signal dependencyRequested()
    /// A right-click or long press on a chip: the menu of its token `kind` ("due" | "scheduled" |
    /// "start" | "reminder" | "recurrence" | "priority") at (x, y) of `item`.
    signal tokenMenuRequested(string kind, Item item, real x, real y)
    /// A right-click or long press on a link of the text (`url`: tag:, page:, …).
    signal linkMenuRequested(string url, Item item, real x, real y)

    /// A long press opened a tag's page: the release that follows is no click on the link.
    property bool _tagOpened: false
    /// The link under the mouse or the pressing finger, underlined (as in the editor). Followed
    /// with linkAt: the Text's own hoveredLink would reset with each change of its text.
    property string _hoveredLink: ""
    function _trackLink(x: real, y: real): void {
        const link = label.linkAt(x, y)
        const h = Js.startsWith(link, "task:") ? "" : link
        if (h !== control._hoveredLink)
            control._hoveredLink = h
    }
    /// The tag of the link at (x, y) of the task text, "" when it is no #tag.
    function tagAt(x: real, y: real): string {
        const link = label.linkAt(x, y)
        return Js.startsWith(link, "tag:") ? Js.decodeURIComponent(Js.sliceFrom(link, 4)) : ""
    }
    function openTagPage(tag: string): void {
        control._tagOpened = true
        control.tagPageRequested(tag)
    }
    /// The menu of what is at (x, y) of the row: a chip's token, a link of the text; false when
    /// there is neither (the row's own menu then).
    function openMenuAt(x: real, y: real): bool {
        control._chipsPoint = chips.mapFromItem(control, x, y)
        control._chip = chips.childAt(control._chipsPoint.x, control._chipsPoint.y)
        if (control._chip !== null && Js.startsWith(control._chip.objectName, "taskChip_")) {
            control.tokenMenuRequested(Js.sliceFrom(control._chip.objectName, 9), control._chip,
                                       control._chipsPoint.x - control._chip.x, control._chipsPoint.y - control._chip.y)
            return true
        }
        control._labelPoint = label.mapFromItem(control, x, y)
        const link = label.linkAt(control._labelPoint.x, control._labelPoint.y)
        if (link !== "") {
            control._tagOpened = true // (the release after a long press is no click on the link)
            control.linkMenuRequested(link, label, control._labelPoint.x, control._labelPoint.y)
            return true
        }
        return false
    }
    /// A right click at (x, y): the menu of what is there, else the row's.
    function openAnyMenuAt(x: real, y: real): void {
        if (!control.openMenuAt(x, y))
            control.openMenuAtPoint(x, y)
    }
    /// A click at (x, y) of the text: follows the link there (`ctrl` on a #tag: its page).
    function clickLinkAt(x: real, y: real, ctrl: bool): void {
        const tag = control.tagAt(x, y)
        if (tag !== "" && ctrl) {
            control.openTagPage(tag)
            return
        }
        const link = label.linkAt(x, y)
        if (link !== "")
            control.linkActivated(link)
    }
    /// A finger's tap at (x, y) of the text: follows the link there — unless the press opened a
    /// link's menu (a long press).
    function tapLinkAt(x: real, y: real): void {
        if (control._tagOpened) {
            control._tagOpened = false // the gesture that opened the link's menu
            return
        }
        const link = label.linkAt(x, y)
        if (link !== "")
            control.linkActivated(link)
    }
    // What openMenuAt() found (mapFromItem's and childAt()'s results read through typed properties).
    property point _chipsPoint
    property point _labelPoint
    property Item _chip: null
    /// The non-empty parts joined with ", ".
    function joinNonEmpty(parts: list<string>): string {
        let out = ""
        for (let i = 0; i < parts.length; ++i) {
            if (parts[i] !== "")
                out = out === "" ? parts[i] : out + ", " + parts[i]
        }
        return out
    }

    readonly property bool closed: status === "done" || status === "cancelled"
    readonly property string journalDay: isJournal ? Workspace.journalDate(path) : ""
    readonly property string where: journalDay !== "" ? qsTr("Journal · %1").arg(DatesHelper.formatShort(journalDay))
                                                      : (folder !== "" ? pageTitle + " · " + folder : pageTitle)
    readonly property string priorityLabel: {
        switch (priorityName) {
        case "highest": return qsTr("Highest")
        case "high": return qsTr("High")
        case "medium": return qsTr("Medium")
        case "low": return qsTr("Low")
        case "lowest": return qsTr("Lowest")
        }
        return ""
    }
    readonly property string statusLabel: {
        switch (status) {
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
    Accessible.name: joinNonEmpty([text, statusLabel, dueLabel !== "" ? qsTr("due %1").arg(dueLabel) : "",
                                   overdue ? qsTr("overdue") : "", priorityLabel, where])

    onClicked: control.opened()
    onPressAndHold: {
        if (!control.openMenuAt(control.pressX, control.pressY))
            control.openMenu()
    }
    Keys.onReturnPressed: control.opened()
    Keys.onEnterPressed: control.opened()
    Keys.onSpacePressed: control.taskToggled()
    Keys.onMenuPressed: {
        control.openMenuFrom(control, Theme.space8, control.height)
        control.statusMenu.highlightStatus() // the keyboard starts at the task's status
    }

    TapHandler {
        // Right-click only: Qt gives a finger (no buttons) to every TapHandler, so on a phone a
        // tap opened the (modal) menu, which swallowed the release: the task never opened.
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        acceptedButtons: Qt.RightButton
        onTapped: (eventPoint) => control.openAnyMenuAt(eventPoint.position.x, eventPoint.position.y)
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
            status: control.statusOrTodo
            Accessible.name: qsTr("Done: %1").arg(control.text)
            onToggled: control.taskToggled()
            ownMenu: false // the row's menu, "Otwórz" first
            onMenuRequested: control.openMenuFrom(box, 0, box.height)
        }

        // The text and its chips, vertically centred below their top margin (as a Layout would).
        Item {
            id: textColumn
            x: box.implicitWidth + content.spacing
            y: content.textTop + Math.max(0, (content.height - content.textTop - height) / 2)
            width: Math.max(0, content.width - x)
            height: label.height + Theme.space1 + chips.height

            BRichText {
                id: label
                width: parent.width
                // Escaped by the core InlineRenderer: notes never inject markup. Tags and chips are
                // rounded shapes; the text ends with "…" after 4 lines.
                html: control.html
                hoveredLink: control._hoveredLink
                font.family: Theme.fontUi
                font.pixelSize: Theme.textBase
                font.strikeout: control.status === "cancelled"
                color: control.closed ? Theme.fgMuted : Theme.fg
                maximumLineCount: 4
                lineHeight: Theme.lineHeightPx(Theme.textBase, 1.4)
            }
            // The links of the text: they take the presses on them (a click elsewhere opens the
            // task: the row's), the hover underline and the preview of a [[page]] or ((block)).
            Item {
                anchors.fill: label
                containmentMask: QtObject {
                    function contains(point: point): bool {
                        return label.linkAt(point.x, point.y) !== ""
                    }
                }
                HoverHandler {
                    id: labelHover
                    cursorShape: Qt.PointingHandCursor
                    onPointChanged: {
                        control._trackLink(point.position.x, point.position.y)
                        LinkPreviews.hover(label, control._hoveredLink, control.path, Workspace,
                                           point.position.x, point.position.y)
                    }
                    onHoveredChanged: {
                        if (!hovered) {
                            control._hoveredLink = ""
                            LinkPreviews.leave(label)
                        }
                    }
                }
                TapHandler {
                    // A click on a link: a #tag filters the list, other links open (Ctrl+click on a
                    // #tag: its page, docs/SYNTAX.md §3).
                    id: mouseTap
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onPressedChanged: if (pressed) control._tagOpened = false
                    onTapped: (eventPoint) => control.clickLinkAt(eventPoint.position.x, eventPoint.position.y,
                                                                  (mouseTap.point.modifiers & Qt.ControlModifier) !== 0)
                }
                TapHandler {
                    // A tap on a link follows it; a long press on it: its menu.
                    id: touchHold
                    acceptedDevices: PointerDevice.TouchScreen
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onPressedChanged: {
                        if (pressed) {
                            control._tagOpened = false
                            control._trackLink(touchHold.point.position.x, touchHold.point.position.y)
                        } else {
                            control._hoveredLink = ""
                        }
                    }
                    onTapped: (eventPoint) => control.tapLinkAt(eventPoint.position.x, eventPoint.position.y)
                    onLongPressed: {
                        control._labelPoint = label.mapToItem(control, touchHold.point.position.x, touchHold.point.position.y)
                        const at = control._labelPoint
                        control.openMenuAt(at.x, at.y)
                    }
                }
            }

            // Screen readers: the task text's links as link items of their own (its 4 shown lines).
            BLinkTargets {
                textItem: label.textItem
                html: control.html
                lineHeight: label.lineHeight
                maxHeight: label.height
                onActivated: (href, x, y) => control.linkActivated(href)
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
                    variant: control.status === "doing" ? "reminder" : "neutral"
                    iconName: control.status === "doing" ? "clock" : control.status === "deferred" ? "hourglass" : "x"
                    text: control.statusLabel
                }
                BChip {
                    objectName: "taskChip_due" // (its token menu: openMenuAt())
                    visible: control.dueLabel !== ""
                    size: "small"
                    variant: control.closed ? "done" : control.overdue ? "overdue" : "due"
                    text: control.overdue ? qsTr("%1, overdue").arg(control.dueLabel) : control.dueLabel
                }
                BChip {
                    objectName: "taskChip_scheduled" // (its token menu: openMenuAt())
                    visible: control.scheduledLabel !== ""
                    size: "small"
                    variant: control.closed ? "done" : "scheduled"
                    text: control.scheduledLabel
                }
                BChip {
                    objectName: "taskChip_start" // (its token menu: openMenuAt())
                    visible: control.startLabel !== ""
                    size: "small"
                    variant: "neutral"
                    iconName: "calendar-clock"
                    text: qsTr("from %1").arg(control.startLabel)
                }
                BChip {
                    objectName: "taskChip_reminder" // (its token menu: openMenuAt())
                    visible: control.reminderLabel !== "" && !control.closed
                    size: "small"
                    variant: control.reminderAlarm ? "alarm" : "reminder"
                    text: control.reminderLabel
                }
                BChip {
                    objectName: "taskChip_recurrence" // (its token menu: openMenuAt())
                    visible: control.recurrenceLabel !== ""
                    size: "small"
                    variant: "recurrence"
                    text: control.recurrenceLabel
                }
                BChip {
                    objectName: "taskBlockedChip"
                    visible: control.blocked && !control.closed
                    size: "small"
                    variant: "overdue"
                    iconName: "lock"
                    interactive: true
                    outlined: false
                    text: qsTr("Waits for: %1").arg(control.blockedBy)
                    width: Math.min(implicitWidth, control.availableWidth - 40)
                    onClicked: control.linkActivated("taskdep:" + control.blockedById)
                }
                BChip {
                    objectName: "taskDependencyProblemChip"
                    visible: control.dependencyProblem !== ""
                    size: "small"
                    variant: "neutral"
                    iconName: "triangle-alert"
                    text: control.dependencyProblem === "cycle" ? qsTr("Dependency cycle") : qsTr("Missing dependency")
                }
                BChip {
                    objectName: "taskBlockingChip"
                    visible: control.blockingCount > 0 && !control.closed
                    size: "small"
                    variant: "neutral"
                    iconName: "link"
                    text: qsTr("Blocks %n task(s)", "", control.blockingCount)
                }
                BChip {
                    objectName: "taskProgressChip"
                    visible: control.progress !== ""
                    size: "small"
                    variant: control.progressComplete ? "done" : "neutral"
                    iconName: "list-checks"
                    text: control.progress
                }
                BChip {
                    objectName: "taskTimeChip"
                    visible: control.timeLabel !== ""
                    size: "small"
                    variant: control.clockRunning ? "reminder" : "neutral"
                    iconName: "clock"
                    text: control.timeLabel
                }
                BChip {
                    objectName: "taskChip_priority" // (its token menu: openMenuAt())
                    visible: control.priorityLabel !== ""
                    size: "small"
                    variant: control.priority >= 4 && !control.closed ? "overdue" : "neutral"
                    iconName: control.priority >= 3 ? "arrow-up" : "arrow-down"
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
    function prepareMenu(): void {
        if (!statusMenu)
            statusMenu = menuComponent.createObject(control) as TaskStatusMenu
        statusMenu.status = control.statusOrTodo
        statusMenu.clockRunning = control.clockRunning
    }
    /// The status menu at the pointer (BMenu.popup()).
    function openMenu(): void {
        control.prepareMenu()
        statusMenu.popup()
    }
    /// The status menu at (x, y) of the row.
    function openMenuAtPoint(x: real, y: real): void {
        control.prepareMenu()
        statusMenu.popup(x, y)
    }
    /// The status menu at (x, y) of `item`.
    function openMenuFrom(item: Item, x: real, y: real): void {
        control.prepareMenu()
        statusMenu.popup(item, x, y)
    }

    Component {
        id: menuComponent
        TaskStatusMenu {
            showOpen: true
            showTaskTools: true
            onOpenRequested: control.opened()
            onClockRequested: control.clockRequested()
            onDependencyRequested: control.dependencyRequested()
            onStatusChosen: (status) => {
                if (status !== control.statusOrTodo)
                    control.statusRequested(status)
            }
        }
    }
}
