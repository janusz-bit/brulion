pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// GitCommitDialog — the changes panel's "Commit…" (docs/CODE-GIT.md), as T3 Code's: the changed
// files as a checklist (all of them checked, or — when something is staged — the staged ones), the
// message, "Commit on a new branch" (its name made of the message until edited), and then — the
// choice T3 Code offers — pushed, or pushed with a pull request opened into a base branch (only
// with the GitHub CLI). Only the checked files are committed (CodeGit.commitFiles: all of them →
// everything; exactly the staged ones → the index as it is; else those files as they are, new ones
// added) — the others stay as they are. The request goes to the owner (commitRequested), which
// asks first when it would push to the repository's default branch. The message can be written by
// the AI (while the owner says it can: aiAvailable): "Write with AI" fills the field, and — T3 Code's
// way — a commit asked for with the field left empty writes it first and then commits with it (a
// note under the field says so; the AI's failure commits nothing). Without the AI an empty message
// commits nothing, as before.
//
//   GitCommitDialog { id: commitDialog; git: panel.git
//                     onCommitRequested: (message, paths, …) => panel.commitRequested(message, paths, …) }
BDialog {
    id: dialog
    objectName: "gitCommitDialog"

    property CodeGit git: null
    /// "commit" | "push" | "pr".
    property string choice: "commit"
    /// The files checked.
    property list<string> checked
    /// The pull request's title / the new branch's name were typed: the message no longer sets them.
    property bool titleEdited: false
    property bool branchEdited: false
    /// The pull request's base ("" the default branch).
    property string prBase
    readonly property bool canPullRequest: dialog.git !== null && dialog.git.ghAvailable
    readonly property bool canPush: dialog.git !== null && (!dialog.git.detached || branchSwitch.checked)
    readonly property string subject: dialog.git !== null ? dialog.git.subjectOf(messageField.typedText) : ""
    readonly property string commitMode: dialog.git !== null ? dialog.git.commitModeFor(dialog.checked) : ""
    /// The field is empty and the AI can write it: a commit asked for writes it first.
    readonly property bool aiWritesMessage: dialog.aiAvailable && dialog.subject === ""
    /// (A new branch named after a message the AI is still to write: named then.)
    readonly property bool branchOk: !branchSwitch.checked || (dialog.git !== null && dialog.git.isValidBranchName(branchField.typedText))
                                     || (dialog.aiWritesMessage && !dialog.branchEdited)

    signal commitRequested(string message, list<string> paths, string newBranch, bool push, bool createPr,
                           string prTitle, string prBody, bool draft, string prBase)
    /// T3 Code's AI-written commit message: the owner can ask the AI (GitChangesPanel.aiAvailable)
    /// — "Write with AI" asks git for the checked files' changes (CodeGit.requestPatch), then the
    /// owner (writeRequested(system, prompt): CodeGit.commitQuestion) for the message, which comes
    /// back by aiWritten(). While it is written the files and the field wait (`busy`).
    property bool aiAvailable: false
    property bool aiWriting: false
    /// The commit asked for with the field empty: it goes once the AI has written the message.
    property bool commitAfterWriting: false
    signal writeRequested(string system, string prompt)
    function aiWritten(ok: bool, text: string): void {
        if (!dialog.aiWriting)
            return
        dialog.aiWriting = false
        const commit = dialog.commitAfterWriting
        dialog.commitAfterWriting = false
        if (!ok) {
            dialog.aiError = text
            return
        }
        messageField.text = text
        // (Accepted as the button would: refused — a branch name made of it is none — it stays open.)
        if (commit)
            dialog.tryAccept()
    }
    property string aiError
    function writeWithAi(): void {
        if (dialog.git === null || dialog.checked.length === 0 || dialog.aiWriting)
            return
        dialog.aiError = ""
        dialog.aiWriting = true
        dialog.git.requestPatch(dialog.checked)
    }
    Connections {
        target: dialog.git
        function onPatchReady(text: string) {
            if (!dialog.aiWriting)
                return
            if (text === "") {
                dialog.aiWriting = false
                dialog.commitAfterWriting = false
                dialog.aiError = qsTr("The changes could not be read.")
                return
            }
            dialog.question.map = dialog.git.commitQuestion(text)
            dialog.writeRequested("" + dialog.question.map.system, "" + dialog.question.map.prompt)
        }
    }
    /// CodeGit.commitQuestion's {system, prompt} (a map read in compiled QML: MapHolder).
    readonly property MapHolder question: MapHolder {}

    function openFor(): void {
        dialog.aiWriting = false
        dialog.commitAfterWriting = false
        dialog.aiError = ""
        messageField.text = ""
        prTitleField.text = ""
        prBodyField.text = ""
        branchField.text = ""
        branchSwitch.checked = false
        draftSwitch.checked = false
        dialog.titleEdited = false
        dialog.branchEdited = false
        dialog.prBase = ""
        if (dialog.git !== null) {
            dialog.checked = dialog.git.stagedPaths.length > 0 ? dialog.git.stagedPaths : dialog.git.changePaths
            dialog.git.branches()
        }
        if (dialog.choice === "pr" && !dialog.canPullRequest)
            dialog.choice = "push"
        if (dialog.choice !== "commit" && !dialog.canPush)
            dialog.choice = "commit"
        dialog.open()
    }
    function choose(value: string): void {
        dialog.choice = value
    }
    function toggle(path: string): void {
        if (dialog.checked.indexOf(path) >= 0)
            dialog.checked = Js.withoutString(dialog.checked, path)
        else
            dialog.checked = Js.appendedString(dialog.checked, path)
    }
    function checkAll(on: bool): void {
        if (dialog.git === null)
            return
        if (on)
            dialog.checked = dialog.git.changePaths
        else
            dialog.checked = dialog.noPaths
    }
    readonly property list<string> noPaths: []
    function request(): void {
        dialog.commitRequested(messageField.text, dialog.checked, branchSwitch.checked ? branchField.text : "",
                               dialog.choice !== "commit", dialog.choice === "pr", prTitleField.text, prBodyField.text,
                               draftSwitch.checked, dialog.prBase)
    }

    title: qsTr("Commit changes")
    subtitle: dialog.git === null ? ""
            : dialog.git.detached ? qsTr("%n of %1 file(s), on a detached HEAD", "", dialog.checked.length).arg(dialog.git.changeCount)
            : qsTr("%n of %1 file(s) on %2", "", dialog.checked.length).arg(dialog.git.changeCount).arg(dialog.git.branch)
    iconName: "git-commit-horizontal"
    acceptText: dialog.commitAfterWriting ? qsTr("Writing the message…")
              : dialog.choice === "pr" ? qsTr("Commit, push and create PR")
              : dialog.choice === "push" ? qsTr("Commit and push") : qsTr("Commit")
    acceptEnabled: dialog.git !== null && !dialog.git.busy && (dialog.subject !== "" || dialog.aiAvailable)
                   && dialog.checked.length > 0 && dialog.branchOk
                   && (dialog.choice !== "pr" || dialog.titleEdited === false || Js.trim(prTitleField.typedText) !== "")
    busy: dialog.aiWriting
    initialFocusItem: messageField
    // (The field left empty: the AI writes the message first — busy holds the accept back —, then
    // aiWritten() accepts.)
    onAcceptRequested: {
        if (dialog.aiWritesMessage && !dialog.aiWriting) {
            dialog.commitAfterWriting = true
            dialog.writeWithAi()
        }
    }
    onAccepted: dialog.request()
    onClosed: {
        dialog.aiWriting = false
        dialog.commitAfterWriting = false
    }

    // ---- The files ---------------------------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        BText {
            Layout.fillWidth: true
            text: qsTr("Files")
            variant: "label"
        }
        BButton {
            objectName: "gitCommitAll"
            text: dialog.git !== null && dialog.checked.length === dialog.git.changeCount ? qsTr("None") : qsTr("All")
            variant: "ghost"
            size: "small"
            onClicked: dialog.checkAll(dialog.git === null || dialog.checked.length !== dialog.git.changeCount)
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(fileList.contentHeight + 2, 180)
        radius: Theme.radiusSm
        color: "transparent"
        border.width: 1
        border.color: Theme.border
        ListView {
            id: fileList
            objectName: "gitCommitFiles"
            anchors.fill: parent
            anchors.margins: 1
            enabled: !dialog.aiWriting
            clip: true
            model: dialog.git !== null ? dialog.git.changes : dialog.noRows
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: BScrollBar {}
            delegate: BCheckBox {
                id: fileCheck
                required property string path
                required property string letter
                required property bool staged
                required property int index
                objectName: "gitCommitFile_" + fileCheck.index
                width: ListView.view.width
                text: fileCheck.letter + "  " + fileCheck.path
                // (A Binding: a click toggles `checked`, which would end a plain binding.)
                Binding on checked {
                    value: dialog.checked.indexOf(fileCheck.path) >= 0
                }
                onClicked: dialog.toggle(fileCheck.path)
            }
        }
    }
    readonly property list<var> noRows: []
    BText {
        objectName: "gitCommitMode"
        Layout.fillWidth: true
        text: dialog.commitMode === "staged" ? qsTr("The staged changes are committed (partly staged files keep the rest).")
            : dialog.commitMode === "paths" ? qsTr("Only the checked files are committed, as they are now; the others stay as they are.")
            : dialog.commitMode === "all" ? qsTr("Every change is committed, new files too.")
            : qsTr("Check the files to commit.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }

    // ---- The message -------------------------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        spacing: Theme.space2
        BText {
            Layout.fillWidth: true
            text: qsTr("Message")
            variant: "label"
        }
        BSpinner {
            visible: dialog.aiWriting
            size: Theme.iconSizeSm
        }
        BButton {
            objectName: "gitCommitWriteAi"
            visible: dialog.aiAvailable
            size: "small"
            variant: "ghost"
            iconName: "sparkles"
            text: dialog.aiWriting ? qsTr("Writing…") : qsTr("Write with AI")
            enabled: !dialog.aiWriting && dialog.checked.length > 0
            onClicked: dialog.writeWithAi()
        }
    }
    BText {
        objectName: "gitCommitAiError"
        Layout.fillWidth: true
        visible: dialog.aiError !== ""
        text: dialog.aiError
        textFormat: Text.PlainText
        variant: "caption"
        color: Theme.dangerText
        wrapMode: Text.Wrap
    }
    BTextArea {
        id: messageField
        objectName: "gitCommitMessage"
        Layout.fillWidth: true
        Layout.preferredHeight: 100
        enabled: !dialog.aiWriting
        wrapMode: TextEdit.Wrap
        accessibleName: qsTr("Commit message")
        placeholderText: qsTr("A short summary, then a blank line and the details")
        onTextChanged: {
            if (dialog.git === null)
                return
            if (!dialog.titleEdited)
                prTitleField.text = dialog.git.subjectOf(messageField.text)
            if (!dialog.branchEdited)
                branchField.text = dialog.git.branchNameFor(messageField.text)
        }
    }
    BText {
        objectName: "gitCommitAiNote"
        Layout.fillWidth: true
        visible: dialog.aiWritesMessage && !dialog.aiWriting
        text: qsTr("Left empty, the AI writes the message from the checked files' changes when you commit.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
    BSwitch {
        id: branchSwitch
        objectName: "gitCommitNewBranch"
        Layout.fillWidth: true
        text: qsTr("Commit on a new branch")
        iconName: "git-branch"
    }
    BTextField {
        id: branchField
        objectName: "gitCommitBranchName"
        Layout.fillWidth: true
        visible: branchSwitch.checked
        accessibleName: qsTr("Branch name")
        placeholderText: qsTr("e.g. fix/sync-conflict")
        errorText: branchSwitch.checked && branchField.typedText !== "" && dialog.git !== null
                   && !dialog.git.isValidBranchName(branchField.typedText)
                   ? qsTr("A branch name has no spaces, no “..”, no backslash and none of ~ ^ : ? * [") : ""
        onTextEdited: dialog.branchEdited = true
    }

    // ---- Then --------------------------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        text: qsTr("Then")
        variant: "label"
    }
    ColumnLayout {
        objectName: "gitCommitChoices"
        Layout.fillWidth: true
        spacing: 2
        BListItem {
            objectName: "gitCommitChoice_commit"
            Layout.fillWidth: true
            iconName: "git-commit-horizontal"
            text: qsTr("Commit")
            subtitle: qsTr("Only on this computer")
            selected: dialog.choice === "commit"
            selectionBar: true
            onClicked: dialog.choose("commit")
        }
        BListItem {
            objectName: "gitCommitChoice_push"
            Layout.fillWidth: true
            enabled: dialog.canPush
            iconName: "arrow-up"
            text: qsTr("Commit and push")
            subtitle: dialog.git !== null && dialog.git.upstream !== "" && !branchSwitch.checked
                      ? qsTr("To %1").arg(dialog.git.upstream) : qsTr("To the remote, as a new branch there")
            selected: dialog.choice === "push"
            selectionBar: true
            onClicked: dialog.choose("push")
        }
        BListItem {
            objectName: "gitCommitChoice_pr"
            Layout.fillWidth: true
            visible: dialog.canPullRequest
            enabled: dialog.canPush
            iconName: "git-pull-request"
            text: qsTr("Commit, push and create PR")
            subtitle: qsTr("A pull request on GitHub, with the GitHub CLI")
            selected: dialog.choice === "pr"
            selectionBar: true
            onClicked: dialog.choose("pr")
        }
    }

    // ---- The pull request --------------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        visible: dialog.choice === "pr"
        text: qsTr("Pull request title")
        variant: "label"
    }
    BTextField {
        id: prTitleField
        objectName: "gitPrTitle"
        Layout.fillWidth: true
        visible: dialog.choice === "pr"
        accessibleName: qsTr("Pull request title")
        placeholderText: qsTr("The commit message's first line")
        onTextEdited: dialog.titleEdited = true
    }
    BTextArea {
        id: prBodyField
        objectName: "gitPrBody"
        Layout.fillWidth: true
        Layout.preferredHeight: 72
        visible: dialog.choice === "pr"
        wrapMode: TextEdit.Wrap
        accessibleName: qsTr("Pull request description")
        placeholderText: qsTr("What and why (optional)…")
    }
    GitBasePicker {
        objectName: "gitPrBase"
        Layout.fillWidth: true
        visible: dialog.choice === "pr"
        git: dialog.git
        base: dialog.prBase
        onPicked: (name) => dialog.prBase = name
    }
    BSwitch {
        id: draftSwitch
        objectName: "gitPrDraft"
        Layout.fillWidth: true
        visible: dialog.choice === "pr"
        text: qsTr("Draft pull request")
        iconName: "git-pull-request"
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.choice !== "commit" && dialog.git !== null && dialog.git.onDefaultBranch && !branchSwitch.checked
        text: qsTr("This is the default branch (%1): you are asked before it is pushed.").arg(dialog.git !== null ? dialog.git.defaultBranch : "")
        variant: "small"
        color: Theme.warningText
        wrapMode: Text.Wrap
    }
    BText {
        Layout.fillWidth: true
        text: qsTr("The repository's hooks run as with git commit. A file that still holds conflict markers is refused.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
}
