pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellNewPageDialog — name a new page (or folder) and open it. `folder` = where ("" = the
// vault's folder for new pages); a "/" in the title creates folders ("Projekty/Dom").
BDialog {
    id: dialog

    property string folder
    property bool folderMode: false

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
    preferredWidth: Theme.dialogWidthSm

    onAboutToShow: nameField.text = ""
    onAccepted: {
        const name = nameField.text.trim()
        if (folderMode) {
            const path = folder !== "" ? folder + "/" + name : name
            if (!Workspace.createFolder(path))
                AppController.toast(qsTr("Could not create the folder."), "error")
            return
        }
        const created = folder !== "" ? Workspace.createPage(name, folder) : Workspace.createPage(name)
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
}
