pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentNewPageDialog — "Nowa strona" from AllPagesView: a title (a "/" makes folders:
// "Projekty/Dom"), created in `folder` (the folder being listed, else the vault's folder for new
// pages) and opened. A title that is taken gets " 2" (Workspace.createPage).
//
//   ContentNewPageDialog { id: newPage }
//   newPage.openIn("Projekty")
BDialog {
    id: dialog

    property string folder

    function openIn(inFolder) {
        folder = inFolder || ""
        nameField.text = ""
        open()
    }

    title: qsTr("New page")
    subtitle: folder !== "" ? qsTr("In the folder %1").arg(folder) : ""
    iconName: "square-pen"
    acceptText: qsTr("Create")
    acceptEnabled: nameField.text.trim() !== ""
    initialFocusItem: nameField
    preferredWidth: Theme.dialogWidthSm

    onAccepted: {
        const path = Workspace.createPage(nameField.text.trim(), dialog.folder)
        if (path !== "")
            AppController.openPage(path)
        else
            AppController.toast(qsTr("The page could not be created."), "error")
    }

    BTextField {
        id: nameField
        Layout.fillWidth: true
        placeholderText: qsTr("Page title, e.g. Wakacje w Zakopanem")
        accessibleName: qsTr("Page title")
        onAccepted: dialog.tryAccept()
    }
}
