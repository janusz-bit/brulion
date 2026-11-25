pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// GitChangesPanel — the "Code" mode's changes panel (docs/CODE-GIT.md), as T3 Code has it beside a
// thread: the folder's branch (a menu to switch to another or make a new one), how far it is ahead
// of / behind its upstream, the changed files — all of the working tree's against the last commit,
// or with a checkpoint only what the agent's turn changed ("This turn" / "All changes") — and a
// file's diff (BDiffView: beside the list on wide panels, in its place on narrow ones). Below:
// Commit… (commit / commit and push / commit, push and create PR), Push, Pull, Create PR… (with
// the GitHub CLI) and, with a checkpoint, Revert this turn… (the files as they were before it).
// Everything runs through CodeGit (C++, asynchronous); what happened comes out as
// actionFinished() for the owner's toasts — the panel calls nothing else in the app.
//
//   GitChangesPanel {
//       folder: thread.folder                // a repository's work tree or a worktree
//       checkpoint: thread.turnCheckpoint    // "" none
//       checkpointLabel: qsTr("Turn 3")
//       onActionFinished: (action, ok, message, url) => toast(message)
//   }
Item {
    id: panel
    objectName: "gitChangesPanel"

    /// The folder (a work tree, or a folder inside one).
    property string folder
    /// The checkpoint before the agent's turn ("" none): "This turn" and "Revert this turn…".
    property string checkpoint
    /// What the turn is called in the panel and the revert dialog ("Turn 3", the prompt…).
    property string checkpointLabel
    /// A refresh every so many ms while shown (0: on git's own changes and after actions only).
    property int pollInterval: 0
    /// "turn" | "all" — what the list shows while there is a checkpoint.
    property string mode: "turn"

    /// The folder's git (CodeGit) — for the owner and the self-tests.
    readonly property CodeGit git: repo
    readonly property bool turnMode: panel.checkpoint !== "" && panel.mode === "turn"
    readonly property list<var> rows: panel.turnMode ? repo.turnChanges : repo.changes
    readonly property list<string> rowPaths: UiModels.stringFields(panel.rows, "path")
    /// Files the list does not show (beyond CodeGit.maxRows).
    readonly property int hiddenCount: panel.turnMode ? 0 : Math.max(0, repo.changeCount - panel.rows.length)
    readonly property bool wide: panel.width >= 760
    readonly property bool showDiff: panel.selectedPath !== ""

    /// The file whose diff is shown ("" none) and its texts.
    property string selectedPath
    property string diffOld
    property string diffNew
    property bool diffBinary: false
    property bool diffTooLarge: false
    property bool diffLoading: false
    property string diffError

    readonly property list<var> modes: [
        { value: "turn", text: qsTr("This turn"), icon: "history" },
        { value: "all", text: qsTr("All changes"), icon: "git-compare" }
    ]

    /// An action ended (CodeGit.actionFinished): for a toast — `url` a pull request's address.
    signal actionFinished(string action, bool ok, string message, string url)

    function openDiff(path: string): void {
        panel.selectedPath = path
        panel.diffOld = ""
        panel.diffNew = ""
        panel.diffBinary = false
        panel.diffTooLarge = false
        panel.diffError = ""
        panel.diffLoading = true
        repo.requestDiff(path, panel.turnMode ? panel.checkpoint : "")
    }
    function closeDiff(): void {
        panel.selectedPath = ""
        panel.diffOld = ""
        panel.diffNew = ""
        panel.diffLoading = false
        panel.diffError = ""
    }
    function setMode(value: string): void {
        panel.mode = value
        follow.restart()
    }
    /// After a refresh: the diff shown again from the new state, or closed when its file has no
    /// changes any more.
    function followChanges(): void {
        if (panel.selectedPath === "")
            return
        if (panel.rowPaths.indexOf(panel.selectedPath) < 0) {
            panel.closeDiff()
            return
        }
        repo.requestDiff(panel.selectedPath, panel.turnMode ? panel.checkpoint : "")
    }
    function openBranchMenu(): void {
        repo.branches()
        branchMenu.popup(branchButton, 0, branchButton.height)
    }
    function openCommit(): void {
        commitDialog.openFor()
    }
    function openPullRequest(): void {
        prDialog.openFor()
    }
    function openNewBranch(): void {
        branchField.text = ""
        newBranchDialog.open()
    }

    onFolderChanged: panel.closeDiff()
    onCheckpointChanged: follow.restart()

    CodeGit {
        id: repo
        folder: panel.folder
        turnCheckpoint: panel.checkpoint
        pollInterval: panel.pollInterval
        onDiffReady: (path, oldText, newText, binary, tooLarge) => {
            if (path !== panel.selectedPath)
                return
            panel.diffOld = oldText
            panel.diffNew = newText
            panel.diffBinary = binary
            panel.diffTooLarge = tooLarge
            panel.diffError = ""
            panel.diffLoading = false
        }
        onDiffFailed: (path, message) => {
            if (path !== panel.selectedPath)
                return
            panel.diffError = message
            panel.diffLoading = false
        }
        onActionFinished: (action, ok, message, url) => panel.actionFinished(action, ok, message, url)
        onRevisionChanged: follow.restart()
    }
    Later {
        id: follow
        onTriggered: panel.followChanges()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Branch, ahead / behind, refresh --------------------------------------------------
        RowLayout {
            objectName: "gitHeader"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space2
            Layout.rightMargin: Theme.space2
            Layout.topMargin: Theme.space2
            Layout.bottomMargin: Theme.space1
            visible: repo.isRepo
            spacing: Theme.space1

            BButton {
                id: branchButton
                objectName: "gitBranchButton"
                Layout.minimumWidth: 0
                Layout.maximumWidth: Math.max(Theme.space16 * 2, panel.width - 140)
                variant: "ghost"
                size: "small"
                iconName: "git-branch"
                text: repo.detached ? qsTr("Detached HEAD") : repo.branch !== "" ? repo.branch : qsTr("No branch")
                Accessible.description: qsTr("Switch to another branch or make a new one")
                onClicked: panel.openBranchMenu()
            }
            RowLayout {
                objectName: "gitAheadBehind"
                visible: repo.upstream !== ""
                spacing: 2
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("%1 ahead, %2 behind %3").arg(repo.ahead).arg(repo.behind).arg(repo.upstream)
                BIcon {
                    name: "arrow-up"
                    size: Theme.iconSizeSm
                    color: repo.ahead > 0 ? Theme.fg : Theme.fgFaint
                }
                BText {
                    objectName: "gitAhead"
                    text: "" + repo.ahead
                    variant: "small"
                    color: repo.ahead > 0 ? Theme.fg : Theme.fgFaint
                }
                BIcon {
                    Layout.leftMargin: Theme.space1
                    name: "arrow-down"
                    size: Theme.iconSizeSm
                    color: repo.behind > 0 ? Theme.fg : Theme.fgFaint
                }
                BText {
                    objectName: "gitBehind"
                    text: "" + repo.behind
                    variant: "small"
                    color: repo.behind > 0 ? Theme.fg : Theme.fgFaint
                }
            }
            BText {
                objectName: "gitNoUpstream"
                visible: repo.upstream === "" && !repo.detached && repo.hasCommits
                text: qsTr("not pushed")
                variant: "faint"
                font.pixelSize: Theme.textSm
            }
            Item {
                Layout.fillWidth: true
            }
            BSpinner {
                objectName: "gitBusy"
                visible: repo.busy || repo.refreshing
                size: Theme.iconSizeSm
            }
            BIconButton {
                objectName: "gitRefresh"
                iconName: "refresh-cw"
                label: qsTr("Refresh")
                size: "small"
                onClicked: repo.refresh()
            }
        }

        // ---- This turn / All changes ----------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space2
            Layout.rightMargin: Theme.space2
            Layout.bottomMargin: Theme.space2
            visible: repo.isRepo && panel.checkpoint !== ""
            spacing: Theme.space2
            BSegmented {
                objectName: "gitChangesMode"
                size: "small"
                accessibleName: qsTr("Which changes")
                model: panel.modes
                currentValue: panel.mode
                onActivated: (value) => panel.setMode("" + value)
            }
            BText {
                objectName: "gitTurnLabel"
                Layout.fillWidth: true
                visible: panel.turnMode && panel.checkpointLabel !== ""
                text: qsTr("Since %1").arg(panel.checkpointLabel)
                variant: "faint"
                font.pixelSize: Theme.textSm
                elide: Text.ElideRight
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            visible: repo.isRepo
            color: Theme.borderSubtle
        }

        // ---- The files and a file's diff ------------------------------------------------------
        Item {
            id: body
            objectName: "gitBody"
            Layout.fillWidth: true
            Layout.fillHeight: true

            BSpinner {
                anchors.centerIn: parent
                visible: !repo.loaded && panel.folder !== ""
            }
            BEmptyState {
                objectName: "gitNotRepo"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space4, maximumWidth)
                visible: (repo.loaded || panel.folder === "") && !repo.isRepo
                compact: true
                iconName: "folder-x"
                title: panel.folder === "" ? qsTr("No folder") : qsTr("Not a git repository")
                description: panel.folder === "" ? qsTr("Open a project to see its changes here.")
                           : repo.gitAvailable ? qsTr("This folder has no git repository. Run “git init” in it, or open a folder that has one.")
                           : qsTr("git is not installed: install it to see and commit the changes of your projects.")
            }

            Item {
                id: listPane
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                width: panel.showDiff && panel.wide ? Math.min(360, Math.round(parent.width * 0.38)) : parent.width
                visible: repo.isRepo && (!panel.showDiff || panel.wide)

                ListView {
                    id: list
                    objectName: "gitChangeList"
                    anchors.fill: parent
                    anchors.margins: Theme.space1
                    clip: true
                    model: panel.rows
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: true
                    T.ScrollBar.vertical: BScrollBar {}
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Changed files")
                    delegate: GitChangeRow {
                        width: ListView.view.width
                        selected: path === panel.selectedPath
                        onActivated: (path) => panel.openDiff(path)
                    }
                    footer: BText {
                        objectName: "gitHiddenCount"
                        width: list.width
                        visible: panel.hiddenCount > 0
                        height: visible ? implicitHeight + Theme.space2 : 0
                        leftPadding: Theme.space3
                        topPadding: Theme.space2
                        text: qsTr("and %n more file(s)", "", panel.hiddenCount)
                        variant: "faint"
                        font.pixelSize: Theme.textSm
                    }
                }
                BEmptyState {
                    objectName: "gitNoChanges"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.space4, maximumWidth)
                    visible: list.count === 0 && (panel.turnMode ? repo.turnLoaded && repo.turnError === "" : repo.loaded)
                    compact: true
                    iconName: "circle-check"
                    title: panel.turnMode ? qsTr("No changes in this turn") : qsTr("No changes")
                    description: panel.turnMode ? qsTr("The files are as they were before the turn.")
                               : qsTr("The working tree matches the last commit.")
                }
                BEmptyState {
                    objectName: "gitTurnError"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.space4, maximumWidth)
                    visible: panel.turnMode && repo.turnLoaded && repo.turnError !== ""
                    compact: true
                    iconName: "history"
                    title: qsTr("This turn's changes cannot be shown")
                    description: repo.turnError
                }
            }

            Item {
                id: diffPane
                objectName: "gitDiffPane"
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.left: panel.wide ? listPane.right : parent.left
                visible: repo.isRepo && panel.showDiff

                Rectangle {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    width: 1
                    visible: panel.wide
                    color: Theme.borderSubtle
                }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.space2
                    spacing: Theme.space2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space1
                        BIconButton {
                            objectName: "gitDiffBack"
                            visible: !panel.wide
                            iconName: "chevron-left"
                            label: qsTr("Back to the files")
                            size: "small"
                            onClicked: panel.closeDiff()
                        }
                        BText {
                            objectName: "gitDiffPath"
                            Layout.fillWidth: true
                            text: panel.selectedPath
                            variant: "label"
                            elide: Text.ElideMiddle
                        }
                        BIconButton {
                            objectName: "gitDiffClose"
                            visible: panel.wide
                            iconName: "x"
                            label: qsTr("Close the diff")
                            size: "small"
                            onClicked: panel.closeDiff()
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        BDiffView {
                            objectName: "gitDiffView"
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
                        BEmptyState {
                            objectName: "gitDiffMessage"
                            anchors.centerIn: parent
                            width: Math.min(parent.width - 2 * Theme.space4, maximumWidth)
                            visible: !panel.diffLoading && (panel.diffError !== "" || panel.diffBinary || panel.diffTooLarge)
                            compact: true
                            iconName: panel.diffError !== "" ? "triangle-alert" : "file"
                            title: panel.diffError !== "" ? qsTr("No diff") : panel.diffBinary ? qsTr("Binary file")
                                 : qsTr("Too large to show")
                            description: panel.diffError !== "" ? panel.diffError
                                       : panel.diffBinary ? qsTr("Its contents are not text.")
                                       : qsTr("One of its versions is larger than 2 MB.")
                        }
                    }
                }
            }
        }

        // ---- Errors and actions ---------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            visible: repo.isRepo
            color: Theme.borderSubtle
        }
        BText {
            objectName: "gitError"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space3
            Layout.topMargin: Theme.space2
            visible: repo.isRepo && repo.lastError !== ""
            text: repo.lastError
            color: Theme.dangerText
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
            maximumLineCount: 4
            elide: Text.ElideRight
        }
        Flow {
            objectName: "gitFooter"
            Layout.fillWidth: true
            Layout.margins: Theme.space2
            visible: repo.isRepo
            spacing: Theme.space2
            BButton {
                objectName: "gitCommit"
                text: qsTr("Commit…")
                variant: "primary"
                size: "small"
                iconName: "git-commit-horizontal"
                enabled: repo.changeCount > 0 && !repo.busy
                busy: repo.busyAction === "commit"
                onClicked: panel.openCommit()
            }
            BButton {
                objectName: "gitPush"
                text: repo.ahead > 0 ? qsTr("Push %1").arg(repo.ahead) : qsTr("Push")
                size: "small"
                iconName: "arrow-up"
                enabled: !repo.busy && !repo.detached && repo.hasCommits && (repo.ahead > 0 || repo.upstream === "")
                busy: repo.busyAction === "push"
                onClicked: repo.push()
            }
            BButton {
                objectName: "gitPull"
                text: repo.behind > 0 ? qsTr("Pull %1").arg(repo.behind) : qsTr("Pull")
                size: "small"
                iconName: "arrow-down"
                enabled: !repo.busy && repo.upstream !== ""
                busy: repo.busyAction === "pull"
                onClicked: repo.pull()
            }
            BButton {
                objectName: "gitCreatePr"
                visible: repo.ghAvailable
                text: qsTr("Create PR…")
                size: "small"
                iconName: "git-pull-request"
                enabled: !repo.busy && !repo.detached && repo.hasCommits
                busy: repo.busyAction === "pullRequest"
                onClicked: panel.openPullRequest()
            }
            BButton {
                objectName: "gitRevertTurn"
                visible: panel.checkpoint !== ""
                text: qsTr("Revert this turn…")
                size: "small"
                iconName: "undo-2"
                enabled: !repo.busy && repo.turnLoaded && repo.turnError === "" && repo.turnChanges.length > 0
                busy: repo.busyAction === "restore"
                onClicked: revertDialog.open()
            }
        }
    }

    BMenu {
        id: branchMenu
        objectName: "gitBranchMenu"
        Instantiator {
            model: repo.branchList
            // CodeGit.branchList: [{ name, remote, current, upstream, ahead, behind, gone, worktreePath }]
            delegate: BMenuItem {
                id: branchItem
                required property string name
                required property bool remote
                required property bool current
                required property string worktreePath
                required property int index
                objectName: "gitBranchItem_" + index
                text: branchItem.name
                iconName: branchItem.remote ? "cloud" : "git-branch"
                hint: branchItem.worktreePath !== "" && !branchItem.current ? qsTr("in a worktree") : ""
                checkable: true
                // (A Binding: a click toggles `checked`, which would end a plain binding.)
                Binding on checked {
                    value: branchItem.current
                }
                onTriggered: {
                    if (!branchItem.current)
                        repo.checkout(branchItem.name)
                }
            }
            onObjectAdded: (index, object) => branchMenu.insertItem(index, object as Item)
            onObjectRemoved: (index, object) => branchMenu.removeItem(object as Item)
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "gitNewBranch"
            text: qsTr("New branch…")
            iconName: "plus"
            onTriggered: panel.openNewBranch()
        }
    }

    BDialog {
        id: newBranchDialog
        objectName: "gitNewBranchDialog"
        title: qsTr("New branch")
        subtitle: qsTr("From the current commit; the changes in the working tree come along.")
        iconName: "git-branch"
        acceptText: qsTr("Create and switch")
        acceptEnabled: repo.isValidBranchName(branchField.typedText) && !repo.busy
        initialFocusItem: branchField
        onAccepted: repo.createBranch(branchField.text)
        BTextField {
            id: branchField
            objectName: "gitNewBranchName"
            Layout.fillWidth: true
            accessibleName: qsTr("Branch name")
            placeholderText: qsTr("e.g. fix/sync-conflict")
            errorText: branchField.typedText !== "" && !repo.isValidBranchName(branchField.typedText)
                       ? qsTr("A branch name has no spaces, no “..”, no backslash and none of ~ ^ : ? * [") : ""
            onAccepted: newBranchDialog.tryAccept()
        }
    }

    BConfirmDialog {
        id: revertDialog
        objectName: "gitRevertDialog"
        title: qsTr("Revert this turn?")
        subject: panel.checkpointLabel
        subjectIcon: "history"
        message: qsTr("The files go back to how they were before the turn: the %n file(s) it changed are put back, files it added are deleted.", "", repo.turnChanges.length)
        detail: qsTr("Commits, the staging area and ignored files stay as they are.")
        confirmText: qsTr("Revert")
        destructive: true
        onAccepted: repo.restore(panel.checkpoint)
    }
    GitCommitDialog {
        id: commitDialog
        git: repo
    }
    GitPullRequestDialog {
        id: prDialog
        git: repo
    }
}
