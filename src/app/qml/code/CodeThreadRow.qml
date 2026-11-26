pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CodeThreadRow — a thread in CodeSidebar (docs/UI.md "Code mode"), a row of its project's
// ChatListModel: its state by priority as T3 Code shows it — a plan to approve, an approval asked
// for, working, failed, an unread answer — its title (semibold while unread or shown), a pin, the
// branch of a worktree of its own, when it last changed. A click opens it; a right click, a long
// press or "⋯" its menu: rename, pin, mark unread, copy its path, branch or link, a new thread in
// this worktree (or in the project's folder), open in the editor, archive, move to the trash.
Item {
    id: thread

    required property string path
    required property string title
    required property string dateLabel
    required property bool running
    required property bool queued
    required property bool asking
    required property string failure
    required property bool unread
    required property bool archived
    required property bool pinned
    required property string branch
    required property string worktree
    /// Its project (the section's): its folder is the thread's without a worktree.
    property string projectId
    property string projectPath
    property string routePath
    readonly property bool current: thread.path === thread.routePath
    readonly property string folder: thread.worktree !== "" ? thread.worktree : thread.projectPath
    /// The question it asks is a plan to approve.
    readonly property bool plan: thread.asking && Workspace.chats.questionPlan(thread.path, Workspace.chats.questionSerial) !== ""
    /// Its state in a word, by priority: "plan", "approval", "working", "failed", "unread", "".
    readonly property string kind: thread.plan ? "plan" : thread.asking ? "approval"
                                    : thread.running || thread.queued ? "working"
                                    : thread.failure !== "" ? "failed" : thread.unread ? "unread" : ""
    readonly property string stateText: thread.kind === "plan" ? qsTr("Plan ready")
                                        : thread.kind === "approval" ? qsTr("Waiting for approval")
                                        : thread.kind === "working" ? qsTr("Working")
                                        : thread.kind === "failed" ? qsTr("Failed")
                                        : thread.kind === "unread" ? qsTr("Finished, unread") : ""

    signal navigate(var route)
    signal renameRequested(string path, string title)
    signal deleteRequested(string path, string title, string worktree, string branch)
    signal copyRequested(string text, string what)

    objectName: "codeThread"
    Layout.fillWidth: true
    Layout.preferredHeight: Theme.listItemHeight
    Accessible.role: Accessible.Button
    Accessible.name: thread.title
    Accessible.description: thread.stateText

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: Theme.space1
        anchors.rightMargin: Theme.space1
        radius: Theme.radius
        color: thread.current ? Theme.bgSelected : threadHover.hovered ? Theme.bgHover : "transparent"
    }
    HoverHandler { id: threadHover; enabled: !Theme.touchUi }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: thread.navigate({ view: "code", path: thread.path })
        onLongPressed: threadMenu.popup(thread, thread.width / 2, thread.height)
    }
    TapHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedButtons: Qt.RightButton
        onTapped: (point) => threadMenu.popup(thread, point.position.x, point.position.y)
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space8
        anchors.rightMargin: Theme.space2
        spacing: Theme.space2
        // Its state: a spinner while it works, the question mark or the plan's icon while it asks,
        // the alert after a failure, a dot while unread — else nothing.
        Item {
            objectName: "codeThreadState"
            Layout.preferredWidth: Theme.iconSizeSm
            Layout.preferredHeight: Theme.iconSizeSm
            BSpinner {
                anchors.centerIn: parent
                visible: thread.kind === "working"
                size: Theme.iconSizeSm
                calm: true
            }
            BIcon {
                anchors.centerIn: parent
                visible: thread.kind === "plan" || thread.kind === "approval" || thread.kind === "failed"
                name: thread.kind === "plan" ? "list-checks" : thread.kind === "approval" ? "circle-help" : "circle-alert"
                size: Theme.iconSizeSm
                color: thread.kind === "failed" ? Theme.danger : Theme.accent
            }
            Rectangle {
                anchors.centerIn: parent
                visible: thread.kind === "unread"
                width: 8
                height: 8
                radius: 4
                color: Theme.accent
            }
        }
        BText {
            Layout.fillWidth: true
            text: thread.title
            color: thread.archived ? Theme.fgMuted : Theme.fg
            font.weight: thread.unread || thread.current ? Theme.weightSemiBold : Theme.weightNormal
            elide: Text.ElideRight
        }
        BIcon {
            visible: thread.pinned
            name: "pin"
            size: Theme.iconSizeSm
            color: Theme.fgFaint
        }
        BIcon {
            visible: thread.worktree !== ""
            name: "git-branch"
            size: Theme.iconSizeSm
            color: Theme.fgFaint
            BToolTip { visible: branchHover.hovered; text: thread.branch }
            HoverHandler { id: branchHover; enabled: !Theme.touchUi }
        }
        // What needs the user in a word (in the state's colour), else when it changed — and "⋯" on
        // hover.
        BText {
            visible: !threadHover.hovered || Theme.touchUi
            text: thread.kind === "plan" ? qsTr("Plan") : thread.kind === "approval" ? qsTr("Approve")
                  : thread.kind === "failed" ? qsTr("Failed") : thread.dateLabel
            variant: "small"
            color: thread.kind === "failed" ? Theme.dangerText
                   : thread.kind === "plan" || thread.kind === "approval" || thread.kind === "unread" ? Theme.accentText
                   : Theme.fgFaint
        }
        BIconButton {
            id: more
            objectName: "codeThreadMore"
            visible: threadHover.hovered && !Theme.touchUi
            size: "small"
            iconName: "ellipsis"
            label: qsTr("More: %1").arg(thread.title)
            onClicked: threadMenu.popup(more, 0, more.height)
        }
    }

    BMenu {
        id: threadMenu
        objectName: "codeThreadMenu"
        BMenuItem {
            objectName: "codeThreadRename"
            text: qsTr("Rename…")
            iconName: "pencil"
            shortcut: thread.current ? ShortcutMap.sequence("code.renameThread") : ""
            onTriggered: thread.renameRequested(thread.path, thread.title)
        }
        BMenuItem {
            objectName: "codeThreadPin"
            text: thread.pinned ? qsTr("Unpin") : qsTr("Pin")
            iconName: "pin"
            onTriggered: Workspace.chats.setPinned(thread.path, !thread.pinned)
        }
        BMenuItem {
            objectName: "codeThreadMarkUnread"
            visible: !thread.unread
            text: qsTr("Mark unread")
            iconName: "circle"
            onTriggered: Workspace.chats.markUnread(thread.path)
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "codeThreadNewHere"
            text: thread.worktree !== "" ? qsTr("New thread in this worktree") : qsTr("New thread in the project's folder")
            iconName: "plus"
            onTriggered: {
                if (thread.worktree !== "")
                    thread.navigate({ view: "code", new: true, project: thread.projectId, worktree: thread.worktree, branch: thread.branch })
                else
                    thread.navigate({ view: "code", new: true, project: thread.projectId, env: "local" })
            }
        }
        BMenuItem {
            objectName: "codeThreadEditor"
            text: qsTr("Open in editor")
            iconName: "code"
            onTriggered: {
                if (!Workspace.projects.openInEditor(thread.folder, -1))
                    AppController.toast(qsTr("The editor could not be started: choose one in Settings → Extensions → Code mode."), "error")
            }
        }
        BMenuItem {
            objectName: "codeThreadCopyPath"
            text: qsTr("Copy path")
            iconName: "copy"
            onTriggered: thread.copyRequested(thread.folder, qsTr("Path copied"))
        }
        BMenuItem {
            objectName: "codeThreadCopyBranch"
            visible: thread.branch !== ""
            text: qsTr("Copy branch")
            iconName: "git-branch"
            onTriggered: thread.copyRequested(thread.branch, qsTr("Branch copied"))
        }
        BMenuItem {
            objectName: "codeThreadCopyLink"
            text: qsTr("Copy link")
            iconName: "link"
            onTriggered: thread.copyRequested(AppController.pageAutomationLink(thread.path, "", ""), qsTr("Link copied"))
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "codeThreadArchive"
            text: thread.archived ? qsTr("Unarchive") : qsTr("Archive")
            iconName: "inbox"
            onTriggered: Workspace.chats.setArchived(thread.path, !thread.archived)
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "codeThreadDelete"
            text: qsTr("Move to trash")
            iconName: "trash-2"
            danger: true
            onTriggered: thread.deleteRequested(thread.path, thread.title, thread.worktree, thread.branch)
        }
    }
}
