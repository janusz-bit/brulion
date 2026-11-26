pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// NotificationsView — route "notifications" (the palette's "Notifications", `go.notifications`; the
// top bar's bell): what the app told the user in this notebook (Workspace.notifications,
// shell/notificationcenter.h) — reminders that rang, the AI chat's answers, failures and questions,
// AI agents', extensions' and scripts' own notifications, timers that ended —, newest first under a
// heading per day. Unread ones (came while the app was not in front) have an accent dot and their
// title semibold. A click marks one read and opens what it points to (a page, a conversation, an
// event's day: AppController.openNotification); × removes it. The header filters All / Unread, marks
// all read and clears the list (confirmed). Kept on this device for 30 days.
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`.
FocusScope {
    id: root

    property var route: ({})
    readonly property int maximumWidth: 860
    readonly property real sideMargin: Math.max(Theme.compact ? Theme.space3 : Theme.space6, (width - maximumWidth) / 2)
    readonly property NotificationCenter center: Workspace.notifications
    property bool unreadOnly: false
    readonly property list<var> items: root.center.entries(root.center.revision, root.unreadOnly)
    readonly property list<var> filters: [{ value: "all", text: qsTr("All") }, { value: "unread", text: qsTr("Unread") }]

    /// The icon of a notification's kind (NotificationCenter::kindName()).
    function kindIcon(kind: string): string {
        switch (kind) {
        case "reminder": return "bell"
        case "chat": return "message-square"
        case "agent": return "sparkles"
        case "extension": return "puzzle"
        case "script": return "terminal"
        case "timer": return "timer"
        }
        return "bell"
    }
    /// What sent it, in words (the row's accessible description).
    function kindLabel(kind: string): string {
        switch (kind) {
        case "reminder": return qsTr("Reminder")
        case "chat": return qsTr("AI chat")
        case "agent": return qsTr("AI agent")
        case "extension": return qsTr("Extension")
        case "script": return qsTr("Script")
        case "timer": return qsTr("Timer")
        }
        return ""
    }
    /// A day's heading: Today, Yesterday, else the date in words.
    function dayLabel(section: string): string {
        if (section === "today")
            return qsTr("Today")
        if (section === "yesterday")
            return qsTr("Yesterday")
        return DatesHelper.formatLong(section)
    }
    /// Marks `id` read and opens what it points to (nothing: it stays here).
    function activate(id: int, open: string): void {
        root.center.markRead(id)
        if (open !== "")
            AppController.openNotification(open, -1)
    }

    focus: true

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ListView {
        id: list
        objectName: "notificationsList"
        TouchFlick.enabled: true
        anchors.fill: parent
        focus: true
        clip: true
        model: root.items
        currentIndex: -1
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: Theme.narrow ? Theme.space16 + Theme.space8 : Theme.space8
        T.ScrollBar.vertical: BScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Notifications")
        Keys.onReturnPressed: {
            const item = list.currentItem as NotificationRow
            if (item)
                root.activate(item.entryId, item.open)
        }
        Keys.onDeletePressed: {
            const item = list.currentItem as NotificationRow
            if (item)
                root.center.remove(item.entryId)
        }

        header: Item {
            width: list.width
            implicitHeight: head.y + head.implicitHeight + Theme.space3

            Flow {
                id: head
                x: root.sideMargin
                y: Theme.compact ? Theme.space3 : Theme.space6
                width: list.width - 2 * root.sideMargin
                spacing: Theme.space3
                BSegmented {
                    objectName: "notificationsFilter"
                    accessibleName: qsTr("Show")
                    model: root.filters
                    currentValue: root.unreadOnly ? "unread" : "all"
                    onActivated: (value) => root.unreadOnly = value === "unread"
                }
                BText {
                    height: implicitHeight + Theme.space2
                    topPadding: Theme.space2
                    visible: root.center.unread > 0
                    text: qsTr("%n unread", "", root.center.unread)
                    variant: "muted"
                }
                BButton {
                    objectName: "notificationsMarkRead"
                    visible: root.center.unread > 0
                    size: "small"
                    variant: "secondary"
                    iconName: "check"
                    text: qsTr("Mark all as read")
                    onClicked: root.center.markAllRead()
                }
                BButton {
                    objectName: "notificationsClear"
                    visible: root.center.count > 0
                    size: "small"
                    variant: "ghost"
                    iconName: "trash-2"
                    text: qsTr("Clear")
                    onClicked: clearDialog.open()
                }
            }
        }

        section.property: "section"
        section.criteria: ViewSection.FullString
        section.delegate: BText {
            required property string section
            x: root.sideMargin
            width: list.width - 2 * root.sideMargin
            topPadding: Theme.space4
            bottomPadding: Theme.space1
            text: root.dayLabel(section)
            variant: "caption"
            font.weight: Theme.weightSemiBold
            color: Theme.fgFaint
        }

        delegate: NotificationRow {}

        footer: BText {
            x: root.sideMargin
            width: list.width - 2 * root.sideMargin
            visible: root.items.length > 0
            topPadding: Theme.space4
            text: qsTr("Kept on this device for 30 days. Reminders, answers of the AI chat, and what AI agents and extensions tell you.")
            variant: "small"
            color: Theme.fgFaint
            wrapMode: Text.Wrap
        }

        BEmptyState {
            parent: list
            anchors.centerIn: parent
            width: Math.min(parent.width - 2 * Theme.space6, implicitWidth)
            visible: root.items.length === 0
            iconName: "bell"
            title: root.unreadOnly && root.center.count > 0 ? qsTr("Nothing unread") : qsTr("No notifications yet")
            description: qsTr("Reminders that rang, answers of the AI chat, and what AI agents and extensions tell you show up here.")
        }
    }

    component NotificationRow: Item {
        id: row
        required property int index
        required property int entryId
        required property string kind
        required property string title
        required property string text
        required property string open
        required property string time
        required property bool read

        objectName: "notificationRow"
        width: list.width
        implicitHeight: Math.max(Theme.listItemHeightTwoLine, rowLayout.implicitHeight + 2 * Theme.space2)
        Accessible.role: Accessible.ListItem
        Accessible.name: (row.read ? "" : qsTr("Unread") + ": ") + row.title
        Accessible.description: root.kindLabel(row.kind) + ", " + row.time + ". " + row.text

        Rectangle {
            x: root.sideMargin - Theme.space2
            width: list.width - 2 * root.sideMargin + 2 * Theme.space2
            height: parent.height
            radius: Theme.radius
            color: rowHover.hovered || row.ListView.isCurrentItem ? Theme.bgHover : "transparent"
            HoverHandler { id: rowHover; enabled: !Theme.touchUi; cursorShape: Qt.PointingHandCursor }
        }
        TapHandler {
            onTapped: root.activate(row.entryId, row.open)
        }

        RowLayout {
            id: rowLayout
            x: root.sideMargin
            width: list.width - 2 * root.sideMargin
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space3

            BIcon {
                name: root.kindIcon(row.kind)
                size: Theme.iconSize
                color: row.read ? Theme.fgMuted : Theme.accent
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 2
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BText {
                        Layout.fillWidth: true
                        text: row.title
                        font.weight: row.read ? Theme.weightMedium : Theme.weightSemiBold
                        elide: Text.ElideRight
                    }
                    BText {
                        text: row.time
                        variant: "small"
                        color: row.read ? Theme.fgFaint : Theme.accent
                    }
                    Rectangle {
                        objectName: "notificationUnread"
                        visible: !row.read
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: Theme.accent
                    }
                }
                BText {
                    Layout.fillWidth: true
                    visible: row.text !== ""
                    text: row.text
                    variant: "small"
                    color: Theme.fgMuted
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
            }
            BIconButton {
                objectName: "notificationRemove"
                size: "small"
                iconName: "x"
                label: qsTr("Remove")
                Accessible.name: qsTr("Remove %1").arg(row.title)
                onClicked: root.center.remove(row.entryId)
            }
        }
    }

    BConfirmDialog {
        id: clearDialog
        parent: T.Overlay.overlay
        title: qsTr("Clear the notifications?")
        message: qsTr("%n notification(s) will be removed from this list. Reminders and conversations stay as they are.", "", root.center.count)
        confirmText: qsTr("Clear")
        destructive: true
        onAccepted: root.center.clear()
    }
}
