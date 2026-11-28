pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CodeRemoteGitDialog — Commit… and Create PR… of a thread whose project is on another computer
// (CodeRemoteChanges; docs/CODE.md "Threads on the phone"): what git does there, asked through that
// computer's `acp serve` (RemoteProjects.commit / createPullRequest). "commit": the message (never
// empty: no AI writes it there), every change or only the files of this turn, then — T3 Code's
// choice — pushed, or pushed with a pull request (gh there). "pr": a pull request of the branch
// there into a base ("" the default branch), the branch pushed first. The owner sends it
// (commitRequested / pullRequestRequested).
//
//   CodeRemoteGitDialog { id: gitDialog; host: panel.host; onCommitRequested: (…) => … }
//   gitDialog.openFor("commit", turnFiles)
BDialog {
    id: dialog
    objectName: "codeRemoteGitDialog"

    /// The computer's name.
    property string host
    /// "commit" | "pr".
    property string mode: "commit"
    /// For a commit: "commit" | "push" | "pr".
    property string choice: "commit"
    /// The files the turn changed ("" none known: every change only).
    property list<string> turnFiles
    readonly property list<string> noFiles: []

    readonly property bool commitMode: dialog.mode === "commit"
    readonly property bool prShown: !dialog.commitMode || dialog.choice === "pr"
    readonly property bool messageOk: Js.trim(messageField.typedText) !== ""
    readonly property bool titleOk: dialog.commitMode || Js.trim(titleField.typedText) !== ""

    readonly property list<var> choices: [
        { value: "commit", text: qsTr("Commit"), icon: "git-commit-horizontal" },
        { value: "push", text: qsTr("Commit and push"), icon: "upload" },
        { value: "pr", text: qsTr("Commit, push and create PR"), icon: "git-pull-request" }
    ]

    signal commitRequested(string message, list<string> files, bool push, bool createPr, string prTitle,
                           string prBody, bool draft)
    signal pullRequestRequested(string title, string body, string base, bool draft)

    function openFor(mode: string, files: list<string>): void {
        dialog.mode = mode
        dialog.turnFiles = files
        dialog.choice = "commit"
        messageField.text = ""
        titleField.text = ""
        bodyField.text = ""
        baseField.text = ""
        draftSwitch.checked = false
        turnOnly.checked = false
        dialog.open()
    }
    function request(): void {
        if (dialog.commitMode)
            dialog.commitRequested(messageField.text, turnOnly.checked ? dialog.turnFiles : dialog.noFiles,
                                   dialog.choice !== "commit", dialog.choice === "pr", titleField.text,
                                   bodyField.text, draftSwitch.checked)
        else
            dialog.pullRequestRequested(titleField.text, bodyField.text, baseField.text, draftSwitch.checked)
    }

    title: dialog.commitMode ? qsTr("Commit changes") : qsTr("Create pull request")
    subtitle: qsTr("On %1").arg(dialog.host)
    iconName: dialog.commitMode ? "git-commit-horizontal" : "git-pull-request"
    acceptText: !dialog.commitMode ? qsTr("Create PR")
              : dialog.choice === "pr" ? qsTr("Commit, push and create PR")
              : dialog.choice === "push" ? qsTr("Commit and push") : qsTr("Commit")
    acceptEnabled: (!dialog.commitMode || dialog.messageOk) && dialog.titleOk
    initialFocusItem: dialog.commitMode ? messageField : titleField
    textEntry: true
    onAccepted: dialog.request()

    // ---- The commit ---------------------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        visible: dialog.commitMode
        text: qsTr("Commit message")
        variant: "label"
    }
    BTextArea {
        id: messageField
        objectName: "codeRemoteCommitMessage"
        Layout.fillWidth: true
        Layout.preferredHeight: 100
        visible: dialog.commitMode
        wrapMode: TextEdit.Wrap
        accessibleName: qsTr("Commit message")
        placeholderText: qsTr("A short summary, then a blank line and the details")
    }
    BSwitch {
        id: turnOnly
        objectName: "codeRemoteCommitTurnOnly"
        Layout.fillWidth: true
        visible: dialog.commitMode && dialog.turnFiles.length > 0
        text: qsTr("Only the %n file(s) of this turn", "", dialog.turnFiles.length)
        iconName: "history"
    }
    BSegmented {
        objectName: "codeRemoteCommitChoice"
        Layout.fillWidth: true
        visible: dialog.commitMode
        fill: true
        size: "small"
        accessibleName: qsTr("After the commit")
        model: dialog.choices
        currentValue: dialog.choice
        onActivated: (value) => dialog.choice = "" + value
    }

    // ---- The pull request ---------------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        Layout.topMargin: dialog.commitMode ? Theme.space2 : 0
        visible: dialog.prShown
        text: qsTr("Pull request title")
        variant: "label"
    }
    BTextField {
        id: titleField
        objectName: "codeRemotePrTitle"
        Layout.fillWidth: true
        visible: dialog.prShown
        accessibleName: qsTr("Pull request title")
        placeholderText: dialog.commitMode ? qsTr("The commit message's first line") : qsTr("What the change does")
    }
    BTextArea {
        id: bodyField
        objectName: "codeRemotePrBody"
        Layout.fillWidth: true
        Layout.preferredHeight: 72
        visible: dialog.prShown
        wrapMode: TextEdit.Wrap
        accessibleName: qsTr("Pull request description")
        placeholderText: qsTr("What and why (optional)…")
    }
    BTextField {
        id: baseField
        objectName: "codeRemotePrBase"
        Layout.fillWidth: true
        visible: !dialog.commitMode
        accessibleName: qsTr("Base branch")
        placeholderText: qsTr("Base branch (empty: the default branch)")
    }
    BSwitch {
        id: draftSwitch
        objectName: "codeRemotePrDraft"
        Layout.fillWidth: true
        visible: dialog.prShown
        text: qsTr("Draft pull request")
        iconName: "git-pull-request"
    }
    BText {
        Layout.fillWidth: true
        text: dialog.prShown ? qsTr("The branch is pushed first: GitHub needs its commits. %1 opens the pull request with its GitHub CLI (gh).").arg(dialog.host)
                             : qsTr("The repository's hooks run there as with git commit.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
}
