pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// GitDefaultBranchDialog — T3 Code's default-branch guard: a commit and push, or a push, on the
// repository's default branch (CodeGit.onDefaultBranch) asks first — Cancel, Continue (push to it)
// or "Create a branch and continue" (the name made of the commit message, editable): answered(
// "continue" | "branch", name).
//
//   GitDefaultBranchDialog { git: repo; onAnswered: (answer, name) => …; onRejected: … }
BDialog {
    id: dialog
    objectName: "gitDefaultBranchDialog"

    property CodeGit git: null

    signal answered(string answer, string name)

    function openFor(name: string): void {
        nameField.text = name
        dialog.open()
    }
    function withBranch(): void {
        if (dialog.git === null || !dialog.git.isValidBranchName(nameField.text))
            return
        const name = nameField.text
        dialog.close()
        dialog.answered("branch", name)
    }

    title: qsTr("Push to %1?").arg(dialog.git !== null ? dialog.git.defaultBranch : "")
    subtitle: qsTr("This is the repository's default branch: changes usually go into it through a pull request.")
    iconName: "triangle-alert"
    iconColor: Theme.warningText
    acceptText: qsTr("Continue")
    initialFocusItem: nameField
    onAccepted: dialog.answered("continue", "")
    footerLeading: [
        BButton {
            objectName: "gitGuardBranch"
            text: qsTr("Create a branch and continue")
            iconName: "git-branch"
            size: "small"
            enabled: dialog.git !== null && dialog.git.isValidBranchName(nameField.typedText)
            onClicked: dialog.withBranch()
        }
    ]

    BText {
        Layout.fillWidth: true
        text: qsTr("New branch")
        variant: "label"
    }
    BTextField {
        id: nameField
        objectName: "gitGuardBranchName"
        Layout.fillWidth: true
        accessibleName: qsTr("Branch name")
        placeholderText: qsTr("e.g. fix/sync-conflict")
        errorText: nameField.typedText !== "" && dialog.git !== null && !dialog.git.isValidBranchName(nameField.typedText)
                   ? qsTr("A branch name has no spaces, no “..”, no backslash and none of ~ ^ : ? * [") : ""
        onAccepted: dialog.withBranch()
    }
}
