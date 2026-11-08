pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// PageView — route "page" ({path, line?, blockId?, heading?, source?, open?}): one note in the
// block editor (docs/UI.md "The block editor"), with
//   * the page header (qml/content/ContentPageHeader.qml): folder breadcrumb, the title edited in
//     place (renaming rewrites links and the tags that name the page everywhere — a confirmation
//     says how many when other notes link here or carry such a tag), a strip for the tag that
//     names the page (tag overview, its tasks, sub-tags), favourite star, page menu (Version history, Copy link, Export page…,
//     Open in side panel, Markdown source, Move to trash), previous / next day for daily notes and the
//     page's properties (aliases, tags, frontmatter values) as chips;
//   * the blocks (route line / blockId / heading scroll there, select the block and flash it —
//     a heading by PageController.rowOfHeading, Obsidian heading paths too; a block or heading
//     that is not there is said, the page stays at its top; the zoom breadcrumb is the editor's),
//     fold state kept per device (Workspace.foldState);
//   * the references below the blocks (ContentBacklinks: linked references — links and tags
//     naming the page — grouped by page, unlinked mentions with "Link"), scrolling with the
//     page; a page opened from a #tag or a [[link]] before it exists lists them too;
//   * the side panel's context (ContentSidePanel: Links / Outline / Calendar);
//   * a picture attachment (assets/photo.png) as a picture (ContentPictureView: fitted into the
//     view, a click — or the size button — its own pixels, the notes that embed it below; the
//     bar's "Show as text" / "Show picture" and Ctrl+E switch to the file read as text and
//     back — read-only, for text-like assets: a picture's bytes never reach the editor, where a
//     typed character would have been saved over them; the side panel lists its references);
//   * a PDF (assets/lecture.pdf) in the PDF viewer (ContentPdfView, docs/UI.md "PDF viewer": its
//     pages, zoom, the page jump, search, "Open in another app", "Draw on this page";
//     route.pdfPage, route.from / fromLine — where it was opened from —, its own route.open values),
//     never loaded into the editor; any other attachment that is no note (DOCX, ZIP, MP3…) read-only
//     as text (ContentPictureView's `textOnly`): no file's bytes reach the block editor.
// Kept from the first version (and checked by the shell's self-test, objectNames "pageController"
// and "pageEditor"): the route's path is applied one event-loop turn late and
// Workspace.pathRenamed goes to followRename(), so a rename never saves unsaved edits under the
// old name; a page that does not exist yet (a link to a missing page) opens with an empty block
// to type into (a day without a note: the journal template) — except for a page opened from a #tag
// on a touch screen (route.fromTag: the keyboard would cover the tagged blocks listed below; the
// header's "Start writing" or a tap under it starts it) — and the editor creates the file with its
// first change; settings (text width, "open pages as source", Ctrl+E); merged external changes are
// told with a toast.
// The note mode (docs/UI.md "Note modes"): the header's toggle, the page menu's "Switch to
// document / outline" and the palette's "Toggle note mode" (uiAction "toggleNoteMode")
// switch it (PageController.toggleMode writes the `brulion:` key); a change of the notebook's
// default modes re-resolves pages that have no key. "Add property" (the page menu, Ctrl+;, the
// palette's uiAction "addProperty") opens the properties panel's add row (BlockEditor.addProperty);
// whether the panel is open is a device setting (AppSettings.propertiesExpanded). The palette's "Copy link to page / …
// block reference / … link to block / … link to heading" (uiAction "copyLink:<kind>") copy
// through the editor (BlockEditor.copyCurrentLink); the documentation notebook writes no block ids
// (PageController.allowNewBlockIds).
// route.open (screenshots, smoke tests): "rename" (the title field), "export" (the export
// dialog), "menu" (the page menu), "unlinked" (the unlinked mentions, scrolled to), "links" |
// "outline" | "calendar" (the side panel on that tab). route.source opens in source mode.
// A picture takes "links" | "outline" | "calendar" and "text" (its text view).
// Ctrl+F, the page menu's "Find and replace…" and the palette's command open the find bar (FindBar,
// NoteFind; Ctrl+H with its replace row; route.open "find"; docs/UI.md "Find and replace"): in the
// main pane only, and a PDF has its own search. These keys — and Ctrl+E, Ctrl+; — are the
// commands' shortcuts as ShortcutMap has them (custom ones too).
// The word count (docs/UI.md "Statistics and focus mode"): the page's and the selection's words,
// characters and reading time in the corner (pageStatsFooter: wide windows, AppSettings.wordCountShown)
// and at the top of the page menu, counted after a pause (PageController.pageStats / selectionStats);
// typewriter scrolling (AppSettings.typewriterScrolling) keeps the caret's line in the middle.
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, `inSidePanel`, `paneActive`
// (false: the other pane of a split view — the window's shortcuts and the palette's actions are
// the focused pane's); offers `title` (daily notes by their date), `sidePanel`, `editing`,
// `toolbarDocked`, `endEditing()`, `sourceMode` and `viewState()` / `restoreViewState()` (where
// the blocks were scrolled: the tab keeps it while another tab is shown, docs/UI.md "Tabs, split
// view and windows"). `--editor-script <state>` drives the
// editor for screenshots through qml/dev/PageScript.qml.
FocusScope {
    id: root

    property var route: ({})
    /// Shown in the side panel: the window's Ctrl+E and "Markdown source" act on the main view.
    property bool inSidePanel: false
    /// In the focused pane of the window (false: the other pane of a split view; its shortcuts
    /// and the palette's actions are the focused pane's).
    property bool paneActive: true
    /// The window's shortcuts and the shell's actions are this view's.
    readonly property bool commandTarget: root.visible && !root.inSidePanel && root.paneActive

    /// Where the blocks are scrolled: { row (the first one in view), offset (px above it) } —
    /// {} for a picture, a PDF, source mode or a page not loaded.
    function viewState(): var {
        const list = editor.listView
        if (!page.loaded || page.path === "" || !editor.visible || page.sourceMode || !list)
            return {}
        const row = editor.rowAt(list.width / 2, list.contentY + 1)
        const item = row >= 0 ? root.editorItemAt(row) : null
        if (!item) {
            // (`if`, not `cond ? {…} : {}`: that compiles to undefined — docs/UI.md "Compiled QML".)
            if (list.contentY > list.originY + 1)
                return { top: list.contentY - list.originY }
            return {}
        }
        return { row: row, offset: Math.round(list.contentY - item.y) }
    }
    /// The editor list's item of `row` (null: not created).
    function editorItemAt(row: int): Item {
        return editor.listView.itemAtIndex(row)
    }
    /// Scroll back to what viewState() gave, once this route's page is loaded (instead of the
    /// route's own target: switching back to a tab does not flash its block again).
    MapHolder { id: stateInfo }
    function restoreViewState(state: var): void {
        stateInfo.map = state
        const hasRow = stateInfo.map.row !== undefined
        const hasTop = stateInfo.map.top !== undefined
        pending.active = hasRow || hasTop
        pending.hasRow = hasRow
        pending.row = hasRow ? stateInfo.map.row : -1
        pending.offset = stateInfo.map.offset !== undefined ? stateInfo.map.offset : 0
        pending.top = hasTop ? stateInfo.map.top : 0
        pending.path = root.routePath
        if (pending.active)
            root._revealed = root.revealKey(root.routePath)
        applyStateLater.restart()
    }
    // The view state to restore ({ row, offset } or { top }) once this path is loaded, and the one
    // being applied (two event-loop turns later).
    QtObject {
        id: pending
        property bool active: false
        property bool hasRow: false
        property int row: -1
        property int offset: 0
        property real top: 0
        property string path
    }
    QtObject {
        id: applying
        property bool hasRow: false
        property int row: -1
        property int offset: 0
        property real top: 0
    }
    function applyViewState(): void {
        if (!pending.active || !page.loaded || page.loading || page.path !== pending.path)
            return
        pending.active = false
        applying.hasRow = pending.hasRow
        applying.row = pending.row
        applying.offset = pending.offset
        applying.top = pending.top
        // After the editor's own scroll to the top once a page has loaded (BlockEditor).
        applyStateFirst.restart()
    }
    function scrollToAppliedState(): void {
        const list = editor.listView
        editor.markUserMoved()
        if (applying.hasRow && applying.row >= 0 && applying.row < list.count) {
            list.positionViewAtIndex(applying.row, ListView.Beginning)
            list.forceLayout()
            const item = root.editorItemAt(applying.row)
            if (item)
                list.contentY = Math.max(list.originY, item.y + applying.offset)
        } else if (!applying.hasRow) {
            list.contentY = list.originY + applying.top
        }
    }
    // Deferred calls (instead of Qt.callLater and the editor's later()).
    Later {
        id: applyStateLater
        onTriggered: root.applyViewState()
    }
    Later {
        id: applyStateFirst
        onTriggered: applyStateSecond.restart()
    }
    Later {
        id: applyStateSecond
        onTriggered: root.scrollToAppliedState()
    }
    Later {
        id: routeLater
        onTriggered: {
            root._routeQueued = false
            root.applyRoute()
        }
    }
    /// What route.open asks for, done one turn later (openRequested()).
    Later {
        id: openLater
        property string action
        onTriggered: {
            switch (action) {
            case "rename":
                if (root.pageHeader)
                    root.pageHeader.startTitleEdit()
                break
            case "find":
                root.openFind(false)
                break
            case "export":
                exportDialog.openFor(page.path, false)
                break
            default: // a side panel section: a panel made now unfolds it, one shown already too
                AppSettings.setValue("ui/sidePanel/tab", action)
                AppController.sidePanelVisible = true
                root.sidePanelRequested(action)
                break
            }
        }
    }
    /// The route's target ("" when it names none) for `path`: revealTarget() goes there once.
    function revealKey(path: string): string {
        return path + "\n" + AppController.routeText(route, "blockId") + "\n"
                + (Js.hasKey(route, "line") ? AppController.routeText(route, "line") : "-") + "\n"
                + AppController.routeText(route, "heading")
    }

    /// The page's day ("YYYY-MM-DD") when it is a daily note, else "".
    readonly property string journalDate: page.path !== "" ? Workspace.journalDate(page.path) : ""
    /// The page's week or month when it is a weekly or monthly note (Workspace.periodicNoteOf:
    /// { period, start, end, label, template, … }), else {}. (Read again as the notebook changes:
    /// another file name format.)
    readonly property var periodic: page.path !== "" && Workspace.revision >= 0 && journalDate === ""
                                    ? Workspace.periodicNoteOf(page.path) : ({})
    /// Top bar title: daily notes by their date, weekly and monthly notes by their period; other
    /// pages use the shell's default (file name).
    readonly property string title: journalDate !== "" ? DatesHelper.formatLong(journalDate) : AppController.routeText(periodic, "label")
    /// A block (or the source) is being edited, or a picture, a PDF or another file fills the view.
    readonly property bool editing: page.editingRow >= 0 || page.sourceMode || root.pictureRoute || root.pdfRoute
                                    || root.fileRoute
    /// The editor's touch toolbar is docked at the bottom (on the keyboard, or on the phone's
    /// bottom navigation once the keyboard went).
    readonly property bool toolbarDocked: editor.toolbarVisible
    /// The shell's quick capture from the bottom navigation: the editing the docked toolbar is for
    /// ends first, as when the page is left.
    function endEditing(): void {
        editor.endTouchEditing()
    }
    /// The page is shown as Markdown source (the shell turns the palette's block commands off).
    readonly property bool sourceMode: page.sourceMode && !root.pictureRoute && !root.pdfRoute && !root.fileRoute
    /// The note's mode, "document" | "outline" (the top bar's menu shows it); "" while loading and
    /// for a picture, a PDF or another file.
    readonly property string noteMode: !page.loaded || root.pictureRoute || root.pdfRoute || root.fileRoute ? ""
                                       : page.documentMode ? "document" : "outline"
    readonly property Component sidePanel: inSidePanel ? null : sidePanelComponent
    /// The shell's status bar (wide windows): the word count — the selection's while there is
    /// one — and the edited block; "" while there is none (docs/UI.md "Status bar").
    readonly property string statusText: !root.statsFooterShown ? ""
                                         : Js.hasKey(root.selectionStats, "words")
                                           ? qsTr("Selection: %1").arg(root.statsSummary(selectionStatsInfo, false))
                                           : root.statsSummary(pageStatsInfo, false)
    readonly property string statusPosition: page.loaded && !page.sourceMode && page.editingRow >= 0
                                             ? qsTr("Block %1 of %2").arg(page.editingRow + 1).arg(page.blocks.count) : ""
    /// The tab this page's context panel showed last in the side panel ("": never shown).
    property string sidePanelTab: ""
    /// route.open asked for a section of the side panel ("links", "outline", "calendar").
    signal sidePanelRequested(string section)
    /// The side panel shows this page's linked references and unlinked mentions (wide windows,
    /// the panel's rule in AppShell; no page pinned there; its Links tab): the section under
    /// the blocks is not drawn a second time. Closing the panel, pinning a page there or another
    /// tab brings it back at once.
    readonly property bool refsInSidePanel: !root.inSidePanel && !Theme.narrow && AppController.sidePanelVisible
                                            && !Js.hasKey(AppController.sidePanelRoute, "view")
                                            && root.sidePanelTab === "links"
    readonly property alias controller: page
    readonly property alias editor: editor

    readonly property string routePath: AppController.routeText(route, "path")
    /// A drawing of Obsidian's Excalidraw plugin (`Szkic.excalidraw.md`, a note whose frontmatter
    /// has `excalidraw-plugin`) or a `.excalidraw` scene: shown as its drawing, never in the block
    /// editor (its Markdown is the plugin's; "Edit drawing" opens the drawing editor).
    readonly property bool drawingPage: Js.test(root.routePath, "\\.(md|excalidraw)$", "i") && Workspace.isDrawing(root.routePath)
    /// A picture attachment opens as a picture (its bytes are not a note, the editor never loads
    /// them), a drawing page as its drawing; the file read as text, read-only, when asked for
    /// ("Show as text", Ctrl+E).
    readonly property bool pictureRoute: Workspace.isPicture(root.routePath) || root.drawingPage
    property bool pictureAsText: false
    /// A PDF opens in the PDF viewer (its bytes never reach the editor).
    readonly property bool pdfRoute: root.routePath !== "" && Workspace.isPdf(root.routePath)
    /// Any other file that is no note (DOCX, ZIP, MP3…): read-only as text — a typed character
    /// would have been saved over its bytes.
    readonly property bool fileRoute: root.routePath !== "" && !root.pictureRoute && !root.pdfRoute
                                      && !Js.test(root.routePath, "\\.(md|markdown)$", "i")
    /// The view swipes sideways from the left edge itself: a PDF zoomed past the view's width pans
    /// (the drawer's edge strip yields to it, ShellRouteView.viewEdgeSwipe).
    readonly property bool edgeSwipe: root.pdfRoute && pdfLoader.item !== null && (pdfLoader.item as ContentPdfView).pannedSideways
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

    MapHolder { id: firstBlock }
    function updateStartsWithTitle(): void {
        // The first block after the properties: { view, headingLevel, plainText, … }.
        let first = -1
        for (let i = 0; i < page.blocks.count && first < 0; ++i) {
            if (AppController.routeText(page.blocks.get(i), "view") !== "frontmatter")
                first = i
        }
        if (first < 0) {
            startsWithTitle = false
            return
        }
        firstBlock.map = page.blocks.get(first)
        const plain = "" + Js.trim("" + firstBlock.map.plainText)
        const title = "" + Js.trim(page.title)
        startsWithTitle = firstBlock.map.view === "heading" && firstBlock.map.headingLevel === 1
                          && ("" + TextHelper.fold(plain)) === ("" + TextHelper.fold(title))
    }

    // One event-loop turn late: on a rename the shell rewrites its routes before
    // Workspace.pathRenamed reaches followRename() below; setting the new path first would save
    // unsaved edits under the old name and bring that page back as a duplicate.
    // (Once per turn of the event loop; not after the view went away — the shell switching to
    // another view updates the route first.)
    onRouteChanged: {
        if (_routeQueued)
            return
        _routeQueued = true
        routeLater.restart()
    }
    property bool _routeQueued: false
    // A new path: a picture opens as a picture again (before applyRoute, called later).
    onRoutePathChanged: root.pictureAsText = false
    Component.onCompleted: applyRoute()
    // The editor's delegates go first, while the editor still exists (they read its geometry).
    Component.onDestruction: {
        alive = false
        editor.controller = null
        // The look's dialogs live in the overlay: they go with the view.
        if (iconDialog)
            iconDialog.destroy()
        if (coverDialog)
            coverDialog.destroy()
    }
    /// False once the view is being destroyed: a route change queued just before (the shell
    /// switching to another view) does nothing then.
    property bool alive: true

    function applyRoute(): void {
        if (root.routePath === "")
            return
        // A picture's, a PDF's or another file's bytes are not a note: the editor stays unloaded
        // (page.path "" saves the previous page without loading anything); ContentPictureView /
        // ContentPdfView shows it.
        if (root.pictureRoute || root.pdfRoute || root.fileRoute) {
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
            // (Reading mode: the blocks, read-only.)
            page.sourceMode = !page.readOnly
                    && (AppSettings.editorSourceMode || AppController.routeText(root.route, "source") === "true")
            // A page that does not exist yet: something to type into. A day without a note starts
            // from the journal template, like in the journal stream — written only once changed; a
            // week or month without a note from its own template.
            // A page opened from a #tag on a touch screen waits for a tap ("Start writing", the
            // space under the header): the keyboard would cover the blocks with the tag it lists.
            const waits = Theme.touchUi && AppController.routeFlag(root.route, "fromTag") && root.journalDate === ""
            if (page.loaded && !page.exists && page.blocks.count === 0 && !waits && !page.readOnly)
                editor.startEditing(page.startNewPage(root.journalDate !== "" ? (Workspace.config.journalTemplate || "")
                                                                               : AppController.routeText(root.periodic, "template")), -1)
        }
        revealTarget()
        openRequested()
        applyViewState()
    }

    /// The picture's two views: the picture itself ⇄ the file read as text (read-only).
    function showPictureAsText(): void {
        if (root.pictureRoute)
            root.pictureAsText = true
    }
    function showPictureView(): void {
        root.pictureAsText = false
    }
    /// Ctrl+E, "Markdown source" and the bar's button on a picture: the other view.
    function togglePictureText(): void {
        if (root.pictureAsText)
            root.showPictureView()
        else
            root.showPictureAsText()
    }

    /// The fold state being restored (Workspace.foldState()).
    property list<var> _folds
    function restoreFolds(): void {
        if (!page.loaded || root._foldsFor === page.path)
            return
        root._folds = Workspace.foldState(page.path)
        if (root._folds.length > 0)
            page.setFoldState(root._folds)
        root._foldsFor = page.path
    }

    /// Scroll to the block the route names (once per route).
    function revealTarget(): void {
        if (!page.loaded)
            return
        const key = root.revealKey(page.path)
        if (key === root._revealed)
            return
        root._revealed = key
        const blockId = "" + AppController.routeText(route, "blockId")
        const heading = "" + AppController.routeText(route, "heading")
        const line = Js.hasKey(route, "line") ? AppController.routeInt(route, "line", 0) : -1
        let row = -1
        if (blockId !== "")
            row = page.rowOfBlockId(blockId)
        else if (line >= 0)
            row = page.rowOfLine(line)
        else if (heading !== "")
            row = page.rowOfHeading(heading)
        if (row >= 0)
            editor.laterRevealRow(row)
        else if (blockId !== "" || heading !== "")
            editor.noticeMissing(blockId, blockId !== "" ? "" : heading, false)
    }

    /// route.open: a dialog or section opened right away (screenshots, smoke tests).
    function openRequested(): void {
        const open = AppController.routeText(route, "open")
        if (open === "" || (!root.pictureRoute && !root.pdfRoute && !root.fileRoute && !page.loaded))
            return
        const key = root.routePath + "\n" + open
        if (key === root._opened)
            return
        root._opened = key
        // A picture has no title, menu or editor: its text view, or the side panel's tabs (a PDF's own
        // values — search, jump, draw — are ContentPdfView's).
        if ((root.pictureRoute || root.fileRoute) && ["text", "links", "outline", "calendar"].indexOf(open) < 0)
            return
        if (root.pdfRoute && ["links", "outline", "calendar"].indexOf(open) < 0)
            return
        switch (open) {
        case "text":
            root.showPictureAsText()
            break
        case "rename":
        case "find":
        case "export":
            openLater.action = open
            openLater.restart()
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
        case "calendar": {
            // The side panel's context on this tab (wide windows), one turn later: from this
            // view's Component.onCompleted (a picture opens at once) the shell still offers the
            // previous view's side panel, whose context went with that view ("Cannot create a
            // component in an invalid context").
            openLater.action = open
            openLater.restart()
            break
        }
        }
    }

    MapHolder { id: renameImpact }
    function rename(newTitle: string): void {
        page.save()
        // { links, tags, … }
        renameImpact.map = Workspace.renameImpact(page.path, newTitle, false)
        renameDialog.openFor(page.path, newTitle, renameImpact.map, false)
        const links = renameImpact.map.links !== undefined ? renameImpact.map.links : 0
        const tags = renameImpact.map.tags !== undefined ? renameImpact.map.tags : 0
        if (links + tags > 0)
            renameDialog.open()
        else
            renameDialog.rename()
    }

    /// Switch between document and outline (the header's toggle, the menu, the palette).
    function toggleNoteMode(): void {
        if (!page.loaded || editor.readOnlyRefuses())
            return
        if (!page.toggleMode())
            AppController.toast(qsTr("The note mode could not be changed: the page's properties are not in a form Brulion can edit safely. Change them in the Markdown source (Ctrl+E)."), "error")
    }

    // ---- The find bar -------------------------------------------------------------------------
    property bool findOpen: false
    /// A note is shown in the editor: there is something to find in.
    readonly property bool findable: !root.pictureRoute && !root.pdfRoute && !root.fileRoute && page.loaded
    /// "Print…" (the menu, the palette, Ctrl+P): a note — a picture, a PDF or another file has
    /// its own app for that.
    readonly property bool printable: !root.pictureRoute && !root.pdfRoute && !root.fileRoute
    onFindableChanged: if (!findable && findOpen) root.closeFind()
    function openFind(withReplace: bool): void {
        if (!root.findable)
            return
        if (!root.findOpen) {
            noteFind.setOriginHere(editor.sourcePosition())
            root.findOpen = true
        }
        // (The bar's Loader made it just now.)
        const bar = findBarLoader.item as FindBar
        if (!bar)
            return
        if (withReplace)
            bar.openReplace()
        else
            bar.focusField()
    }
    function closeFind(): void {
        root.findOpen = false
        editor.findClosed()
    }

    NoteFind {
        id: noteFind
        objectName: "pageFind"
        controller: page
        active: root.findOpen
    }
    Loader {
        id: findBarLoader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        active: root.findOpen
        height: active && item !== null ? (item as Item).implicitHeight : 0
        sourceComponent: FindBar {
            find: noteFind
            editor: editor
            onCloseRequested: root.closeFind()
        }
    }
    // (The shortcuts of the views' commands can be changed: ShortcutMap.)
    Shortcut {
        sequence: ShortcutMap.sequences["page.find"] || ""
        enabled: root.commandTarget && root.findable
        onActivated: root.openFind(false)
    }
    Shortcut {
        sequence: ShortcutMap.sequences["page.replace"] || ""
        enabled: root.commandTarget && root.findable
        onActivated: root.openFind(true)
    }

    // ---- Statistics (docs/UI.md "Statistics and focus mode") -------------------------------------
    /// The page's counts ({ words, characters, charactersNoSpaces, readingMinutes, length }:
    /// PageController.pageStats), counted again a moment after the page changed — later on long
    /// pages, never on every keystroke —; {} for a picture, a PDF or another file.
    property var pageStats: ({})
    /// The selection's counts while text or blocks are selected, else {}.
    property var selectionStats: ({})
    readonly property bool statsAvailable: page.loaded && !root.pictureRoute && !root.pdfRoute && !root.fileRoute
    /// The counts in the view's corner (wide windows, the setting on; the page menu has them always).
    readonly property bool statsFooterShown: AppSettings.wordCountShown && !Theme.narrow && !root.inSidePanel
                                             && root.statsAvailable && root.visible
    onStatsAvailableChanged: root.countNow()
    onStatsFooterShownChanged: if (statsFooterShown) root.countNow()
    function countNow(): void {
        statsTimer.stop()
        root.pageStats = root.statsAvailable ? page.pageStats() : ({})
        root.countSelection()
    }
    function scheduleCount(): void {
        if (!root.statsFooterShown)
            return // counted when the menu opens
        // About 5 ms per 100 000 characters: a long page waits longer after the last change.
        statsTimer.interval = Math.min(3000, 400 + Math.round(AppController.routeInt(root.pageStats, "length", 0) / 100))
        statsTimer.restart()
    }
    function countSelection(): void {
        selectionTimer.stop()
        const edit = editor.activeEditor
        // (The selected text is "" without a selection.)
        const text = edit ? "" + edit.selectedText : ""
        root.selectionStats = root.statsAvailable && (text !== "" || page.selectionCount > 0)
                              ? page.selectionStats(text) : ({})
    }
    /// "123 words · 820 characters · 1 min read" (`reading`: with the reading time and the
    /// characters without spaces).
    // The counts as typed maps (bound, never written by statsSummary(): it runs in bindings).
    MapHolder { id: pageStatsInfo; map: root.pageStats }
    MapHolder { id: selectionStatsInfo; map: root.selectionStats }
    function statsSummary(stats: MapHolder, details: bool): string {
        const s = stats.map
        if (s.words === undefined)
            return ""
        const words = s.words
        const characters = s.characters
        const noSpaces = s.charactersNoSpaces
        const minutes = s.readingMinutes
        return qsTr("%n word(s)", "", words) + " · " + qsTr("%n character(s)", "", characters)
                + (details ? " · " + qsTr("%n without spaces", "characters", noSpaces) : "")
                + (minutes > 0 ? " · " + qsTr("%n min read", "", minutes) : "")
    }
    Timer {
        id: statsTimer
        onTriggered: root.pageStats = root.statsAvailable ? page.pageStats() : ({})
    }
    Timer {
        id: selectionTimer
        interval: 150
        onTriggered: root.countSelection()
    }
    Connections {
        target: page.blocks
        function onModelReset() { root.scheduleCount() }
        function onRowsInserted() { root.scheduleCount() }
        function onRowsRemoved() { root.scheduleCount() }
        function onDataChanged() { root.scheduleCount() }
    }
    Connections {
        target: page
        function onSourceTextChanged() { root.scheduleCount() }
        function onSelectionChanged() { if (root.statsFooterShown) selectionTimer.restart() }
        function onPathChanged() { root.pageStats = ({}); root.selectionStats = ({}) }
        function onEditingRowChanged() { if (root.statsFooterShown) selectionTimer.restart() }
    }
    Connections {
        target: editor.activeEditor
        enabled: root.statsFooterShown
        ignoreUnknownSignals: true
        function onSelectionStartChanged() { selectionTimer.restart() }
        function onSelectionEndChanged() { selectionTimer.restart() }
    }

    function copyLink(): void {
        const link = tools.wikiLink(page.path)
        if (tools.copyText(link))
            AppController.toast(qsTr("Copied %1").arg(link), "success")
    }

    PageController {
        id: page
        objectName: "pageController" // the shell's self-test finds the editor by it
        host: Workspace
        // The documentation's pages stay as shipped (they are brought up to date on every open).
        allowNewBlockIds: !AppController.inDocsVault
        // Reading mode (docs/UI.md "Reading mode"): a device setting.
        readOnly: AppSettings.readingMode
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
                root.applyViewState()
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
            editor.markUserMoved()
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
        function onPathRenamed(oldPath: string, newPath: string) {
            if (page.path === oldPath) {
                page.followRename(oldPath, newPath)
                root._foldsFor = newPath
            } else if (Js.startsWith(page.path, oldPath + "/")) {
                const moved = newPath + Js.sliceFrom(page.path, oldPath.length)
                page.followRename(page.path, moved)
                root._foldsFor = moved
            }
        }
    }

    BlockEditor {
        id: editor
        objectName: "pageEditor"
        anchors.top: findBarLoader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        find: root.findOpen ? noteFind : null
        // A picture, a PDF or another file fills the view instead (its bytes are never loaded into
        // the editor).
        visible: !root.pictureRoute && !root.pdfRoute && !root.fileRoute
        focus: !root.pdfRoute
        controller: page
        // The reading mode's toast: "Edit" leaves it.
        onReadOnlyLeaveRequested: AppSettings.readingMode = false
        // Problems the editor reports (a picture that could not be added) as the window's toast.
        onMessage: (text, kind, action) => {
            if (action) {
                root.messageAction = action
                root.messageToast = AppController.toastWithAction(text, kind, action.text)
            } else {
                AppController.toast(text, kind)
            }
        }
        maxTextWidth: AppSettings.editorWidth > 0 ? AppSettings.editorWidth : 100000
        // The properties panel open or folded: a device setting (the panel's header switches it);
        // bound here for the rows made with the editor, then by the Binding below.
        propertiesExpanded: AppSettings.propertiesExpanded
        onPropertiesExpandedToggled: (expanded) => AppSettings.propertiesExpanded = expanded
        // The header below shows the tags and aliases as chips while the panel is folded.
        headerShowsListChips: true
        // The side panel is narrow: phone-like margins there.
        horizontalPadding: Theme.compact || root.inSidePanel ? Theme.space3 : Theme.space8
        // Typewriter scrolling: room under the last block to bring it to the middle.
        bottomSpace: (refsSection.visible ? root.refsGap + refsSection.height + Theme.space16 : Theme.space16 * 2)
                     + (typewriter ? height / 2 : 0)
        typewriter: AppSettings.typewriterScrolling && !root.inSidePanel
        // The editor's own keys as the shortcuts are now (ShortcutMap).
        copyRefShortcut: ShortcutMap.sequences["block.copyRef"] || ""
        sourceModeShortcut: ShortcutMap.sequences["page.source"] || ""
        header: Component {
            ContentPageHeader {
                id: pageHeader
                width: editor.width
                controller: page
                editor: editor
                tools: tools
                journalDate: root.journalDate
                periodic: root.periodic
                startsWithTitle: root.startsWithTitle
                inSidePanel: root.inSidePanel
                onRenameRequested: (newTitle) => root.rename(newTitle)
                onModeToggleRequested: root.toggleNoteMode()
                onIconRequested: root.openIconDialog()
                onCoverRequested: root.openCoverDialog()
                onMenuRequested: (anchor) => pageMenu.popup(anchor, anchor.width - pageMenu.width, anchor.height)
                Component.onCompleted: root.pageHeader = pageHeader
                Component.onDestruction: if (root.pageHeader === pageHeader) root.pageHeader = null
            }
        }
    }

    Binding {
        target: editor
        property: "propertiesExpanded"
        value: AppSettings.propertiesExpanded
    }

    // The page's word count in the corner (the selection's while there is one); its details in a
    // tooltip. Not over a docked touch toolbar.
    Rectangle {
        id: statsFooter
        objectName: "pageStatsFooter"
        // (In the main window's focused pane the shell's status bar shows it instead.)
        visible: root.statsFooterShown && Js.hasKey(root.pageStats, "words") && !editor.toolbarVisible
                 && !(root.paneActive && AppController.statusBarShown)
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.space4
        anchors.bottomMargin: Theme.space2
        z: 2
        width: statsLabel.implicitWidth + 2 * Theme.space2
        height: statsLabel.implicitHeight + Theme.space1 * 2
        radius: Theme.radiusSm
        color: Theme.bg.alpha(0.85)
        readonly property bool selection: Js.hasKey(root.selectionStats, "words")
        readonly property string text: selection ? qsTr("Selection: %1").arg(root.statsSummary(selectionStatsInfo, false))
                                                 : root.statsSummary(pageStatsInfo, false)
        Accessible.role: Accessible.StaticText
        Accessible.name: selection ? qsTr("Selection: %1").arg(root.statsSummary(selectionStatsInfo, true))
                                   : root.statsSummary(pageStatsInfo, true)
        BText {
            id: statsLabel
            anchors.centerIn: parent
            text: statsFooter.text
            font.pixelSize: Theme.textXs
            color: Theme.fgMuted
        }
        HoverHandler { id: statsHover }
        BToolTip {
            visible: statsHover.hovered
            text: statsFooter.Accessible.name
        }
    }

    // A picture attachment: the picture itself or its text, read-only (the editor behind it
    // stays unloaded — page.path "" — and hidden). Created for pictures only.
    Loader {
        anchors.fill: parent
        active: root.pictureRoute || root.fileRoute
        sourceComponent: ContentPictureView {
            path: root.routePath
            // Another file than a picture: only its text, read-only.
            textOnly: root.fileRoute
            asText: root.pictureAsText || root.fileRoute
            rendererPalette: editor.rendererPalette
            onTextToggleRequested: root.togglePictureText()
        }
    }

    // A PDF: the PDF viewer (the editor behind it stays unloaded and hidden). Created for PDFs only.
    Loader {
        id: pdfLoader
        objectName: "pdfLoader"
        anchors.fill: parent
        active: root.pdfRoute
        focus: root.pdfRoute
        sourceComponent: ContentPdfView {
            path: root.routePath
            route: root.route
            inSidePanel: root.inSidePanel
            paneActive: root.paneActive
            rendererPalette: editor.rendererPalette
            refsInSidePanel: root.refsInSidePanel
        }
        // The viewer takes the keys (PageDown, Ctrl+F, Esc…) when it opens in the main pane.
        onLoaded: if (!root.inSidePanel) pdfFocusLater.restart()
    }
    Later {
        id: pdfFocusLater
        onTriggered: if (pdfLoader.item !== null) (pdfLoader.item as Item).forceActiveFocus()
    }
    /// The editor's last message with an action (a toast's button runs it: messageToast).
    property EditorToastAction messageAction: null
    property int messageToast: -1
    Connections {
        target: AppController
        function onToastActionTriggered(id: int) {
            if (id === root.messageToast && root.messageAction)
                root.messageAction.run()
        }
    }
    /// The editor list's footer (the references go right below it).
    readonly property Item editorFooter: editor.listView.footerItem

    // The references live in the editor's scrolling content, right under the blocks (in the
    // space the editor's footer keeps free: bottomSpace).
    ContentBacklinks {
        id: refsSection
        objectName: "pageReferences" // the shell's self-test reads linkedCount
        parent: editor.listView.contentItem
        // (Not drawn while the side panel shows the same references: refsInSidePanel. The model
        // stays alive.)
        visible: page.loaded && !page.sourceMode && page.zoomBlock < 0 && page.path !== "" && !root.refsInSidePanel
        x: editor.contentX + (root.inSidePanel || Theme.compact || editor.documentMode ? 0 : editor.arrowColumn)
        y: (root.editorFooter ? root.editorFooter.y : 0) + root.refsGap
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
        onAboutToShow: root.countNow()

        // The page's counts (and the selection's), for reading only.
        BMenuItem {
            objectName: "pageMenuStats"
            visible: root.statsAvailable && Js.hasKey(root.pageStats, "words")
            height: visible ? implicitHeight : 0
            enabled: false
            text: root.statsSummary(pageStatsInfo, true)
            iconName: "type"
        }
        BMenuSeparator {
            visible: root.statsAvailable && Js.hasKey(root.pageStats, "words")
            height: visible ? implicitHeight : 0
        }

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
            objectName: "pageMenuExport"
            text: qsTr("Export page…")
            iconName: "share"
            enabled: page.exists
            onTriggered: exportDialog.openFor(page.path, false)
        }
        BMenuItem {
            objectName: "pageMenuPrint"
            visible: root.printable
            height: visible ? implicitHeight : 0
            text: qsTr("Print…")
            iconName: "printer"
            shortcut: root.inSidePanel ? "" : ShortcutMap.sequences["page.print"] || ""
            enabled: page.exists
            onTriggered: exportDialog.openFor(page.path, true)
        }
        BMenuItem {
            visible: root.journalDate === "" && page.exists
            height: visible ? implicitHeight : 0
            text: qsTr("Rename…")
            iconName: "pencil"
            shortcut: root.inSidePanel ? "" : ShortcutMap.sequences["page.rename"] || ""
            onTriggered: if (root.pageHeader) root.pageHeader.startTitleEdit()
        }
        BMenuItem {
            objectName: "pageMenuMove"
            visible: root.journalDate === "" && page.exists
            height: visible ? implicitHeight : 0
            text: qsTr("Move to…")
            iconName: "folder-input"
            onTriggered: AppController.requestItemAction("move", page.path)
        }
        BMenuItem {
            objectName: "pageMenuFind"
            visible: !root.inSidePanel
            height: visible ? implicitHeight : 0
            text: page.readOnly ? qsTr("Find…") : qsTr("Find and replace…")
            iconName: "search"
            shortcut: ShortcutMap.sequences["page.find"] || ""
            enabled: root.findable
            onTriggered: root.openFind(false)
        }
        BMenuItem {
            visible: !Theme.narrow && !root.inSidePanel
            height: visible ? implicitHeight : 0
            text: qsTr("Open in side panel")
            iconName: "panel-right"
            onTriggered: AppController.openPage(page.path, -1, true)
        }
        // A page named by a tag in use: Tasks with this tag (Tag overview is the header's "#").
        BMenuItem {
            objectName: "pageMenuTagTasks"
            visible: root.pageHeader !== null && root.pageHeader.tagInUse
            height: visible ? implicitHeight : 0
            text: qsTr("Tasks with this tag")
            iconName: "list-todo"
            onTriggered: AppController.navigate({ view: "tasks", query: root.pageHeader.tagQuery(root.pageHeader.tagKey) })
        }
        // Reading mode (docs/UI.md "Reading mode"; also the top bar's book, the palette, the keys).
        BMenuItem {
            objectName: "pageMenuReadingMode"
            text: AppSettings.readingMode ? qsTr("Leave reading mode") : qsTr("Reading mode")
            iconName: AppSettings.readingMode ? "pencil" : "book-open"
            shortcut: root.inSidePanel ? "" : ShortcutMap.sequences["view.reading"] || ""
            onTriggered: AppSettings.readingMode = !AppSettings.readingMode
        }
        // The note mode (also the header's toggle on a wide header, More, the palette).
        BMenuItem {
            objectName: "pageMenuNoteMode"
            text: page.documentMode ? qsTr("Switch to outline") : qsTr("Switch to document")
            iconName: page.documentMode ? "list" : "file-text"
            enabled: page.loaded
            unavailable: page.readOnly ? editor.readOnlyReason : ""
            onTriggered: root.toggleNoteMode()
        }
        // The page's look (docs/UI.md "Page icon and cover").
        BMenuItem {
            objectName: "pageMenuIcon"
            text: AppController.routeFlag(page.look.icon, "kind") ? qsTr("Change icon…") : qsTr("Add icon…")
            iconName: "sparkles"
            enabled: page.loaded && !root.pictureRoute
            unavailable: page.readOnly ? editor.readOnlyReason : ""
            onTriggered: root.openIconDialog()
        }
        BMenuItem {
            objectName: "pageMenuCover"
            text: AppController.routeFlag(page.look.cover, "kind") ? qsTr("Change cover…") : qsTr("Add cover…")
            iconName: "image"
            enabled: page.loaded && !root.pictureRoute
            unavailable: page.readOnly ? editor.readOnlyReason : ""
            onTriggered: root.openCoverDialog()
        }
        BMenuItem {
            objectName: "pageMenuAddProperty"
            text: qsTr("Add property")
            iconName: "plus"
            shortcut: root.inSidePanel ? "" : ShortcutMap.sequences["page.addProperty"] || ""
            enabled: page.loaded && !page.sourceMode && !root.pictureRoute
            unavailable: page.readOnly ? editor.readOnlyReason : ""
            onTriggered: editor.addProperty()
        }
        BMenuItem {
            text: page.sourceMode ? qsTr("Edit as blocks") : qsTr("Markdown source")
            iconName: "code"
            shortcut: root.inSidePanel ? "" : ShortcutMap.sequences["page.source"] || ""
            unavailable: page.readOnly ? editor.readOnlyReason : ""
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

    // Made when first opened.
    property ContentPageIconDialog iconDialog: null
    property ContentPageCoverDialog coverDialog: null
    function openIconDialog(): void {
        if (!page.loaded || editor.readOnlyRefuses())
            return
        if (!iconDialog)
            iconDialog = iconDialogComponent.createObject(T.Overlay.overlay, { controller: page, editor: editor }) as ContentPageIconDialog
        iconDialog.open()
    }
    function openCoverDialog(): void {
        if (!page.loaded || editor.readOnlyRefuses())
            return
        if (!coverDialog)
            coverDialog = coverDialogComponent.createObject(T.Overlay.overlay, { controller: page, editor: editor }) as ContentPageCoverDialog
        coverDialog.open()
    }
    Component {
        id: iconDialogComponent
        ContentPageIconDialog { objectName: "pageIconDialog" }
    }
    Component {
        id: coverDialogComponent
        ContentPageCoverDialog { objectName: "pageCoverDialog" }
    }
    ContentRenameDialog {
        id: renameDialog
        objectName: "pageRenameDialog"
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
            const trashPath = Workspace.deletePage(page.path)
            if (trashPath !== "") {
                // Restored by the controller: the toast outlives this view (the page closes when deleted).
                AppController.toastRestorePage(qsTr("Moved to trash"), qsTr("Undo"), trashPath)
            }
        }
    }

    Component {
        id: sidePanelComponent
        ContentSidePanel {
            id: contextPanel
            // A picture's references (the index knows the notes that embed it; nothing loads it).
            path: root.pictureRoute || root.pdfRoute || root.fileRoute ? root.routePath : page.path
            attachment: root.pictureRoute || root.pdfRoute || root.fileRoute
            controller: page
            editor: editor
            Connections {
                target: root
                function onSidePanelRequested(section: string) {
                    AppSettings.setValue("ui/sidePanel/tab", "")
                    contextPanel.reveal(section)
                }
            }
            // Its tab, for refsInSidePanel (made again with the remembered tab each time the side
            // panel shows it).
            onCurrentTabChanged: root.sidePanelTab = contextPanel.currentTab
            Component.onCompleted: root.sidePanelTab = contextPanel.currentTab
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
        function onUiActionRequested(action: string) {
            if (!root.paneActive)
                return
            if (action === "toggleSourceMode" && root.visible && !root.inSidePanel) {
                // A picture: Ctrl+E / "Markdown source" switch between the picture and its text;
                // a PDF and another file have no Markdown.
                if (root.pictureRoute)
                    root.togglePictureText()
                else if (!root.pdfRoute && !root.fileRoute)
                    editor.toggleSourceMode()
            } else if (action === "toggleNoteMode" && root.visible && !root.inSidePanel) {
                root.toggleNoteMode()
            } else if ((action === "findInNote" || action === "replaceInNote") && root.visible && !root.inSidePanel) {
                root.openFind(action === "replaceInNote")
            } else if (Js.startsWith(action, "copyLink:") && root.visible && !root.inSidePanel && !root.pictureRoute
                       && !root.pdfRoute && !root.fileRoute) {
                editor.copyCurrentLink(Js.sliceFrom(action, 9))
            } else if (action === "saveWebPictures" && root.visible && !root.inSidePanel && editor.visible) {
                editor.downloadWebPictures("")
            } else if (action === "addProperty" && root.visible && !root.inSidePanel && editor.visible) {
                editor.addProperty()
            } else if (action === "taskClock" && root.visible && !root.inSidePanel && editor.visible) {
                editor.toggleCurrentTaskClock()
            } else if (action === "taskDependency" && root.visible && !root.inSidePanel && editor.visible) {
                editor.openCurrentDependencyPicker()
            } else if ((action === "exportPage" || action === "printPage") && root.visible && !root.inSidePanel
                       && page.exists && !root.pictureRoute && !root.pdfRoute && !root.fileRoute) {
                exportDialog.openFor(page.path, action === "printPage")
            }
        }
    }

    // "Add property" (docs/UI.md "Page properties"): the panel's add row.
    Shortcut {
        sequence: ShortcutMap.sequences["page.addProperty"] || ""
        enabled: root.commandTarget && editor.visible && page.loaded && !page.sourceMode
        onActivated: editor.addProperty()
    }

    Shortcut {
        sequence: ShortcutMap.sequences["page.source"] || ""
        enabled: root.commandTarget // one per window (two would be ambiguous)
        // A picture: the picture ⇄ its text (read-only); a PDF, another file: nothing.
        onActivated: {
            if (root.pictureRoute)
                root.togglePictureText()
            else if (!root.pdfRoute && !root.fileRoute)
                editor.toggleSourceMode()
        }
    }
}
