pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// GitCommitDialog — the changes panel's "Commit…" (docs/CODE-GIT.md): every change of the working
// tree staged and committed with a message, and then — the choice T3 Code offers — pushed, or
// pushed with a pull request opened (only when the GitHub CLI is there). The pull request's title
// follows the message's first line until it is edited; its description and "draft" are optional.
// The result comes from `git` as actionFinished("commit", ok, message, url).
//
//   GitCommitDialog { id: commitDialog; git: panel.git }   …   commitDialog.openFor()
BDialog {
    id: dialog
    objectName: "gitCommitDialog"

    property CodeGit git: null
    /// "commit" | "push" | "pr".
    property string choice: "commit"
    /// The pull request's title was typed: the message no longer sets it.
    property bool titleEdited: false
    readonly property bool canPullRequest: dialog.git !== null && dialog.git.ghAvailable
    readonly property bool canPush: dialog.git !== null && !dialog.git.detached
    readonly property string subject: dialog.git !== null ? dialog.git.subjectOf(messageField.typedText) : ""

    function openFor(): void {
        messageField.text = ""
        prTitleField.text = ""
        prBodyField.text = ""
        draftSwitch.checked = false
        dialog.titleEdited = false
        if (dialog.choice === "pr" && !dialog.canPullRequest)
            dialog.choice = "push"
        if (dialog.choice !== "commit" && !dialog.canPush)
            dialog.choice = "commit"
        dialog.open()
    }
    function choose(value: string): void {
        dialog.choice = value
    }

    title: qsTr("Commit changes")
    subtitle: dialog.git === null ? ""
            : dialog.git.detached ? qsTr("%n changed file(s), on a detached HEAD", "", dialog.git.changeCount)
            : qsTr("%n changed file(s) on %1", "", dialog.git.changeCount).arg(dialog.git.branch)
    iconName: "git-commit-horizontal"
    acceptText: dialog.choice === "pr" ? qsTr("Commit, push and create PR")
              : dialog.choice === "push" ? qsTr("Commit and push") : qsTr("Commit")
    acceptEnabled: dialog.git !== null && !dialog.git.busy && dialog.subject !== ""
                   && (dialog.choice !== "pr" || dialog.titleEdited === false || Js.trim(prTitleField.typedText) !== "")
    initialFocusItem: messageField
    onAccepted: dialog.git.commit(messageField.text, dialog.choice !== "commit", dialog.choice === "pr",
                                  prTitleField.text, prBodyField.text, draftSwitch.checked)

    BText {
        Layout.fillWidth: true
        text: qsTr("Message")
        variant: "label"
    }
    BTextArea {
        id: messageField
        objectName: "gitCommitMessage"
        Layout.fillWidth: true
        Layout.preferredHeight: 120
        wrapMode: TextEdit.Wrap
        accessibleName: qsTr("Commit message")
        placeholderText: qsTr("A short summary, then a blank line and the details")
        onTextChanged: {
            if (!dialog.titleEdited && dialog.git !== null)
                prTitleField.text = dialog.git.subjectOf(messageField.text)
        }
    }

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
            subtitle: dialog.git !== null && dialog.git.upstream !== "" ? qsTr("To %1").arg(dialog.git.upstream)
                                                                          : qsTr("To the remote, as a new branch there")
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

    // The pull request.
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
        Layout.preferredHeight: 80
        visible: dialog.choice === "pr"
        wrapMode: TextEdit.Wrap
        accessibleName: qsTr("Pull request description")
        placeholderText: qsTr("What and why (optional)…")
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
        text: qsTr("Every change is staged first, new files too (git add -A). The repository's hooks run as with git commit.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
}
