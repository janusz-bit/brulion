pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// PlanningItemRow — one calendar entry (a CalendarModel entry map) in the day panel, the agenda
// and the overdue list: the task checkbox (tasks) or the entry's icon, the time ("" = all day),
// the title (plain text, two lines at most), where it is written (page title, or the day of a
// daily note) and the kind of date as a small chip.
//
//   PlanningItemRow { entry: modelData; onTaskToggled: actions.toggleTask(entry.path, entry.line, entry.title)
//                     onStatusRequested: (status) => actions.setTaskStatus(entry.path, entry.line, status, entry.title)
//                     onOpened: AppController.openPage(entry.path, entry.line) }
//
// Click / Enter opens the note at the entry; the checkbox (Space when focused) toggles the task, a
// right-click or long press on it opens the status menu (statusRequested()); a right-click or long
// press elsewhere on the row asks for the menu of the entry's date token (tokenMenuRequested(): the
// view's PlanningTokenMenu — its date, a reminder's time and alarm, "Change the whole series…" on a
// series' other dates).
T.ItemDelegate {
    id: control

    /// The CalendarModel entry (a map: { kind, title, time, status, isTask, closed, variant, icon,
    /// spec, path, pageTitle, isJournalPage, dueLabel, recurring, … }).
    required property var entry
    // Its fields (a missing one: "" / false).
    readonly property string entryKind: AppController.routeText(entry, "kind")
    readonly property string entryTitle: AppController.routeText(entry, "title")
    readonly property string entryTime: AppController.routeText(entry, "time")
    readonly property string entryStatus: AppController.routeText(entry, "status")
    readonly property string entryIcon: AppController.routeText(entry, "icon")
    readonly property string entryPath: AppController.routeText(entry, "path")
    readonly property string entryPageTitle: AppController.routeText(entry, "pageTitle")
    readonly property string entryDueLabel: AppController.routeText(entry, "dueLabel")
    readonly property bool recurring: AppController.routeText(entry, "recurring") === "true"
    /// Show the entry's date in the subtitle (lists spanning several days: overdue tasks).
    property bool showDate: false

    signal taskToggled()
    /// The checkbox's status menu (right-click / long press): "todo" … "cancelled", "" = none.
    signal statusRequested(string status)
    signal opened()
    /// A right-click or long press on the row (not on the checkbox): the menu of the entry's token
    /// at (x, y) of `item`.
    signal tokenMenuRequested(Item item, real x, real y)

    /// The entry names a token of a note (journals have none).
    readonly property bool hasTokenMenu: AppController.routeText(entry, "spec") !== "" && entryKind !== "journal"

    readonly property bool isTask: AppController.routeText(entry, "isTask") === "true"
    readonly property bool closed: AppController.routeText(entry, "closed") === "true"
    readonly property string variant: AppController.routeText(entry, "variant") !== "" ? AppController.routeText(entry, "variant") : "neutral"
    readonly property string journalDay: AppController.routeFlag(entry, "isJournalPage") ? Workspace.journalDate(entryPath) : ""
    readonly property string kindLabel: {
        switch (variant) {
        case "overdue": return qsTr("Overdue")
        case "alarm": return qsTr("Alarm")
        case "reminder": return qsTr("Reminder")
        case "scheduled": return qsTr("Scheduled")
        case "done": return control.entryKind === "done" ? qsTr("Done") : qsTr("Finished")
        }
        switch (control.entryKind) {
        case "due": return qsTr("Due")
        case "start": return qsTr("Starts")
        case "journal": return qsTr("Journal")
        }
        return ""
    }
    readonly property string subtitle: {
        const where = journalDay !== "" ? qsTr("Journal · %1").arg(DatesHelper.formatShort(journalDay))
                                        : control.entryPageTitle
        if (showDate && control.entryDueLabel !== "")
            return control.entryDueLabel + (where !== "" ? " · " + where : "")
        return where
    }

    /// The non-empty parts joined with ", ".
    function joinNonEmpty(parts: list<string>): string {
        let out = ""
        for (let i = 0; i < parts.length; ++i) {
            if (parts[i] !== "")
                out = out === "" ? parts[i] : out + ", " + parts[i]
        }
        return out
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
    Accessible.name: joinNonEmpty([entryTime, entryTitle, kindLabel, subtitle])

    onClicked: control.opened()
    onPressAndHold: if (control.hasTokenMenu) control.tokenMenuRequested(control, control.pressX, control.pressY)
    Keys.onReturnPressed: control.opened()
    Keys.onEnterPressed: control.opened()
    Keys.onSpacePressed: (event) => {
        if (control.isTask) {
            control.taskToggled()
            event.accepted = true
        }
    }

    TapHandler {
        // Right-click only (a finger's long press is the delegate's pressAndHold).
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        acceptedButtons: Qt.RightButton
        enabled: control.hasTokenMenu
        onTapped: (eventPoint) => control.tokenMenuRequested(control, eventPoint.position.x, eventPoint.position.y)
    }

    // Laid out with bindings, not Layouts: the row has its final height (a title on two lines)
    // as soon as it is created — the agenda is a lazy list, and a row that grew after the list had
    // placed it pushed the days below it while the agenda scrolled (docs/UI.md "Scrolling
    // performance").
    contentItem: Item {
        id: content
        readonly property int spacing: Theme.touchUi ? 6 : 8
        readonly property real chipWidth: chip.visible ? chip.implicitWidth + spacing : 0
        // (Not chipWidth: whether the chip shows depends on the row's width.)
        implicitWidth: textColumn.x + Math.max(title.implicitWidth, subtitleText.implicitWidth)
                       + (control.kindLabel !== "" ? chip.implicitWidth + spacing : 0)
        implicitHeight: Math.max(marker.height, timeText.visible ? timeText.y + timeText.implicitHeight : 0,
                                 textColumn.height, chip.visible ? chip.y + chip.implicitHeight : 0)

        // Checkbox for tasks, the entry's icon otherwise (both in the same column width).
        Item {
            id: marker
            width: Theme.touchUi ? Theme.touchTarget : 24
            height: width
            TaskBox {
                objectName: "planningTaskBox"
                anchors.centerIn: parent
                visible: control.isTask
                status: control.entryStatus !== "" ? control.entryStatus : "todo"
                Accessible.name: qsTr("Done: %1").arg(control.entryTitle)
                onToggled: control.taskToggled()
                onStatusChosen: (status) => {
                    if (status !== (control.entryStatus !== "" ? control.entryStatus : "todo"))
                        control.statusRequested(status)
                }
            }
            BIcon {
                anchors.centerIn: parent
                visible: !control.isTask
                name: control.entryIcon !== "" ? control.entryIcon : "calendar"
                size: Theme.iconSizeSm
                color: control.variant === "alarm" || control.variant === "reminder" ? Theme.markColor(control.variant)
                                                                                     : Theme.fgMuted
            }
        }

        Text {
            id: timeText
            x: marker.width + content.spacing
            y: Theme.touchUi ? 12 : 4
            width: timeMetrics.width
            visible: control.entryTime !== ""
            text: control.entryTime
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

        Item {
            id: textColumn
            x: timeText.visible ? timeText.x + timeText.width + content.spacing : marker.width + content.spacing
            width: Math.max(0, content.width - x - content.chipWidth)
            height: title.implicitHeight + (subtitleText.visible ? 1 + subtitleText.implicitHeight : 0)
            y: Math.max(0, (content.height - height) / 2)

            Text {
                id: title
                width: parent.width
                text: control.entryTitle
                font.family: Theme.fontUi
                font.pixelSize: Theme.textBase
                // Done: muted (its ticked box says so); cancelled: struck out too — as in Tasks.
                font.strikeout: control.entryStatus === "cancelled"
                color: control.closed ? Theme.fgMuted : Theme.fg
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.lineHeightPx(font.pixelSize, 1.35)
            }
            Text {
                id: subtitleText
                y: title.implicitHeight + 1
                width: parent.width
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
            id: chip
            x: content.width - width
            y: Theme.touchUi ? 10 : 2
            visible: control.kindLabel !== "" && control.width > 300
            size: "small"
            variant: control.variant
            showIcon: control.recurring
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
