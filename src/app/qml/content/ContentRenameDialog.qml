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
//   renameDialog.openFor(page.path, "New title", Workspace.renameImpact(page.path, "New title", false))
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
    /// A map a call returned, read with AppController.route*() (compiled calls cannot take a map
    /// held in a local after another call: kept here).
    property var _held: ({})

    /// Ask about renaming `pagePath` to `title`, with Workspace.renameImpact() of it (`show`
    /// false: only set it up, for rename() at once; pass it always).
    function openFor(pagePath: string, title: string, impact: var, show: bool): void {
        dialog.path = pagePath
        dialog.newTitle = title
        dialog._held = impact
        const newPath = AppController.routeText(dialog._held, "path")
        dialog.newName = newPath !== "" ? Workspace.pageTitle(newPath) : title
        dialog.notes = AppController.routeInt(dialog._held, "notes", 0)
        dialog.links = AppController.routeInt(dialog._held, "links", 0)
        dialog.tags = AppController.routeInt(dialog._held, "tags", 0)
        if (show)
            dialog.open()
    }
    /// Rename now (also without the dialog when nothing else changes).
    function rename(): void {
        // (Read before the rename: compiled calls take no arguments a call before may change.)
        const links = dialog.links
        const tags = dialog.tags
        const renamed = "" + Workspace.renamePage(dialog.path, dialog.newTitle)
        if (renamed === "" || renamed === dialog.path)
            return
        const title = "" + Workspace.pageTitle(renamed)
        const message = tags > 0 ? qsTr("Renamed to “%1”, %n link(s) and tag(s) updated", "", links + tags).arg(title)
                      : links > 0 ? qsTr("Renamed to “%1”, %n link(s) updated", "", links).arg(title)
                      : qsTr("Renamed to “%1”").arg(title)
        AppController.toast(message, "success")
        dialog.renamed(renamed)
    }

    title: qsTr("Rename page?")
    iconName: "pencil"
    acceptText: qsTr("Rename")
    preferredWidth: Theme.dialogWidthSm
    onAccepted: dialog.rename()

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
