pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CodePullRequestDialog — "Check out pull request #123" (docs/CODE.md "Pull requests"; the branch
// picker offers it for "#123", "123" or a pull request's address): in the project's folder (Local:
// refused while it has uncommitted changes) or in a new worktree of its own (the default) —
// CodeProjects.checkoutPullRequest: `gh pr checkout` where the GitHub CLI is, else the remote's
// pull/<n>/head fetched into a branch of its own. checkedOut() tells CodeView where it is: a new
// thread works there, its agent told which pull request it is.
BDialog {
    id: dialog

    property string projectId
    property int number: 0
    property bool inWorktree: true
    property string error

    /// Done: `folder` the new worktree ("" the project's folder), its branch, the pull request's title.
    signal checkedOut(int number, string folder, string branch, string title)

    function openFor(project: string, number: int, inWorktree: bool): void {
        dialog.projectId = project
        dialog.number = number
        dialog.inWorktree = inWorktree
        dialog.error = ""
        dialog.busy = false
        dialog.open()
    }

    objectName: "codePullRequestDialog"
    title: qsTr("Check out pull request #%1").arg(dialog.number)
    subtitle: Workspace.projects.nameOf(dialog.projectId)
    iconName: "git-pull-request"
    acceptText: qsTr("Check out")
    onAcceptRequested: {
        dialog.error = ""
        dialog.busy = Workspace.projects.checkoutPullRequest(dialog.projectId, dialog.number, dialog.inWorktree)
        if (!dialog.busy)
            dialog.error = qsTr("git is not on this computer.")
    }

    Connections {
        target: Workspace.projects
        function onPullRequestCheckedOut(project: string, number: int, folder: string, branch: string, title: string, error: string) {
            if (!dialog.busy || project !== dialog.projectId || number !== dialog.number)
                return
            dialog.busy = false
            if (error !== "") {
                dialog.error = error
                return
            }
            dialog.close()
            dialog.checkedOut(number, folder, branch, title)
        }
    }

    BSegmented {
        objectName: "codePullRequestWhere"
        Layout.fillWidth: true
        fill: true
        accessibleName: qsTr("Where the pull request is checked out")
        model: [{ value: "worktree", text: qsTr("New worktree"), icon: "folder-tree" },
                { value: "local", text: qsTr("Local"), icon: "hard-drive" }]
        currentValue: dialog.inWorktree ? "worktree" : "local"
        enabled: !dialog.busy
        onActivated: (value) => dialog.inWorktree = value === "worktree"
    }
    BText {
        Layout.fillWidth: true
        text: dialog.inWorktree
              ? qsTr("A folder and a branch of its own: a new thread works there, the project's folder stays as it is.")
              : qsTr("In the project's folder: refused while it has uncommitted changes. The threads that work there go on on its branch.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
    BText {
        Layout.fillWidth: true
        text: Workspace.projects.ghAvailable
              ? qsTr("With the GitHub CLI (gh pr checkout): its branch, tracking the pull request's.")
              : qsTr("Without the GitHub CLI: fetched from the repository's remote as pull/%1/head into the branch %2.")
                .arg(dialog.number).arg(Workspace.projects.branchPrefix + "pr-" + dialog.number)
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
    BText {
        objectName: "codePullRequestError"
        visible: dialog.error !== ""
        Layout.fillWidth: true
        text: dialog.error
        variant: "small"
        color: Theme.dangerText
        wrapMode: Text.Wrap
    }
}
