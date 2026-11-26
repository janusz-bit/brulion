pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// CodeCloneDialog — "Clone a repository…" (docs/CODE.md "Cloning a repository"; T3 Code's Add
// project → clone): CodeClonePicker first (your repositories on GitHub, or what is typed), then this
// dialog — the repository, its address, the folder it goes into (<clonesRoot>/<name> by default:
// Settings → Extensions → Code mode → "Cloned repositories"; another one typed or chosen, the
// repository's folder made inside it) — and the clone's progress (CodeProjects.cloneRepository: git
// in the background). Closing the dialog leaves the clone running (CodeSidebar lists it, a toast
// says when it is done); done while shown, the dialog closes and added() opens the new project.
//
//   cloner.start()        // the picker, then the dialog: added(project)
Item {
    id: cloner

    /// The repository as typed or picked.
    property string source
    /// The clone this dialog follows (its folder: the key of CodeProjects.clones), "" none.
    property string cloning
    property string error
    /// cloneRepository() refused the folder: Clone waits for another one (the dialog stays open).
    property bool refused: false
    property string status
    property int percent: -1

    signal added(string project)

    function start(): void {
        picker.openPicker()
    }
    function openFor(source: string): void {
        cloner.source = source
        cloner.error = ""
        cloner.refused = false
        cloner.cloning = ""
        cloner.status = ""
        cloner.percent = -1
        dialog.busy = false
        folderField.text = Workspace.projects.cloneFolder(source, "")
        dialog.open()
    }
    function startClone(): void {
        const folder = Js.trim(folderField.text)
        cloner.error = Workspace.projects.cloneRepository(cloner.source, folder)
        if (cloner.error !== "") {
            cloner.refused = true
            return
        }
        cloner.cloning = Workspace.projects.cleanFolder(folder)
        cloner.follow()
        dialog.busy = true
    }
    function follow(): void {
        if (cloner.cloning === "")
            return
        cloner.status = AppController.routeText(Workspace.projects.cloneOf(cloner.cloning), "status")
        cloner.percent = AppController.routeInt(Workspace.projects.cloneOf(cloner.cloning), "percent", -1)
    }

    CodeClonePicker {
        id: picker
        parent: T.Overlay.overlay
        onSourcePicked: (source) => cloner.openFor(source)
    }

    BDialog {
        id: dialog
        objectName: "codeCloneDialog"
        parent: T.Overlay.overlay
        title: qsTr("Clone %1").arg(AppController.routeText(Workspace.projects.cloneSource(cloner.source), "title"))
        subtitle: AppController.routeText(Workspace.projects.cloneSource(cloner.source), "url")
        iconName: "download"
        acceptText: qsTr("Clone")
        rejectText: dialog.busy ? qsTr("Hide") : qsTr("Cancel")
        acceptEnabled: Js.trim(folderField.text) !== "" && !cloner.refused
        initialFocusItem: folderField
        onAcceptRequested: cloner.startClone()

        BText {
            Layout.fillWidth: true
            text: qsTr("The folder it goes into")
            variant: "label"
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            BTextField {
                id: folderField
                objectName: "codeCloneFolder"
                Layout.fillWidth: true
                enabled: !dialog.busy
                accessibleName: qsTr("The folder the repository goes into")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onTextEdited: {
                    cloner.refused = false
                    cloner.error = ""
                }
                onAccepted: dialog.tryAccept()
            }
            BButton {
                objectName: "codeCloneChooseFolder"
                visible: folderPicker.available
                enabled: !dialog.busy
                variant: "secondary"
                iconName: "folder-open"
                text: qsTr("Choose…")
                onClicked: folderPicker.open()
            }
        }
        BText {
            Layout.fillWidth: true
            visible: !dialog.busy
            text: qsTr("A new folder, or an empty one. A thread of the new project works there, or in a worktree of its own.")
            variant: "small"
            color: Theme.fgMuted
            wrapMode: Text.Wrap
        }
        BProgressBar {
            objectName: "codeCloneProgress"
            visible: dialog.busy
            Layout.fillWidth: true
            indeterminate: cloner.percent < 0
            value: cloner.percent < 0 ? 0 : cloner.percent
            to: 100
        }
        BText {
            objectName: "codeCloneStatus"
            visible: dialog.busy
            Layout.fillWidth: true
            text: cloner.status
            variant: "small"
            color: Theme.fgMuted
            elide: Text.ElideRight
        }
        BText {
            visible: dialog.busy
            Layout.fillWidth: true
            text: qsTr("You can close this window: the clone goes on, and the project is added when it is done.")
            variant: "small"
            color: Theme.fgMuted
            wrapMode: Text.Wrap
        }
        BText {
            objectName: "codeCloneError"
            visible: cloner.error !== ""
            Layout.fillWidth: true
            text: cloner.error
            variant: "small"
            color: Theme.dangerText
            wrapMode: Text.Wrap
        }
    }

    FilePicker {
        id: folderPicker
        objectName: "codeCloneFolderPicker"
        title: qsTr("Choose where the repository goes")
        fileMode: FilePicker.OpenFolder
        dark: Theme.dark
        colors: Theme.colors
        // The repository's own folder inside the one chosen (an empty one of its name: that one).
        onAccepted: {
            folderField.text = Workspace.projects.cloneFolder(cloner.source, "" + folderPicker.selectedFolder)
            cloner.refused = false
            cloner.error = ""
        }
    }

    Connections {
        target: Workspace.projects
        function onClonesChanged() {
            cloner.follow()
        }
        function onCloned(folder: string, project: string, error: string, cancelled: bool) {
            if (folder !== cloner.cloning)
                return
            cloner.cloning = ""
            dialog.busy = false
            if (cancelled)
                return
            if (error !== "") {
                cloner.error = error
                if (!dialog.visible)
                    AppController.toast(qsTr("The repository could not be cloned: %1").arg(error), "error")
                return
            }
            if (dialog.visible) {
                dialog.close()
                cloner.added(project)
            } else {
                AppController.toast(qsTr("Cloned %1: it is a project now.").arg(Workspace.projects.nameOf(project)), "success")
            }
        }
    }
}
