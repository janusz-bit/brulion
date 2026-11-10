pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellRenameDialog — rename a page or a folder. The links and tags that name the page (or a page
// in the folder) are rewritten in every note so each keeps its target (docs/SYNTAX.md §2, §3); the
// dialog says how many — exactly, for the name typed (Workspace.renameImpact: also the links to
// other pages a new name would take over). A "/" in the name moves it — a path from the notebook
// root ("Archive/Home") —, with a live "New place: …" line. What is wrong with the name typed (a
// page or folder of that name there already, a hidden name) shows at the field, Rename disabled;
// the subtitle only ever says what changes. (Only here: the page header's title edit keeps a "/"
// in the title.)
BDialog {
    id: dialog

    property string path
    property bool isDir: false
    /// PageTools.renameImpact() of the page: { notes, links, tags } — the links and tags naming it,
    /// which any new name rewrites (said before a name is typed).
    property var impact: ({})
    readonly property int impactTags: AppController.routeInt(impact, "tags", 0)
    readonly property int changes: AppController.routeInt(impact, "links", 0) + impactTags
    /// The name typed; a "/" in it moves the page or folder.
    readonly property string typed: Js.trim(nameField.typedText)
    readonly property string currentName: isDir ? Js.sliceFrom(path, Js.lastIndexOf(path, "/") + 1) : Workspace.pageTitle(path)
    readonly property bool moves: Js.includes(typed, "/")
    /// Workspace.renameImpact() of what is typed (exact; a moment after typing stops), and the
    /// name it is for.
    property var live: ({})
    property string liveTyped
    readonly property bool liveOk: AppController.routeFlag(live, "ok")
    readonly property string livePath: AppController.routeText(live, "path")
    /// The live impact applies: a new name typed.
    readonly property bool useLive: typed !== "" && typed !== currentName && liveTyped === typed
    /// Why the name typed cannot be used ("" when it can): the field's error.
    readonly property string liveError: useLive && !liveOk ? AppController.routeText(live, "error") : ""

    function openFor(itemPath: string, folder: bool): void {
        dialog.path = itemPath
        dialog.isDir = folder
        dialog.impact = dialog.isDir ? ({}) : tools.renameImpact(itemPath)
        dialog.live = ({})
        dialog.liveTyped = ""
        nameField.text = dialog.isDir ? Js.sliceFrom(itemPath, Js.lastIndexOf(itemPath, "/") + 1) : Workspace.pageTitle(itemPath)
        dialog.open()
    }
    function computeLive(): void {
        if (!dialog.opened || dialog.typed === "" || dialog.typed === dialog.currentName) {
            dialog.live = ({})
            dialog.liveTyped = ""
            return
        }
        dialog.live = Workspace.renameImpact(dialog.path, dialog.typed, dialog.isDir, true)
        dialog.liveTyped = dialog.typed
    }
    property var _result: ({})
    function textOf(text: string): string {
        return text
    }
    /// "Updated links: 3 (in 2 notes)", with the notebook settings that follow.
    function liveText(): string {
        return words.impactText(words.count(dialog.live, "links"), words.count(dialog.live, "tags"),
                                words.count(dialog.live, "notes"), words.strings(dialog.live, "specials"))
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
    subtitle: useLive && liveOk ? liveText()
              : isDir ? qsTr("Links and tags that name the folder are updated.")
              : dialog.changes === 0 ? qsTr("Links and tags that name this page are updated in all notes.")
              : (dialog.impactTags > 0 ? qsTr("%n link(s) and tag(s) %1 will be updated to the new name.", "", dialog.changes)
                                       : qsTr("%n link(s) %1 will be updated to the new name.", "", dialog.changes))
                .arg(qsTr("in %n note(s)", "", AppController.routeInt(dialog.impact, "notes", 0)))
    iconName: "pencil"
    acceptText: moves ? qsTr("Move") : qsTr("Rename")
    acceptEnabled: typed !== "" && (!useLive || dialog.liveOk)
    initialFocusItem: nameField
    preferredWidth: Theme.dialogWidthSm

    onOpened: nameField.selectAll()
    onAccepted: {
        const name = dialog.typed
        // What the rename rewrites, counted now (not a moment ago); a "/" moves.
        // (Typed copies — a property, typed functions: a call's result read after another call
        // is not compiled.)
        dialog._result = Workspace.renameImpact(dialog.path, name, dialog.isDir, true)
        const resultTags = words.count(dialog._result, "tags")
        const n = words.count(dialog._result, "links") + resultTags
        const renamed = dialog.textOf(dialog.isDir ? Workspace.renameFolder(dialog.path, name, true)
                                                   : Workspace.renamePage(dialog.path, name, true))
        if (renamed === "" || renamed === dialog.path)
            return
        const tags = resultTags > 0
        const title = dialog.textOf(dialog.isDir ? Js.sliceFrom(renamed, Js.lastIndexOf(renamed, "/") + 1)
                                                 : Workspace.pageTitle(renamed))
        if (dialog.moves) {
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
        errorText: dialog.liveError
        onAccepted: dialog.tryAccept()
    }
    // Where a "/" puts it.
    RowLayout {
        objectName: "renameNewPlace"
        visible: dialog.moves && dialog.useLive && dialog.livePath !== ""
        Layout.fillWidth: true
        spacing: Theme.space2
        BIcon { name: "folder-input"; size: Theme.iconSizeSm; color: Theme.fgMuted }
        BText {
            Layout.fillWidth: true
            text: qsTr("New place: %1").arg(Js.replaceRegExp(dialog.livePath, "\\.md$", "i", ""))
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
