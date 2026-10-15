pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentRenameDialog — confirms renaming a page when other notes link to it: how many links
// in how many notes Workspace.renamePage rewrites. renamed(path) follows a successful rename.
//
//   ContentRenameDialog { id: renameDialog; onRenamed: (path) => … }
//   renameDialog.openFor(page.path, "Nowy tytuł", tools.renameImpact(page.path))
BDialog {
    id: dialog

    property string path
    property string newTitle
    property int notes: 0
    property int links: 0

    signal renamed(string newPath)

    function openFor(pagePath, title, impact) {
        path = pagePath
        newTitle = title
        notes = impact && impact.notes ? impact.notes : 0
        links = impact && impact.links ? impact.links : 0
        open()
    }
    /// Rename now (also without the dialog when nothing links to the page).
    function rename() {
        const renamed = Workspace.renamePage(dialog.path, dialog.newTitle)
        if (renamed !== "" && renamed !== dialog.path) {
            AppController.toast(dialog.links > 0
                                ? qsTr("Renamed to “%1”, %n link(s) updated", "", dialog.links).arg(Workspace.pageTitle(renamed))
                                : qsTr("Renamed to “%1”").arg(Workspace.pageTitle(renamed)), "success")
            dialog.renamed(renamed)
        }
    }

    title: qsTr("Rename page?")
    iconName: "pencil"
    acceptText: qsTr("Rename")
    preferredWidth: Theme.dialogWidthSm
    onAccepted: rename()

    BText {
        Layout.fillWidth: true
        text: qsTr("“%1” becomes “%2”.").arg(Workspace.pageTitle(dialog.path)).arg(dialog.newTitle)
        wrapMode: Text.Wrap
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        spacing: Theme.space2
        BIcon { name: "link"; size: Theme.iconSize; color: Theme.fgMuted; Layout.alignment: Qt.AlignTop }
        BText {
            Layout.fillWidth: true
            text: qsTr("%n link(s)", "", dialog.links) + " " + qsTr("in %n note(s) will be updated to the new name.", "", dialog.notes)
            variant: "muted"
            wrapMode: Text.Wrap
        }
    }
}
