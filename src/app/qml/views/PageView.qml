pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// PageView — route "page" ({path, line?, blockId?, heading?, source?, open?}): one note in the
// block editor (docs/UI.md "The block editor"), with
//   * the page header (qml/content/ContentPageHeader.qml): folder breadcrumb, the title edited in
//     place (renaming rewrites links and the tags that name the page everywhere — a confirmation
//     says how many when other notes link here or carry such a tag), a strip for the tag that
//     names the page (tag overview, its tasks, sub-tags), favourite star, page menu (Historia wersji, Kopiuj odnośnik, Eksportuj stronę,
//     panel boczny, tekst źródłowy, Przenieś do kosza), previous / next day for daily notes and the
//     page's properties (aliases, tags, frontmatter values) as chips;
//   * the blocks (route line / blockId / heading scroll there, select the block and flash it —
//     a heading by PageController.rowOfHeading, Obsidian heading paths too; a block or heading
//     that is not there is said, the page stays at its top; the zoom breadcrumb is the editor's),
//     fold state kept per device (Workspace.foldState);
//   * the references below the blocks (ContentBacklinks: linked references — links and tags
//     naming the page — grouped by page, unlinked mentions with "Połącz"), scrolling with the
//     page; a page opened from a #tag or a [[link]] before it exists lists them too;
//   * the side panel's context (ContentSidePanel: Odnośniki / Konspekt / Kalendarz);
//   * a picture attachment (assets/photo.png) as a picture (ContentPictureView: fitted into the
//     view, a click — or the size button — its own pixels, the notes that embed it below; the
//     bar's "Pokaż jako tekst" / "Pokaż obraz" and Ctrl+E switch to the file read as text and
//     back — read-only, for text-like assets: a picture's bytes never reach the editor, where a
//     typed character would have been saved over them; the side panel lists its references);
//   * a PDF (assets/wykład.pdf) in the PDF viewer (ContentPdfView, docs/UI.md "PDF viewer": its
//     pages, zoom, the page jump, search, "Otwórz w innej aplikacji", "Rysuj na tej stronie";
//     route.pdfPage, route.from / fromLine — where it was opened from —, its own route.open values),
//     never loaded into the editor; any other attachment that is no note (DOCX, ZIP, MP3…) read-only
//     as text (ContentPictureView's `textOnly`): no file's bytes reach the block editor.
// Kept from the first version (and checked by the shell's self-test, objectNames "pageController"
// and "pageEditor"): the route's path is applied one event-loop turn late and
// Workspace.pathRenamed goes to followRename(), so a rename never saves unsaved edits under the
// old name; a page that does not exist yet (a link to a missing page) opens with an empty block
// to type into (a day without a note: the journal template) — except for a page opened from a #tag
// on a touch screen (route.fromTag: the keyboard would cover the tagged blocks listed below; the
// header's "Zacznij pisać" or a tap under it starts it) — and the editor creates the file with its
// first change; settings (text width, "open pages as source", Ctrl+E); merged external changes are
// told with a toast.
// The note mode (docs/UI.md "Note modes"): the header's toggle, the page menu's "Przełącz na
// dokument / konspekt" and the palette's "Przełącz tryb notatki" (uiAction "toggleNoteMode")
// switch it (PageController.toggleMode writes the `brulion:` key); a change of the notebook's
// default modes re-resolves pages that have no key. "Dodaj właściwość" (the page menu, Ctrl+;, the
// palette's uiAction "addProperty") opens the properties panel's add row (BlockEditor.addProperty);
// whether the panel is open is a device setting (AppSettings.propertiesExpanded). The palette's "Kopiuj link do strony / …
// odwołanie do bloku / … link do bloku / … link do nagłówka" (uiAction "copyLink:<kind>") copy
// through the editor (BlockEditor.copyCurrentLink); the documentation notebook writes no block ids
// (PageController.allowNewBlockIds).
// route.open (screenshots, smoke tests): "rename" (the title field), "export" (the export
// dialog), "menu" (the page menu), "unlinked" (the unlinked mentions, scrolled to), "links" |
// "outline" | "calendar" (the side panel on that tab). route.source opens in source mode.
// A picture takes "links" | "outline" | "calendar" and "text" (its text view).
// Ctrl+F, the page menu's "Znajdź i zamień…" and the palette's command open the find bar (FindBar,
// NoteFind; Ctrl+H with its replace row; route.open "find"; docs/UI.md "Find and replace"): in the
// main pane only, and a PDF has its own search.
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
    function viewState() {
        const list = editor.listView
        if (!page.loaded || page.path === "" || !editor.visible || page.sourceMode || !list)
            return {}
        const row = editor.rowAt(list.width / 2, list.contentY + 1)
        const item = row >= 0 ? list.itemAtIndex(row) : null
        if (!item)
            return list.contentY > list.originY + 1 ? { top: list.contentY - list.originY } : {}
        return { row: row, offset: Math.round(list.contentY - item.y) }
    }
    /// Scroll back to what viewState() gave, once this route's page is loaded (instead of the
    /// route's own target: switching back to a tab does not flash its block again).
    function restoreViewState(state) {
        root._pendingState = state && (state.row !== undefined || state.top !== undefined)
                ? { state: state, path: root.routePath } : null
        if (root._pendingState)
            root._revealed = JSON.stringify([root.routePath, route.blockId, route.line, route.heading])
        Qt.callLater(() => { if (root && root.alive) root.applyViewState() })
    }
    property var _pendingState: null
    function applyViewState() {
        const pending = root._pendingState
        if (!pending || !page.loaded || page.loading || page.path !== pending.path)
            return
        root._pendingState = null
        // After the editor's own scroll to the top once a page has loaded (BlockEditor).
        editor.later(() => editor.later(() => {
            const list = editor.listView
            const s = pending.state
            list.userMoved = true
            if (s.row !== undefined && s.row >= 0 && s.row < list.count) {
                list.positionViewAtIndex(s.row, ListView.Beginning)
                list.forceLayout()
                const item = list.itemAtIndex(s.row)
                if (item)
                    list.contentY = Math.max(list.originY, item.y + (s.offset || 0))
            } else if (s.top !== undefined) {
                list.contentY = list.originY + s.top
            }
        }))
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
    readonly property string title: journalDate !== "" ? DatesHelper.formatLong(journalDate) : (periodic.label || "")
    /// A block (or the source) is being edited, or a picture, a PDF or another file fills the view.
    readonly property bool editing: page.editingRow >= 0 || page.sourceMode || root.pictureRoute || root.pdfRoute
                                    || root.fileRoute
    /// The editor's touch toolbar is docked at the bottom (on the keyboard, or on the phone's
    /// bottom navigation once the keyboard went).
    readonly property bool toolbarDocked: editor.toolbarVisible
    /// The shell's quick capture from the bottom navigation: the editing the docked toolbar is for
    /// ends first, as when the page is left.
    function endEditing() {
        editor.endTouchEditing()
    }
    /// The page is shown as Markdown source (the shell turns the palette's block commands off).
    readonly property bool sourceMode: page.sourceMode && !root.pictureRoute && !root.pdfRoute && !root.fileRoute
    readonly property Component sidePanel: inSidePanel ? null : sidePanelComponent
    /// The tab this page's context panel showed last in the side panel ("": never shown).
    property string sidePanelTab: ""
    /// The side panel shows this page's linked references and unlinked mentions (wide windows,
    /// the panel's rule in AppShell; no page pinned there; its Odnośniki tab): the section under
    /// the blocks is not drawn a second time. Closing the panel, pinning a page there or another
    /// tab brings it back at once.
    readonly property bool refsInSidePanel: !root.inSidePanel && !Theme.narrow && AppController.sidePanelVisible
                                            && !(AppController.sidePanelRoute && AppController.sidePanelRoute.view !== undefined)
                                            && root.sidePanelTab === "links"
    readonly property alias controller: page
    readonly property alias editor: editor

    readonly property string routePath: route && route.path ? route.path : ""
    /// A drawing of Obsidian's Excalidraw plugin (`Szkic.excalidraw.md`, a note whose frontmatter
    /// has `excalidraw-plugin`) or a `.excalidraw` scene: shown as its drawing, never in the block
    /// editor (its Markdown is the plugin's; "Edytuj rysunek" opens the drawing editor).
    readonly property bool drawingPage: /\.(md|excalidraw)$/i.test(root.routePath) && Workspace.isDrawing(root.routePath)
    /// A picture attachment opens as a picture (its bytes are not a note, the editor never loads
    /// them), a drawing page as its drawing; the file read as text, read-only, when asked for
    /// ("Pokaż jako tekst", Ctrl+E).
    readonly property bool pictureRoute: Workspace.isPicture(root.routePath) || root.drawingPage
    property bool pictureAsText: false
    /// A PDF opens in the PDF viewer (its bytes never reach the editor).
    readonly property bool pdfRoute: root.routePath !== "" && Workspace.isPdf(root.routePath)
    /// Any other file that is no note (DOCX, ZIP, MP3…): read-only as text — a typed character
    /// would have been saved over its bytes.
    readonly property bool fileRoute: root.routePath !== "" && !root.pictureRoute && !root.pdfRoute
                                      && !/\.(md|markdown)$/i.test(root.routePath)
    /// The view swipes sideways from the left edge itself: a PDF zoomed past the view's width pans
    /// (the drawer's edge strip yields to it, ShellRouteView.viewEdgeSwipe).
    readonly property bool edgeSwipe: root.pdfRoute && pdfLoader.item !== null && pdfLoader.item.pannedSideways
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
    // (Once per turn of the event loop; not after the view went away — the shell switching to
    // another view updates the route first.)
    onRouteChanged: {
        if (_routeQueued)
            return
        _routeQueued = true
        Qt.callLater(() => {
            if (!root || !root.alive)
                return
            root._routeQueued = false
            root.applyRoute()
        })
    }
    property bool _routeQueued: false
    // A new path: a picture opens as a picture again (before applyRoute, called later).
    onRoutePathChanged: root.pictureAsText = false
    Component.onCompleted: applyRoute()
    // The editor's delegates go first, while the editor still exists (they read its geometry).
    Component.onDestruction: {
        alive = false
        editor.controller = null
    }
    /// False once the view is being destroyed: a route change queued just before (the shell
    /// switching to another view) does nothing then.
    property bool alive: true

    function applyRoute() {
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
            page.sourceMode = AppSettings.editorSourceMode || root.route.source === true
            // A page that does not exist yet: something to type into. A day without a note starts
            // from the journal template, like in the journal stream — written only once changed; a
            // week or month without a note from its own template.
            // A page opened from a #tag on a touch screen waits for a tap ("Zacznij pisać", the
            // space under the header): the keyboard would cover the blocks with the tag it lists.
            const waits = Theme.touchUi && !!root.route.fromTag && root.journalDate === ""
            if (page.loaded && !page.exists && page.blocks.count === 0 && !waits)
                editor.startEditing(page.startNewPage(root.journalDate !== "" ? (Workspace.config.journalTemplate || "")
                                                                               : (root.periodic.template || "")), -1)
        }
        revealTarget()
        openRequested()
        applyViewState()
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
            row = page.rowOfHeading(route.heading)
        if (row >= 0)
            editor.later(() => editor.revealRow(row))
        else if (route.blockId || route.heading)
            editor.noticeMissing(route.blockId || "", route.blockId ? "" : route.heading)
    }

    /// route.open: a dialog or section opened right away (screenshots, smoke tests).
    function openRequested() {
        if (!route || !route.open || (!root.pictureRoute && !root.pdfRoute && !root.fileRoute && !page.loaded))
            return
        const key = root.routePath + "\n" + route.open
        if (key === root._opened)
            return
        root._opened = key
        // A picture has no title, menu or editor: its text view, or the side panel's tabs (a PDF's own
        // values — search, jump, draw — are ContentPdfView's).
        if ((root.pictureRoute || root.fileRoute) && ["text", "links", "outline", "calendar"].indexOf(route.open) < 0)
            return
        if (root.pdfRoute && ["links", "outline", "calendar"].indexOf(route.open) < 0)
            return
        switch (route.open) {
        case "text":
            root.showPictureAsText()
            break
        case "rename":
            editor.later(() => { if (root.pageHeader) root.pageHeader.startTitleEdit() })
            break
        case "find":
            editor.later(() => root.openFind(false))
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
        case "calendar": {
            // The side panel's context on this tab (wide windows), one turn later: from this
            // view's Component.onCompleted (a picture opens at once) the shell still offers the
            // previous view's side panel, whose context went with that view ("Cannot create a
            // component in an invalid context").
            const tab = route.open
            editor.later(() => {
                AppSettings.setValue("ui/sidePanel/tab", tab)
                AppController.sidePanelVisible = true
            })
            break
        }
        }
    }

     function rename(newTitle) {
        page.save()
        const impact = Workspace.renameImpact(page.path, newTitle, false)
        renameDialog.openFor(page.path, newTitle, impact, false)
        if ((impact.links || 0) + (impact.tags || 0) > 0)
            renameDialog.open()
        else
            renameDialog.rename()
    }

    /// Switch between document and outline (the header's toggle, the menu, the palette).
    function toggleNoteMode() {
        if (!page.loaded)
            return
        if (!page.toggleMode())
            AppController.toast(qsTr("The note mode could not be changed: the page's properties are not in a form Brulion can edit safely. Change them in the Markdown source (Ctrl+E)."), "error")
    }

    // ---- The find bar -------------------------------------------------------------------------
    property bool findOpen: false
    /// A note is shown in the editor: there is something to find in.
    readonly property bool findable: !root.pictureRoute && !root.pdfRoute && !root.fileRoute && page.loaded
    onFindableChanged: if (!findable && findOpen) root.closeFind()
    function openFind(withReplace) {
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
    function closeFind() {
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
        height: active && item ? item.implicitHeight : 0
        sourceComponent: FindBar {
            find: noteFind
            editor: editor
            onCloseRequested: root.closeFind()
        }
    }
    Shortcut {
        sequence: "Ctrl+F"
        enabled: root.commandTarget && root.findable
        onActivated: root.openFind(false)
    }
    Shortcut {
        sequence: "Ctrl+H"
        enabled: root.commandTarget && root.findable
        onActivated: root.openFind(true)
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
        // The documentation's pages stay as shipped (they are brought up to date on every open).
        allowNewBlockIds: !AppController.inDocsVault
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
        // Problems the editor reports (a picture that could not be added) as the window's toast.
        onMessage: (text, kind, action) => action ? AppController.toastAction(text, kind, action.text, action.run)
                                                  : AppController.toast(text, kind)
        maxTextWidth: AppSettings.editorWidth > 0 ? AppSettings.editorWidth : 100000
        // The properties panel open or folded: a device setting (the panel's header switches it);
        // bound here for the rows made with the editor, then by the Binding below.
        propertiesExpanded: AppSettings.propertiesExpanded
        onPropertiesExpandedToggled: (expanded) => AppSettings.propertiesExpanded = expanded
        // The header below shows the tags and aliases as chips while the panel is folded.
        headerShowsListChips: true
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
                periodic: root.periodic
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

    Binding {
        target: editor
        property: "propertiesExpanded"
        value: AppSettings.propertiesExpanded
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
        onLoaded: if (!root.inSidePanel) Qt.callLater(() => { if (item) item.forceActiveFocus() })
    }

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
            text: qsTr("Find and replace…")
            iconName: "search"
            shortcut: "Ctrl+F"
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
        // A page named by a tag in use: Zadania z tym tagiem (Widok tagu is the header's "#").
        BMenuItem {
            objectName: "pageMenuTagTasks"
            visible: root.pageHeader !== null && root.pageHeader.tagInUse
            height: visible ? implicitHeight : 0
            text: qsTr("Tasks with this tag")
            iconName: "list-todo"
            onTriggered: AppController.navigate({ view: "tasks", query: root.pageHeader.tagQuery(root.pageHeader.tagInfo.key) })
        }
        // The note mode (also the header's toggle on a wide header, Więcej, the palette).
        BMenuItem {
            objectName: "pageMenuNoteMode"
            text: page.documentMode ? qsTr("Switch to outline") : qsTr("Switch to document")
            iconName: page.documentMode ? "list" : "file-text"
            enabled: page.loaded
            onTriggered: root.toggleNoteMode()
        }
        BMenuItem {
            objectName: "pageMenuAddProperty"
            text: qsTr("Add property")
            iconName: "plus"
            shortcut: root.inSidePanel ? "" : "Ctrl+;"
            enabled: page.loaded && !page.sourceMode && !root.pictureRoute
            onTriggered: editor.addProperty()
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
                // A bound method: the toast outlives this view (the page closes when deleted).
                AppController.toastAction(qsTr("Moved to trash"), "info", qsTr("Undo"),
                                          Workspace.restore.bind(Workspace, trashPath))
            }
        }
    }

    Component {
        id: sidePanelComponent
        ContentSidePanel {
            id: contextPanel
            // A picture's references (the index knows the notes that embed it; nothing loads it).
            path: root.pictureRoute || root.pdfRoute || root.fileRoute ? root.routePath : page.path
            controller: page
            editor: editor
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
        function onUiActionRequested(action) {
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
            } else if (action === "findInNote" && root.visible && !root.inSidePanel) {
                root.openFind(false)
            } else if (action.startsWith("copyLink:") && root.visible && !root.inSidePanel && !root.pictureRoute
                       && !root.pdfRoute && !root.fileRoute) {
                editor.copyCurrentLink(action.slice(9))
            } else if (action === "saveWebPictures" && root.visible && !root.inSidePanel && editor.visible) {
                editor.downloadWebPictures("")
            } else if (action === "addProperty" && root.visible && !root.inSidePanel && editor.visible) {
                editor.addProperty()
            }
        }
    }

    // "Dodaj właściwość" (docs/UI.md "Page properties"): the panel's add row.
    Shortcut {
        sequence: "Ctrl+;"
        enabled: root.commandTarget && editor.visible && page.loaded && !page.sourceMode
        onActivated: editor.addProperty()
    }

    Shortcut {
        sequence: "Ctrl+E"
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
