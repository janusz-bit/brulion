pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// GitHubCommitDialog — what the notebook sends to GitHub (Workspace.github): a commit on the
// notebook's branch, or a proposal — a pull request from a branch of the repository or of the
// user's fork; while one is open, proposing again adds a commit to it. The message (a commit
// message, a pull request's title), a pull request's description, and the notes not on GitHub
// yet, each ticked (unticked ones stay here for later; conflict copies are never sent).
//
//   GitHubCommitDialog { id: commitDialog }  …  commitDialog.openFor(true)   // propose
BDialog {
    id: dialog

    readonly property WorkspaceGitHub github: Workspace.github
    property bool proposing: false
    /// The notes unticked: they stay here.
    property list<string> leftOut: []
    readonly property bool updating: proposing && AppController.routeFlag(github.proposal, "open")
    readonly property int selectedCount: Math.max(0, github.changeCount - leftOut.length)

    function openFor(propose: bool): void {
        dialog.proposing = propose
        dialog.leftOut = []
        dialog.github.refreshChanges()
        const count = dialog.github.changeCount
        messageField.text = !propose ? dialog.github.defaultCommitMessage(count)
                          : dialog.updating ? "" : dialog.github.defaultProposalTitle(count)
        bodyArea.text = ""
        dialog.open()
    }
    function setLeftOut(path: string, out: bool): void {
        dialog.leftOut = out ? Js.appendedString(dialog.leftOut, path) : Js.withoutString(dialog.leftOut, path)
    }

    iconName: proposing ? "git-pull-request" : "git-commit-horizontal"
    title: !proposing ? qsTr("Commit to GitHub")
         : updating ? qsTr("Update the proposal") : qsTr("Propose changes")
    subtitle: !proposing ? qsTr("One commit on the branch “%1” of %2.").arg(github.branch).arg(github.repository)
            : updating ? qsTr("Pull request #%1 gets one more commit with these notes; the notes it already has stay in it.")
                         .arg(AppController.routeInt(github.proposal, "number", 0))
            : github.canPush ? qsTr("A pull request to the branch “%1” of %2, from a new branch there.").arg(github.branch).arg(github.repository)
            : qsTr("A pull request to the branch “%1” of %2, from your copy of the repository (a fork, made when you have none).")
              .arg(github.branch).arg(github.repository)
    acceptText: !proposing ? qsTr("Commit") : updating ? qsTr("Update") : qsTr("Propose")
    acceptEnabled: Js.trim(messageField.typedText) !== "" && selectedCount > 0 && !github.busy
    initialFocusItem: messageField
    onAccepted: {
        // The texts are trimmed by RepoSync.
        if (dialog.proposing)
            dialog.github.propose(messageField.text, bodyArea.text, dialog.leftOut)
        else
            dialog.github.commit(messageField.text, dialog.leftOut)
    }

    BText {
        Layout.fillWidth: true
        text: dialog.proposing ? (dialog.updating ? qsTr("What changed") : qsTr("Title")) : qsTr("Message")
        variant: "label"
    }
    BTextField {
        id: messageField
        objectName: "githubCommitMessage"
        Layout.fillWidth: true
        accessibleName: dialog.proposing ? qsTr("Title of the pull request") : qsTr("Commit message")
        placeholderText: dialog.proposing ? qsTr("e.g. Correct the page about sync") : qsTr("e.g. Notes from the meeting")
        onAccepted: dialog.tryAccept()
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.proposing && !dialog.updating
        text: qsTr("Description")
        variant: "label"
    }
    BTextArea {
        id: bodyArea
        Layout.fillWidth: true
        Layout.preferredHeight: 96
        visible: dialog.proposing && !dialog.updating
        accessibleName: qsTr("Description of the pull request")
        placeholderText: qsTr("What and why (optional)…")
    }

    BSectionHeader {
        Layout.fillWidth: true
        text: qsTr("Notes")
        count: dialog.selectedCount
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.github.changeCount === 0
        text: qsTr("Nothing to send: the notes are the same as on GitHub.")
        variant: "muted"
        wrapMode: Text.Wrap
    }
    Repeater {
        model: dialog.github.changes
        // Workspace.github.changes: [{ path, title, kind, kindText, proposed }]
        delegate: BCheckBox {
            id: box
            required property string path
            required property string title
            required property string kindText
            required property bool proposed
            Layout.fillWidth: true
            text: box.title
            description: box.proposed ? box.kindText + " · " + qsTr("in the proposal") + " · " + box.path
                                      : box.kindText + " · " + box.path
            onToggled: dialog.setLeftOut(box.path, !box.checked)
            Binding on checked { value: dialog.leftOut.indexOf(box.path) < 0 }
        }
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.github.changeCount > 0
        text: qsTr("Unticked notes stay on this device, to send later. Conflict copies are never sent.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
}
