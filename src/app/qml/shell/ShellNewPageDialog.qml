pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellNewPageDialog — name a new page (or folder) and open it. `folder` = where: a page goes
// into the folder chosen — shown as a removable chip "In folder: Projects ×" with "Change…" (the
// folder picker of ShellMoveDialog) —, else into the notebook's folder for new pages; a "/" in the
// title creates folders ("Projects/Home"). AppShell.openNewPage() prefills the folder of the page
// shown (Ctrl+N, the top bar, the sidebar's pencil) — the root too ("In the notebook root"). A page gets its note mode here
// (ContentNoteModeChoice: document like Obsidian / outline like Logseq, preselected from the last
// choice or the notebook's default; Workspace.createPage writes the `brulion:` key only when
// needed). A folder's name is sanitised and a hidden one refused; the new folder is shown in the
// tree. What would keep Create from making a new folder — one of that name there already (in any
// case, once sanitised: createFolder would only show it), a hidden name, a file in the way — is
// said at the field while typing, in the core's words (Workspace.folderProblem), Create disabled.
BDialog {
    id: dialog

    property string folder
    /// A page goes into `folder` ("" = the root); otherwise into the notebook's new-pages folder.
    property bool folderChosen: false
    property bool folderMode: false
    /// The new page's note mode: "document" | "outline".
    property string mode: "document"

    /// A page (or with `makeFolder` a folder) in `folderPath`. A page goes there when the folder
    /// is given or `chosen` (the root of a page shown at the root), else into the notebook's
    /// folder for new pages.
    function openFor(folderPath: string, makeFolder: bool, chosen: bool): void {
        dialog.folder = folderPath
        dialog.folderChosen = dialog.folder !== "" || chosen
        dialog.folderMode = makeFolder
        dialog.open()
    }
    /// The notebook's folder for new pages ("" = the root).
    readonly property string newPagesFolder: AppController.routeText(Workspace.config, "newPagesFolder")
    /// Something is typed besides slashes and blanks.
    readonly property bool named: Js.replaceRegExp(nameField.typedText, "[\\/\\\\\\s]", "g", "") !== ""
    /// Folder mode: why Create would make no new folder of the name typed, in the core's words
    /// (Workspace.folderProblem: createFolder's own checks), "" when it would make one. Asked
    /// again when the notebook changes (Workspace.revision: a folder made or removed meanwhile).
    readonly property string folderProblem: dialog.folderMode && dialog.visible && dialog.named && Workspace.revision >= 0
                                            ? Workspace.folderProblem(dialog.folderPathOf(nameField.typedText)) : ""
    /// Folder mode: the folder the name typed asks for (in `folder`).
    function folderPathOf(name: string): string {
        return dialog.folder !== "" ? dialog.folder + "/" + name : name
    }

    title: folderMode ? qsTr("New folder") : qsTr("New page")
    subtitle: !folderMode ? "" : folder !== "" ? qsTr("In %1").arg(folder) : qsTr("In the notebook root")
    iconName: folderMode ? "folder-plus" : "square-pen"
    acceptText: qsTr("Create")
    acceptEnabled: named && folderProblem === ""
    initialFocusItem: nameField
    preferredWidth: folderMode ? Theme.dialogWidthSm : Theme.dialogWidth

    onAboutToShow: {
        nameField.text = ""
        dialog.mode = modeChoice.preferred()
    }
    onAccepted: {
        const name = "" + Js.trim(nameField.text)
        if (dialog.folderMode) {
            const created = Workspace.createFolder(dialog.folderPathOf(name))
            if (created !== "")
                AppController.requestItemAction("reveal", created)
            return
        }
        modeChoice.remember(dialog.mode)
        const created = Workspace.createPage(name, dialog.folderChosen ? dialog.folder : dialog.newPagesFolder, dialog.mode)
        if (created !== "")
            AppController.openPage(created)
    }

    BTextField {
        id: nameField
        objectName: "newPageName"
        Layout.fillWidth: true
        placeholderText: dialog.folderMode ? qsTr("Folder name") : qsTr("Page title")
        leadingIcon: dialog.folderMode ? "folder" : "file-text"
        errorText: dialog.folderProblem
        onAccepted: dialog.tryAccept()
    }
    // Where the page goes: a chip to remove (the notebook's folder for new pages then) and
    // "Change…" for another folder.
    Flow {
        visible: !dialog.folderMode
        Layout.fillWidth: true
        spacing: Theme.space2
        BChip {
            objectName: "newPageFolderChip"
            visible: dialog.folderChosen
            text: dialog.folder !== "" ? qsTr("In folder: %1").arg(dialog.folder) : qsTr("In the notebook root")
            iconName: dialog.folder !== "" ? "folder" : "house"
            removable: true
            height: Theme.controlHeight
            Accessible.name: text
            onRemoveClicked: dialog.folderChosen = false
        }
        BText {
            visible: !dialog.folderChosen
            height: Theme.controlHeight
            verticalAlignment: Text.AlignVCenter
            text: dialog.newPagesFolder !== ""
                  ? qsTr("In folder: %1").arg(dialog.newPagesFolder) : qsTr("In the notebook root")
            variant: "muted"
        }
        BButton {
            objectName: "newPageChangeFolder"
            size: "small"
            variant: "ghost"
            iconName: "folder-input"
            text: qsTr("Change…")
            onClicked: picker.pickFolder(dialog.folderChosen ? dialog.folder : dialog.newPagesFolder)
        }
    }
    BText {
        Layout.fillWidth: true
        visible: !dialog.folderMode
        text: qsTr("Tip: “Projects/Kitchen” creates the page in a folder.")
        variant: "faint"
        font.pixelSize: Theme.textSm
        wrapMode: Text.Wrap
    }
    ContentNoteModeChoice {
        id: modeChoice
        objectName: "newPageModeChoice"
        visible: !dialog.folderMode
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        mode: dialog.mode
        onActivated: (value) => dialog.mode = value
    }

    ShellMoveDialog {
        id: picker
        onChosen: (chosen) => {
            dialog.folder = chosen
            dialog.folderChosen = true
        }
    }
}
