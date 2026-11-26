pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CodeBranchPicker — the branch picker (T3 Code's Ctrl+Shift+G; docs/UI.md "Code mode"): the
// branches of a folder (CodeProjects.listBranches: local ones first, then remote ones; badges —
// current, default, remote, the worktree that has it — as the rows' subtitles), searched as the
// palette searches (BPalette), and offers for what is typed: a new branch of that name ("switch"
// mode, unless switching is blocked), "Check out pull request #123" for "#123", "123" or a pull
// request's address. In "base" mode the branch picked is where a new worktree starts.
//
//   picker.openFor(folder, "switch", blockedReason)    // branchPicked / createRequested / pullRequestRequested
BPalette {
    id: picker

    property string folder
    /// "switch": the picked branch is checked out (a thread, a new Local one); "base": a new
    /// worktree starts from it.
    property string mode: "switch"
    /// Why the branch cannot be switched now ("" it can): shown, and no branch is created.
    property string blocked
    property list<var> branches
    property bool loading: false
    property string error
    readonly property list<var> noBranches: []

    signal branchPicked(string name, bool remote, string worktree)
    signal createRequested(string name)
    signal pullRequestRequested(int number)

    function openFor(folder: string, mode: string, blocked: string): void {
        picker.folder = folder
        picker.mode = mode
        picker.blocked = blocked
        picker.branches = picker.noBranches
        picker.error = ""
        picker.loading = Workspace.projects.listBranches(folder)
        if (!picker.loading)
            picker.error = qsTr("git is not on this computer, or the folder is gone.")
        picker.query = ""
        picker.currentIndex = 0
        picker.open()
    }

    objectName: "codeBranchPicker"
    items: Workspace.projects.branchItems(picker.branches)
    extraResults: Workspace.projects.branchOffers(picker.query, picker.branches, picker.mode === "switch" && picker.blocked === "")
    defaultIcon: "git-branch"
    placeholderText: picker.mode === "base" ? qsTr("The branch the new worktree starts from…")
                                            : qsTr("A branch, a new branch's name, or a pull request: #123…")
    emptyTitle: picker.loading ? qsTr("Reading the branches…") : picker.error !== "" ? picker.error : qsTr("No branch matches")
    emptyDescription: picker.blocked
    onRowActivated: (row) => {
        const kind = picker.itemString(row, "kind")
        if (kind === "pr")
            picker.pullRequestRequested(Js.parseInt(picker.itemString(row, "number")))
        else if (kind === "create")
            picker.createRequested(picker.itemString(row, "name"))
        else
            picker.branchPicked(picker.itemString(row, "name"), picker.itemBool(row, "remote"), picker.itemString(row, "worktree"))
    }

    Connections {
        target: Workspace.projects
        function onBranchesListed(folder: string, branches: list<var>, error: string) {
            if (folder !== picker.folder)
                return
            picker.loading = false
            picker.branches = branches
            picker.error = error
        }
    }
}
