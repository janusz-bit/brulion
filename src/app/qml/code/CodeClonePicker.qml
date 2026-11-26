pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CodeClonePicker — the first step of "Clone a repository…" (docs/CODE.md "Cloning a repository";
// T3 Code's Add project → clone): the repositories of the user signed in to the GitHub CLI
// (CodeProjects.listRepositories: `gh repo list`, a lock on private ones, the last list shown at
// once while gh is asked again), searched as the palette searches, and an offer for what is typed —
// "owner/name", an https or ssh address, a repository's page (cut to the repository) — when it is
// none of them; why it is no address when it looks like one. The source picked goes on to
// CodeCloneDialog: sourcePicked().
//
//   picker.openPicker()
BPalette {
    id: picker

    property list<var> repositories
    property bool loading: false
    property string error

    signal sourcePicked(string source)

    function openPicker(): void {
        picker.repositories = Workspace.projects.knownRepositories()
        picker.error = ""
        picker.loading = Workspace.projects.listRepositories()
        picker.query = ""
        picker.currentIndex = 0
        picker.open()
    }
    function emptyText(): string {
        if (picker.query !== "" && Workspace.projects.cloneProblem(picker.query) !== "")
            return Workspace.projects.cloneProblem(picker.query)
        if (picker.loading)
            return qsTr("Reading your repositories on GitHub…")
        if (picker.query !== "")
            return qsTr("No repository of yours matches: type owner/name or paste its address")
        return qsTr("Type owner/name, or paste a git address or a repository’s page")
    }
    function hintText(): string {
        if (!Workspace.projects.ghAvailable)
            return qsTr("With the GitHub CLI (gh) signed in, your repositories on GitHub are listed here.")
        if (picker.error !== "")
            return qsTr("Your repositories could not be listed: %1").arg(picker.error)
        return ""
    }

    objectName: "codeClonePicker"
    items: Workspace.projects.repositoryItems(picker.repositories)
    extraResults: Workspace.projects.cloneOffers(picker.query, picker.repositories)
    defaultIcon: "download"
    placeholderText: qsTr("owner/name, a git address or a repository’s page…")
    emptyTitle: picker.emptyText()
    emptyDescription: picker.hintText()
    onRowActivated: (row) => {
        const source = picker.itemString(row, "source")
        if (source !== "")
            picker.sourcePicked(source)
    }

    Connections {
        target: Workspace.projects
        function onRepositoriesListed(repositories: list<var>, error: string) {
            picker.loading = false
            picker.repositories = repositories
            picker.error = error
        }
    }
}
