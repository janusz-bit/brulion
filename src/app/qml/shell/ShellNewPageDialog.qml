pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellNewPageDialog — name a new page (or folder) and open it. `folder` = where ("" = the
// vault's folder for new pages); a "/" in the title creates folders ("Projekty/Dom"). A page gets
// its note mode here (ContentNoteModeChoice: document like Obsidian / outline like Logseq,
// preselected from the last choice or the notebook's default; Workspace.createPage writes the
// `brulion:` key only when needed).
BDialog {
    id: dialog

    property string folder
    property bool folderMode: false
    /// The new page's note mode: "document" | "outline".
    property string mode: "document"

    function openFor(folderPath, makeFolder) {
        folder = folderPath || ""
        folderMode = makeFolder === true
        open()
    }

    title: folderMode ? qsTr("New folder") : qsTr("New page")
    subtitle: folder !== "" ? qsTr("In %1").arg(folder) : ""
    iconName: folderMode ? "folder" : "square-pen"
    acceptText: qsTr("Create")
    acceptEnabled: nameField.text.trim() !== ""
    initialFocusItem: nameField
    preferredWidth: folderMode ? Theme.dialogWidthSm : Theme.dialogWidth

    onAboutToShow: {
        nameField.text = ""
        mode = modeChoice.preferred()
    }
    onAccepted: {
        const name = nameField.text.trim()
        if (folderMode) {
            const path = folder !== "" ? folder + "/" + name : name
            if (!Workspace.createFolder(path))
                AppController.toast(qsTr("Could not create the folder."), "error")
            return
        }
        modeChoice.remember(mode)
        const created = Workspace.createPage(name, folder !== "" ? folder : (Workspace.config.newPagesFolder || ""), mode)
        if (created !== "")
            AppController.openPage(created)
    }

    BTextField {
        id: nameField
        Layout.fillWidth: true
        placeholderText: dialog.folderMode ? qsTr("Folder name") : qsTr("Page title")
        leadingIcon: dialog.folderMode ? "folder" : "file-text"
        onAccepted: dialog.tryAccept()
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
}
