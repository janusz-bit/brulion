pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellMoveDialog — "Move to…": choose a folder for a page, an attachment or a folder
// (Workspace.movePath: every link and tag keeps its target, docs/SYNTAX.md §2). A search field
// over the notebook's folders (TextHelper.fuzzyRank of their names, then paths; at most 100),
// "Notebook root" first; the item's current folder is shown but disabled, a folder's
// own subtree is left out; the last row "New folder “…”" creates the folder typed and moves
// there. A chosen row shows what the move changes before "Move here" (Workspace.moveImpact:
// "Updated links: 3 (in 2 notes)", or why it cannot move). A bottom sheet on phones.
//
// Pick mode (`pickFolder(current)`, the new-page dialog's "Change…"): a tap chooses a folder at
// once (`chosen(folder)`, "" = the root); nothing is moved.
//
//   ShellMoveDialog { id: move; onMoveRequested: (path, folder, links) => shell.performMove(...) }
//   move.openFor("Projects/Home.md", false)
BDialog {
    id: dialog

    /// "move" | "pick"
    property string mode: "move"
    property string path
    property bool isDir: false
    property string query
    /// The chosen row's folder ("" = the root); `hasTarget` false: none yet.
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
    readonly property string itemName: path === "" ? "" : Js.endsWith(Js.toLowerCase(path), ".md")
                                                           ? Workspace.pageTitle(path) : Js.sliceFrom(path, Js.lastIndexOf(path, "/") + 1)
    /// Workspace.moveImpact()'s fields.
    readonly property bool impactOk: AppController.routeFlag(impact, "ok")
    readonly property bool impactSame: AppController.routeFlag(impact, "same")
    /// `ok` is there and false (an empty impact: none yet).
    readonly property bool impactRefused: Js.hasKey(impact, "ok") && !impactOk

    function openFor(itemPath: string, folder: bool): void {
        dialog.mode = "move"
        dialog.path = itemPath
        dialog.isDir = folder
        dialog.open()
    }
    function pickFolder(current: string): void {
        dialog.mode = "pick"
        dialog.path = ""
        dialog.isDir = false
        dialog.pickCurrent = current
        dialog.open()
    }
    /// Choose the row of `folder` (`create`: the row creating it).
    function choose(folder: string, create: bool): void {
        if (dialog.mode === "pick") {
            let chosenFolder = folder
            if (create) {
                chosenFolder = Workspace.createFolder(folder)
                if (chosenFolder === "")
                    return // the workspace said why
            }
            dialog.chosen(chosenFolder)
            dialog.close()
            return
        }
        dialog.target = folder
        dialog.createTarget = create
        dialog.hasTarget = true
        dialog.impact = Workspace.moveImpact(dialog.path, folder)
    }
    /// Choose row `index` of `rows` (none, or a disabled one: nothing).
    function chooseAt(index: int): void {
        if (index < 0 || index >= dialog.rows.length)
            return
        if (dialog.rowFlag(index, "disabled"))
            return
        dialog.choose(dialog.rowText(index, "folder"), dialog.rowFlag(index, "create"))
    }
    /// A field of row `index` (typed).
    function rowText(index: int, key: string): string {
        return AppController.routeText(dialog.rows[index], key)
    }
    function rowFlag(index: int, key: string): bool {
        return AppController.routeFlag(dialog.rows[index], key)
    }
    /// "Updated links: 3 (in 2 notes)" — what the move rewrites, with every notebook
    /// setting that follows.
    function impactText(): string {
        if (!dialog.hasTarget)
            return qsTr("Choose where to move it.")
        if (!dialog.impactOk)
            return AppController.routeText(dialog.impact, "error")
        if (dialog.impactSame)
            return qsTr("It is in this folder already.")
        return words.impactText(words.count(dialog.impact, "links"), words.count(dialog.impact, "tags"),
                                words.count(dialog.impact, "notes"), words.strings(dialog.impact, "specials"))
    }

    ShellMoveWords { id: words }

    /// The rows: the root, the folders matching the query, a new folder of the typed name —
    /// { folder, title, subtitle, icon, create, disabled } (AppController.folderChoices).
    readonly property list<var> rows: {
        // (Read for real: a bare `void Workspace.revision` is compiled away with the dependency.)
        if (!visible || Workspace.revision < 0)
            return []
        return AppController.folderChoices(query, mode === "move" && isDir ? path : "", currentFolder, mode === "move",
                                           qsTr("Notebook root"), qsTr("New folder “%1”"),
                                           mode === "move" ? qsTr("Create it and move here") : qsTr("Create it"))
    }

    objectName: "shellMoveDialog"
    title: mode === "pick" ? qsTr("Choose a folder") : qsTr("Move “%1”").arg(itemName)
    subtitle: mode === "pick" ? "" : currentFolder !== "" ? qsTr("Now in %1").arg(currentFolder) : qsTr("Now in the notebook root")
    iconName: mode === "pick" ? "folder" : "folder-input"
    acceptText: qsTr("Move here")
    acceptEnabled: mode === "move" && hasTarget && impactOk && !impactSame
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
        onAccepted: dialog.chooseAt(0)
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
        Keys.onReturnPressed: dialog.chooseAt(list.currentIndex)
        Keys.onEnterPressed: dialog.chooseAt(list.currentIndex)
        delegate: BListItem {
            id: row
            required property int index
            // (The row's fields: AppController.folderChoices.)
            // (`icon` and `subtitle` are the item's own properties: read from the row.)
            required property string folder
            required property string title
            required property bool create
            required property bool disabled
            readonly property string rowIcon: index < dialog.rows.length ? AppController.routeText(dialog.rows[index], "icon") : ""
            readonly property string folderSubtitle: index < dialog.rows.length ? AppController.routeText(dialog.rows[index], "subtitle") : ""
            objectName: "moveDialogRow"
            width: ListView.view.width
            iconName: row.rowIcon
            text: row.title
            subtitle: row.disabled ? qsTr("Here now") : row.folderSubtitle
            enabled: !row.disabled
            selected: dialog.mode === "move" ? dialog.hasTarget && dialog.target === row.folder
                                               && dialog.createTarget === row.create
                                             : !row.create && row.folder === dialog.pickCurrent
            onClicked: dialog.chooseAt(row.index)
        }
        BText {
            // (Declared in the list, so its default parent is the content item, which has no height
            // without rows: the list itself.)
            parent: list
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
        // (impact.ok false: a move that cannot be made, said in the danger colour.)
        variant: dialog.hasTarget && dialog.impactRefused ? "body" : "muted"
        color: dialog.hasTarget && dialog.impactRefused ? Theme.dangerText : Theme.fgMuted
        wrapMode: Text.Wrap
    }
}
