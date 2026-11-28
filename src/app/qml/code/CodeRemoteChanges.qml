pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// CodeRemoteChanges — the changes of a thread whose project is on another computer of the user's
// (docs/CODE.md "Threads on the phone"), in place of the Changes panel: the computer has the
// project, its git and its terminal, so this side reads what it says (Workspace.projects.remote:
// RemoteProjects.requestChanges / requestDiff over its `acp serve`) — the files the agent's last
// turn changed there (since the turn's checkpoint), or every change not committed, read-only, and
// a file's diff (BDiffView: beside the list when wide, in its place when narrow). Its git is asked
// there: Commit… (every change or this turn's files; then pushed, or pushed with a pull request),
// Push, Create PR… (CodeRemoteGitDialog) and Revert this turn… (the files back as the turn's
// checkpoint has them) — RemoteProjects.commit / push / createPullRequest / revertTurn, one at a time,
// none while the turn runs there (`working`), what happened in a toast. The terminal stays that
// computer's (said in the header). Refreshed when the checkpoint changes, when the owner asks
// (refresh(): a turn ended), after git did something and with its button.
//
//   CodeRemoteChanges { project: id; worktree: place.worktree; checkpoint: lastCheckpoint; host: place.host
//                       working: chat.busyHere }
Rectangle {
    id: panel
    objectName: "codeRemoteChanges"

    /// The project's id there, the thread's worktree there ("" the project's folder), the turn's
    /// checkpoint ("" none: all changes), the computer's name.
    property string project
    property string worktree
    property string checkpoint
    property string host
    /// "turn" | "all" — what the list shows while there is a checkpoint.
    property string mode: "turn"
    /// The thread's turn runs there: git waits.
    property bool working: false

    /// The git request on its way ("" none: "commit" | "push" | "pr" | "revert") and its number.
    property string gitAction
    property int gitSerial: 0
    readonly property bool gitBusy: panel.gitAction !== ""
    readonly property bool canGit: !panel.gitBusy && !panel.working && panel.project !== ""

    readonly property bool turnMode: panel.checkpoint !== "" && panel.mode === "turn"
    readonly property bool wide: panel.width >= 760
    readonly property bool showDiff: panel.selectedPath !== ""

    property list<var> rows
    property bool loading: false
    property bool loaded: false
    property string error
    property bool truncated: false
    property int changesSerial: 0

    property string selectedPath
    property string selectedOldPath
    property string diffOld
    property string diffNew
    property bool diffBinary: false
    property bool diffTooLarge: false
    property bool diffLoading: false
    property string diffError
    property int diffSerial: 0

    readonly property list<var> modes: [
        { value: "turn", text: qsTr("This turn"), icon: "history" },
        { value: "all", text: qsTr("All changes"), icon: "git-compare" }
    ]

    function refresh(): void {
        const remote = Workspace.projects.remote
        if (remote === null || panel.project === "")
            return
        panel.loading = true
        panel.changesSerial = remote.requestChanges(panel.project, panel.worktree, panel.turnMode ? panel.checkpoint : "")
    }
    function openDiff(path: string, oldPath: string): void {
        const remote = Workspace.projects.remote
        if (remote === null)
            return
        panel.selectedPath = path
        panel.selectedOldPath = oldPath
        panel.diffOld = ""
        panel.diffNew = ""
        panel.diffBinary = false
        panel.diffTooLarge = false
        panel.diffError = ""
        panel.diffLoading = true
        panel.diffSerial = remote.requestDiff(panel.project, panel.worktree, panel.turnMode ? panel.checkpoint : "",
                                              path, oldPath)
    }
    function commit(message: string, files: list<string>, push: bool, createPr: bool, prTitle: string,
                    prBody: string, draft: bool): void {
        const remote = Workspace.projects.remote
        if (remote === null)
            return
        // (Asked before anything is set: a list argument is handed on only so.)
        const serial = remote.commit(panel.project, panel.worktree, message, files, push, createPr, prTitle, prBody,
                                     draft)
        panel.gitAction = "commit"
        panel.gitSerial = serial
    }
    function push(): void {
        const remote = Workspace.projects.remote
        if (remote === null)
            return
        panel.gitAction = "push"
        panel.gitSerial = remote.push(panel.project, panel.worktree)
    }
    function pullRequest(title: string, body: string, base: string, draft: bool): void {
        const remote = Workspace.projects.remote
        if (remote === null)
            return
        panel.gitAction = "pr"
        panel.gitSerial = remote.createPullRequest(panel.project, panel.worktree, title, body, base, draft)
    }
    function revertTurn(): void {
        const remote = Workspace.projects.remote
        if (remote === null || panel.checkpoint === "")
            return
        panel.gitAction = "revert"
        panel.gitSerial = remote.revertTurn(panel.project, panel.worktree, panel.checkpoint)
    }
    function gitDone(serial: int, ok: bool, message: string, url: string): void {
        if (serial !== panel.gitSerial || panel.gitAction === "")
            return
        panel.gitAction = ""
        if (message !== "")
            AppController.toast(url !== "" ? message + "\n" + url : message, ok ? "success" : "error")
        panel.closeDiff()
        again.restart()
    }
    function closeDiff(): void {
        panel.selectedPath = ""
        panel.selectedOldPath = ""
        panel.diffLoading = false
        panel.diffError = ""
    }
    function setMode(value: string): void {
        panel.mode = value
        panel.closeDiff()
        again.restart()
    }

    onProjectChanged: {
        panel.closeDiff()
        again.restart()
    }
    onWorktreeChanged: again.restart()
    onCheckpointChanged: again.restart()
    onVisibleChanged: {
        if (panel.visible)
            again.restart()
    }
    Component.onCompleted: again.restart()

    Later {
        id: again
        onTriggered: panel.refresh()
    }
    Connections {
        target: Workspace.projects.remote
        function onChangesReady(serial: int, changes: list<var>, truncated: bool, error: string) {
            if (serial !== panel.changesSerial)
                return
            panel.loading = false
            panel.loaded = true
            panel.error = error
            panel.truncated = truncated
            panel.rows = error === "" ? changes : []
        }
        function onDiffReady(serial: int, path: string, oldText: string, newText: string, binary: bool,
                             tooLarge: bool, error: string) {
            if (serial !== panel.diffSerial || path !== panel.selectedPath)
                return
            panel.diffLoading = false
            panel.diffError = error
            panel.diffOld = oldText
            panel.diffNew = newText
            panel.diffBinary = binary
            panel.diffTooLarge = tooLarge
        }
        function onGitDone(serial: int, action: string, ok: bool, message: string, url: string) {
            panel.gitDone(serial, ok, message, url)
        }
    }

    color: Theme.bg

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Where it is, refresh ----------------------------------------------------------
        RowLayout {
            objectName: "codeRemoteHeader"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space2
            Layout.topMargin: Theme.space2
            Layout.bottomMargin: Theme.space1
            spacing: Theme.space2
            BIcon {
                name: "laptop"
                size: Theme.iconSize
                color: Theme.fgMuted
            }
            BText {
                objectName: "codeRemoteNote"
                Layout.fillWidth: true
                text: qsTr("The project is on %1: its git works there, its terminal is there.").arg(panel.host)
                variant: "small"
                color: Theme.fgMuted
                wrapMode: Text.Wrap
            }
            BSpinner {
                visible: panel.loading
                size: Theme.iconSizeSm
            }
            BIconButton {
                objectName: "codeRemoteRefresh"
                iconName: "refresh-cw"
                label: qsTr("Refresh")
                size: "small"
                onClicked: panel.refresh()
            }
        }

        // ---- This turn / All changes -----------------------------------------------------------
        BSegmented {
            objectName: "codeRemoteMode"
            Layout.leftMargin: Theme.space2
            Layout.bottomMargin: Theme.space2
            visible: panel.checkpoint !== ""
            size: "small"
            accessibleName: qsTr("Which changes")
            model: panel.modes
            currentValue: panel.mode
            onActivated: (value) => panel.setMode("" + value)
        }

        // ---- Its git there --------------------------------------------------------------------------
        Flow {
            objectName: "codeRemoteActions"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space2
            Layout.rightMargin: Theme.space2
            Layout.bottomMargin: Theme.space2
            spacing: Theme.space1
            BButton {
                objectName: "codeRemoteCommit"
                text: qsTr("Commit…")
                size: "small"
                iconName: "git-commit-horizontal"
                enabled: panel.canGit && panel.rows.length > 0
                busy: panel.gitAction === "commit"
                onClicked: gitDialog.openFor("commit", panel.turnMode ? UiModels.stringFields(panel.rows, "path") : panel.noPaths)
            }
            BButton {
                objectName: "codeRemotePush"
                text: qsTr("Push")
                size: "small"
                variant: "ghost"
                iconName: "upload"
                enabled: panel.canGit
                busy: panel.gitAction === "push"
                onClicked: panel.push()
            }
            BButton {
                objectName: "codeRemotePullRequest"
                text: qsTr("Create PR…")
                size: "small"
                variant: "ghost"
                iconName: "git-pull-request"
                enabled: panel.canGit
                busy: panel.gitAction === "pr"
                onClicked: gitDialog.openFor("pr", panel.noPaths)
            }
            BButton {
                objectName: "codeRemoteRevertTurn"
                visible: panel.turnMode
                text: qsTr("Revert this turn…")
                size: "small"
                variant: "ghost"
                iconName: "undo-2"
                enabled: panel.canGit && panel.rows.length > 0
                busy: panel.gitAction === "revert"
                onClicked: revertDialog.open()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.borderSubtle
        }

        // ---- The files and a file's diff ----------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Item {
                id: listPane
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                width: panel.showDiff && panel.wide ? Math.min(360, Math.round(parent.width * 0.38)) : parent.width
                visible: !panel.showDiff || panel.wide

                ListView {
                    id: list
                    objectName: "codeRemoteChangeList"
                    anchors.fill: parent
                    anchors.margins: Theme.space1
                    clip: true
                    model: panel.rows
                    boundsBehavior: Flickable.StopAtBounds
                    T.ScrollBar.vertical: BScrollBar {}
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Changed files")
                    delegate: ChangeRow {}
                    footer: BText {
                        width: list.width
                        visible: panel.truncated
                        height: visible ? implicitHeight + Theme.space2 : 0
                        leftPadding: Theme.space3
                        topPadding: Theme.space2
                        text: qsTr("and more files")
                        variant: "faint"
                        font.pixelSize: Theme.textSm
                    }
                }
                BEmptyState {
                    objectName: "codeRemoteNoChanges"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.space4, maximumWidth)
                    visible: panel.loaded && panel.error === "" && list.count === 0
                    compact: true
                    iconName: "circle-check"
                    title: panel.turnMode ? qsTr("No changes in this turn") : qsTr("No changes")
                    description: panel.turnMode ? qsTr("The files are as they were before the turn.")
                               : qsTr("The working tree matches the last commit.")
                }
                BEmptyState {
                    objectName: "codeRemoteError"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.space4, maximumWidth)
                    visible: panel.error !== ""
                    compact: true
                    iconName: "cloud-off"
                    title: qsTr("The changes cannot be shown")
                    description: panel.error
                }
            }

            Item {
                id: diffPane
                objectName: "codeRemoteDiffPane"
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.left: panel.wide ? listPane.right : parent.left
                visible: panel.showDiff

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.space2
                    spacing: Theme.space2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space1
                        BIconButton {
                            objectName: "codeRemoteDiffBack"
                            iconName: panel.wide ? "x" : "chevron-left"
                            label: panel.wide ? qsTr("Close the diff") : qsTr("Back to the files")
                            size: "small"
                            onClicked: panel.closeDiff()
                        }
                        BText {
                            Layout.fillWidth: true
                            text: panel.selectedPath
                            variant: "label"
                            elide: Text.ElideMiddle
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        BDiffView {
                            objectName: "codeRemoteDiffView"
                            anchors.fill: parent
                            visible: !panel.diffLoading && panel.diffError === "" && !panel.diffBinary && !panel.diffTooLarge
                            oldText: panel.diffOld
                            newText: panel.diffNew
                            oldLabel: panel.turnMode ? qsTr("Before the turn") : qsTr("Last commit")
                            newLabel: qsTr("Now")
                            mode: panel.wide ? "auto" : "unified"
                        }
                        BSpinner {
                            anchors.centerIn: parent
                            visible: panel.diffLoading
                        }
                        BText {
                            anchors.centerIn: parent
                            width: parent.width - 2 * Theme.space4
                            visible: !panel.diffLoading && (panel.diffError !== "" || panel.diffBinary || panel.diffTooLarge)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            color: Theme.fgMuted
                            text: panel.diffError !== "" ? panel.diffError
                                  : panel.diffBinary ? qsTr("A binary file: no diff to show.")
                                  : qsTr("Too big to show here.")
                        }
                    }
                }
            }
        }
    }

    readonly property list<string> noPaths: []

    CodeRemoteGitDialog {
        id: gitDialog
        host: panel.host
        onCommitRequested: (message, files, push, createPr, prTitle, prBody, draft) =>
                           panel.commit(message, files, push, createPr, prTitle, prBody, draft)
        onPullRequestRequested: (title, body, base, draft) => panel.pullRequest(title, body, base, draft)
    }

    BConfirmDialog {
        id: revertDialog
        objectName: "codeRemoteRevertDialog"
        title: qsTr("Revert this turn?")
        subject: panel.host
        subjectIcon: "laptop"
        message: qsTr("The files on %1 go back to how they were before the turn: the %n file(s) it changed are put back, files it added are deleted.", "", panel.rows.length).arg(panel.host)
        detail: qsTr("Commits, the staging area and ignored files stay as they are.")
        confirmText: qsTr("Revert")
        destructive: true
        onAccepted: panel.revertTurn()
    }

    // ---- A changed file ----------------------------------------------------------------------
    component ChangeRow: T.ItemDelegate {
        id: row
        required property string path
        required property string oldPath
        required property string letter
        required property string name
        required property string dir
        required property int additions
        required property int deletions
        required property bool binary
        readonly property bool selected: row.path === panel.selectedPath

        width: ListView.view.width
        implicitHeight: Theme.touchUi ? 44 : 32
        objectName: "codeRemoteChange"
        Accessible.name: row.path
        onClicked: panel.openDiff(row.path, row.oldPath)

        background: Rectangle {
            radius: Theme.radius
            color: row.selected ? Theme.bgSelected : row.hovered ? Theme.bgHover : "transparent"
        }
        contentItem: RowLayout {
            spacing: Theme.space2
            BText {
                Layout.leftMargin: Theme.space2
                Layout.preferredWidth: Theme.space4
                text: row.letter
                font.weight: Theme.weightSemiBold
                color: row.letter === "A" || row.letter === "U" ? Theme.successText
                       : row.letter === "D" || row.letter === "C" ? Theme.dangerText
                       : row.letter === "R" ? Theme.infoText : Theme.warningText
            }
            BText {
                text: row.name
                elide: Text.ElideRight
                Layout.maximumWidth: row.width * 0.6
            }
            BText {
                Layout.fillWidth: true
                text: row.dir
                variant: "faint"
                font.pixelSize: Theme.textSm
                elide: Text.ElideLeft
            }
            BText {
                Layout.rightMargin: Theme.space2
                visible: !row.binary && (row.additions > 0 || row.deletions > 0)
                text: "+" + row.additions + " −" + row.deletions
                variant: "faint"
                font.pixelSize: Theme.textSm
            }
        }
    }
}
