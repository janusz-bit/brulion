pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellRenameDialog — rename a page or a folder. The links and tags that name the page (or a page
// in the folder) are rewritten in every note so each keeps its target (docs/SYNTAX.md §2, §3); the
// dialog says how many. A "/" in the name moves it — a path from the notebook root ("Archiwum/Dom")
// —, with a live "Nowe miejsce: …" line and what the move rewrites (Workspace.renameImpact).
BDialog {
    id: dialog

    property string path
    property bool isDir: false
    /// PageTools.renameImpact() of the page: { notes, links, tags } — what any new name rewrites.
    property var impact: ({})
    readonly property int changes: (impact.links || 0) + (impact.tags || 0)
    /// The name typed; a "/" in it moves the page or folder.
    readonly property string typed: nameField.text.trim()
    readonly property string currentName: isDir ? path.split("/").pop() : Workspace.pageTitle(path)
    readonly property bool moves: typed.indexOf("/") >= 0
    /// Workspace.renameImpact() of what is typed (exact; a moment after typing stops).
    property var live: ({})
    /// The live impact applies: a move, or a folder renamed (its pages keep their bare links).
    readonly property bool useLive: (moves || isDir) && typed !== "" && typed !== currentName && live.typed === typed

    function openFor(itemPath, folder) {
        path = itemPath
        isDir = folder === true
        impact = isDir ? ({}) : tools.renameImpact(itemPath)
        live = ({})
        nameField.text = isDir ? itemPath.split("/").pop() : Workspace.pageTitle(itemPath)
        open()
    }
    function computeLive() {
        if (!dialog.opened || typed === "" || typed === currentName) {
            live = ({})
            return
        }
        const result = Workspace.renameImpact(path, typed, isDir)
        result.typed = typed
        live = result
    }
    /// "Zaktualizowane linki: 3 (w 2 notatkach)".
    function liveText() {
        if (!live.ok)
            return live.error || ""
        const n = (live.links || 0) + (live.tags || 0)
        if (n === 0)
            return qsTr("No link needs to change.")
        return ((live.tags || 0) > 0 ? qsTr("Updated links and tags: %n", "", n) : qsTr("Updated links: %n", "", n))
               + " " + qsTr("(in %n note(s))", "", live.notes || 0)
    }

    PageTools { id: tools; workspace: Workspace }
    Timer {
        id: liveTimer
        interval: 250
        onTriggered: dialog.computeLive()
    }
    onTypedChanged: liveTimer.restart()

    title: isDir ? qsTr("Rename folder") : qsTr("Rename page")
    // One sentence (the verb agrees with the number in Polish); %1 = "in %n note(s)".
    subtitle: useLive ? liveText()
              : isDir ? qsTr("Links and tags that name the folder are updated.")
              : dialog.changes === 0 ? qsTr("Links and tags that name this page are updated in all notes.")
              : ((impact.tags || 0) > 0 ? qsTr("%n link(s) and tag(s) %1 will be updated to the new name.", "", dialog.changes)
                                        : qsTr("%n link(s) %1 will be updated to the new name.", "", dialog.changes))
                .arg(qsTr("in %n note(s)", "", impact.notes || 0))
    iconName: "pencil"
    acceptText: moves ? qsTr("Move") : qsTr("Rename")
    acceptEnabled: typed !== "" && (!useLive || live.ok === true)
    initialFocusItem: nameField
    preferredWidth: Theme.dialogWidthSm

    onOpened: nameField.selectAll()
    onAccepted: {
        const name = typed
        // What the rename rewrites, counted now (not a moment ago).
        const result = (moves || isDir) ? Workspace.renameImpact(dialog.path, name, dialog.isDir) : null
        const renamed = dialog.isDir ? Workspace.renameFolder(dialog.path, name) : Workspace.renamePage(dialog.path, name)
        if (renamed === "" || renamed === dialog.path)
            return
        const n = result ? (result.links || 0) + (result.tags || 0) : dialog.changes
        const tags = result ? (result.tags || 0) > 0 : (dialog.impact.tags || 0) > 0
        if (moves) {
            const folder = Workspace.folderOf(renamed)
            const place = folder !== "" ? folder.split("/").pop() : qsTr("Notebook root")
            AppController.toast(n === 0 ? qsTr("Moved to “%1”").arg(place)
                                : tags ? qsTr("Moved to “%1”, %n link(s) and tag(s) updated", "", n).arg(place)
                                : qsTr("Moved to “%1”, %n link(s) updated", "", n).arg(place), "success")
            return
        }
        const title = dialog.isDir ? renamed.split("/").pop() : Workspace.pageTitle(renamed)
        AppController.toast(tags ? qsTr("Renamed to “%1”, %n link(s) and tag(s) updated", "", n).arg(title)
                            : n > 0 ? qsTr("Renamed to “%1”, %n link(s) updated", "", n).arg(title)
                            : qsTr("Renamed to “%1”").arg(title), "success")
    }

    BTextField {
        id: nameField
        objectName: "renameField"
        Layout.fillWidth: true
        placeholderText: dialog.isDir ? qsTr("Folder name") : qsTr("Page title")
        onAccepted: dialog.tryAccept()
    }
    // Where a "/" puts it.
    RowLayout {
        objectName: "renameNewPlace"
        visible: dialog.moves && dialog.useLive && (dialog.live.path || "") !== ""
        Layout.fillWidth: true
        spacing: Theme.space2
        BIcon { name: "folder-input"; size: Theme.iconSizeSm; color: Theme.fgMuted }
        BText {
            Layout.fillWidth: true
            text: qsTr("New place: %1").arg(String(dialog.live.path || "").replace(/\.md$/i, ""))
            variant: "muted"
            elide: Text.ElideMiddle
        }
    }
    BText {
        Layout.fillWidth: true
        visible: !dialog.moves
        text: qsTr("A “/” moves it: “Archive/Home” is a path from the notebook root.")
        variant: "faint"
        font.pixelSize: Theme.textSm
        wrapMode: Text.Wrap
    }
}
