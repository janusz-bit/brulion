pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentRenameDialog — confirms renaming a page in its folder (the page header's title edit) when
// that rewrites other notes (docs/SYNTAX.md §2, §3): how many links and tags in how many notes
// Workspace.renamePage rewrites, as Workspace.renameImpact counts them. The title is a title: a
// "/" in it stays in the folder ("TCP/IP" -> "TCP IP"). renamed(path) follows a successful rename.
//
//   ContentRenameDialog { id: renameDialog; onRenamed: (path) => … }
//   renameDialog.openFor(page.path, "Nowy tytuł", Workspace.renameImpact(page.path, "Nowy tytuł", false))
BDialog {
    id: dialog

    property string path
    property string newTitle
    /// The page's name after the rename ("TCP IP" for "TCP/IP").
    property string newName
    property int notes: 0
    property int links: 0
    property int tags: 0

    signal renamed(string newPath)

    /// Ask about renaming `pagePath` to `title`, with Workspace.renameImpact() of it (`show`
    /// false: only set it up, for rename() at once).
    function openFor(pagePath, title, impact, show) {
        path = pagePath
        newTitle = title
        newName = impact && impact.path ? Workspace.pageTitle(impact.path) : title
        notes = impact && impact.notes ? impact.notes : 0
        links = impact && impact.links ? impact.links : 0
        tags = impact && impact.tags ? impact.tags : 0
        if (show !== false)
            open()
    }
    /// Rename now (also without the dialog when nothing else changes).
    function rename() {
        const renamed = Workspace.renamePage(dialog.path, dialog.newTitle)
        if (renamed !== "" && renamed !== dialog.path) {
            AppController.toast(dialog.tags > 0
                                ? qsTr("Renamed to “%1”, %n link(s) and tag(s) updated", "", dialog.links + dialog.tags).arg(Workspace.pageTitle(renamed))
                                : dialog.links > 0
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
        text: qsTr("“%1” becomes “%2”.").arg(Workspace.pageTitle(dialog.path)).arg(dialog.newName)
        wrapMode: Text.Wrap
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        spacing: Theme.space2
        BIcon { name: "link"; size: Theme.iconSize; color: Theme.fgMuted; Layout.alignment: Qt.AlignTop }
        BText {
            Layout.fillWidth: true
            // One sentence (the verb agrees with the number in Polish); %1 = "in %n note(s)". Tags
            // that name the page follow it too (#old, #[[old]], tags:: and frontmatter tags).
            text: (dialog.tags > 0 ? qsTr("%n link(s) and tag(s) %1 will be updated to the new name.", "", dialog.links + dialog.tags)
                                   : qsTr("%n link(s) %1 will be updated to the new name.", "", dialog.links))
                  .arg(qsTr("in %n note(s)", "", dialog.notes))
            variant: "muted"
            wrapMode: Text.Wrap
        }
    }
}
