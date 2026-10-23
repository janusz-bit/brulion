pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellRenameDialog — rename a page or a folder. The links and tags that name the page (or a page
// in the folder by its path) are rewritten in every note (docs/SYNTAX.md §2, §3); for a page the
// dialog says how many, as ContentRenameDialog does.
BDialog {
    id: dialog

    property string path
    property bool isDir: false
    /// PageTools.renameImpact() of the page: { notes, links, tags }.
    property var impact: ({})
    readonly property int changes: (impact.links || 0) + (impact.tags || 0)

    function openFor(itemPath, folder) {
        path = itemPath
        isDir = folder === true
        impact = isDir ? ({}) : tools.renameImpact(itemPath)
        nameField.text = isDir ? itemPath.split("/").pop() : Workspace.pageTitle(itemPath)
        open()
    }

    PageTools { id: tools; workspace: Workspace }

    title: isDir ? qsTr("Rename folder") : qsTr("Rename page")
    // One sentence (the verb agrees with the number in Polish); %1 = "in %n note(s)".
    subtitle: isDir ? qsTr("Links and tags that name the folder are updated.")
              : dialog.changes === 0 ? qsTr("Links and tags that name this page are updated in all notes.")
              : ((impact.tags || 0) > 0 ? qsTr("%n link(s) and tag(s) %1 will be updated to the new name.", "", dialog.changes)
                                        : qsTr("%n link(s) %1 will be updated to the new name.", "", dialog.changes))
                .arg(qsTr("in %n note(s)", "", impact.notes || 0))
    iconName: "pencil"
    acceptText: qsTr("Rename")
    acceptEnabled: nameField.text.trim() !== ""
    initialFocusItem: nameField
    preferredWidth: Theme.dialogWidthSm

    onOpened: nameField.selectAll()
    onAccepted: {
        const name = nameField.text.trim()
        const renamed = dialog.isDir ? Workspace.renameFolder(dialog.path, name) : Workspace.renamePage(dialog.path, name)
        if (renamed === "" || renamed === dialog.path)
            return
        const title = dialog.isDir ? name : Workspace.pageTitle(renamed)
        AppController.toast((dialog.impact.tags || 0) > 0
                            ? qsTr("Renamed to “%1”, %n link(s) and tag(s) updated", "", dialog.changes).arg(title)
                            : dialog.changes > 0
                            ? qsTr("Renamed to “%1”, %n link(s) updated", "", dialog.changes).arg(title)
                            : qsTr("Renamed to “%1”").arg(title), "success")
    }

    BTextField {
        id: nameField
        Layout.fillWidth: true
        placeholderText: dialog.isDir ? qsTr("Folder name") : qsTr("Page title")
        onAccepted: dialog.tryAccept()
    }
}
