pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// DrawingView — route "drawing" ({path}): the drawing editor (docs/UI.md "Drawing editor") on a
// drawing attachment (docs/SYNTAX.md §2 "Drawings": an SVG with an embedded Excalidraw scene, a
// `.excalidraw` scene, an Obsidian Excalidraw plugin file `*.excalidraw.md` — each saved in its
// own format: DrawingCanvas.fileBytes).
//
//  * The paper (DrawingCanvas: a stylus works the tool with its pressure, its eraser end erases; a
//    mouse works it; one finger works it or moves the paper — "Rysuj palcem", on by default until
//    a stylus has been seen on this device —, two fingers move and zoom) and the tool bar
//    (DrawingToolbar: above the paper, at the bottom on phones). Tools: the pen, the highlighter,
//    the eraser, the selection (a lasso, a rectangle or a tap; its frame's handles resize and turn
//    it; DrawingSelectionBar over the selection), shapes (rectangle, ellipse, diamond, line, arrow
//    and the polygons in their menu), text (DrawingTextEditor writes it). Keys (none
//    while a text is written): Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y, P / H / E (pen, highlighter,
//    eraser), V (select), R / O / D / L / A (rectangle, ellipse, diamond, line, arrow), T (text),
//    Delete / Backspace (the selection), Ctrl+C / Ctrl+X / Ctrl+V (copy, cut, paste — as
//    Excalidraw's clipboard, so excalidraw.com pastes it and Brulion pastes what it copied, plain
//    text as a text), Ctrl+D (duplicate), Ctrl+A (select all), Escape (a shape
//    or a lasso in progress dropped, else the selection cleared), the arrows (move the selection
//    1, with Shift 10), Ctrl+] / Ctrl+[ (one step up / down), Ctrl+Shift+] / Ctrl+Shift+[ (to the
//    front / back), Ctrl+G / Ctrl+Shift+G (group, ungroup), Shift+H / Shift+V (mirror across, up
//    and down), Ctrl+Shift+arrows (align left, right, top, bottom), Alt+H / Alt+V (distribute
//    across, down), PgUp / PgDown (the previous / next PDF page's drawing), Space held (move the
//    paper), Ctrl+0 (the whole drawing), Ctrl++ / Ctrl+- (zoom).
//  * Saved by itself: 1.2 s after the last change, when the route changes, the view goes and the
//    app leaves the foreground (Workspace.writeAttachment). A text being written is written first
//    by those but never by the 1.2 s timer, which waits until its editor closes (committing it
//    would close the editor mid-word and give the next keys to the tools). The tools' colours and widths, finger
//    drawing, the last shape, the text's size and whether a stylus was seen are remembered on the
//    device (AppSettings "drawing/…").
//  * A change of the file from elsewhere (Syncthing, Nextcloud) while it is open is taken in: as
//    it is when nothing changed here, else merged stroke by stroke (DrawingCanvas.takeExternal)
//    and saved; the canvas's own saves coming back are recognised (isKnownFile). Every save
//    first reads the file: a change not reported yet is taken in the same way, never written
//    over. A version Brulion cannot edit (frames, embedded pages) is shown as it is; what was drawn
//    here and not saved is kept in a new file beside it ("… (to urządzenie).svg", in the file's own
//    format and extension).
//  * A file Brulion cannot edit is shown as the picture it is, with why: an Excalidraw drawing
//    with frames or embedded web pages ("readonly"; one of Obsidian's Excalidraw plugin says to
//    change it in Obsidian, one in the plugin's oldest form to open it there once), a picture
//    without a scene, a scene that cannot be read; a missing file says so. Renames follow the file (the route applied one event-loop turn late, as
//    in PageView); a deleted file is never written again.
//  * The paper is light or dark as the drawing chooses ("Tło rysunku": as the app — the default —,
//    light, dark; DrawingCanvas.theme, saved in the file, an undo step) and follows the app's
//    theme at once (DrawingCanvas.appDark: Theme.dark). The previews of the drawing elsewhere
//    follow it too (Workspace.drawingUrl), the notice's picture below as well.
//  * A backdrop (DrawingCanvas.setBackdrop; core drawing/backdrop.h): "Podkład z obrazu lub PDF…"
//    in the paper's menu picks a picture or a PDF (FilePicker; the browser's picker: pictures),
//    imports it into the notebook (Workspace.importAttachment: a file of the notebook stays where
//    it is) and puts it — a PDF's page chosen in DrawingBackdropDialog — under the drawing (a copy
//    stored for nothing — cancelled, a locked PDF — is removed again: Workspace.discardImport); "Inna
//    strona PDF…" chooses another page of the same PDF, "Usuń podkład" removes it. Undo takes each
//    back. "Poprzednia / Następna strona PDF" (PgUp / PgDown) opens the drawing of the neighbouring
//    page in the same note, made and embedded beside this one when there is none (stepPage()).
// route.open (screenshots): "style" (the colours and widths) and "more" on phones, "paper" (the
// paper's menu) on wide windows, "theme" (the background's menu; on phones "Więcej"), "shape"
// (the shapes' menu), "select" (everything selected — or route.ids —: the selection bar),
// "selectstyle" and "selectmore" (the same with its style panel, its "Więcej"), "backdrop" (the page
// picker of the backdrop's PDF; a picture's backdrop: the paper's menu).
// View contract (ShellRouteView): `title` (the file's name), `editing` (always: the paper is being
// worked on), `toolbarDocked` and `replacesBottomNav` (phones: the tool bar at the bottom — the
// bottom navigation makes way; no keyboard involved, unlike an editor's toolbar),
// `edgeSwipe` (no drawer edge strip: a stroke from the left edge stays a stroke).
FocusScope {
    id: root

    property var route: ({})
    property bool inSidePanel: false

    focus: true

    readonly property string routePath: AppController.routeText(route, "path")
    readonly property string fileName: {
        const i = Js.lastIndexOf(root.loadedPath, "/")
        return i < 0 ? root.loadedPath : Js.sliceFrom(root.loadedPath, i + 1)
    }
    /// The drawing file's extension as written (".svg", ".excalidraw", ".excalidraw.md"; the title
    /// goes without it, a kept copy gets it).
    readonly property string extension: root.extensionOf(root.fileName)
    function extensionOf(name: string): string {
        const m = root.extensionMatch(name)
        return m.length > 1 ? m[1] : ".svg"
    }
    function extensionMatch(name: string): list<string> {
        return Js.match(name, "(\\.excalidraw\\.md|\\.excalidraw|\\.svg|\\.md)$", "i")
    }
    readonly property string title: Js.endsWith(root.fileName, root.extension)
                                    ? Js.slice(root.fileName, 0, root.fileName.length - root.extension.length)
                                    : root.fileName
    readonly property bool editing: true
    readonly property bool toolbarDocked: Theme.narrow && root.status === ""
    readonly property bool replacesBottomNav: toolbarDocked
    readonly property bool edgeSwipe: true
    readonly property alias canvas: canvas

    /// "" (editable), "loading", "readonly", "notDrawing", "corrupt", "missing".
    property string status: "loading"
    /// A backdrop being chosen: the browser's pick, the job preparing a picture (0 none).
    property int backdropPick: 0
    property int backdropJob: 0
    property DrawingBackdropDialog backdropDialog: null
    property DrawingPaperDialog paperDialog: null
    /// The picked file's vault path until the backdrop is set: abandoned, its copy goes again.
    property string backdropImport: ""
    /// The drawing the backdrop is chosen for (loadedPath then): another one loaded meanwhile
    /// gets none.
    property string backdropFor: ""
    /// A prepared backdrop waiting for the stroke, shape or text in progress to end
    /// (backdropPending: its items in pendingBackdrop).
    property bool backdropPending: false
    property list<var> pendingBackdrop: []
    /// A map or a list a call returned, read with AppController.route*() (compiled calls cannot
    /// take one held in a local after another call: kept here).
    property var _held: ({})
    property list<var> _heldList: []
    property list<int> _pages: []
    /// No pages (a picture is prepared as itself).
    readonly property list<int> noPages: []
    /// File bytes (the drawing's, the file's on disk) passed on to more than one call.
    property var _bytes
    property var _disk
    /// The drawing of a neighbouring PDF page being made (stepPage()): the job preparing the page
    /// (0 none), the drawing it goes beside, the note it goes into, before the current one or after.
    property int pageJob: 0
    property string pageFrom: ""
    property string pageNote: ""
    property bool pageBefore: false
    /// The file the paper shows (follows renames; "" none).
    property string loadedPath: ""
    property bool _writing: false

    // The tools, remembered on the device.
    property string penColor: root.setting("drawing/penColor", "#1f2430")
    property real penWidth: root.settingNumber("drawing/penWidth", 1)
    property string highlighterColor: root.setting("drawing/highlighterColor", "#ffd43b")
    property real highlighterWidth: root.settingNumber("drawing/highlighterWidth", 5)
    /// The shape tool the shape button stands for.
    property string shapeTool: root.setting("drawing/shape", "rectangle")
    /// How the select tool takes elements: "lasso", "box".
    property string selectMode: root.setting("drawing/selectMode", "lasso")
    property real fontSize: root.settingNumber("drawing/fontSize", 20)
    /// "auto" (a finger draws until a stylus has been seen), "on", "off".
    property string fingerMode: root.setting("drawing/finger", "auto")
    property bool stylusKnown: root.setting("drawing/stylusSeen", "false") === "true"
    readonly property bool fingerDraws: fingerMode === "on" || (fingerMode === "auto" && !stylusKnown)

    readonly property list<var> penColors: [
        { color: "#1f2430", label: qsTr("Ink") },
        { color: "#2f6fdd", label: qsTr("Blue") },
        { color: "#e03131", label: qsTr("Red") },
        { color: "#2f9e44", label: qsTr("Green") },
        { color: "#f08c00", label: qsTr("Orange") },
        { color: "#7048e8", label: qsTr("Violet") }
    ]
    readonly property list<var> highlighterColors: [
        { color: "#ffd43b", label: qsTr("Yellow") },
        { color: "#8ce99a", label: qsTr("Green") },
        { color: "#faa2c1", label: qsTr("Pink") },
        { color: "#74c0fc", label: qsTr("Blue") },
        { color: "#ffa94d", label: qsTr("Orange") }
    ]
    readonly property list<var> penWidths: [
        { value: 0.5, label: qsTr("Fine") },
        { value: 1, label: qsTr("Medium") },
        { value: 2, label: qsTr("Thick") }
    ]
    readonly property list<var> highlighterWidths: [
        { value: 3, label: qsTr("Narrow") },
        { value: 5, label: qsTr("Medium") },
        { value: 8, label: qsTr("Wide") }
    ]
    /// Excalidraw's text sizes.
    readonly property list<var> fontSizes: [
        { value: 16, label: qsTr("Small") },
        { value: 20, label: qsTr("Medium", "text size") },
        { value: 28, label: qsTr("Large") },
        { value: 36, label: qsTr("Extra large") }
    ]

    /// A device setting as text (true: "true"), `fallback` when it is not set.
    function setting(key: string, fallback: string): string {
        return AppSettings.value(key, fallback)
    }
    /// A device setting as a number (Number(value) || fallback).
    function settingNumber(key: string, fallback: real): real {
        const n = Js.toNumber(root.setting(key, ""))
        return n !== 0 && !Js.isNaN(n) ? n : fallback
    }

    /// Write the drawing when it changed (true: nothing to write, or written); a text being written
    /// is committed first.
    function save(): bool {
        saveTimer.stop()
        if (textEditor)
            textEditor.commit() // (a text being written goes in first)
        if (root.loadedPath === "" || root.status !== "" || !canvas.modified)
            return true
        // Written first (with pictures that takes a moment), then compared with what the file
        // holds now, on disk, right before the write: another program may have written it a
        // moment ago, before its change was reported (a folder watch that missed it, Android's
        // shared storage, Syncthing while this was written) — taken in first, never written over.
        root._bytes = canvas.fileBytes()
        for (let checks = 0; checks < 3 && Workspace.exists(root.loadedPath); ++checks) {
            root._disk = Workspace.readAttachment(root.loadedPath)
            if (!canvas.hasBytes(root._disk) || canvas.isKnownFile(root._disk))
                break
            if (!root.takeIn(root._disk))
                return true // shown as it is now; what was drawn here kept in a copy beside it
            if (!canvas.modified)
                return true // (theirs as it is: nothing of this side to write)
            root._bytes = canvas.fileBytes() // (the merge)
        }
        // The vault reports the write at once (Workspace.fileChanged, below): that is this save,
        // not another device's change to merge.
        root._writing = true
        const written = Workspace.writeAttachment(root.loadedPath, root._bytes)
        root._writing = false
        if (!written)
            return false // (Workspace.errorOccurred: the shell's toast says why)
        canvas.markSaved(root._bytes)
        return true
    }

    /// Another program's version of the open, editable file: taken in — merged with what was
    /// drawn here (DrawingCanvas.takeExternal) — or, when it is no drawing Brulion can edit any
    /// more (a frame or an embedded page added in excalidraw.com), shown as it is, with what was
    /// drawn here and not yet saved kept in a copy beside it. True when the paper still edits it.
    property var _taken
    function takeIn(bytes: var): bool {
        root._taken = bytes
        const merged = canvas.modified
        if (canvas.takeExternal(root._taken)) {
            if (merged) {
                AppController.toast(qsTr("The drawing was changed on another device: both versions are kept in it"))
                saveTimer.restart()
            }
            return true
        }
        saveTimer.stop()
        if (canvas.modified)
            root.keepCopy()
        root.status = canvas.load(root._taken)
        return false
    }

    /// What was drawn here, written beside the drawing as a new file in its format ("Rysunek (to
    /// urządzenie).svg", "Szkic (to urządzenie).excalidraw.md").
    function keepCopy(): void {
        const slash = Js.lastIndexOf(root.loadedPath, "/")
        const folder = slash < 0 ? "" : Js.slice(root.loadedPath, 0, slash + 1)
        const path = Workspace.createAttachment(folder + qsTr("%1 (this device)").arg(root.title) + root.extension,
                                                canvas.fileBytes())
        if (path !== "")
            AppController.toast(qsTr("The drawing was changed elsewhere into one Brulion cannot edit. What you drew here is kept in %1").arg(root.baseName(path)))
    }
    /// The last part of a path.
    function baseName(path: string): string {
        return Js.sliceFrom(path, Js.lastIndexOf(path, "/") + 1)
    }

    function load(path: string): void {
        root.loadedPath = path
        root.dropBackdropChoice()
        if (!Workspace.exists(path)) {
            canvas.loadEmpty()
            root.status = "missing"
            return
        }
        root.status = canvas.load(Workspace.readAttachment(path))
        root.forceActiveFocus()
    }

    function applyRoute(): void {
        if (root.routePath === "" || root.routePath === root.loadedPath)
            return
        root.save()
        root.load(root.routePath)
        // route.open (screenshots): "style" | "more" (phones), "paper" (wide windows), "theme".
        const open = AppController.routeText(root.route, "open")
        if (open !== "" && root.status === "") {
            root._open = open
            openLater.restart()
        }
    }
    /// route.open being applied (a moment later: openLater).
    property string _open: ""
    Timer {
        id: openLater
        interval: 0
        onTriggered: root.openFromRoute(root._open)
    }
    function openFromRoute(open: string): void {
                const bar = Theme.narrow ? bottomBar : topBar
                if (open === "style")
                    bar.openStyle()
                else if (open === "more")
                    bar.openMore()
                else if (open === "paper")
                    bar.openBackground()
                else if (open === "theme")
                    bar.openTheme()
                else if (open === "papercolor")
                    root.openPaperDialog()
                else if (open === "shape")
                    bar.openShapes()
                else if (open === "select" || open === "selectstyle" || open === "selectmore") {
                    canvas.tool = "select"
                    if (AppController.routeFlag(root.route, "ids"))
                        canvas.select(AppController.routeStrings(root.route, "ids"))
                    else
                        canvas.selectAll()
                    if (open === "selectstyle")
                        selectionBar.openStyle()
                    else if (open === "selectmore")
                        selectionBar.openMore()
                } else if (open === "backdrop") {
                    if (canvas.backdropPage > 0 && canvas.backdropPath !== "" && DrawingBackdrops.pdfAvailable)
                        root.otherBackdropPage()
                    else if (Theme.narrow)
                        bar.openMore()
                    else
                        bar.openBackground()
                }
    }

    function applyTool(): void {
        // (Shapes and text in the pen's colour; shapes twice the pen's width.)
        canvas.color = canvas.tool === "highlighter" ? root.highlighterColor : root.penColor
        canvas.penWidth = canvas.tool === "highlighter" ? root.highlighterWidth : root.penWidth
        canvas.fontSize = root.fontSize
    }

    function chooseColor(color: string): void {
        if (canvas.tool === "highlighter") {
            root.highlighterColor = color
            AppSettings.setValue("drawing/highlighterColor", color)
        } else {
            root.penColor = color
            AppSettings.setValue("drawing/penColor", color)
        }
        if (canvas.tool === "eraser" || canvas.tool === "select")
            canvas.tool = "pen"
        root.applyTool()
    }

    function chooseFontSize(size: real): void {
        root.fontSize = size
        AppSettings.setValue("drawing/fontSize", size)
        root.applyTool()
    }

    function chooseShape(tool: string): void {
        root.shapeTool = tool
        AppSettings.setValue("drawing/shape", tool)
    }

    function chooseSelectMode(mode: string): void {
        root.selectMode = mode
        AppSettings.setValue("drawing/selectMode", mode)
    }

    function useShape(tool: string): void {
        canvas.tool = tool
        root.chooseShape(tool)
    }

    function chooseWidth(width: real): void {
        if (canvas.tool === "highlighter") {
            root.highlighterWidth = width
            AppSettings.setValue("drawing/highlighterWidth", width)
        } else {
            root.penWidth = width
            AppSettings.setValue("drawing/penWidth", width)
        }
        root.applyTool()
    }

    function toggleFinger(): void {
        root.fingerMode = root.fingerDraws ? "off" : "on"
        AppSettings.setValue("drawing/finger", root.fingerMode)
    }

    /// "Podkład z obrazu lub PDF…": a picture or a PDF picked and imported into the notebook.
    function pickBackdrop(): void {
        if (root.status !== "")
            return
        root.backdropFor = root.loadedPath
        const pick = DrawingBackdrops.pickInBrowser()
        if (pick > 0)
            root.backdropPick = pick // (the browser's picker: picked() below)
        else
            backdropPicker.open()
    }
    /// The backdrop's file (a vault path, just imported): a PDF's page is chosen, a picture
    /// prepared at once.
    function useBackdropFile(path: string): void {
        root.backdropImport = path
        if (path === "")
            return
        if (DrawingBackdrops.isPdf(path))
            root.openPageDialog(path, 1)
        else
            root.backdropJob = DrawingBackdrops.prepare(path, root.noPages)
    }
    function openPaperDialog(): void {
        if (!root.paperDialog)
            root.paperDialog = paperDialogComponent.createObject(root) as DrawingPaperDialog
        root.paperDialog.openWith(canvas.paper)
    }
    function openPageDialog(path: string, page: int): void {
        if (!root.backdropDialog)
            root.backdropDialog = backdropDialogComponent.createObject(root) as DrawingBackdropDialog
        root.backdropDialog.openFor(path, "single", page)
    }
    /// No backdrop was set from the picked file: the copy stored for it goes (never a file of the
    /// notebook or one reused).
    function abandonBackdropFile(): void {
        if (root.backdropImport !== "")
            Workspace.discardImport(root.backdropImport)
        root.backdropImport = ""
    }
    /// "Inna strona PDF…": the backdrop's PDF again (renames do not follow it into the drawing).
    function otherBackdropPage(): void {
        root.backdropImport = "" // (the file is the drawing's: never discarded)
        root.backdropFor = root.loadedPath
        const path = canvas.backdropPath
        if (path === "" || !Workspace.exists(path)) {
            AppController.toast(qsTr("The file %1 is not in the notebook any more.").arg(root.baseName(path)))
            return
        }
        root.openPageDialog(path, canvas.backdropPage)
    }
    /// Ctrl+V, "Wklej": Excalidraw's elements (excalidraw.com's, another drawing's) or plain text
    /// from the clipboard (DrawingCanvas.paste), saying what was left out.
    function paste(): void {
        root._held = canvas.paste()
        const skipped = AppController.routeInt(root._held, "skipped", 0)
        if (skipped > 0)
            AppController.toast(qsTr("%n pasted element(s) Brulion does not draw (frames, embedded pages) left out.", "", skipped))
        else if (AppController.routeInt(root._held, "pasted", 0) === 0 && root.status === "")
            AppController.toast(qsTr("The clipboard holds nothing to paste on a drawing."))
    }
    /// "Poprzednia / Następna strona PDF" (PgUp / PgDown): the drawing of page N ± 1 of the
    /// backdrop's PDF in the note this one belongs to (Workspace.noteOfDrawing: the note it was
    /// opened from, else the first that links to it) — the first that note embeds is opened, else a
    /// new one is made ("<PDF> s. N.svg") and embedded right before (the previous page) or after
    /// this one's block (no note: today's journal). It replaces this drawing in the route, so Back
    /// returns to the note.
    function stepPage(step: int): void {
        const path = canvas.backdropPath
        const page = canvas.backdropPage + step
        if (root.status !== "" || root.pageJob > 0 || canvas.backdropPage < 1 || path === ""
                || !DrawingBackdrops.pdfAvailable)
            return
        if (page < 1) {
            AppController.toast(qsTr("This is the first page of the PDF."))
            return
        }
        if (!Workspace.exists(path)) {
            AppController.toast(qsTr("The file %1 is not in the notebook any more.").arg(root.baseName(path)))
            return
        }
        root._held = DrawingBackdrops.pdfInfo(path)
        const pages = AppController.routeInt(root._held, "pages", -1)
        if (pages < 1) {
            const error = AppController.routeText(root._held, "error")
            AppController.toast(error !== "" ? error : DrawingBackdrops.errorText("pdf"))
            return
        }
        if (page > pages) {
            AppController.toast(qsTr("This is the last page of the PDF."))
            return
        }
        if (!root.save())
            return
        const note = Workspace.noteOfDrawing(root.loadedPath, AppController.routeText(root.route, "from"))
        root._heldList = root.drawingsOn(path, page, note)
        if (root._heldList.length > 0) {
            AppController.replaceRoute({ view: "drawing", path: AppController.routeText(root._heldList[0], "path"), from: note })
            return
        }
        root.pageFrom = root.loadedPath
        root.pageNote = note
        root.pageBefore = step < 0
        root._pages = [page]
        root.pageJob = DrawingBackdrops.prepare(path, root._pages, true)
    }
    Connections {
        target: DrawingBackdrops
        enabled: root.pageJob > 0
        function onFinished(job: int, items: list<var>, cancelled: bool) {
            if (job !== root.pageJob)
                return
            root.pageJob = 0
            if (cancelled || root.pageFrom !== root.loadedPath)
                return // (another drawing was opened meanwhile)
            root._heldList = items
            const error = items.length > 0 ? AppController.routeText(root._heldList[0], "error") : "pdf"
            if (error !== "") {
                AppController.toast(DrawingBackdrops.errorText(error), "error")
                return
            }
            root._held = Workspace.addDrawingBeside(root.pageFrom, root._heldList[0], root.pageNote, root.pageBefore)
            const path = AppController.routeText(root._held, "path")
            if (path === "")
                return
            if (AppController.routeFlag(root._held, "journal"))
                AppController.toast(qsTr("The drawing was added to today's journal."), "info")
            AppController.replaceRoute({ view: "drawing", path: path, from: AppController.routeText(root._held, "note") })
        }
    }
    /// The drawings of `page` of the PDF `path` the note `note` embeds ([] without a note).
    function drawingsOn(path: string, page: int, note: string): list<var> {
        return note !== "" ? Workspace.drawingsOnPdfPage(path, page, note) : []
    }
    function applyBackdrop(items: list<var>): void {
        root.backdropPending = false
        root.pendingBackdrop = []
        if (root.backdropFor !== root.loadedPath) {
            root.abandonBackdropFile() // (another drawing is open now)
            return
        }
        root._heldList = items
        const error = items.length > 0 ? AppController.routeText(root._heldList[0], "error") : "invalid"
        if (items.length > 0 && error !== "")
            AppController.toast(DrawingBackdrops.errorText(error))
        const result = error === "" ? canvas.setBackdrop(root._heldList[0]) : "invalid"
        if (result === "busy") {
            root.pendingBackdrop = items // (set when the stroke, shape or text ends)
            root.backdropPending = true
            return
        }
        if (result === "") {
            root.backdropImport = "" // (the drawing uses it: kept)
        } else {
            if (result === "tooLarge")
                AppController.toast(qsTr("This drawing's pictures leave no room for the backdrop: the file would be too large to open again."))
            root.abandonBackdropFile()
        }
        root.forceActiveFocus()
    }
    /// Another drawing loaded: a backdrop being prepared or waiting for this one is not set.
    function dropBackdropChoice(): void {
        if (root.backdropJob > 0)
            DrawingBackdrops.cancel(root.backdropJob) // (finished(): its copy goes)
        if (root.backdropPending) {
            root.backdropPending = false
            root.pendingBackdrop = []
            root.abandonBackdropFile()
        }
    }

    function applyPendingBackdrop(): void {
        if (root.backdropPending && !canvas.busy && !canvas.textEditing)
            pendingLater.restart()
    }
    Timer {
        id: pendingLater
        interval: 0
        onTriggered: {
            if (root.backdropPending && !canvas.busy && !canvas.textEditing)
                root.applyBackdrop(root.pendingBackdrop)
        }
    }

    /// An Obsidian Excalidraw plugin file's pictures in their own files ("## Embedded Files",
    /// canvas.embeddedFiles: { fileId, link }): their links resolved as the file's links, their
    /// bytes handed to the canvas.
    function provideEmbeddedFiles(): void {
        const from = root.loadedPath !== "" ? root.loadedPath : root.routePath
        const count = canvas.embeddedFiles.length
        for (let i = 0; i < count; ++i) {
            const path = "" + Workspace.resolveLink(root.embeddedField(i, "link"), from)
            if (path === "")
                continue
            const fileId = root.embeddedField(i, "fileId")
            root._disk = Workspace.readAttachment(path)
            if (canvas.hasBytes(root._disk))
                canvas.provideEmbeddedFile(fileId, root._disk)
        }
    }
    function embeddedField(i: int, key: string): string {
        const files = canvas.embeddedFiles
        const f = files[i]
        return AppController.routeText(f, key)
    }

    /// Gotowe: written, back to where the drawing was opened from.
    function done(): void {
        root.save()
        if (AppController.canGoBack)
            AppController.back()
        else
            AppController.goHome()
    }

    // One event-loop turn late (as PageView): on a rename the shell rewrites its routes before
    // Workspace.pathRenamed reaches this view; loading the new path first would write the
    // drawing back under the old name.
    property bool _routeQueued: false
    property bool alive: true
    onRouteChanged: {
        if (root._routeQueued)
            return
        root._routeQueued = true
        routeLater.restart()
    }
    Timer {
        id: routeLater
        interval: 0
        onTriggered: {
            if (!root.alive)
                return
            root._routeQueued = false
            root.applyRoute()
        }
    }
    // Text on drawings in the app's fonts (the previews' too).
    readonly property string _fonts: Theme.fontUi + "\n" + Theme.fontMono
    on_FontsChanged: DrawingFonts.set(Theme.fontUi, Theme.fontMono)
    Component.onCompleted: {
        DrawingFonts.set(Theme.fontUi, Theme.fontMono)
        root.applyRoute()
    }
    Component.onDestruction: {
        root.alive = false
        root.save()
        if (root.backdropJob > 0 || root.backdropPending) {
            DrawingBackdrops.cancel(root.backdropJob)
            root.abandonBackdropFile() // (no backdrop set: the copy stored for it goes)
        }
    }

    Timer {
        id: saveTimer
        interval: 1200
        // (Not while a text is written: the editor's close restarts it.)
        onTriggered: if (!canvas.textEditing) root.save()
    }

    Connections {
        target: Workspace
        function onPathRenamed(oldPath: string, newPath: string) {
            if (oldPath === root.backdropFor)
                root.backdropFor = newPath
            if (oldPath === root.loadedPath)
                root.loadedPath = newPath
        }
        function onPathRemoved(path: string) {
            if (path !== root.loadedPath)
                return
            saveTimer.stop()
            root.loadedPath = ""
            root.status = "missing"
        }
        function onFileChanged(path: string) {
            if (path !== root.loadedPath || root.status === "missing" || root._writing)
                return
            root._disk = Workspace.readAttachment(path)
            if (!canvas.hasBytes(root._disk) || canvas.isKnownFile(root._disk))
                return // its own save, or a file being written
            if (root.status === "")
                root.takeIn(root._disk)
            else
                root.status = canvas.load(root._disk)
        }
    }
    Connections {
        target: DrawingBackdrops
        enabled: root.backdropPick > 0 || root.backdropJob > 0
        function onPicked(pick: int, path: string) {
            if (pick !== root.backdropPick)
                return
            root.backdropPick = 0
            root.useBackdropFile(path)
        }
        function onPickFailed(pick: int, name: string) {
            if (pick !== root.backdropPick)
                return
            root.backdropPick = 0
            AppController.toast(DrawingBackdrops.errorText("unreadable"))
        }
        function onFinished(job: int, items: list<var>, cancelled: bool) {
            if (job !== root.backdropJob)
                return
            root._heldList = items
            root.backdropJob = 0
            if (!cancelled)
                root.applyBackdrop(root._heldList)
            else
                root.abandonBackdropFile()
        }
    }
    FilePicker {
        id: backdropPicker
        title: qsTr("Backdrop from a picture or PDF")
        fileMode: FilePicker.OpenFile
        dark: Theme.dark
        colors: Theme.colors
        nameFilters: DrawingBackdrops.nameFilters
        onAccepted: {
            const path = "" + Workspace.importAttachment(backdropPicker.selectedFile)
            if (path === "")
                AppController.toast(DrawingBackdrops.errorText("unreadable"))
            root.useBackdropFile(path)
        }
    }
    Component {
        id: paperDialogComponent
        DrawingPaperDialog {
            canvas: canvas
            onChosen: (color) => { canvas.paper = color }
            onClosed: root.forceActiveFocus()
        }
    }
    Component {
        id: backdropDialogComponent
        DrawingBackdropDialog {
            onPrepared: (items) => root.applyBackdrop(items)
            onRejected: {
                root.abandonBackdropFile()
                root.forceActiveFocus()
            }
        }
    }

    Connections {
        target: Application
        function onStateChanged() {
            if (Application.state !== Qt.ApplicationActive)
                root.save()
        }
    }
    /// The selection's move for an arrow key (`axis` 0: x, 1: y; 0 for another key).
    function arrowStep(key: int, axis: int): int {
        switch (key) {
        case Qt.Key_Left: return axis === 0 ? -1 : 0
        case Qt.Key_Right: return axis === 0 ? 1 : 0
        case Qt.Key_Up: return axis === 0 ? 0 : -1
        case Qt.Key_Down: return axis === 0 ? 0 : 1
        default: return 0
        }
    }
    /// The side an arrow key aligns the selection to.
    function arrowSide(key: int): string {
        switch (key) {
        case Qt.Key_Left: return "left"
        case Qt.Key_Right: return "right"
        case Qt.Key_Up: return "top"
        default: return "bottom"
        }
    }

    Keys.onPressed: (event) => {
        if (canvas.textEditing)
            return // (the text editor has the keys)
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        const plain = event.modifiers === Qt.NoModifier
        const arrow = event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Up
                      || event.key === Qt.Key_Down
        if (event.matches(StandardKey.Undo)) {
            canvas.undo()
        } else if (event.matches(StandardKey.Redo) || (ctrl && event.key === Qt.Key_Y)) {
            canvas.redo()
        } else if ((event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) && plain
                   && canvas.backdropPage > 0 && !canvas.busy) {
            root.stepPage(event.key === Qt.Key_PageUp ? -1 : 1)
        } else if (event.key === Qt.Key_Space && !ctrl) {
            if (!event.isAutoRepeat)
                canvas.panMode = true
        } else if (ctrl && event.key === Qt.Key_0) {
            canvas.fitAll()
        } else if (ctrl && (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)) {
            canvas.zoomBy(1.25)
        } else if (ctrl && event.key === Qt.Key_Minus) {
            canvas.zoomBy(0.8)
        } else if (event.modifiers === Qt.NoModifier && event.key === Qt.Key_P) {
            canvas.tool = "pen"
        } else if (event.modifiers === Qt.NoModifier && event.key === Qt.Key_H) {
            canvas.tool = "highlighter"
        } else if (event.modifiers === Qt.NoModifier && event.key === Qt.Key_E) {
            canvas.tool = "eraser"
        } else if (plain && event.key === Qt.Key_V) {
            canvas.tool = "select"
        } else if (plain && event.key === Qt.Key_R) {
            root.useShape("rectangle")
        } else if (plain && event.key === Qt.Key_O) {
            root.useShape("ellipse")
        } else if (plain && event.key === Qt.Key_D) {
            root.useShape("diamond")
        } else if (plain && event.key === Qt.Key_L) {
            root.useShape("line")
        } else if (plain && event.key === Qt.Key_A) {
            root.useShape("arrow")
        } else if (plain && event.key === Qt.Key_T) {
            canvas.tool = "text"
        } else if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) && canvas.selectionCount > 0) {
            canvas.deleteSelection()
        } else if (event.matches(StandardKey.Copy) && canvas.selectionCount > 0) {
            canvas.copySelection()
        } else if (event.matches(StandardKey.Cut) && canvas.selectionCount > 0) {
            canvas.cutSelection()
        } else if (event.matches(StandardKey.Paste)) {
            root.paste()
        } else if (ctrl && !shift && event.key === Qt.Key_D) {
            canvas.duplicateSelection()
        } else if (event.matches(StandardKey.SelectAll)) {
            canvas.tool = "select"
            canvas.selectAll()
        } else if (ctrl && event.key === Qt.Key_G && canvas.selectionCount > 0) {
            if (shift)
                canvas.ungroupSelection()
            else
                canvas.groupSelection()
        } else if (event.modifiers === Qt.ShiftModifier && event.key === Qt.Key_H && canvas.selectionCount > 0) {
            canvas.flipSelection("horizontal")
        } else if (event.modifiers === Qt.ShiftModifier && event.key === Qt.Key_V && canvas.selectionCount > 0) {
            canvas.flipSelection("vertical")
        } else if (event.key === Qt.Key_Escape && (canvas.busy || canvas.selectionCount > 0)) {
            if (!canvas.cancelGesture())
                canvas.clearSelection()
        } else if (arrow && canvas.selectionCount > 0 && ctrl && shift) {
            canvas.alignSelection(root.arrowSide(event.key))
        } else if (event.modifiers === Qt.AltModifier && (event.key === Qt.Key_H || event.key === Qt.Key_V)
                   && canvas.selectionCount > 0) {
            canvas.distributeSelection(event.key === Qt.Key_H ? "horizontal" : "vertical")
        } else if (arrow && canvas.selectionCount > 0 && !ctrl) {
            const step = shift ? 10 : 1
            canvas.nudgeSelection(root.arrowStep(event.key, 0) * step, root.arrowStep(event.key, 1) * step)
        } else if (ctrl && (event.key === Qt.Key_BracketRight || event.key === Qt.Key_BraceRight)) {
            canvas.reorderSelection(shift || event.key === Qt.Key_BraceRight ? "front" : "forward")
        } else if (ctrl && (event.key === Qt.Key_BracketLeft || event.key === Qt.Key_BraceLeft)) {
            canvas.reorderSelection(shift || event.key === Qt.Key_BraceLeft ? "back" : "backward")
        } else {
            return
        }
        event.accepted = true
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            canvas.panMode = false
            event.accepted = true
        }
    }

    Rectangle {
        // The paper; the view's own background around a notice.
        anchors.fill: parent
        color: root.status === "" || root.status === "loading" ? canvas.paperColor : Theme.bg
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        DrawingToolbar {
            id: topBar
            objectName: "drawingToolbar"
            Layout.fillWidth: true
            visible: !Theme.narrow && root.status === ""
            canvas: canvas
            colors: canvas.tool === "highlighter" ? root.highlighterColors : root.penColors
            widths: canvas.tool === "highlighter" ? root.highlighterWidths : root.penWidths
            fontSizes: root.fontSizes
            shapeTool: root.shapeTool
            fingerDraws: root.fingerDraws
            onColorChosen: (color) => root.chooseColor(color)
            onWidthChosen: (width) => root.chooseWidth(width)
            onFontSizeChosen: (size) => root.chooseFontSize(size)
            onShapeChosen: (tool) => root.chooseShape(tool)
            onSelectModeChosen: (mode) => root.chooseSelectMode(mode)
            onFingerToggled: root.toggleFinger()
            onFitRequested: canvas.fitAll()
            onDone: root.done()
            onBackdropRequested: root.pickBackdrop()
            onBackdropPageRequested: root.otherBackdropPage()
            onPageStepRequested: (step) => root.stepPage(step)
            onPasteRequested: root.paste()
            onBackdropRemoved: canvas.removeBackdrop()
            onPaperColorRequested: root.openPaperDialog()
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            DrawingCanvas {
                id: canvas
                objectName: "drawingCanvas"
                anchors.fill: parent
                visible: root.status === ""
                fingerDraws: root.fingerDraws
                appDark: Theme.dark
                selectionColor: Theme.info
                selectMode: root.selectMode
                onToolChanged: root.applyTool()
                onContentChanged: if (canvas.modified) saveTimer.restart()
                onTextEditingChanged: {
                    if (!canvas.textEditing && canvas.modified)
                        saveTimer.restart()
                    root.applyPendingBackdrop()
                }
                onBusyChanged: root.applyPendingBackdrop()
                // An Obsidian Excalidraw plugin file's pictures in their own files ("## Embedded
                // Files"): their links resolved as the file's links, their bytes shown.
                onEmbeddedFilesChanged: root.provideEmbeddedFiles()
                onStylusSeenChanged: {
                    if (canvas.stylusSeen && !root.stylusKnown) {
                        root.stylusKnown = true
                        AppSettings.setValue("drawing/stylusSeen", true)
                    }
                }
                Component.onCompleted: root.applyTool()
            }

            // The selection's actions, above the selected elements.
            DrawingSelectionBar {
                id: selectionBar
                anchors.fill: parent
                canvas: canvas
                colors: root.penColors
            }

            // A backdrop being prepared (a large photo takes a moment) or waiting for a gesture, the
            // next PDF page's drawing being made.
            BProgressBar {
                objectName: "drawingBackdropProgress"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                visible: root.backdropJob > 0 || root.backdropPending || root.pageJob > 0
                indeterminate: true
            }

            // Text being written on the paper.
            DrawingTextEditor {
                id: textEditor
                anchors.fill: parent
                canvas: canvas
                onClosed: root.forceActiveFocus()
            }

            // A file Brulion cannot edit: the picture it is, and why.
            Flickable {
                id: notice
                objectName: "drawingNotice"
                anchors.fill: parent
                visible: root.status !== "" && root.status !== "loading"
                contentWidth: width
                contentHeight: noticeColumn.height + 2 * Theme.space8
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                Column {
                    id: noticeColumn
                    x: Theme.space4
                    y: Theme.space8
                    width: notice.width - 2 * Theme.space4
                    spacing: Theme.space4

                    BIcon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: root.status === "missing" ? "circle-alert" : "pen-tool"
                        size: Theme.iconSizeLg
                        color: Theme.fgMuted
                    }
                    BText {
                        objectName: "drawingNoticeText"
                        width: Math.min(parent.width, 520)
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        variant: "muted"
                        text: {
                            switch (root.status) {
                            case "readonly":
                                if (canvas.legacy)
                                    return qsTr("This drawing is in an old format of Obsidian's Excalidraw plugin. Open it once in Obsidian to update it; Brulion shows it as it is.")
                                if (canvas.format === "excalidrawMd")
                                    return qsTr("This drawing from Obsidian's Excalidraw plugin has frames or embedded pages, which Brulion does not edit yet. Change it in Obsidian; Brulion shows it as it is.")
                                return qsTr("This drawing has frames or embedded web pages from Excalidraw, which Brulion does not edit. Open the file in excalidraw.com to change it.")
                            case "notDrawing":
                                return qsTr("This picture is not a drawing: it can only be viewed.")
                            case "corrupt":
                                return qsTr("The drawing data in this file cannot be read. The picture is shown as it was saved.")
                            case "missing":
                                return qsTr("The drawing %1 is not in the notebook.").arg(root.fileName !== "" ? root.fileName : root.routePath)
                            }
                            return ""
                        }
                    }
                    BButton {
                        visible: root.status !== "missing"
                        anchors.horizontalCenter: parent.horizontalCenter
                        iconName: "image"
                        text: qsTr("Open as a picture")
                        onClicked: AppController.navigate({ view: "page", path: root.loadedPath })
                    }
                    Image {
                        id: noticePicture
                        objectName: "drawingNoticePicture"
                        /// The picture's own size (its SVG root), shown at most as wide as the view.
                        // (Set by readNatural(): a call's size cannot be bound compiled.)
                        property size natural: Qt.size(0, 0)
                        function readNatural(): void {
                            if (root.loadedPath !== "" && notice.visible)
                                noticePicture.natural = Workspace.pictureSize(root.loadedPath)
                            else
                                noticePicture.natural = Qt.size(0, 0)
                        }
                        Connections {
                            target: root
                            function onLoadedPathChanged() { noticePicture.readNatural() }
                        }
                        Connections {
                            target: notice
                            function onVisibleChanged() { noticePicture.readNatural() }
                        }
                        Component.onCompleted: noticePicture.readNatural()
                        // (Also while the app's other theme loads: the shown picture stays.)
                        visible: root.status !== "missing"
                                 && (noticePicture.status === Image.Ready
                                     || (noticePicture.status === Image.Loading && noticePicture.implicitWidth > 0))
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(parent.width, natural.width > 0 ? natural.width : parent.width)
                        height: natural.width > 0 ? width * natural.height / natural.width : implicitHeight
                        fillMode: Image.PreserveAspectFit
                        // (Logical pixels: Qt renders SVG and image providers at the screen's ratio.)
                        sourceSize: Qt.size(Math.ceil(width), 0)
                        // A drawing as the app's theme shows it (an Excalidraw drawing with frames
                        // turned dark like Excalidraw does); any other picture as it is.
                        source: !notice.visible || root.status === "missing" || root.loadedPath === "" ? ""
                                : root.status !== "notDrawing" && Workspace.isDrawing(root.loadedPath)
                                  ? Workspace.drawingUrl(root.loadedPath, Theme.dark)
                                  : Workspace.assetUrl(root.loadedPath)
                        retainWhileLoading: true
                        asynchronous: true
                    }
                }
            }

            BText {
                // The zoom while it is not 100 %.
                objectName: "drawingZoom"
                visible: root.status === "" && Math.abs(canvas.zoom - 1) > 0.005
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.space3
                text: Math.round(canvas.zoom * 100) + " %"
                variant: "caption"
                // On the paper (light or dark as the drawing is shown): 6.9:1 / 7.2:1.
                color: canvas.paperIsDark ? "#9ba1af" : "#535a6a"
            }
        }

        DrawingToolbar {
            id: bottomBar
            objectName: "drawingToolbarBottom"
            Layout.fillWidth: true
            visible: Theme.narrow && root.status === ""
            compact: true
            canvas: canvas
            colors: canvas.tool === "highlighter" ? root.highlighterColors : root.penColors
            widths: canvas.tool === "highlighter" ? root.highlighterWidths : root.penWidths
            fontSizes: root.fontSizes
            shapeTool: root.shapeTool
            fingerDraws: root.fingerDraws
            onColorChosen: (color) => root.chooseColor(color)
            onWidthChosen: (width) => root.chooseWidth(width)
            onFontSizeChosen: (size) => root.chooseFontSize(size)
            onShapeChosen: (tool) => root.chooseShape(tool)
            onSelectModeChosen: (mode) => root.chooseSelectMode(mode)
            onFingerToggled: root.toggleFinger()
            onFitRequested: canvas.fitAll()
            onDone: root.done()
            onBackdropRequested: root.pickBackdrop()
            onBackdropPageRequested: root.otherBackdropPage()
            onPageStepRequested: (step) => root.stepPage(step)
            onPasteRequested: root.paste()
            onBackdropRemoved: canvas.removeBackdrop()
            onPaperColorRequested: root.openPaperDialog()
        }
    }
}
