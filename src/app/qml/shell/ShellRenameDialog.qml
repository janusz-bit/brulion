pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellRenameDialog — rename a page (links to it are rewritten in every note) or a folder.
BDialog {
    id: dialog

    property string path
    property bool isDir: false

    function openFor(itemPath, folder) {
        path = itemPath
        isDir = folder === true
        nameField.text = isDir ? itemPath.split("/").pop() : Workspace.pageTitle(itemPath)
        open()
    }

    title: isDir ? qsTr("Rename folder") : qsTr("Rename page")
    subtitle: isDir ? qsTr("Links that name the folder are updated.") : qsTr("Links to this page are updated in all notes.")
    iconName: "pencil"
    acceptText: qsTr("Rename")
    acceptEnabled: nameField.text.trim() !== ""
    initialFocusItem: nameField
    preferredWidth: Theme.dialogWidthSm

    onOpened: nameField.selectAll()
    onAccepted: {
        const name = nameField.text.trim()
        const renamed = dialog.isDir ? Workspace.renameFolder(dialog.path, name) : Workspace.renamePage(dialog.path, name)
        if (renamed !== "" && renamed !== dialog.path)
            AppController.toast(qsTr("Renamed to “%1”").arg(dialog.isDir ? name : Workspace.pageTitle(renamed)), "success")
    }

    BTextField {
        id: nameField
        Layout.fillWidth: true
        placeholderText: dialog.isDir ? qsTr("Folder name") : qsTr("Page title")
        onAccepted: dialog.tryAccept()
    }
}
