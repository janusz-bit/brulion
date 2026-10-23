pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentNewPageDialog — "Nowa strona" from AllPagesView: a title (a "/" makes folders:
// "Projekty/Dom"), created in `folder` (the folder being listed, else the vault's folder for new
// pages) and opened, in the note mode chosen here (ContentNoteModeChoice). A title that is taken
// gets " 2"; one differing only in letter case from a page opens that page
// (Workspace.createPage).
//
//   ContentNewPageDialog { id: newPage }
//   newPage.openIn("Projekty")
BDialog {
    id: dialog

    property string folder
    /// The new page's note mode: "document" | "outline".
    property string mode: "document"

    function openIn(inFolder) {
        folder = inFolder || ""
        nameField.text = ""
        mode = modeChoice.preferred()
        open()
    }

    title: qsTr("New page")
    subtitle: folder !== "" ? qsTr("In the folder %1").arg(folder) : ""
    iconName: "square-pen"
    acceptText: qsTr("Create")
    acceptEnabled: nameField.typedText.trim() !== ""
    initialFocusItem: nameField
    preferredWidth: Theme.dialogWidth

    onAccepted: {
        modeChoice.remember(dialog.mode)
        const path = Workspace.createPage(nameField.text.trim(), dialog.folder, dialog.mode)
        if (path !== "")
            AppController.openPage(path)
        else
            AppController.toast(qsTr("The page could not be created."), "error")
    }

    BTextField {
        id: nameField
        Layout.fillWidth: true
        placeholderText: qsTr("Page title, e.g. Holiday in Zakopane")
        accessibleName: qsTr("Page title")
        onAccepted: dialog.tryAccept()
    }
    ContentNoteModeChoice {
        id: modeChoice
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        mode: dialog.mode
        onActivated: (value) => dialog.mode = value
    }
}
