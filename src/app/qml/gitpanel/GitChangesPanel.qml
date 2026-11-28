pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// GitChangesPanel — the "Code" mode's changes panel (docs/CODE-GIT.md "The panel"), as T3 Code has
// it beside a thread. The header: the folder's branch (a menu to switch, make a new one, stash),
// its pull request (#12 open / draft / merged / closed; a click opens it), ahead / behind, the view
// options (side by side, wrapped lines, whitespace hidden, a tree of folders) and a refresh. Under
// it the source of the changes: "Latest turn" / "Turn N" (what an agent's turn alone changed:
// between its checkpoint and the next one), "Uncommitted" (the working tree against the last
// commit), "Branch" (the branch against a base branch, from the commit they share) and "Commits"
// (one of the newest 100). A banner while a merge, rebase or cherry-pick waits for its conflicts.
// The files (a list or a tree; a right click, a long press or "⋯": open, copy the path, stage,
// unstage, discard, take a side of a conflict) and a file's diff (GitDiffView: hunks staged,
// unstaged or thrown away, lines commented into the chat, find). Below: the one action that fits
// now (Commit… / Push / Pull / Create PR… / Publish… / Initialize git), the rest in a menu (a
// disabled entry says why), and for a turn its revert. Everything runs through CodeGit (C++,
// asynchronous); the panel calls nothing else in the app — what the owner does comes out as
// signals: actionFinished (toasts), openFile, addToChat, copyText, openUrl.
//
//   GitChangesPanel {
//       folder: thread.folder                                   // a repository's work tree or a worktree
//       checkpoints: Workspace.chats.checkpoints(thread.path)   // the turns' snapshots, oldest first
//       onActionFinished: (action, ok, message, url) => toast(message)
//       onAddToChat: (text) => chat.addToComposer(text)
//   }
Item {
    id: panel
    objectName: "gitChangesPanel"

    /// The folder (a work tree, or a folder inside one).
    property string folder
    /// The thread's checkpoints, oldest first: [{id, at, message}] (WorkspaceChats.checkpoints()).
    property list<var> checkpoints
    /// One checkpoint instead (with no `checkpoints`): the latest turn; `checkpointLabel` names it.
    property string checkpoint
    property string checkpointLabel
    /// A refresh every so many ms while shown (0: on git's own changes and after actions only).
    property int pollInterval: 0
    /// A quiet `git fetch` every so many ms (CodeGit.fetchInterval; 0 never).
    property int fetchInterval: 5 * 60 * 1000
    /// "turn" | "uncommitted" | "branch" | "commit" — the changes listed ("all" is "uncommitted").
    property string source: "turn"
    /// The turn shown: an index into `checkpoints` (-1 the latest).
    property int turnIndex: -1
    /// The base of "Branch" ("" the repository's default branch).
    property string baseRef
    /// The commit "Commits" shows ("" none picked yet), its first parent and subject.
    property string commitOid
    property string commitParent
    property string commitTitle
    /// Kept for the owner: "turn" | "all" (setMode()).
    readonly property string mode: panel.source === "uncommitted" ? "all" : panel.source

    // View options, kept per device.
    property bool splitView: AppSettings.value("code/diffSplit", false) === true
    property bool wrapLines: AppSettings.value("code/diffWrap", true) !== false
    property bool hideWhitespace: AppSettings.value("code/diffHideWhitespace", false) === true
    property bool tree: AppSettings.value("code/diffTree", false) === true
    property list<string> collapsedFolders

    /// The folder's git (CodeGit) — for the owner and the self-tests.
    readonly property CodeGit git: repo
    readonly property list<var> noRows: []
    readonly property list<var> turns: repo.turnRows(panel.turnCheckpoints())
    readonly property bool hasTurns: panel.turns.length > 0
    /// The turn shown, newest first in `turns` (0 the latest).
    readonly property int turnRow: panel.turnRowOf(panel.turnIndex)
    readonly property bool turnMode: panel.source === "turn" && panel.hasTurns
    readonly property string turnLabel: panel.turnMode ? UiModels.stringField(panel.turns[panel.turnRow], "label") : ""
    readonly property bool latestTurn: panel.turnMode && panel.turnRow === 0
    /// The turn shown: its checkpoint's id, ref and the next one's ("" the work tree).
    readonly property string turnId: panel.turnMode ? UiModels.stringField(panel.turns[panel.turnRow], "id") : ""
    readonly property string turnBase: panel.turnMode ? UiModels.stringField(panel.turns[panel.turnRow], "base") : ""
    readonly property string turnTarget: panel.turnMode ? UiModels.stringField(panel.turns[panel.turnRow], "target") : ""
    readonly property bool uncommitted: panel.source === "uncommitted" || (panel.source === "turn" && !panel.hasTurns)
    readonly property list<var> sourceRows: panel.uncommitted ? repo.changes : repo.compareChanges
    readonly property list<var> rows: panel.tree ? repo.treeRows(panel.sourceRows, panel.collapsedFolders) : panel.sourceRows
    readonly property list<string> rowPaths: UiModels.stringFields(panel.sourceRows, "path")
    /// Files the list does not show (beyond CodeGit.maxRows).
    readonly property int hiddenCount: panel.uncommitted ? Math.max(0, repo.changeCount - panel.sourceRows.length) : 0
    readonly property bool wide: panel.width >= 760
    readonly property bool showDiff: panel.selectedPath !== ""
    readonly property string sourceText: panel.turnMode ? (panel.latestTurn ? qsTr("Latest turn") : panel.turnLabel)
                                       : panel.source === "branch" ? qsTr("Branch")
                                       : panel.source === "commit" ? qsTr("Commits")
                                       : qsTr("Uncommitted")
    readonly property string baseShown: panel.baseRef !== "" ? panel.baseRef : repo.defaultBranchRef
    readonly property string prState: UiModels.stringField(repo.pullRequest, "state")
    readonly property int prNumber: UiModels.numberField(repo.pullRequest, "number", 0)

    /// The file whose diff is shown ("" none), what is known of it and its texts.
    property string selectedPath
    property string selectedOldPath
    property string selectedStatus
    property bool selectedStaged: false
    property bool selectedUnstaged: false
    property bool selectedBinary: false
    property string diffOld
    property string diffNew
    property bool diffBinary: false
    property bool diffTooLarge: false
    property bool diffLoading: false
    property string diffError

    /// The file of the open file menu.
    property string menuPath
    property string menuOldPath
    property string menuStatus
    property bool menuStaged: false
    property bool menuUnstaged: false
    /// A hunk waiting for its "Discard" to be confirmed.
    property int discardFirst: 0
    property int discardLast: 0
    /// The discard asked for is of rows' lines (discardFirst … discardLast rows), not a hunk's.
    property bool discardLines: false
    /// A commit or a push waiting for the default branch's question: "commit" | "push" ("" none).
    property string pendingAction
    property string pendingMessage
    property list<string> pendingPaths
    property bool pendingPush: false
    property bool pendingPr: false
    property string pendingPrTitle
    property string pendingPrBody
    property bool pendingDraft: false
    property string pendingPrBase
    /// The stash waiting for "Drop" to be confirmed.
    property string dropOid

    /// An action ended (CodeGit.actionFinished): for a toast — `url` a pull request's address.
    signal actionFinished(string action, bool ok, string message, string url)
    /// Open a file of the work tree (an absolute path) in the editor, at `line` (0: anywhere).
    signal openFile(string path, int line)
    /// Text for the chat's composer (review comments on lines of a diff).
    signal addToChat(string text)
    /// Put `text` on the clipboard (a file's path).
    signal copyText(string text)
    /// Open a web address (the pull request).
    signal openUrl(string url)
    /// The AI can write a commit message and a pull request's text (the owner's AiAsk is set up):
    /// the commit and pull request dialogs offer "Write with AI" (and the commit one writes an empty
    /// message itself); their question goes out by writeWithAi(system, prompt), the answer comes
    /// back by aiWritten(ok, text) to the dialog that asked (only one is open, the other ignores it).
    property bool aiAvailable: false
    signal writeWithAi(string system, string prompt)
    function aiWritten(ok: bool, text: string): void {
        commitDialog.aiWritten(ok, text)
        prDialog.aiWritten(ok, text)
    }

    function turnCheckpoints(): list<var> {
        if (panel.checkpoints.length > 0)
            return panel.checkpoints
        if (panel.checkpoint === "")
            return []
        return [{ id: panel.checkpoint, at: "", message: 0 }]
    }
    /// The row in `turns` of checkpoint `index` (-1: the latest, row 0).
    function turnRowOf(index: int): int {
        if (index < 0)
            return 0
        for (let i = 0; i < panel.turns.length; ++i) {
            if (UiModels.numberField(panel.turns[i], "index", -1) === index)
                return i
        }
        return 0
    }
    function one(text: string): list<string> {
        return [text]
    }
    function absolutePath(path: string): string {
        return repo.topLevel !== "" ? repo.topLevel + "/" + path : path
    }

    // ---- The source --------------------------------------------------------------------------------

    function setSource(value: string): void {
        panel.source = value === "all" ? "uncommitted" : value
        panel.closeDiff()
        if (panel.source === "commit")
            repo.commits()
        if (panel.source === "branch")
            repo.branches()
        apply.restart()
    }
    /// Kept for the owner: "turn" | "all".
    function setMode(value: string): void {
        panel.setSource(value)
    }
    function showTurn(index: int): void {
        panel.turnIndex = index
        panel.setSource("turn")
    }
    function setBase(ref: string): void {
        panel.baseRef = ref
        panel.closeDiff()
        apply.restart()
    }
    function pickCommit(oid: string, parent: string, title: string): void {
        panel.commitOid = oid
        panel.commitParent = parent
        panel.commitTitle = title
        panel.closeDiff()
        apply.restart()
    }
    /// CodeGit.compare() for the source chosen.
    function applySource(): void {
        if (panel.turnMode) {
            repo.compare(true, panel.turnBase, panel.turnTarget, false)
        } else if (panel.source === "branch" && panel.baseShown !== "") {
            repo.compare(true, panel.baseShown, "", true)
        } else if (panel.source === "commit" && panel.commitOid !== "") {
            repo.compare(true, panel.commitParent, panel.commitOid, false)
        } else {
            repo.compare(false, "", "", false)
        }
    }

    // ---- The diff ----------------------------------------------------------------------------------

    function openDiff(path: string, oldPath: string): void {
        panel.selectedPath = path
        panel.selectedOldPath = oldPath
        panel.readSelected()
        panel.diffOld = ""
        panel.diffNew = ""
        panel.diffBinary = false
        panel.diffTooLarge = false
        panel.diffError = ""
        panel.diffLoading = true
        panel.requestDiff()
    }
    function requestDiff(): void {
        if (panel.uncommitted)
            repo.requestDiff(panel.selectedPath, "")
        else
            repo.requestDiffBetween(panel.selectedPath, panel.selectedOldPath, repo.comparisonBase, repo.compareTarget)
    }
    /// What the rows say of the file shown (staged, its status …).
    function readSelected(): void {
        selectedRow.map = repo.rowOf(panel.sourceRows, panel.selectedPath)
        panel.selectedStatus = UiModels.stringField(selectedRow.map, "status")
        panel.selectedStaged = UiModels.boolField(selectedRow.map, "staged")
        panel.selectedUnstaged = UiModels.boolField(selectedRow.map, "unstaged")
        panel.selectedBinary = UiModels.boolField(selectedRow.map, "binary")
    }
    function closeDiff(): void {
        panel.selectedPath = ""
        panel.selectedOldPath = ""
        panel.diffOld = ""
        panel.diffNew = ""
        panel.diffLoading = false
        panel.diffError = ""
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
        panel.readSelected()
        panel.requestDiff()
    }
    function onLines(action: string, from: int, to: int): void {
        if (action === "stage") {
            repo.stageLines(panel.selectedPath, diffView.rows, from, to)
            diffView.clearSelection()
            return
        }
        panel.discardFirst = from
        panel.discardLast = to
        panel.discardLines = true
        discardDialog.openFor(panel.selectedPath, true)
    }
    function onHunk(action: string, first: int, last: int): void {
        panel.discardLines = false
        if (action === "stage") {
            repo.stageHunk(panel.selectedPath, first, last)
        } else if (action === "unstage") {
            repo.unstageHunk(panel.selectedPath, first, last)
        } else if (action === "revert") {
            panel.discardFirst = first
            panel.discardLast = last
            hunkRevertDialog.open()
        } else {
            panel.discardFirst = first
            panel.discardLast = last
            discardDialog.openFor(panel.selectedPath, true)
        }
    }

    // ---- The list ----------------------------------------------------------------------------------

    function toggleFolder(path: string): void {
        if (panel.collapsedFolders.indexOf(path) >= 0)
            panel.collapsedFolders = Js.withoutString(panel.collapsedFolders, path)
        else
            panel.collapsedFolders = Js.appendedString(panel.collapsedFolders, path)
    }
    function openFileMenu(target: Item, x: real, y: real): void {
        const row = target as GitChangeRow
        if (row === null)
            return
        panel.menuPath = row.path
        panel.menuOldPath = row.oldPath
        panel.menuStatus = row.status
        panel.menuStaged = row.staged
        panel.menuUnstaged = row.unstaged
        fileMenu.popup(target, x, y)
    }
    function openSelectedFileMenu(target: Item): void {
        panel.menuPath = panel.selectedPath
        panel.menuOldPath = panel.selectedOldPath
        panel.menuStatus = panel.selectedStatus
        panel.menuStaged = panel.selectedStaged
        panel.menuUnstaged = panel.selectedUnstaged
        fileMenu.popup(target, 0, target.height)
    }

    // ---- View options ------------------------------------------------------------------------------

    function setSplit(on: bool): void {
        panel.splitView = on
        AppSettings.setValue("code/diffSplit", on)
    }
    function setWrap(on: bool): void {
        panel.wrapLines = on
        AppSettings.setValue("code/diffWrap", on)
    }
    function setHideWhitespace(on: bool): void {
        panel.hideWhitespace = on
        AppSettings.setValue("code/diffHideWhitespace", on)
    }
    function setTree(on: bool): void {
        panel.tree = on
        AppSettings.setValue("code/diffTree", on)
    }

    // ---- Actions -----------------------------------------------------------------------------------

    /// An entry of the actions' menu: run it, or say why not.
    function run(action: string): void {
        const why = repo.unavailableReason(action)
        if (why !== "") {
            panel.actionFinished(action, false, why, "")
            return
        }
        if (action === "commit")
            panel.openCommit()
        else if (action === "push")
            panel.pushGuarded()
        else if (action === "pull")
            repo.pull()
        else if (action === "fetch")
            repo.fetch()
        else if (action === "createPr")
            panel.openPullRequest()
        else if (action === "openPr")
            panel.openUrl(UiModels.stringField(repo.pullRequest, "url"))
        else if (action === "publish")
            publishDialog.openFor()
        else if (action === "init")
            repo.initRepository()
        else if (action === "stash")
            stashDialog.openFor()
    }
    function openCommit(): void {
        commitDialog.openFor()
    }
    function openPullRequest(): void {
        prDialog.openFor()
    }
    function openBranchMenu(): void {
        repo.branches()
        repo.stashes()
        branchMenu.popup(branchButton, 0, branchButton.height)
    }
    function openNewBranch(): void {
        branchField.text = ""
        newBranchDialog.open()
    }
    function pushGuarded(): void {
        if (repo.onDefaultBranch) {
            panel.pendingAction = "push"
            guardDialog.openFor("")
            return
        }
        repo.push()
    }
    /// GitCommitDialog's request: committed now, or after the default branch's question.
    function commitRequested(message: string, paths: list<string>, newBranch: string, push: bool, createPr: bool,
                             prTitle: string, prBody: string, draft: bool, prBase: string): void {
        if ((push || createPr) && newBranch === "" && repo.onDefaultBranch) {
            panel.pendingAction = "commit"
            panel.pendingMessage = message
            panel.pendingPaths = paths
            panel.pendingPush = push
            panel.pendingPr = createPr
            panel.pendingPrTitle = prTitle
            panel.pendingPrBody = prBody
            panel.pendingDraft = draft
            panel.pendingPrBase = prBase
            guardDialog.openFor(repo.branchNameFor(message))
            return
        }
        repo.commitFiles(message, paths, newBranch, push, createPr, prTitle, prBody, draft, prBase)
    }
    /// The default branch's answer: "continue" or "branch" (with `name`).
    function guardAnswered(answer: string, name: string): void {
        const branch = answer === "branch" ? name : ""
        if (panel.pendingAction === "commit") {
            repo.commitFiles(panel.pendingMessage, panel.pendingPaths, branch, panel.pendingPush, panel.pendingPr,
                             panel.pendingPrTitle, panel.pendingPrBody, panel.pendingDraft, panel.pendingPrBase)
        } else if (panel.pendingAction === "push") {
            if (branch !== "")
                repo.createBranch(branch)
            repo.push()
        }
        panel.pendingAction = ""
    }
    function revertTurn(): void {
        if (!panel.turnMode)
            return
        // (A turn that ends at the working tree — the latest, without a snapshot where it ended: back
        // to its checkpoint; else its own changes undone, the user's later ones kept.)
        if (panel.turnTarget === "")
            repo.restore(panel.turnId)
        else
            repo.revertBetween(panel.turnBase, panel.turnTarget)
    }
    function revertToTurn(): void {
        if (panel.turnMode)
            repo.restore(panel.turnId)
    }
    /// Why `action` cannot run now ("" it can) — `revision` and `busy` only make a binding ask again.
    function why(action: string, revision: int, busy: bool): string {
        return repo.unavailableReason(action)
    }
    function primaryText(action: string): string {
        switch (action) {
        case "commit":
            return qsTr("Commit…")
        case "push":
            return repo.ahead > 0 ? qsTr("Push %1").arg(repo.ahead) : qsTr("Push")
        case "pull":
            return qsTr("Pull %1").arg(repo.behind)
        case "createPr":
            return qsTr("Create PR…")
        case "openPr":
            return qsTr("Open PR #%1").arg(panel.prNumber)
        case "publish":
            return qsTr("Publish…")
        case "init":
            return qsTr("Initialize git")
        }
        return qsTr("Up to date")
    }
    function primaryIcon(action: string): string {
        switch (action) {
        case "commit":
            return "git-commit-horizontal"
        case "push":
            return "arrow-up"
        case "pull":
            return "arrow-down"
        case "createPr":
        case "openPr":
            return "git-pull-request"
        case "publish":
            return "upload"
        case "init":
            return "git-branch"
        }
        return "check"
    }
    function operationTitle(operation: string): string {
        switch (operation) {
        case "merge":
            return qsTr("A merge is in progress")
        case "rebase":
            return qsTr("A rebase is in progress")
        case "cherryPick":
            return qsTr("A cherry-pick is in progress")
        case "revert":
            return qsTr("A revert is in progress")
        case "apply":
            return qsTr("Patches are being applied")
        }
        return ""
    }

    onFolderChanged: {
        panel.closeDiff()
        panel.turnIndex = -1
        panel.commitOid = ""
        panel.baseRef = ""
        apply.restart()
    }
    onCheckpointsChanged: apply.restart()
    onCheckpointChanged: apply.restart()
    // ("turn" with no turns yet shows Uncommitted: the first turn's changes show by themselves.)
    Component.onCompleted: apply.restart()

    CodeGit {
        id: repo
        folder: panel.folder
        pollInterval: panel.pollInterval
        // (What the remote has, every five minutes: ahead / behind and Pull follow it.)
        fetchInterval: panel.fetchInterval
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
        onStateChanged: {
            // (The default branch came: "Branch" can compare now.)
            if (panel.source === "branch" && panel.baseRef === "")
                apply.restart()
        }
    }
    MapHolder {
        id: selectedRow
    }
    Later {
        id: follow
        onTriggered: panel.followChanges()
    }
    Later {
        id: apply
        onTriggered: panel.applySource()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Branch, pull request, ahead / behind, options, refresh ---------------------------
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
                Layout.maximumWidth: Math.max(Theme.space16 * 2, panel.width - 200)
                variant: "ghost"
                size: "small"
                iconName: "git-branch"
                text: repo.detached ? qsTr("Detached HEAD") : repo.branch !== "" ? repo.branch : qsTr("No branch")
                Accessible.description: qsTr("Switch to another branch, make a new one or stash the changes")
                onClicked: panel.openBranchMenu()
            }
            BChip {
                objectName: "gitPrBadge"
                visible: panel.prNumber > 0
                size: "small"
                interactive: true
                variant: panel.prState === "merged" ? "tag" : panel.prState === "closed" ? "overdue"
                       : panel.prState === "draft" ? "neutral" : "done"
                iconName: panel.prState === "merged" ? "git-merge" : panel.prState === "closed" ? "circle-x" : "git-pull-request"
                text: panel.prState === "draft" ? qsTr("#%1 draft").arg(panel.prNumber)
                    : panel.prState === "merged" ? qsTr("#%1 merged").arg(panel.prNumber)
                    : panel.prState === "closed" ? qsTr("#%1 closed").arg(panel.prNumber)
                    : qsTr("#%1 open").arg(panel.prNumber)
                Accessible.description: UiModels.stringField(repo.pullRequest, "title")
                onClicked: panel.openUrl(UiModels.stringField(repo.pullRequest, "url"))
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
                text: repo.hasRemote ? qsTr("not pushed") : qsTr("no remote")
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
                id: optionsButton
                objectName: "gitViewOptions"
                iconName: "sliders-horizontal"
                label: qsTr("View options")
                size: "small"
                onClicked: optionsMenu.popup(optionsButton, 0, optionsButton.height)
            }
            BIconButton {
                objectName: "gitRefresh"
                iconName: "refresh-cw"
                label: qsTr("Refresh")
                size: "small"
                onClicked: {
                    repo.refresh()
                    repo.refreshPullRequest()
                }
            }
        }

        // ---- The source: a turn, uncommitted, the branch, a commit ------------------------------
        RowLayout {
            objectName: "gitSourceRow"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space2
            Layout.rightMargin: Theme.space2
            Layout.bottomMargin: Theme.space2
            visible: repo.isRepo
            spacing: Theme.space1
            BButton {
                id: sourceButton
                objectName: "gitSource"
                size: "small"
                iconName: "chevron-down"
                iconRight: true
                text: panel.sourceText
                Accessible.description: qsTr("Which changes to show")
                onClicked: sourceMenu.popup(sourceButton, 0, sourceButton.height)
            }
            BButton {
                id: baseButton
                objectName: "gitBaseButton"
                visible: panel.source === "branch"
                Layout.minimumWidth: 0
                Layout.maximumWidth: Math.max(Theme.space16 * 2, panel.width - sourceButton.width - 40)
                size: "small"
                variant: "ghost"
                iconName: "chevron-down"
                iconRight: true
                text: panel.baseShown !== "" ? qsTr("against %1").arg(panel.baseShown) : qsTr("Choose a base")
                onClicked: {
                    repo.branches()
                    baseMenu.popup(baseButton, 0, baseButton.height)
                }
            }
            BButton {
                id: commitPickButton
                objectName: "gitCommitPick"
                visible: panel.source === "commit"
                Layout.minimumWidth: 0
                Layout.maximumWidth: Math.max(Theme.space16 * 2, panel.width - sourceButton.width - 40)
                size: "small"
                variant: "ghost"
                iconName: "chevron-down"
                iconRight: true
                text: panel.commitOid !== "" ? panel.commitTitle : qsTr("Choose a commit")
                onClicked: {
                    repo.commits()
                    commitMenu.popup(commitPickButton, 0, commitPickButton.height)
                }
            }
            BText {
                objectName: "gitTurnLabel"
                Layout.fillWidth: true
                visible: panel.turnMode
                text: panel.checkpoints.length === 0 && panel.checkpointLabel !== "" ? panel.checkpointLabel
                    : UiModels.stringField(panel.turns[panel.turnRow], "at")
                variant: "faint"
                font.pixelSize: Theme.textSm
                elide: Text.ElideRight
            }
            Item {
                Layout.fillWidth: true
                visible: !panel.turnMode
            }
        }

        // ---- A merge, rebase … in progress --------------------------------------------------------
        Rectangle {
            objectName: "gitOperationBanner"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space2
            Layout.rightMargin: Theme.space2
            Layout.bottomMargin: Theme.space2
            visible: repo.isRepo && repo.operation !== ""
            implicitHeight: bannerColumn.implicitHeight + 2 * Theme.space2
            radius: Theme.radius
            color: Theme.warningBg
            ColumnLayout {
                id: bannerColumn
                anchors.fill: parent
                anchors.margins: Theme.space2
                spacing: Theme.space1
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BIcon {
                        name: "git-merge"
                        size: Theme.iconSize
                        color: Theme.warningText
                    }
                    BText {
                        objectName: "gitOperationTitle"
                        Layout.fillWidth: true
                        text: panel.operationTitle(repo.operation)
                        variant: "label"
                        color: Theme.warningText
                        wrapMode: Text.Wrap
                    }
                }
                BText {
                    objectName: "gitOperationHint"
                    Layout.fillWidth: true
                    text: repo.conflictCount > 0
                          ? qsTr("%n file(s) with conflicts: take a side, or edit them and mark them resolved.", "", repo.conflictCount)
                          : qsTr("No conflicts left: continue, or abort to go back.")
                    variant: "small"
                    wrapMode: Text.Wrap
                }
                Repeater {
                    model: repo.conflictPaths
                    delegate: RowLayout {
                        id: conflictRow
                        required property string modelData
                        required property int index
                        objectName: "gitConflict_" + index
                        Layout.fillWidth: true
                        spacing: Theme.space1
                        BText {
                            Layout.fillWidth: true
                            text: conflictRow.modelData
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.textSm
                            elide: Text.ElideMiddle
                        }
                        BButton {
                            objectName: "gitConflictOurs"
                            text: qsTr("Ours")
                            size: "small"
                            variant: "ghost"
                            enabled: !repo.busy
                            onClicked: repo.resolve(conflictRow.modelData, "ours")
                        }
                        BButton {
                            objectName: "gitConflictTheirs"
                            text: qsTr("Theirs")
                            size: "small"
                            variant: "ghost"
                            enabled: !repo.busy
                            onClicked: repo.resolve(conflictRow.modelData, "theirs")
                        }
                        BButton {
                            objectName: "gitConflictResolved"
                            text: qsTr("Resolved")
                            size: "small"
                            variant: "ghost"
                            enabled: !repo.busy
                            onClicked: repo.resolve(conflictRow.modelData, "resolved")
                        }
                        BIconButton {
                            objectName: "gitConflictOpen"
                            iconName: "square-pen"
                            label: qsTr("Open in the editor")
                            size: "small"
                            onClicked: panel.openFile(panel.absolutePath(conflictRow.modelData), 0)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    Item {
                        Layout.fillWidth: true
                    }
                    BButton {
                        objectName: "gitAbort"
                        text: qsTr("Abort…")
                        size: "small"
                        enabled: !repo.busy
                        busy: repo.busyAction === "abort"
                        onClicked: abortDialog.open()
                    }
                    // The commit that stopped it left out (`--skip`): a merge has none.
                    BButton {
                        objectName: "gitSkip"
                        visible: repo.operation !== "merge"
                        text: qsTr("Skip this commit")
                        size: "small"
                        enabled: !repo.busy
                        busy: repo.busyAction === "skip"
                        onClicked: repo.skipOperation()
                    }
                    BButton {
                        objectName: "gitContinue"
                        text: repo.operation === "merge" ? qsTr("Commit the merge") : qsTr("Continue")
                        variant: "primary"
                        size: "small"
                        enabled: !repo.busy && repo.conflictCount === 0
                        busy: repo.busyAction === "continue"
                        onClicked: repo.continueOperation()
                    }
                }
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
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space4, 360)
                visible: (repo.loaded || panel.folder === "") && !repo.isRepo
                spacing: Theme.space3
                BEmptyState {
                    objectName: "gitNotRepo"
                    Layout.fillWidth: true
                    compact: true
                    iconName: "folder-x"
                    title: panel.folder === "" ? qsTr("No folder") : qsTr("Not a git repository")
                    description: panel.folder === "" ? qsTr("Open a project to see its changes here.")
                               : repo.gitAvailable ? qsTr("This folder has no git repository yet: initialize one to follow its changes, commit and push them.")
                               : qsTr("git is not installed: install it to see and commit the changes of your projects.")
                }
                BButton {
                    objectName: "gitInitButton"
                    Layout.alignment: Qt.AlignHCenter
                    visible: panel.folder !== "" && repo.gitAvailable
                    text: qsTr("Initialize git")
                    iconName: "git-branch"
                    variant: "primary"
                    busy: repo.busyAction === "init"
                    onClicked: repo.initRepository()
                }
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
                    Accessible.role: panel.tree ? Accessible.Tree : Accessible.List
                    Accessible.name: qsTr("Changed files")
                    delegate: GitChangeRow {
                        width: ListView.view.width
                        selected: path === panel.selectedPath
                        showDir: !panel.tree
                        showStaged: panel.uncommitted
                        onActivated: (path, oldPath) => panel.openDiff(path, oldPath)
                        onFolderToggled: (path) => panel.toggleFolder(path)
                        onMenuRequested: (target, x, y) => panel.openFileMenu(target, x, y)
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
                    visible: list.count === 0 && (panel.uncommitted ? repo.loaded
                                                  : repo.compareActive && repo.compareLoaded && repo.compareError === "")
                    compact: true
                    iconName: "circle-check"
                    title: panel.turnMode ? qsTr("No changes in this turn")
                         : panel.source === "branch" ? qsTr("No changes on this branch")
                         : panel.source === "commit" ? qsTr("The commit changes no file")
                         : qsTr("No changes")
                    description: panel.turnMode ? qsTr("The files are as they were before the turn.")
                               : panel.source === "branch" ? qsTr("The branch has nothing that %1 does not have.").arg(panel.baseShown)
                               : panel.source === "commit" ? ""
                               : qsTr("The working tree matches the last commit.")
                }
                BEmptyState {
                    objectName: "gitPickCommit"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.space4, maximumWidth)
                    visible: panel.source === "commit" && panel.commitOid === ""
                    compact: true
                    iconName: "git-commit-horizontal"
                    title: qsTr("Choose a commit")
                    description: qsTr("Its own changes are shown: against its parent, the first commit against nothing.")
                }
                BEmptyState {
                    objectName: "gitTurnError"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.space4, maximumWidth)
                    visible: !panel.uncommitted && repo.compareActive && repo.compareLoaded && repo.compareError !== ""
                    compact: true
                    iconName: "history"
                    title: panel.turnMode ? qsTr("This turn's changes cannot be shown") : qsTr("These changes cannot be shown")
                    description: repo.compareError
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
                            id: diffMore
                            objectName: "gitDiffMore"
                            iconName: "ellipsis"
                            label: qsTr("Actions for this file")
                            size: "small"
                            onClicked: panel.openSelectedFileMenu(diffMore)
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
                        GitDiffView {
                            id: diffView
                            anchors.fill: parent
                            visible: !panel.diffLoading && panel.diffError === "" && !panel.diffBinary && !panel.diffTooLarge
                            git: repo
                            path: panel.selectedPath
                            oldText: panel.diffOld
                            newText: panel.diffNew
                            split: panel.splitView && panel.wide
                            wrap: panel.wrapLines
                            ignoreWhitespace: panel.hideWhitespace
                            readonly property bool hunks: panel.uncommitted && panel.selectedStatus !== "untracked"
                                                          && panel.selectedStatus !== "conflicted" && !panel.selectedBinary
                            canStage: diffView.hunks && panel.selectedUnstaged
                            canUnstage: diffView.hunks && panel.selectedStaged
                            canDiscard: diffView.hunks && panel.selectedUnstaged
                            // (Lines by their numbers: only while nothing of the file is staged.)
                            canStageLines: diffView.hunks && panel.selectedUnstaged && !panel.selectedStaged
                            canDiscardLines: diffView.hunks && panel.selectedUnstaged && !panel.selectedStaged
                            // (Another source's hunk of a changed file: taken out of the files alone.)
                            canRevert: !panel.uncommitted && repo.compareLoaded && panel.selectedStatus === "modified"
                                       && !panel.selectedBinary
                            onHunkRequested: (action, first, last) => panel.onHunk(action, first, last)
                            onLinesRequested: (action, from, to) => panel.onLines(action, from, to)
                            onCommented: (text) => panel.addToChat(text)
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
                id: primaryButton
                objectName: "gitPrimary"
                // (A merge or rebase in progress: the banner has its actions.)
                visible: repo.operation === ""
                readonly property string chosen: repo.primaryAction
                text: panel.primaryText(primaryButton.chosen)
                variant: primaryButton.chosen !== "" ? "primary" : "secondary"
                size: "small"
                iconName: panel.primaryIcon(primaryButton.chosen)
                enabled: primaryButton.chosen !== "" && !repo.busy
                busy: repo.busy && repo.busyAction !== "checkpoint"
                onClicked: panel.run(primaryButton.chosen)
            }
            BIconButton {
                id: moreActions
                objectName: "gitMoreActions"
                variant: "secondary"
                iconName: "chevron-down"
                label: qsTr("More git actions")
                size: "small"
                onClicked: actionsMenu.popup(moreActions, 0, moreActions.height)
            }
            BButton {
                objectName: "gitRevertTurn"
                visible: panel.turnMode
                text: qsTr("Revert this turn…")
                size: "small"
                iconName: "undo-2"
                enabled: !repo.busy && repo.compareLoaded && repo.compareError === "" && repo.compareChanges.length > 0
                busy: repo.busyAction === "restore" || repo.busyAction === "revertTurn"
                onClicked: revertDialog.openFor(false)
            }
            BButton {
                objectName: "gitRevertTo"
                // (The latest turn's "Revert this turn" puts back its checkpoint, unless it ended at a
                // snapshot of its own: then this does.)
                visible: panel.turnMode && (!panel.latestTurn || panel.turnTarget !== "")
                text: qsTr("Revert to before %1…").arg(panel.turnLabel)
                size: "small"
                variant: "ghost"
                iconName: "history"
                enabled: !repo.busy
                onClicked: revertDialog.openFor(true)
            }
        }
    }

    // ---- Menus --------------------------------------------------------------------------------------

    BMenu {
        id: sourceMenu
        objectName: "gitSourceMenu"
        Instantiator {
            model: panel.turns
            // CodeGit.turnRows: [{index, id, number, label, at, latest, base, target}], the newest first.
            delegate: BMenuItem {
                id: turnItem
                required property int index
                required property string label
                required property string at
                required property bool latest
                readonly property int checkpointIndex: UiModels.numberField(panel.turns[turnItem.index], "index", -1)
                objectName: "gitSource_turn_" + turnItem.index
                text: turnItem.latest ? qsTr("Latest turn") : turnItem.label
                hint: turnItem.at
                iconName: "history"
                checkable: true
                Binding on checked {
                    value: panel.turnMode && panel.turnRow === turnItem.index
                }
                onTriggered: panel.showTurn(turnItem.latest ? -1 : turnItem.checkpointIndex)
            }
            onObjectAdded: (index, object) => sourceMenu.insertItem(index, object as Item)
            onObjectRemoved: (index, object) => sourceMenu.removeItem(object as Item)
        }
        BMenuSeparator {
            visible: panel.hasTurns
        }
        BMenuItem {
            objectName: "gitSource_uncommitted"
            text: qsTr("Uncommitted")
            hint: qsTr("against the last commit")
            iconName: "git-compare"
            checkable: true
            Binding on checked {
                value: panel.uncommitted && !panel.turnMode
            }
            onTriggered: panel.setSource("uncommitted")
        }
        BMenuItem {
            objectName: "gitSource_branch"
            text: qsTr("Branch")
            hint: repo.defaultBranchRef !== "" ? qsTr("against %1").arg(panel.baseShown) : ""
            iconName: "git-branch"
            checkable: true
            Binding on checked {
                value: panel.source === "branch"
            }
            onTriggered: panel.setSource("branch")
        }
        BMenuItem {
            objectName: "gitSource_commits"
            text: qsTr("Commits")
            hint: qsTr("one of the newest 100")
            iconName: "git-commit-horizontal"
            checkable: true
            Binding on checked {
                value: panel.source === "commit"
            }
            onTriggered: panel.setSource("commit")
        }
    }

    BMenu {
        id: baseMenu
        objectName: "gitBaseMenu"
        Instantiator {
            model: repo.branchList
            delegate: BMenuItem {
                id: baseItem
                required property string name
                required property bool remote
                required property bool current
                required property int index
                objectName: "gitBase_" + baseItem.index
                visible: !baseItem.current
                height: visible ? implicitHeight : 0
                text: baseItem.name
                iconName: baseItem.remote ? "cloud" : "git-branch"
                hint: baseItem.name === repo.defaultBranch || baseItem.name === repo.defaultBranchRef ? qsTr("default") : ""
                checkable: true
                Binding on checked {
                    value: baseItem.name === panel.baseShown
                }
                onTriggered: panel.setBase(baseItem.name)
            }
            onObjectAdded: (index, object) => baseMenu.insertItem(index, object as Item)
            onObjectRemoved: (index, object) => baseMenu.removeItem(object as Item)
        }
    }

    BMenu {
        id: commitMenu
        objectName: "gitCommitMenu"
        Instantiator {
            model: repo.commitList
            // CodeGit.commitList: [{oid, shortOid, subject, author, time, when, parentOid}]
            delegate: BMenuItem {
                id: commitItem
                required property string oid
                required property string shortOid
                required property string subject
                required property string author
                required property string when
                required property string parentOid
                required property int index
                objectName: "gitCommitItem_" + commitItem.index
                text: commitItem.subject
                hint: commitItem.shortOid + " · " + commitItem.when
                checkable: true
                Accessible.description: commitItem.author
                Binding on checked {
                    value: commitItem.oid === panel.commitOid
                }
                onTriggered: panel.pickCommit(commitItem.oid, commitItem.parentOid, commitItem.subject)
            }
            onObjectAdded: (index, object) => commitMenu.insertItem(index, object as Item)
            onObjectRemoved: (index, object) => commitMenu.removeItem(object as Item)
        }
    }

    BMenu {
        id: optionsMenu
        objectName: "gitOptionsMenu"
        BMenuItem {
            objectName: "gitOptSplit"
            text: qsTr("Side by side")
            iconName: "columns-2"
            checkable: true
            unavailable: panel.wide ? "" : qsTr("The panel is too narrow: make it wider")
            Binding on checked {
                value: panel.splitView
            }
            onTriggered: panel.setSplit(!panel.splitView)
        }
        BMenuItem {
            objectName: "gitOptWrap"
            text: qsTr("Wrap long lines")
            iconName: "list-indent-increase"
            checkable: true
            Binding on checked {
                value: panel.wrapLines
            }
            onTriggered: panel.setWrap(!panel.wrapLines)
        }
        BMenuItem {
            objectName: "gitOptWhitespace"
            text: qsTr("Hide whitespace changes")
            iconName: "eye-off"
            checkable: true
            Binding on checked {
                value: panel.hideWhitespace
            }
            onTriggered: panel.setHideWhitespace(!panel.hideWhitespace)
        }
        BMenuItem {
            objectName: "gitOptTree"
            text: qsTr("Files as a tree")
            iconName: "folder-tree"
            checkable: true
            Binding on checked {
                value: panel.tree
            }
            onTriggered: panel.setTree(!panel.tree)
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "gitOptFind"
            text: qsTr("Find in the diff")
            iconName: "search"
            shortcut: "Ctrl+F"
            unavailable: panel.showDiff ? "" : qsTr("Open a file's diff first")
            onTriggered: {
                if (panel.showDiff)
                    diffView.openFind()
            }
        }
    }

    BMenu {
        id: fileMenu
        objectName: "gitFileMenu"
        readonly property bool conflicted: panel.menuStatus === "conflicted"
        readonly property bool here: panel.uncommitted
        BMenuItem {
            objectName: "gitFileOpen"
            text: qsTr("Open in the editor")
            iconName: "square-pen"
            unavailable: panel.menuStatus === "deleted" && fileMenu.here ? qsTr("The file is deleted") : ""
            onTriggered: panel.openFile(panel.absolutePath(panel.menuPath), 0)
        }
        BMenuItem {
            objectName: "gitFileCopyPath"
            text: qsTr("Copy the path")
            iconName: "copy"
            onTriggered: panel.copyText(panel.menuPath)
        }
        BMenuSeparator {
            visible: fileMenu.here
        }
        BMenuItem {
            objectName: "gitFileStage"
            visible: fileMenu.here && panel.menuUnstaged && !fileMenu.conflicted
            height: visible ? implicitHeight : 0
            text: qsTr("Stage")
            iconName: "plus"
            onTriggered: repo.stage(panel.menuOldPath !== "" ? Js.appendedString(panel.one(panel.menuPath), panel.menuOldPath)
                                                             : panel.one(panel.menuPath))
        }
        BMenuItem {
            objectName: "gitFileUnstage"
            visible: fileMenu.here && panel.menuStaged && !fileMenu.conflicted
            height: visible ? implicitHeight : 0
            text: qsTr("Unstage")
            iconName: "minus"
            onTriggered: repo.unstage(panel.menuOldPath !== "" ? Js.appendedString(panel.one(panel.menuPath), panel.menuOldPath)
                                                               : panel.one(panel.menuPath))
        }
        BMenuItem {
            objectName: "gitFileOurs"
            visible: fileMenu.here && fileMenu.conflicted
            height: visible ? implicitHeight : 0
            text: qsTr("Take ours")
            hint: qsTr("this branch's side")
            iconName: "corner-down-left"
            onTriggered: repo.resolve(panel.menuPath, "ours")
        }
        BMenuItem {
            objectName: "gitFileTheirs"
            visible: fileMenu.here && fileMenu.conflicted
            height: visible ? implicitHeight : 0
            text: qsTr("Take theirs")
            hint: qsTr("the incoming side")
            iconName: "arrow-right"
            onTriggered: repo.resolve(panel.menuPath, "theirs")
        }
        BMenuItem {
            objectName: "gitFileResolved"
            visible: fileMenu.here && fileMenu.conflicted
            height: visible ? implicitHeight : 0
            text: qsTr("Mark resolved")
            iconName: "check"
            onTriggered: repo.resolve(panel.menuPath, "resolved")
        }
        BMenuSeparator {
            visible: fileMenu.here && !fileMenu.conflicted
        }
        BMenuItem {
            objectName: "gitFileDiscard"
            visible: fileMenu.here && !fileMenu.conflicted
            height: visible ? implicitHeight : 0
            text: qsTr("Discard changes…")
            iconName: "trash-2"
            danger: true
            onTriggered: discardDialog.openFor(panel.menuPath, false)
        }
    }

    BMenu {
        id: actionsMenu
        objectName: "gitActionsMenu"
        BMenuItem {
            objectName: "gitCommit"
            text: qsTr("Commit…")
            iconName: "git-commit-horizontal"
            unavailable: panel.why("commit", repo.revision, repo.busy)
            onTriggered: panel.run("commit")
        }
        BMenuItem {
            objectName: "gitPush"
            text: repo.ahead > 0 ? qsTr("Push %1").arg(repo.ahead) : qsTr("Push")
            iconName: "arrow-up"
            unavailable: panel.why("push", repo.revision, repo.busy)
            onTriggered: panel.run("push")
        }
        BMenuItem {
            objectName: "gitPull"
            text: repo.behind > 0 ? qsTr("Pull %1").arg(repo.behind) : qsTr("Pull")
            iconName: "arrow-down"
            unavailable: panel.why("pull", repo.revision, repo.busy)
            onTriggered: panel.run("pull")
        }
        BMenuItem {
            objectName: "gitFetch"
            text: qsTr("Fetch")
            iconName: "refresh-cw"
            unavailable: panel.why("fetch", repo.revision, repo.busy)
            onTriggered: panel.run("fetch")
        }
        BMenuItem {
            objectName: "gitCreatePr"
            text: qsTr("Create PR…")
            iconName: "git-pull-request"
            unavailable: panel.why("createPr", repo.revision, repo.busy)
            onTriggered: panel.run("createPr")
        }
        BMenuItem {
            objectName: "gitPublish"
            text: qsTr("Publish to GitHub…")
            iconName: "upload"
            unavailable: panel.why("publish", repo.revision, repo.busy)
            onTriggered: panel.run("publish")
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "gitStash"
            text: qsTr("Stash changes…")
            iconName: "inbox"
            onTriggered: {
                repo.stashes()
                stashDialog.openFor()
            }
        }
        BMenuItem {
            objectName: "gitNewBranchAction"
            text: qsTr("New branch…")
            iconName: "plus"
            onTriggered: panel.openNewBranch()
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
        BMenuItem {
            objectName: "gitStashes"
            text: repo.stashList.length > 0 ? qsTr("Stashes (%1)…").arg(repo.stashList.length) : qsTr("Stash changes…")
            iconName: "inbox"
            onTriggered: stashDialog.openFor()
        }
    }

    // ---- Dialogs -------------------------------------------------------------------------------------

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
        /// "Revert to before turn N" (later turns too) instead of this turn alone.
        property bool toBefore: false
        function openFor(before: bool): void {
            revertDialog.toBefore = before
            revertDialog.open()
        }
        title: revertDialog.toBefore ? qsTr("Revert to before %1?").arg(panel.turnLabel) : qsTr("Revert this turn?")
        subject: panel.checkpoints.length === 0 && panel.checkpointLabel !== "" ? panel.checkpointLabel : panel.turnLabel
        subjectIcon: "history"
        message: revertDialog.toBefore
                 ? qsTr("The files go back to how they were before %1: its changes and those of every turn after it are undone.").arg(panel.turnLabel)
                 : panel.turnTarget === "" || panel.checkpoints.length === 0
                   ? qsTr("The files go back to how they were before the turn: the %n file(s) it changed are put back, files it added are deleted.", "", repo.compareChanges.length)
                   : qsTr("Only this turn's changes are taken out; what you or later turns changed after it stays. If they changed the same lines, nothing is changed.")
        detail: qsTr("Commits, the staging area and ignored files stay as they are.")
        confirmText: qsTr("Revert")
        destructive: true
        onAccepted: {
            if (revertDialog.toBefore)
                panel.revertToTurn()
            else
                panel.revertTurn()
        }
    }

    BConfirmDialog {
        id: hunkRevertDialog
        objectName: "gitHunkRevertDialog"
        title: qsTr("Revert this change?")
        subject: panel.selectedPath
        subjectIcon: "undo-2"
        message: qsTr("Only this change is taken out of the file; the rest of it stays. If its lines changed since, nothing is changed.")
        detail: qsTr("Commits and the staging area stay as they are.")
        confirmText: qsTr("Revert")
        destructive: true
        onAccepted: repo.revertHunk(panel.selectedPath, repo.comparisonBase, repo.compareTarget, panel.discardFirst,
                                    panel.discardLast)
    }

    BConfirmDialog {
        id: discardDialog
        objectName: "gitDiscardDialog"
        property string path
        /// A hunk (panel.discardFirst … discardLast) instead of the whole file.
        property bool hunk: false
        function openFor(path: string, hunk: bool): void {
            discardDialog.path = path
            discardDialog.hunk = hunk
            discardDialog.open()
        }
        title: discardDialog.hunk ? qsTr("Discard this change?") : qsTr("Discard the changes?")
        subject: discardDialog.path
        subjectIcon: "file"
        message: discardDialog.hunk ? qsTr("The lines go back to how they are in the staging area.")
               : panel.menuStatus === "untracked" || panel.menuStatus === "added"
                 ? qsTr("The new file goes to the trash (deleted where there is no trash).")
                 : qsTr("The file goes back to how it is in the last commit, staged changes too.")
        detail: qsTr("This cannot be undone with git.")
        confirmText: qsTr("Discard")
        destructive: true
        onAccepted: {
            if (discardDialog.hunk && panel.discardLines)
                repo.discardLines(discardDialog.path, diffView.rows, panel.discardFirst, panel.discardLast)
            else if (discardDialog.hunk)
                repo.discardHunk(discardDialog.path, panel.discardFirst, panel.discardLast)
            else
                repo.discard(discardDialog.path, discardDialog.path === panel.menuPath ? panel.menuOldPath : "")
        }
    }

    BConfirmDialog {
        id: abortDialog
        objectName: "gitAbortDialog"
        title: qsTr("Abort?")
        subject: panel.operationTitle(repo.operation)
        subjectIcon: "git-merge"
        message: qsTr("The branch and the files go back to how they were before it started: conflicts you resolved are lost.")
        confirmText: qsTr("Abort")
        destructive: true
        onAccepted: repo.abortOperation()
    }

    BConfirmDialog {
        id: dropDialog
        objectName: "gitDropStashDialog"
        title: qsTr("Drop the stash?")
        subjectIcon: "inbox"
        message: qsTr("Its changes are gone for good.")
        confirmText: qsTr("Drop")
        destructive: true
        onAccepted: repo.stashDrop(panel.dropOid)
    }

    GitDefaultBranchDialog {
        id: guardDialog
        git: repo
        onAnswered: (answer, name) => panel.guardAnswered(answer, name)
        onRejected: panel.pendingAction = ""
    }
    GitCommitDialog {
        id: commitDialog
        git: repo
        aiAvailable: panel.aiAvailable
        onWriteRequested: (system, prompt) => panel.writeWithAi(system, prompt)
        onCommitRequested: (message, paths, newBranch, push, createPr, prTitle, prBody, draft, prBase) =>
                           panel.commitRequested(message, paths, newBranch, push, createPr, prTitle, prBody, draft, prBase)
    }
    GitPullRequestDialog {
        id: prDialog
        git: repo
        aiAvailable: panel.aiAvailable
        onWriteRequested: (system, prompt) => panel.writeWithAi(system, prompt)
    }
    GitPublishDialog {
        id: publishDialog
        git: repo
    }
    GitStashDialog {
        id: stashDialog
        git: repo
        onDropRequested: (oid, message) => {
            panel.dropOid = oid
            dropDialog.subject = message
            dropDialog.open()
        }
    }
}
