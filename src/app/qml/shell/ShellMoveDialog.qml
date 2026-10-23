pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellMoveDialog — "Przenieś do…": choose a folder for a page, an attachment or a folder
// (Workspace.movePath: every link and tag keeps its target, docs/SYNTAX.md §2). A search field
// over the notebook's folders (TextHelper.fuzzyRank of their names, then paths; at most 100),
// "Główny folder notatnika" first; the item's current folder is shown but disabled, a folder's
// own subtree is left out; the last row "Nowy folder „…”" creates the folder typed and moves
// there. A chosen row shows what the move changes before "Przenieś tutaj" (Workspace.moveImpact:
// "Zaktualizowane linki: 3 (w 2 notatkach)", or why it cannot move). A bottom sheet on phones.
//
// Pick mode (`pickFolder(current)`, the new-page dialog's "Zmień…"): a tap chooses a folder at
// once (`chosen(folder)`, "" = the root); nothing is moved.
//
//   ShellMoveDialog { id: move; onMoveRequested: (path, folder, links) => shell.performMove(...) }
//   move.openFor("Projekty/Dom.md", false)
BDialog {
    id: dialog

    /// "move" | "pick"
    property string mode: "move"
    property string path
    property bool isDir: false
    property string query
    /// The chosen row's folder ("" = the root); `chosen` false: none yet.
    property string target
    property bool hasTarget: false
    /// The chosen row creates its folder first.
    property bool createTarget: false
    /// Workspace.moveImpact() of the chosen folder.
    property var impact: ({})
    /// Pick mode: the folder chosen before (highlighted).
    property string pickCurrent

    /// Pick mode: a folder was chosen ("" = the notebook's root).
    signal chosen(string folder)
    /// Move mode, accepted: move `path` into `folder` (created first when `create`); the shell
    /// does it (Workspace.movePath) and says how it went.
    signal moveRequested(string path, string folder, bool create)

    readonly property string currentFolder: mode === "move" ? Workspace.folderOf(path) : pickCurrent
    readonly property string itemName: path === "" ? "" : path.toLowerCase().endsWith(".md")
                                                           ? Workspace.pageTitle(path) : path.split("/").pop()

    function openFor(itemPath, folder) {
        mode = "move"
        path = itemPath
        isDir = folder === true
        open()
    }
    function pickFolder(current) {
        mode = "pick"
        path = ""
        isDir = false
        pickCurrent = current || ""
        open()
    }
    function choose(row) {
        if (!row || row.disabled)
            return
        if (mode === "pick") {
            let folder = row.folder
            if (row.create) {
                folder = Workspace.createFolder(row.folder)
                if (folder === "")
                    return // the workspace said why
            }
            dialog.chosen(folder)
            dialog.close()
            return
        }
        target = row.folder
        createTarget = row.create === true
        hasTarget = true
        impact = Workspace.moveImpact(path, row.folder)
    }
    /// "Zaktualizowane linki: 3 (w 2 notatkach)" — what the move rewrites.
    function impactText() {
        if (!hasTarget)
            return qsTr("Choose where to move it.")
        if (!impact.ok)
            return impact.error || ""
        if (impact.same)
            return qsTr("It is in this folder already.")
        const n = (impact.links || 0) + (impact.tags || 0)
        let text = n === 0 ? qsTr("No link needs to change.")
                 : ((impact.tags || 0) > 0 ? qsTr("Updated links and tags: %n", "", n) : qsTr("Updated links: %n", "", n))
                   + " " + qsTr("(in %n note(s))", "", impact.notes || 0)
        if (impact.special)
            text += " " + qsTr("Notebook settings will be updated: %1.").arg(specialName(impact.special))
        return text
    }
    function specialName(special) {
        switch (special) {
        case "journals": return qsTr("journals folder")
        case "templates": return qsTr("templates folder")
        case "attachments": return qsTr("attachments folder")
        case "newPages": return qsTr("folder for new pages")
        }
        return special
    }

    /// The rows: the root, the folders matching the query, a new folder of the typed name.
    readonly property var rows: {
        if (!visible)
            return []
        void Workspace.revision
        const own = mode === "move" && isDir ? path : ""
        const folders = Workspace.folderPaths().filter(f => own === "" || (f !== own && !f.startsWith(own + "/")))
        const q = query.trim()
        const out = []
        const rootName = qsTr("Notebook root")
        if (q === "" || TextHelper.fold(rootName).indexOf(TextHelper.fold(q)) >= 0)
            out.push({ folder: "", title: rootName, subtitle: "", icon: "house", create: false })
        const names = folders.map(f => f.split("/").pop())
        for (const r of TextHelper.fuzzyRank(q, names, 100, folders)) {
            const f = folders[r.index]
            out.push({ folder: f, title: names[r.index], icon: "folder", create: false,
                       subtitle: f.indexOf("/") >= 0 ? f.slice(0, f.lastIndexOf("/")) : "" })
        }
        const typed = q.replace(/^\/+|\/+$/g, "")
        if (typed !== "" && !folders.some(f => f.toLowerCase() === typed.toLowerCase()))
            out.push({ folder: typed, title: qsTr("New folder “%1”").arg(typed), icon: "folder-plus", create: true,
                       subtitle: mode === "move" ? qsTr("Create it and move here") : qsTr("Create it") })
        for (const row of out)
            row.disabled = mode === "move" && !row.create && row.folder === currentFolder
        return out
    }

    objectName: "shellMoveDialog"
    title: mode === "pick" ? qsTr("Choose a folder") : qsTr("Move “%1”").arg(itemName)
    subtitle: mode === "pick" ? "" : currentFolder !== "" ? qsTr("Now in %1").arg(currentFolder) : qsTr("Now in the notebook root")
    iconName: mode === "pick" ? "folder" : "folder-input"
    acceptText: qsTr("Move here")
    acceptEnabled: mode === "move" && hasTarget && impact.ok === true && !impact.same
    showFooter: mode === "move"
    initialFocusItem: search
    preferredWidth: Theme.dialogWidth

    onAboutToShow: {
        search.text = ""
        query = ""
        hasTarget = false
        createTarget = false
        target = ""
        impact = ({})
        list.currentIndex = -1
    }
    onAccepted: dialog.moveRequested(dialog.path, dialog.target, dialog.createTarget)

    BSearchField {
        id: search
        objectName: "moveDialogSearch"
        Layout.fillWidth: true
        placeholderText: qsTr("Search folders…")
        onSearch: (text) => {
            dialog.query = text
            dialog.hasTarget = false
            list.currentIndex = -1
        }
        Keys.onDownPressed: {
            list.currentIndex = 0
            list.forceActiveFocus()
        }
        onAccepted: if (dialog.rows.length > 0) dialog.choose(dialog.rows[0])
    }

    ListView {
        id: list
        objectName: "moveDialogList"
        TouchFlick.enabled: true
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(contentHeight, Theme.listItemHeightTwoLine), 340)
        clip: true
        model: dialog.rows
        currentIndex: -1
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: dialog.title
        Keys.onReturnPressed: dialog.choose(dialog.rows[currentIndex])
        Keys.onEnterPressed: dialog.choose(dialog.rows[currentIndex])
        delegate: BListItem {
            required property var modelData
            required property int index
            objectName: "moveDialogRow"
            width: ListView.view.width
            iconName: modelData.icon
            text: modelData.title
            subtitle: modelData.disabled ? qsTr("Here now") : modelData.subtitle
            enabled: !modelData.disabled
            selected: dialog.mode === "move" ? dialog.hasTarget && dialog.target === modelData.folder
                                               && dialog.createTarget === modelData.create
                                             : !modelData.create && modelData.folder === dialog.pickCurrent
            onClicked: dialog.choose(modelData)
        }
        BText {
            anchors.centerIn: parent
            visible: list.count === 0
            text: qsTr("No folders found.")
            variant: "muted"
        }
    }

    BText {
        objectName: "moveDialogImpact"
        visible: dialog.mode === "move"
        Layout.fillWidth: true
        text: dialog.impactText()
        variant: dialog.hasTarget && dialog.impact.ok === false ? "body" : "muted"
        color: dialog.hasTarget && dialog.impact.ok === false ? Theme.dangerText : Theme.fgMuted
        wrapMode: Text.Wrap
    }
}
