pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellRenameDialog — rename a page or a folder. The links and tags that name the page (or a page
// in the folder) are rewritten in every note so each keeps its target (docs/SYNTAX.md §2, §3); the
// dialog says how many — exactly, for the name typed (Workspace.renameImpact: also the links to
// other pages a new name would take over). A "/" in the name moves it — a path from the notebook
// root ("Archiwum/Dom") —, with a live "Nowe miejsce: …" line. (Only here: the page header's
// title edit keeps a "/" in the title.)
BDialog {
    id: dialog

    property string path
    property bool isDir: false
    /// PageTools.renameImpact() of the page: { notes, links, tags } — the links and tags naming it,
    /// which any new name rewrites (said before a name is typed).
    property var impact: ({})
    readonly property int changes: (impact.links || 0) + (impact.tags || 0)
    /// The name typed; a "/" in it moves the page or folder.
    readonly property string typed: nameField.typedText.trim()
    readonly property string currentName: isDir ? path.split("/").pop() : Workspace.pageTitle(path)
    readonly property bool moves: typed.indexOf("/") >= 0
    /// Workspace.renameImpact() of what is typed (exact; a moment after typing stops).
    property var live: ({})
    /// The live impact applies: a new name typed.
    readonly property bool useLive: typed !== "" && typed !== currentName && live.typed === typed

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
        const result = Workspace.renameImpact(path, typed, isDir, true)
        result.typed = typed
        live = result
    }
    /// "Zaktualizowane linki: 3 (w 2 notatkach)", with the notebook settings that follow.
    function liveText() {
        if (!live.ok)
            return live.error || ""
        return words.impactText(live)
    }

    PageTools { id: tools; workspace: Workspace }
    ShellMoveWords { id: words }
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
        // What the rename rewrites, counted now (not a moment ago); a "/" moves.
        const result = Workspace.renameImpact(dialog.path, name, dialog.isDir, true)
        const renamed = dialog.isDir ? Workspace.renameFolder(dialog.path, name, true)
                                     : Workspace.renamePage(dialog.path, name, true)
        if (renamed === "" || renamed === dialog.path)
            return
        const n = (result.links || 0) + (result.tags || 0)
        const tags = (result.tags || 0) > 0
        const title = dialog.isDir ? renamed.split("/").pop() : Workspace.pageTitle(renamed)
        if (moves) {
            // Moved, and renamed too when the name after the last "/" is another one.
            AppController.toast(words.movedText(Workspace.folderOf(renamed), n, tags,
                                                title !== dialog.currentName ? title : ""), "success")
            return
        }
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
