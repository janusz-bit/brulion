pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// GitPullRequestDialog — the changes panel's "Create PR…": a pull request on GitHub for the
// branch checked out, through the GitHub CLI (`gh pr create`; the branch is pushed first when it
// has no upstream or is ahead of it). The result comes from `git` as
// actionFinished("pullRequest", ok, message, url).
//
//   GitPullRequestDialog { id: prDialog; git: panel.git }   …   prDialog.openFor()
BDialog {
    id: dialog
    objectName: "gitPullRequestDialog"

    property CodeGit git: null

    function openFor(): void {
        titleField.text = ""
        bodyField.text = ""
        draftSwitch.checked = false
        dialog.open()
    }

    title: qsTr("Create pull request")
    subtitle: dialog.git !== null && dialog.git.branch !== "" ? qsTr("From the branch %1").arg(dialog.git.branch) : ""
    iconName: "git-pull-request"
    acceptText: qsTr("Create PR")
    acceptEnabled: dialog.git !== null && !dialog.git.busy && Js.trim(titleField.typedText) !== ""
    initialFocusItem: titleField
    onAccepted: dialog.git.createPullRequest(titleField.text, bodyField.text, draftSwitch.checked)

    BText {
        Layout.fillWidth: true
        text: qsTr("Title")
        variant: "label"
    }
    BTextField {
        id: titleField
        objectName: "gitPullRequestTitle"
        Layout.fillWidth: true
        accessibleName: qsTr("Pull request title")
        placeholderText: qsTr("What the change does")
        onAccepted: dialog.tryAccept()
    }
    BText {
        Layout.fillWidth: true
        text: qsTr("Description")
        variant: "label"
    }
    BTextArea {
        id: bodyField
        objectName: "gitPullRequestBody"
        Layout.fillWidth: true
        Layout.preferredHeight: 96
        wrapMode: TextEdit.Wrap
        accessibleName: qsTr("Pull request description")
        placeholderText: qsTr("What and why (optional)…")
    }
    BSwitch {
        id: draftSwitch
        objectName: "gitPullRequestDraft"
        Layout.fillWidth: true
        text: qsTr("Draft pull request")
        iconName: "git-pull-request"
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.git !== null && (dialog.git.upstream === "" || dialog.git.ahead > 0)
        text: qsTr("The branch is pushed first: GitHub needs its commits.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
}
