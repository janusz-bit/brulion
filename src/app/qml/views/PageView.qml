pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// PageView — route "page" ({path, line?, blockId?, heading?, source?, open?}): one note in the
// block editor (docs/UI.md "The block editor"), with
//   * the page header (qml/content/ContentPageHeader.qml): folder breadcrumb, the title edited in
//     place (renaming rewrites links everywhere — a confirmation says how many when other notes
//     link here), favourite star, page menu (Historia wersji, Kopiuj odnośnik, Eksportuj stronę,
//     panel boczny, tekst źródłowy, Przenieś do kosza), previous / next day for daily notes and the
//     page's properties (aliases, tags, frontmatter values) as chips;
//   * the blocks (route line / blockId / heading scroll there and select the block; the zoom
//     breadcrumb is the editor's), fold state kept per device (Workspace.foldState);
//   * the references below the blocks (ContentBacklinks: linked references grouped by page,
//     unlinked mentions with "Połącz"), scrolling with the page;
//   * the side panel's context (ContentSidePanel: Odnośniki / Konspekt / Kalendarz);
//   * a picture attachment (assets/photo.png) as a picture (ContentPictureView: fitted into the
//     view, a click — or the size button — its own pixels, the notes that embed it below; the
//     bar's "Pokaż jako tekst" / "Pokaż obraz" and Ctrl+E switch to the file read as text and
//     back — read-only, for text-like assets: a picture's bytes never reach the editor, where a
//     typed character would have been saved over them; the side panel lists its references).
// Kept from the first version (and checked by the shell's self-test, objectNames "pageController"
// and "pageEditor"): the route's path is applied one event-loop turn late and
// Workspace.pathRenamed goes to followRename(), so a rename never saves unsaved edits under the
// old name; a page that does not exist yet (a link to a missing page) opens with an empty block
// to type into (a day without a note: the journal template) and the editor creates the file with
// its first change; settings (text width,
// "open pages as source", Ctrl+E); merged external changes are told with a toast.
// The note mode (docs/UI.md "Note modes"): the header's toggle, the page menu's "Przełącz na
// dokument / konspekt" and the palette's "Przełącz tryb notatki" (uiAction "toggleNoteMode")
// switch it (PageController.toggleMode writes the `brulion:` key); a change of the notebook's
// default modes re-resolves pages that have no key.
// route.open (screenshots, smoke tests): "rename" (the title field), "export" (the export
// dialog), "menu" (the page menu), "unlinked" (the unlinked mentions, scrolled to), "links" |
// "outline" | "calendar" (the side panel on that tab). route.source opens in source mode.
// A picture takes "links" | "outline" | "calendar" and "text" (its text view).
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, `inSidePanel`; offers `title`
// (daily notes by their date), `sidePanel` and `editing`. `--editor-script <state>` drives the
// editor for screenshots through qml/dev/PageScript.qml.
FocusScope {
    id: root

    property var route: ({})
    /// Shown in the side panel: the window's Ctrl+E and "Markdown source" act on the main view.
    property bool inSidePanel: false

    /// The page's day ("YYYY-MM-DD") when it is a daily note, else "".
    readonly property string journalDate: page.path !== "" ? Workspace.journalDate(page.path) : ""
    /// Top bar title: daily notes by their date; other pages use the shell's default (file name).
    readonly property string title: journalDate !== "" ? DatesHelper.formatLong(journalDate) : ""
    /// A block (or the source) is being edited, or a picture fills the view: the shell hides
    /// its floating capture button (it would float over the photo).
    readonly property bool editing: page.editingRow >= 0 || page.sourceMode || root.pictureRoute
    readonly property Component sidePanel: inSidePanel ? null : sidePanelComponent
    readonly property alias controller: page
    readonly property alias editor: editor

    readonly property string routePath: route && route.path ? route.path : ""
    /// A picture attachment opens as a picture (its bytes are not a note, the editor never loads
    /// them); the file read as text, read-only, when asked for ("Pokaż jako tekst", Ctrl+E).
    readonly property bool pictureRoute: Workspace.isPicture(root.routePath)
    property bool pictureAsText: false
    property string _revealed: ""
    property string _opened: ""
    property bool _openedMissing: false
    /// Fold state is stored for this path once it was restored after loading.
    property string _foldsFor: ""
    /// The first block repeats the title as "# Title" (after the properties): no second title.
    property bool startsWithTitle: false
    /// The header in the editor (created by the editor's list; null until then).
    property ContentPageHeader pageHeader: null
    /// Space between the last block and the references (a tap there adds a block).
    readonly property int refsGap: Theme.space12

    focus: true

    function updateStartsWithTitle() {
        let first = null
        for (let i = 0; i < page.blocks.count && first === null; ++i) {
            const b = page.blocks.get(i)
            if (b.view !== "frontmatter")
                first = b
        }
        startsWithTitle = first !== null && first.view === "heading" && first.headingLevel === 1
                          && TextHelper.fold(first.plainText.trim()) === TextHelper.fold(page.title.trim())
    }

    // One event-loop turn late: on a rename the shell rewrites its routes before
    // Workspace.pathRenamed reaches followRename() below; setting the new path first would save
    // unsaved edits under the old name and bring that page back as a duplicate.
    onRouteChanged: Qt.callLater(root.applyRoute)
    // A new path: a picture opens as a picture again (before applyRoute, called later).
    onRoutePathChanged: root.pictureAsText = false
    Component.onCompleted: applyRoute()
    // The editor's delegates go first, while the editor still exists (they read its geometry).
    Component.onDestruction: editor.controller = null

    function applyRoute() {
        if (root.routePath === "")
            return
        // A picture's bytes are not a note: the editor stays unloaded (page.path "" saves the
        // previous page without loading anything); ContentPictureView shows the picture.
        if (root.pictureRoute) {
            if (page.path !== "") {
                root._revealed = ""
                root._foldsFor = ""
                page.path = ""
            }
            root.openRequested()
            return
        }
        if (page.path !== root.routePath) {
            root._revealed = ""
            root._opened = ""
            root._foldsFor = ""
            page.path = root.routePath // saves the previous page, loads this one
            root.restoreFolds()
            root.updateStartsWithTitle()
            root._openedMissing = page.loaded && !page.exists
            page.sourceMode = AppSettings.editorSourceMode || root.route.source === true
            // A page that does not exist yet: something to type into. A day without a note starts
            // from the journal template, like in the journal stream — written only once changed.
            if (page.loaded && !page.exists && page.blocks.count === 0)
                editor.startEditing(page.startNewPage(root.journalDate !== "" ? (Workspace.config.journalTemplate || "") : ""), -1)
        }
        revealTarget()
        openRequested()
    }

    /// The picture's two views: the picture itself ⇄ the file read as text (read-only).
    function showPictureAsText() {
        if (root.pictureRoute)
            root.pictureAsText = true
    }
    function showPictureView() {
        root.pictureAsText = false
    }
    /// Ctrl+E, "Markdown source" and the bar's button on a picture: the other view.
    function togglePictureText() {
        if (root.pictureAsText)
            root.showPictureView()
        else
            root.showPictureAsText()
    }

    function restoreFolds() {
        if (!page.loaded || root._foldsFor === page.path)
            return
        const folds = Workspace.foldState(page.path)
        if (folds.length > 0)
            page.setFoldState(folds)
        root._foldsFor = page.path
    }

    /// Scroll to the block the route names (once per route).
    function revealTarget() {
        if (!page.loaded || !route)
            return
        const key = JSON.stringify([page.path, route.blockId, route.line, route.heading])
        if (key === root._revealed)
            return
        root._revealed = key
        let row = -1
        if (route.blockId)
            row = page.rowOfBlockId(route.blockId)
        else if (route.line !== undefined)
            row = page.rowOfLine(route.line)
        else if (route.heading)
            row = headingRow(route.heading)
        if (row >= 0)
            editor.later(() => editor.revealRow(row))
    }

    /// route.open: a dialog or section opened right away (screenshots, smoke tests).
    function openRequested() {
        if (!route || !route.open || (!root.pictureRoute && !page.loaded))
            return
        const key = root.routePath + "\n" + route.open
        if (key === root._opened)
            return
        root._opened = key
        // A picture has no title, menu or editor: its text view, or the side panel's tabs.
        if (root.pictureRoute && ["text", "links", "outline", "calendar"].indexOf(route.open) < 0)
            return
        switch (route.open) {
        case "text":
            root.showPictureAsText()
            break
        case "rename":
            editor.later(() => { if (root.pageHeader) root.pageHeader.startTitleEdit() })
            break
        case "export":
            editor.later(() => exportDialog.openFor(page.path))
            break
        case "menu":
            menuTimer.restart()
            break
        case "unlinked":
            refsSection.unlinkedOpen = true
            scrollToEnd.restart()
            break
        case "links":
        case "outline":
        case "calendar":
            // The side panel's context on this tab (wide windows).
            AppSettings.setValue("ui/sidePanel/tab", route.open)
            AppController.sidePanelVisible = true
            break
        }
    }

    /// Row of the heading `name` ([[Page#Heading]]), -1 when there is none.
    function headingRow(name) {
        const wanted = TextHelper.fold(String(name).trim())
        for (let i = 0; i < page.blocks.count; ++i) {
            const b = page.blocks.get(i)
            if (b.headingLevel > 0 && TextHelper.fold(b.plainText.trim()) === wanted)
                return i
        }
        return -1
    }

    /// The title was edited: rename, confirming first when other notes link here.
    function rename(newTitle) {
        page.save()
        const impact = tools.renameImpact(page.path)
        if (impact.links > 0) {
            renameDialog.openFor(page.path, newTitle, impact)
        } else {
            renameDialog.path = page.path
            renameDialog.newTitle = newTitle
            renameDialog.notes = 0
            renameDialog.links = 0
            renameDialog.rename()
        }
    }

    /// Switch between document and outline (the header's toggle, the menu, the palette).
    function toggleNoteMode() {
        if (!page.loaded)
            return
        if (!page.toggleMode())
            AppController.toast(qsTr("The note mode could not be changed: the page's properties are not in a form Brulion can edit safely. Change them in the Markdown source (Ctrl+E)."), "error")
    }

    function copyLink() {
        const link = tools.wikiLink(page.path)
        if (tools.copyText(link))
            AppController.toast(qsTr("Copied %1").arg(link), "success")
    }

    PageController {
        id: page
        objectName: "pageController" // the shell's self-test finds the editor by it
        host: Workspace
        // A page opened before it existed (a link to a missing page) joins the recent pages
        // once its first content created it.
        onExistsChanged: {
            if (page.exists && root._openedMissing && root.journalDate === "")
                Workspace.notePageOpened(page.path)
            root._openedMissing = false
        }
        onExternallyChanged: (merged) => {
            if (merged)
                AppController.toast(qsTr("Merged changes made elsewhere into this page"), "info")
        }
        // Folds are device-local: remembered per page (they follow renames in the Workspace).
        onFoldStateChanged: {
            if (page.loaded && !page.loading && root._foldsFor === page.path)
                Workspace.setFoldState(page.path, page.foldState)
        }
        onLoadingChanged: {
            if (page.loaded && !page.loading) {
                root.restoreFolds()
                root.revealTarget()
                root.openRequested()
            }
        }
    }

    PageTools { id: tools; workspace: Workspace }

    // route.open "menu": once the header has its buttons.
    Timer {
        id: menuTimer
        interval: 300
        onTriggered: if (root.pageHeader && root.pageHeader.menuAnchor) root.pageHeader.menuRequested(root.pageHeader.menuAnchor)
    }

    // route.open "unlinked": once the editor settled (it scrolls to the top after loading).
    Timer {
        id: scrollToEnd
        interval: 400
        onTriggered: {
            editor.listView.userMoved = true
            editor.listView.positionViewAtEnd()
        }
    }

    Connections {
        target: page.blocks
        function onModelReset() { root.updateStartsWithTitle() }
        function onRowsInserted() { root.updateStartsWithTitle() }
        function onRowsRemoved() { root.updateStartsWithTitle() }
        function onDataChanged() { root.updateStartsWithTitle() }
    }

    Connections {
        target: Workspace
        // The notebook's default modes changed (settings, another device).
        function onConfigChanged() { page.refreshMode() }
        function onPathRenamed(oldPath, newPath) {
            if (page.path === oldPath) {
                page.followRename(oldPath, newPath)
                root._foldsFor = newPath
            } else if (page.path.startsWith(oldPath + "/")) {
                const moved = newPath + page.path.slice(oldPath.length)
                page.followRename(page.path, moved)
                root._foldsFor = moved
            }
        }
    }

    BlockEditor {
        id: editor
        objectName: "pageEditor"
        anchors.fill: parent
        // A picture fills the view instead (its bytes are never loaded into the editor).
        visible: !root.pictureRoute
        controller: page
        // Problems the editor reports (a picture that could not be added) as the window's toast.
        onNotice: (text, kind) => AppController.toast(text, kind)
        focus: true
        maxTextWidth: AppSettings.editorWidth > 0 ? AppSettings.editorWidth : 100000
        // The side panel is narrow: phone-like margins there.
        horizontalPadding: Theme.compact || root.inSidePanel ? Theme.space3 : Theme.space8
        bottomSpace: refsSection.visible ? root.refsGap + refsSection.height + Theme.space16 : Theme.space16 * 2
        header: Component {
            ContentPageHeader {
                id: pageHeader
                width: editor.width
                controller: page
                editor: editor
                tools: tools
                journalDate: root.journalDate
                startsWithTitle: root.startsWithTitle
                inSidePanel: root.inSidePanel
                onRenameRequested: (newTitle) => root.rename(newTitle)
                onModeToggleRequested: root.toggleNoteMode()
                onMenuRequested: (anchor) => pageMenu.popup(anchor, anchor.width - pageMenu.width, anchor.height)
                Component.onCompleted: root.pageHeader = pageHeader
                Component.onDestruction: if (root.pageHeader === pageHeader) root.pageHeader = null
            }
        }
    }

    // A picture attachment: the picture itself or its text, read-only (the editor behind it
    // stays unloaded — page.path "" — and hidden). Created for pictures only.
    Loader {
        anchors.fill: parent
        active: root.pictureRoute
        sourceComponent: ContentPictureView {
            path: root.routePath
            asText: root.pictureAsText
            rendererPalette: editor.rendererPalette
            onTextToggleRequested: root.togglePictureText()
        }
    }

    // The references live in the editor's scrolling content, right under the blocks (in the
    // space the editor's footer keeps free: bottomSpace).
    ContentBacklinks {
        id: refsSection
        parent: editor.listView.contentItem
        visible: page.loaded && !page.sourceMode && page.zoomBlock < 0 && page.path !== ""
        x: editor.contentX + (root.inSidePanel || Theme.compact || editor.documentMode ? 0 : editor.arrowColumn)
        y: (editor.listView.footerItem ? editor.listView.footerItem.y : 0) + root.refsGap
        width: editor.contentWidth - (root.inSidePanel || Theme.compact || editor.documentMode ? 0 : editor.arrowColumn)
        path: page.path
        rendererPalette: editor.rendererPalette
        compact: Theme.compact || root.inSidePanel
        showUnlinked: !root.inSidePanel

        Rectangle {
            // A rule between the page and its references.
            y: -Theme.space4
            width: parent.width
            height: 1
            color: Theme.borderSubtle
        }
        MouseArea {
            // Taps between the references must not reach the editor's "add a block" area.
            anchors.fill: parent
            z: -1
        }
    }

    BMenu {
        id: pageMenu

        BMenuItem {
            text: qsTr("Version history")
            iconName: "history"
            enabled: page.exists
            onTriggered: AppController.navigate({ view: "history", path: page.path })
        }
        BMenuItem {
            text: qsTr("Copy link %1").arg(tools.wikiLink(page.path))
            iconName: "link"
            onTriggered: root.copyLink()
        }
        BMenuItem {
            text: qsTr("Export page…")
            iconName: "share"
            enabled: page.exists
            onTriggered: exportDialog.openFor(page.path)
        }
        BMenuItem {
            visible: root.journalDate === "" && page.exists
            height: visible ? implicitHeight : 0
            text: qsTr("Rename…")
            iconName: "pencil"
            onTriggered: if (root.pageHeader) root.pageHeader.startTitleEdit()
        }
        BMenuItem {
            visible: !Theme.narrow && !root.inSidePanel
            height: visible ? implicitHeight : 0
            text: qsTr("Open in side panel")
            iconName: "panel-right"
            onTriggered: AppController.openPage(page.path, -1, true)
        }
        BMenuItem {
            text: page.documentMode ? qsTr("Switch to outline") : qsTr("Switch to document")
            iconName: page.documentMode ? "list" : "file-text"
            enabled: page.loaded
            onTriggered: root.toggleNoteMode()
        }
        BMenuItem {
            text: page.sourceMode ? qsTr("Edit as blocks") : qsTr("Markdown source")
            iconName: "code"
            shortcut: root.inSidePanel ? "" : "Ctrl+E"
            onTriggered: editor.toggleSourceMode()
        }
        BMenuSeparator {}
        BMenuItem {
            text: qsTr("Move to trash")
            iconName: "trash-2"
            danger: true
            enabled: page.exists
            onTriggered: deleteDialog.open()
        }
    }

    ContentRenameDialog {
        id: renameDialog
        parent: T.Overlay.overlay
    }
    ContentExportDialog {
        id: exportDialog
        parent: T.Overlay.overlay
        tools: tools
    }
    BConfirmDialog {
        id: deleteDialog
        parent: T.Overlay.overlay
        title: qsTr("Move to trash?")
        message: qsTr("“%1” will be moved to the trash. You can restore it from there.")
                 .arg(root.journalDate !== "" ? DatesHelper.formatLong(root.journalDate) : page.title)
        confirmText: qsTr("Move to trash")
        destructive: true
        onAccepted: {
            page.save()
            if (Workspace.deletePage(page.path) !== "")
                AppController.toast(qsTr("Moved to trash"), "info")
        }
    }

    Component {
        id: sidePanelComponent
        ContentSidePanel {
            // A picture's references (the index knows the notes that embed it; nothing loads it).
            path: root.pictureRoute ? root.routePath : page.path
            controller: page
            editor: editor
        }
    }

    // Dev: `--editor-script <state>` puts this editor into a state for screenshots of the real
    // app (qml/dev/PageScript.qml, tests/app/shell/screenshots.sh "page-*" shots).
    Loader {
        active: DevTools.editorScript !== "" && !DevTools.editor && !root.inSidePanel
        sourceComponent: PageScript {
            editor: editor
            controller: page
        }
    }

    // Command palette / page menu: "Markdown source".
    Connections {
        target: AppController
        function onUiActionRequested(action) {
            if (action === "toggleSourceMode" && root.visible && !root.inSidePanel) {
                // A picture: Ctrl+E / "Markdown source" switch between the picture and its text.
                if (root.pictureRoute)
                    root.togglePictureText()
                else
                    editor.toggleSourceMode()
            } else if (action === "toggleNoteMode" && root.visible && !root.inSidePanel)
                root.toggleNoteMode()
        }
    }

    Shortcut {
        sequence: "Ctrl+E"
        enabled: root.visible && !root.inSidePanel // one per window (two would be ambiguous)
        // A picture: the picture ⇄ its text (read-only).
        onActivated: root.pictureRoute ? root.togglePictureText() : editor.toggleSourceMode()
    }
}
