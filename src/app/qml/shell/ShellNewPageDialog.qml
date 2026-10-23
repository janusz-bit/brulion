pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellNewPageDialog — name a new page (or folder) and open it. `folder` = where: a page goes
// into the folder chosen — shown as a removable chip "W folderze: Projekty ×" with "Zmień…" (the
// folder picker of ShellMoveDialog) —, else into the notebook's folder for new pages; a "/" in the
// title creates folders ("Projekty/Dom"). AppShell.openNewPage() prefills the folder of the page
// shown (Ctrl+N, the top bar, the sidebar's pencil). A page gets its note mode here
// (ContentNoteModeChoice: document like Obsidian / outline like Logseq, preselected from the last
// choice or the notebook's default; Workspace.createPage writes the `brulion:` key only when
// needed). A folder's name is sanitised and a hidden one refused (Workspace.createFolder says why);
// the new folder is shown in the tree.
BDialog {
    id: dialog

    property string folder
    /// A page goes into `folder` ("" = the root); otherwise into the notebook's new-pages folder.
    property bool folderChosen: false
    property bool folderMode: false
    /// The new page's note mode: "document" | "outline".
    property string mode: "document"

    function openFor(folderPath, makeFolder) {
        folder = folderPath || ""
        folderChosen = folder !== ""
        folderMode = makeFolder === true
        open()
    }

    title: folderMode ? qsTr("New folder") : qsTr("New page")
    subtitle: folderMode && folder !== "" ? qsTr("In %1").arg(folder) : ""
    iconName: folderMode ? "folder-plus" : "square-pen"
    acceptText: qsTr("Create")
    acceptEnabled: nameField.text.replace(/[\/\\\s]/g, "") !== ""
    initialFocusItem: nameField
    preferredWidth: folderMode ? Theme.dialogWidthSm : Theme.dialogWidth

    onAboutToShow: {
        nameField.text = ""
        mode = modeChoice.preferred()
    }
    onAccepted: {
        const name = nameField.text.trim()
        if (folderMode) {
            const created = Workspace.createFolder(folder !== "" ? folder + "/" + name : name)
            if (created !== "")
                AppController.requestItemAction("reveal", created)
            return
        }
        modeChoice.remember(mode)
        const created = Workspace.createPage(name, folderChosen ? folder : (Workspace.config.newPagesFolder || ""), mode)
        if (created !== "")
            AppController.openPage(created)
    }

    BTextField {
        id: nameField
        objectName: "newPageName"
        Layout.fillWidth: true
        placeholderText: dialog.folderMode ? qsTr("Folder name") : qsTr("Page title")
        leadingIcon: dialog.folderMode ? "folder" : "file-text"
        onAccepted: dialog.tryAccept()
    }
    // Where the page goes: a chip to remove (the notebook's folder for new pages then) and
    // "Zmień…" for another folder.
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
            text: (Workspace.config.newPagesFolder || "") !== ""
                  ? qsTr("In folder: %1").arg(Workspace.config.newPagesFolder) : qsTr("In the notebook root")
            variant: "muted"
        }
        BButton {
            objectName: "newPageChangeFolder"
            size: "small"
            variant: "ghost"
            iconName: "folder-input"
            text: qsTr("Change…")
            onClicked: picker.pickFolder(dialog.folderChosen ? dialog.folder : (Workspace.config.newPagesFolder || ""))
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
