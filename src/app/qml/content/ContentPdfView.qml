pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ContentPdfView — a PDF of the notebook opened as a page (route "page" with a PDF's path; docs/UI.md
// "PDF viewer"): its pages one under another (PdfViewModel lays them out exactly and keeps only those
// near the view; their pictures come from the image provider "pdfview", drawn off the GUI thread at
// the screen's ratio with a memory cap — a page's detail picture when zoomed in), zoom (pinch, double
// tap, Ctrl+wheel, Ctrl+± and the bar's buttons, fit width), "Page 3 of 12" with a jump to a page,
// search with highlights where the renderer extracts text (Workspace.pdfSearchable: hidden
// otherwise), "Open in another app", "Draw on this page" (a drawing whose backdrop is the
// current page: DrawingBackdrops.prepare → Workspace.addDrawingOnPdfPage, embedded after the PDF in
// the note the viewer was opened from — route.from / fromLine — or in today's journal; a drawing of
// that page already there is offered first), and the notes that embed or link the PDF after the last
// page. Esc closes the search or the jump, else goes back (Back on Android and Alt+← through the
// shell). The pages stay white, framed, on bgSunken in both themes.
// The view writes its page into the route (pdfPage, half a second after it settles; the main pane
// only) so Back from the drawing editor or another page returns to it, and jumps to route.pdfPage
// when the PDF is ready and whenever the route's page changes from outside (a link to another page
// of the same PDF) — never for its own writes.
// Where the renderer has them (PdfViewModel): text selected by dragging the mouse over a page or, on
// a touch screen, by a long press (a word) and moving the finger on — Ctrl+C, the floating "Copy",
// the right-click menu, Ctrl+A for the current page —; the PDF's table of contents ("Contents": a
// list that jumps); its links (a page of the PDF, or an http(s) / mailto address through
// Workspace.openUrl); a password asked for a protected PDF (never kept); two pages side by side
// (remembered on the device: AppSettings "pdf/twoPages", "pdf/coverAlone").
// Where no renderer is built in (the browser, a desktop build without one) it shows the file's name
// and size, explains that and offers "Open in new tab" / "Open in another app" (a real
// click: the gesture the browser wants); a PDF that cannot be shown (a broken file, a password where
// the renderer opens none) says why, with the same button; the notes that embed or link it follow in
// both cases.
//   route.open (screenshots, tests): "search" (with route.search typed), "jump", "draw" (its menu or
//   the drawing); route.pdfScale: a zoom in percent.
Item {
    id: root

    objectName: "pdfView" // (the shell's self-test)

    property string path
    property var route: ({})
    property bool inSidePanel: false
    /// In the focused pane (false: the other pane of a split view, whose route is not the one
    /// navigation and replaceRoute() act on).
    property bool paneActive: true
    /// Renderer colours for the references (BlockEditor.rendererPalette).
    property var rendererPalette: ({})
    /// The wide window's side panel lists the references (PageView sets it).
    property bool refsInSidePanel: false
    /// The page a right click was on (the selection menu's "Select the whole page").
    property int contextPage: 0

    readonly property alias model: pdf
    readonly property string fileName: {
        const i = Js.lastIndexOf(root.path, "/")
        return i < 0 ? root.path : Js.sliceFrom(root.path, i + 1)
    }
    /// Zoomed in beyond the view's width: the drawer's edge strip yields to panning (PageView).
    readonly property bool pannedSideways: pdf.status === PdfViewModel.Ready && pdf.contentWidth > flick.width + 1
    /// Room for the full bar (else the narrow one: page "3/12", search, ⋯).
    readonly property bool wide: root.width >= Theme.compactBreakpoint && !root.inSidePanel
    readonly property bool ready: pdf.status === PdfViewModel.Ready
    /// The page shown (1 while the model has none yet).
    readonly property int shownPage: Math.max(1, pdf.currentPage)
    readonly property bool browser: Qt.platform.os === "wasm"
    readonly property string openElsewhereText: root.browser ? qsTr("Open in a new tab") : qsTr("Open in another app")
    property bool searchOpen: false
    /// The search in words ("" before anything is searched): "3 of 12" (+ while still searching or
    /// capped), "No results", or a long state — the pages searched before a first hit, why this PDF
    /// has nothing to search.
    readonly property string searchStatus: {
        if (Js.trim(pdf.searchText) === "")
            return ""
        if (pdf.hitCount > 0)
            //: Search hits in the PDF viewer: "3 of 12" (+ while still searching or capped)
            return qsTr("%1 of %2").arg(pdf.currentHit + 1).arg(pdf.hitCount)
                   + (pdf.hitsCapped || pdf.searching ? "+" : "")
        if (pdf.searching)
            return pdf.searchedPages > 0 ? qsTr("Searching… page %1 of %2").arg(pdf.searchedPages).arg(pdf.pageCount)
                                         : ""
        return pdf.textless ? qsTr("This PDF has no text to search (it may be a scan).") : qsTr("No results")
    }
    /// A long state in the narrow bar (a phone, the side panel) goes on a line of its own under the
    /// search row, full width: in the row it was cut ("Ten PDF nie ma tekstu do pr…"), squeezed the
    /// field and pushed × off the screen. The counters stay in the row.
    readonly property bool searchStatusBelow: !root.wide && root.searchStatus !== "" && pdf.hitCount === 0
                                              && (pdf.searching || pdf.textless)
    /// "Draw on this page" being prepared (DrawingBackdrops' job) and its page.
    property int drawJob: 0
    property int drawPage: 0
    /// The drawings of the current page the note embeds already (the draw menu offers them).
    property list<var> existingDrawings: []
    /// A map or a list a call returned, read with AppController.route*() (compiled calls cannot
    /// take one held in a local after another call: kept here).
    property var _held: ({})
    property list<var> _heldList: []
    property list<int> _pages: []
    /// No rows (a Repeater's model while there is nothing to show).
    readonly property list<var> noRows: []
    /// A position the model computed (contentX/Y of the Flickable), kept to be read compiled.
    property point _at: Qt.point(0, 0)
    /// A page's rectangle (content coordinates) and the last pagePoint(): its page and the point
    /// in fractions of it.
    property rect _box: Qt.rect(0, 0, 0, 0)
    property int _hitPage: 0
    property point _hitAt: Qt.point(0, 0)
    /// The zoom preview of a gesture (Ctrl+wheel, pinch): the pages layer scaled on the GPU around
    /// `previewOrigin` (content coordinates); committed once at the end (the pages drawn again once).
    property real previewScale: 1
    property point previewOrigin: Qt.point(0, 0)
    /// Grows when the file changes on disk: its version is asked for again (the model reloads).
    property int revision: 0
    /// Selecting text: the page the press was on and where (fractions of that page); a mouse drag
    /// (`selecting`) or a finger after a long press (`touchSelecting`).
    property int selectPage: 0
    property point selectFrom: Qt.point(0, 0)
    property bool selecting: false
    property bool touchSelecting: false
    readonly property bool hasSelection: pdf.selectionPage > 0 && pdf.selectedText !== ""
    /// The route page applied last (also the page this view wrote into the route itself).
    property int _routePage: -1
    property string _opened: ""
    property string _scaled: ""
    /// The path the model has opened (set once it is ready): until then the route's page and zoom
    /// wait — right after a rename the model still shows the old file as Ready for a moment.
    property string _readyPath: ""
    // Another file in the same view (a rename or a move — the route keeps its pdfPage —, the quick
    // switcher, Back to the PDF before): its route page and zoom applied afresh once it is ready.
    onPathChanged: {
        root._readyPath = ""
        root._routePage = -1
        root._opened = ""
        root._scaled = ""
    }

    Component.onDestruction: if (root.drawJob !== 0) DrawingBackdrops.cancel(root.drawJob)

    /// A hairline across its parent (under the bar, above the references).
    component Rule: Rectangle {
        width: parent.width
        height: 1
        color: Theme.borderSubtle
    }
    /// The viewer's small muted UI text; plain, since file names and PDF text come from anywhere
    /// (sync, imports) and are no markup.
    component UiText: Text {
        textFormat: Text.PlainText
        font.family: Theme.fontUi
        font.pixelSize: Theme.textSm
        color: Theme.fgMuted
    }
    /// A menu entry that takes no room while hidden.
    component HidingMenuItem: BMenuItem {
        height: visible ? implicitHeight : 0
    }
    /// The entry that opens the `slot`-th drawing of the page the note embeds already.
    component ExistingDrawing: HidingMenuItem {
        required property int slot
        visible: root.existingDrawings.length > slot
        //: "Draw on this page": a drawing of this page already in the note
        text: visible ? qsTr("Open the drawing “%1”").arg(root.existingName(slot)) : ""
        iconName: "pen-tool"
        onTriggered: root.openExisting(slot)
    }

    //: The PDF viewer's page indicator ("Page 3 of 12"); a click jumps to a page
    function pageOf(page: int): string {
        return qsTr("Page %1 of %2").arg(page).arg(pdf.pageCount)
    }

    Connections {
        target: Workspace
        function onFileChanged(path: string) {
            if (path === root.path)
                root.revision++
        }
    }

    PdfViewModel {
        id: pdf
        file: root.path !== "" ? Workspace.absoluteFilePath(root.path) : ""
        version: root.revision >= 0 && root.path !== "" ? Workspace.fileVersion(root.path) : ""
        viewportWidth: flick.width
        viewportHeight: flick.height
        devicePixelRatio: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
        margin: Theme.compact || root.inSidePanel ? Theme.space2 : Theme.space6
        gap: Theme.space3
        maxFitWidth: Theme.narrow || root.inSidePanel ? 1e6 : 1000
        onMoveViewport: (x, y) => root.moveTo(x, y)
        // Two pages side by side, as last chosen on this device (QSettings' INI gives strings).
        twoPages: root.settingText("pdf/twoPages") === "true"
        coverAlone: root.settingText("pdf/coverAlone") === "true"
        // (Once the bindings on the status, `ready` among them, followed it: readyLater — a
        // Timer of this view, not Qt.callLater, so a view destroyed meanwhile — a tab switched
        // away — is never called back.)
        onStatusChanged: if (pdf.status === PdfViewModel.Ready) readyLater.restart()
        onCurrentPageChanged: if (root.ready) writeRoute.restart()
    }

    Timer {
        id: readyLater
        interval: 0
        onTriggered: root.whenReady()
    }

    // ---- Route ------------------------------------------------------------------------------

    /// A device setting as text (String(AppSettings.value(key, false))).
    function settingText(key: string): string {
        return AppSettings.value(key, false)
    }
    function whenReady(): void {
        if (!root || !root.ready)
            return
        root._readyPath = root.path
        root.applyRouteScale()
        root.applyRoutePage()
        root.openRequested()
    }

    /// The page a route names: pdfPage, else Obsidian's "#page=N" kept as a heading.
    function routePage(r: var): int {
        root._held = r
        const pdfPage = AppController.routeInt(root._held, "pdfPage", 0)
        if (pdfPage > 0)
            return pdfPage
        const m = root.headingPage("" + AppController.routeText(root._held, "heading"))
        return m.length > 1 ? Js.parseInt(m[1]) : 1
    }
    /// Obsidian's "#page=N" in a heading: [match, N] ([] without one).
    function headingPage(heading: string): list<string> {
        return heading !== "" ? Js.match(heading, "(?:^|&)\\s*page\\s*=\\s*(\\d{1,7})\\s*(?:&|$)", "i") : []
    }
    onRouteChanged: routeLater.restart()
    Later {
        id: routeLater
        onTriggered: root.applyRouteFromOutside()
    }
    function applyRouteFromOutside(): void {
        root.applyRouteScale()
        root.applyRoutePage()
    }
    /// route.pdfScale (a zoom in percent): once per path and value, before the page is jumped to.
    function applyRouteScale(): void {
        const scale = Js.toNumber(AppController.routeText(root.route, "pdfScale"))
        if (!root.ready || root._readyPath !== root.path || !(scale > 0))
            return
        const key = root.path + "\n" + AppController.routeText(root.route, "pdfScale")
        if (key === root._scaled)
            return
        root._scaled = key
        root.moveToPoint(pdf.setScaleAround(scale / 100 * 96 / 72, Qt.point(flick.width / 2, 0)))
    }
    function applyRoutePage(): void {
        if (!root.ready || root._readyPath !== root.path)
            return
        const page = Math.max(1, Math.min(pdf.pageCount, root.routePage(root.route)))
        if (page === root._routePage)
            return
        root._routePage = page
        if (page !== pdf.currentPage)
            root.jumpTo(page)
    }
    // The page shown, into the route (Back returns to it); the main pane's only.
    Timer {
        id: writeRoute
        interval: 500
        onTriggered: root.writePageIntoRoute()
    }
    function writePageIntoRoute(): void {
        if (root.inSidePanel || !root.paneActive || !root.ready || root._readyPath !== root.path
                || AppController.routeText(AppController.currentRoute, "path") !== root.path)
            return
        const page = pdf.currentPage
        if (page < 1 || root.routePage(AppController.currentRoute) === page)
            return
        root._routePage = page
        AppController.replaceRoute(Js.withKey(Js.withoutKey(AppController.currentRoute, "heading"), "pdfPage", page))
    }
    /// route.open: once per path and value.
    function openRequested(): void {
        const open = AppController.routeText(root.route, "open")
        if (open === "" || !root.ready || root._readyPath !== root.path)
            return
        const key = root.path + "\n" + open
        if (key === root._opened)
            return
        root._opened = key
        switch (open) {
        case "search": {
            root.openSearch()
            const search = AppController.routeText(root.route, "search")
            if (search !== "") {
                searchField.text = search
                pdf.searchText = search
            }
            break
        }
        case "jump":
            root.openJump()
            break
        case "draw":
            root.drawOnPage()
            break
        }
    }

    // ---- Moving and zooming -------------------------------------------------------------------

    function moveTo(x: real, y: real): void {
        if (!Js.isFinite(x) || !Js.isFinite(y))
            return
        flick.contentX = Math.max(0, x)
        flick.contentY = Math.max(0, y)
    }
    function moveToPoint(p: point): void {
        root.moveTo(p.x, p.y)
    }
    function jumpTo(page: int): void {
        if (!root.ready)
            return
        const p = Math.max(1, Math.min(pdf.pageCount, page))
        root.moveToPoint(pdf.positionForPage(p, 0))
        pdf.pinPage(p) // (named also when it cannot reach the view's top)
        pdf.settle()
    }
    function scrollBy(dx: real, dy: real): void {
        const maxX = Math.max(0, flick.contentWidth - flick.width)
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        flick.contentX = Math.max(0, Math.min(maxX, flick.contentX + dx))
        flick.contentY = Math.max(0, Math.min(maxY, flick.contentY + dy))
    }
    readonly property point centre: Qt.point(flick.width / 2, flick.height / 2)
    function zoomBy(factor: real): void {
        root.zoomByAt(factor, root.centre)
    }
    function zoomByAt(factor: real, point: point): void {
        if (!root.ready)
            return
        root.moveToPoint(pdf.zoomAround(factor, point))
    }
    function zoomTo(percent: real): void {
        if (!root.ready)
            return
        root.moveToPoint(pdf.setScaleAround(percent / 100 * 96 / 72, root.centre))
    }
    /// A double tap / click: fit width ↔ twice that, around `point` (in the view).
    function toggleZoomAt(point: point): void {
        if (!root.ready)
            return
        if (pdf.fitWidth) {
            root.zoomByAt(2, point)
        } else {
            root.moveToPoint(pdf.fitToWidth(point))
        }
    }
    function fitWidth(): void {
        if (!root.ready)
            return
        root.moveToPoint(pdf.fitToWidth(Qt.point(0, 0)))
    }
    /// The gesture's preview committed: one zoom around where it happened.
    function commitPreview(): void {
        if (root.previewScale === 1)
            return
        const factor = root.previewScale
        root._at = Qt.point(root.previewOrigin.x - flick.contentX, root.previewOrigin.y - flick.contentY)
        root.previewScale = 1
        root.zoomByAt(factor, root._at)
    }
    /// A preview factor that keeps the committed zoom within the model's limits.
    function boundedPreview(factor: real): real {
        return Math.max(pdf.minScale / pdf.scale, Math.min(pdf.maxScale / pdf.scale, factor))
    }
    Timer {
        id: wheelCommit
        interval: 150
        onTriggered: root.commitPreview()
    }

    // ---- Search, jump, open elsewhere, draw -----------------------------------------------------

    function openSearch(): void {
        if (!pdf.searchAvailable || !root.ready)
            return
        root.searchOpen = true
        searchField.forceActiveFocus()
        searchField.selectAll()
    }
    function closeSearch(): void {
        root.searchOpen = false
        searchField.text = ""
        pdf.clearSearch()
        root.forceActiveFocus()
    }
    function nextHit(backwards: bool): void {
        root._at = backwards ? pdf.previousHit() : pdf.nextHit()
        if (Js.isFinite(root._at.x))
            root.moveToPoint(root._at)
    }
    function openJump(): void {
        if (!root.ready)
            return
        jumpField.text = "" + pdf.currentPage
        jumpDialog.open()
    }
    function openElsewhere(): void {
        Workspace.openInOtherApp(root.path)
    }
    /// The drawings of `page` the note `from` embeds (Workspace.drawingsOnPdfPage).
    function drawingsOn(page: int): list<var> {
        return Workspace.drawingsOnPdfPage(root.path, page, AppController.routeText(root.route, "from"))
    }
    function drawOnPage(): void {
        if (!root.ready || root.drawJob !== 0)
            return
        const page = root.shownPage
        const existing = root.drawingsOn(page)
        if (existing.length > 0) {
            root.existingDrawings = existing
            root.drawPage = page
            const anchor = root.drawButtonAnchor()
            drawMenu.popup(anchor, 0, anchor.height)
        } else {
            root.startDrawing(page)
        }
    }
    function drawButtonAnchor(): Item {
        return root.wide ? drawButton : moreButton
    }
    /// The page shown, into the route now (the view is about to be left: Back returns here).
    function rememberPage(): void {
        writeRoute.stop()
        root.writePageIntoRoute()
    }
    /// The name of the drawing `i` of existingDrawings ("" none).
    function existingName(i: int): string {
        return i < root.existingDrawings.length ? AppController.routeText(root.existingDrawings[i], "name") : ""
    }
    /// A drawing of this page the note has already, opened.
    function openExisting(i: int): void {
        root.rememberPage()
        AppController.navigate({ view: "drawing", path: AppController.routeText(root.existingDrawings[i], "path") })
    }
    function startDrawing(page: int): void {
        root.rememberPage()
        root.drawPage = page
        root._pages = [page]
        root.drawJob = DrawingBackdrops.prepare(root.path, root._pages, true)
    }
    Connections {
        target: DrawingBackdrops
        enabled: root.drawJob !== 0
        function onFinished(job: int, items: list<var>, cancelled: bool) {
            if (job !== root.drawJob)
                return
            root.drawJob = 0
            if (cancelled)
                return
            root._heldList = items
            const error = items.length > 0 ? AppController.routeText(root._heldList[0], "error") : "pdf"
            if (error !== "") {
                AppController.toast(DrawingBackdrops.errorText(error), "error")
                return
            }
            const from = "" + AppController.routeText(root.route, "from")
            const fromLine = 0 + AppController.routeInt(root.route, "fromLine", -1)
            root._held = Workspace.addDrawingOnPdfPage(root.path, root._heldList[0], from, fromLine)
            const path = AppController.routeText(root._held, "path")
            if (path === "")
                return
            if (AppController.routeFlag(root._held, "journal"))
                AppController.toast(qsTr("The drawing was added to today's journal."), "info")
            AppController.navigate({ view: "drawing", path: path })
        }
    }

    // ---- Selection, links, outline, password, two pages -------------------------------------------

    /// A page's search hits, selected text and links (PdfViewModel's rows: { box, … }).
    function hitsOn(page: int): list<var> {
        return pdf.hitRects(page)
    }
    function selectionOn(page: int): list<var> {
        return pdf.selectionRects(page)
    }
    function linksOn(page: int): list<var> {
        return pdf.linkRects(page)
    }
    /// The page at a point of the pages layer (content coordinates) and the point in fractions of
    /// it: _hitPage and _hitAt (false: no page there).
    function pagePoint(x: real, y: real, page: int): bool {
        const p = page > 0 ? page : pdf.pageAt(x, y)
        root._box = pdf.pageRect(p)
        if (p < 1 || !(root._box.width > 0) || !(root._box.height > 0))
            return false
        root._hitPage = p
        root._hitAt = Qt.point((x - root._box.x) / root._box.width, (y - root._box.y) / root._box.height)
        return true
    }
    /// A selection starts at a point of the pages layer.
    function beginSelection(x: real, y: real): bool {
        if (!root.pagePoint(x, y, 0))
            return false
        root.selectPage = root._hitPage
        root.selectFrom = root._hitAt
        pdf.clearSelection()
        return true
    }
    /// …and goes on to another point (fractions of the press's page, beyond it too).
    function extendSelection(x: real, y: real): void {
        if (root.pagePoint(x, y, root.selectPage))
            pdf.selectText(root.selectPage, root.selectFrom, root._hitAt)
    }
    function copySelection(): void {
        if (pdf.copySelection())
            AppController.toast(qsTr("Copied to the clipboard"), "success")
    }
    function selectAllOnPage(): void {
        if (pdf.selectionAvailable && root.ready)
            pdf.selectAll(root.shownPage)
    }
    /// A link of the PDF: a page of it (and a place there), or an address for the browser / mail.
    function followLink(url: string, page: int, top: real): void {
        if (url !== "")
            Workspace.openUrl(url) // (only http, https, mailto)
        else if (page > 0)
            root.jumpToTarget(page, top)
    }
    function jumpToTarget(page: int, top: real): void {
        if (!root.ready || page < 1)
            return
        root.moveToPoint(pdf.positionForTarget(page, top))
        pdf.pinPage(page)
        pdf.settle()
    }
    function openOutline(): void {
        if (root.ready && pdf.outline.length > 0)
            outlineDialog.open()
    }
    function setTwoPages(on: bool): void {
        pdf.twoPages = on
        AppSettings.setValue("pdf/twoPages", on)
    }
    function setCoverAlone(on: bool): void {
        pdf.coverAlone = on
        AppSettings.setValue("pdf/coverAlone", on)
    }
    function submitPassword(): void {
        if (passwordField.text === "")
            return
        const password = passwordField.text
        passwordField.text = "" // (the field never keeps it either)
        pdf.unlock(password)
    }

    focus: true
    Keys.onPressed: (event) => {
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        let handled = true
        if (ctrl && event.key === Qt.Key_F && pdf.searchAvailable)
            root.openSearch()
        else if (ctrl && event.key === Qt.Key_C && root.hasSelection)
            root.copySelection()
        else if (ctrl && event.key === Qt.Key_A && pdf.selectionAvailable)
            root.selectAllOnPage()
        else if (ctrl && event.key === Qt.Key_G)
            root.openJump()
        else if (ctrl && (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal))
            root.zoomBy(1.25)
        else if (ctrl && event.key === Qt.Key_Minus)
            root.zoomBy(0.8)
        else if (ctrl && event.key === Qt.Key_0)
            root.fitWidth()
        else if (event.key === Qt.Key_F3 && pdf.hitCount > 0)
            root.nextHit(shift)
        else if (ctrl || (event.modifiers & Qt.AltModifier))
            handled = false // (Alt+←/→: the shell's history)
        else if (event.key === Qt.Key_PageDown || (event.key === Qt.Key_Space && !shift))
            root.scrollBy(0, flick.height * 0.9)
        else if (event.key === Qt.Key_PageUp || (event.key === Qt.Key_Space && shift))
            root.scrollBy(0, -flick.height * 0.9)
        else if (event.key === Qt.Key_Home)
            root.jumpTo(1)
        else if (event.key === Qt.Key_End)
            root.moveTo(flick.contentX, Math.max(0, pdf.contentHeight - flick.height))
        else if (event.key === Qt.Key_Down)
            root.scrollBy(0, 48)
        else if (event.key === Qt.Key_Up)
            root.scrollBy(0, -48)
        else if (event.key === Qt.Key_Right && root.pannedSideways)
            root.scrollBy(48, 0)
        else if (event.key === Qt.Key_Left && root.pannedSideways)
            root.scrollBy(-48, 0)
        else if (event.key === Qt.Key_Escape) {
            if (root.hasSelection)
                pdf.clearSelection()
            else if (root.searchOpen)
                root.closeSearch()
            else if (AppController.canGoBack && !root.inSidePanel)
                AppController.back()
            else
                handled = false
        } else
            handled = false
        event.accepted = handled
    }

    // ---- Bar ------------------------------------------------------------------------------------

    Rectangle {
        id: bar

        objectName: "pdfBar"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        implicitHeight: barRow.implicitHeight + Theme.space2 * 2
        color: Theme.bg
        z: 2

        Rule { anchors.bottom: parent.bottom }

        RowLayout {
            id: barRow

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.space4
            anchors.rightMargin: Theme.space2
            spacing: Theme.space1

            BIcon {
                name: "file-text"
                size: Theme.iconSize
                color: Theme.fgMuted
                Layout.alignment: Qt.AlignVCenter
                Layout.rightMargin: Theme.space1
            }
            UiText {
                objectName: "pdfName"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
                text: root.fileName
                elide: Text.ElideMiddle
            }
            BButton {
                id: pageButton
                objectName: "pdfPageButton"
                visible: root.ready
                size: "small"
                variant: "ghost"
                text: root.wide ? root.pageOf(root.shownPage) : "%1/%2".arg(root.shownPage).arg(pdf.pageCount)
                Accessible.name: root.pageOf(root.shownPage)
                Accessible.description: qsTr("Go to page")
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.openJump()
            }
            BIconButton {
                objectName: "pdfZoomOut"
                visible: root.ready && root.wide
                size: "small"
                iconName: "zoom-out"
                label: qsTr("Zoom out")
                shortcut: "Ctrl+-"
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.zoomBy(0.8)
            }
            BButton {
                id: zoomButton
                objectName: "pdfZoomButton"
                visible: root.ready && root.wide
                size: "small"
                variant: "ghost"
                text: "%1 %".arg(pdf.zoomPercent)
                Accessible.name: qsTr("Zoom %1 %").arg(pdf.zoomPercent)
                Layout.alignment: Qt.AlignVCenter
                onClicked: zoomMenu.popup(zoomButton, 0, zoomButton.height)
            }
            BIconButton {
                objectName: "pdfZoomIn"
                visible: root.ready && root.wide
                size: "small"
                iconName: "zoom-in"
                label: qsTr("Zoom in")
                shortcut: "Ctrl++"
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.zoomBy(1.25)
            }
            BIconButton {
                objectName: "pdfSearchButton"
                visible: root.ready && pdf.searchAvailable
                size: "small"
                iconName: "search"
                label: qsTr("Search in PDF")
                shortcut: "Ctrl+F"
                Layout.alignment: Qt.AlignVCenter
                onClicked: {
                    if (root.searchOpen)
                        root.closeSearch()
                    else
                        root.openSearch()
                }
            }
            BIconButton {
                id: outlineButton
                objectName: "pdfOutlineButton"
                visible: root.ready && pdf.outline.length > 0
                size: "small"
                iconName: "list"
                label: qsTr("Table of contents")
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.openOutline()
            }
            BButton {
                id: drawButton
                objectName: "pdfDrawButton"
                visible: root.ready && root.wide && !pdf.unlocked
                size: "small"
                variant: "secondary"
                iconName: "pen-tool"
                busy: root.drawJob !== 0
                text: root.drawJob !== 0 ? qsTr("Preparing page %1…").arg(root.drawPage) : qsTr("Draw on this page")
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.drawOnPage()
            }
            BIconButton {
                id: moreButton
                objectName: "pdfMoreButton"
                size: "small"
                iconName: "ellipsis"
                label: qsTr("More")
                Layout.alignment: Qt.AlignVCenter
                onClicked: moreMenu.popup(moreButton, moreButton.width - moreMenu.width, moreButton.height)
            }
        }
    }

    BMenu {
        id: zoomMenu
        BMenuItem {
            text: qsTr("Fit width")
            shortcut: "Ctrl+0"
            onTriggered: root.fitWidth()
        }
        BMenuSeparator {}
        BMenuItem { text: "50 %"; onTriggered: root.zoomTo(50) }
        BMenuItem { text: "75 %"; onTriggered: root.zoomTo(75) }
        BMenuItem { text: "100 %"; onTriggered: root.zoomTo(100) }
        BMenuItem { text: "125 %"; onTriggered: root.zoomTo(125) }
        BMenuItem { text: "150 %"; onTriggered: root.zoomTo(150) }
        BMenuItem { text: "200 %"; onTriggered: root.zoomTo(200) }
        BMenuItem { text: "400 %"; onTriggered: root.zoomTo(400) }
        BMenuSeparator {}
        BMenuItem {
            objectName: "pdfZoomTwoPages"
            text: qsTr("Two pages side by side")
            checkable: true
            checked: pdf.twoPages
            onTriggered: root.setTwoPages(!pdf.twoPages)
        }
        HidingMenuItem {
            visible: pdf.twoPages
            text: qsTr("Cover page alone")
            checkable: true
            checked: pdf.coverAlone
            onTriggered: root.setCoverAlone(!pdf.coverAlone)
        }
    }

    BMenu {
        id: moreMenu
        HidingMenuItem {
            visible: root.ready && !root.wide
            text: qsTr("Zoom in")
            iconName: "zoom-in"
            onTriggered: root.zoomBy(1.25)
        }
        HidingMenuItem {
            visible: root.ready && !root.wide
            text: qsTr("Zoom out")
            iconName: "zoom-out"
            onTriggered: root.zoomBy(0.8)
        }
        HidingMenuItem {
            visible: root.ready && !root.wide
            text: qsTr("Fit width")
            iconName: "maximize"
            onTriggered: root.fitWidth()
        }
        HidingMenuItem {
            objectName: "pdfMenuTwoPages"
            visible: root.ready && !root.wide
            text: qsTr("Two pages side by side")
            iconName: "columns-2"
            checkable: true
            checked: pdf.twoPages
            onTriggered: root.setTwoPages(!pdf.twoPages)
        }
        HidingMenuItem {
            visible: root.ready && !root.wide && pdf.twoPages
            text: qsTr("Cover page alone")
            checkable: true
            checked: pdf.coverAlone
            onTriggered: root.setCoverAlone(!pdf.coverAlone)
        }
        HidingMenuItem {
            objectName: "pdfMenuDraw"
            visible: root.ready && !root.wide && !pdf.unlocked
            text: qsTr("Draw on this page")
            iconName: "pen-tool"
            enabled: root.drawJob === 0
            onTriggered: root.drawOnPage()
        }
        BMenuItem {
            objectName: "pdfMenuOpenElsewhere"
            text: root.openElsewhereText
            iconName: "external-link"
            onTriggered: root.openElsewhere()
        }
    }

    // "Draw on this page" when the note has drawings of this page already: open one, or a new one.
    BMenu {
        id: drawMenu
        objectName: "pdfDrawMenu"
        ExistingDrawing { objectName: "pdfDrawExisting"; slot: 0 }
        ExistingDrawing { slot: 1 }
        ExistingDrawing { slot: 2 }
        BMenuSeparator {}
        BMenuItem {
            objectName: "pdfDrawNew"
            text: qsTr("New drawing on this page")
            iconName: "plus"
            onTriggered: root.startDrawing(root.drawPage)
        }
    }

    // ---- Search bar -----------------------------------------------------------------------------

    Rectangle {
        id: searchBar

        objectName: "pdfSearchBar"
        visible: root.searchOpen
        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: visible ? searchColumn.implicitHeight + Theme.space2 * 2 : 0
        color: Theme.bg
        z: 2

        Rule { anchors.bottom: parent.bottom }

        // The field, the status, ‹ › and ×; in the narrow bar (a phone, the side panel) a long status
        // takes the line under them instead (searchStatusBelow).
        ColumnLayout {
            id: searchColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.space3
            anchors.rightMargin: Theme.space2
            spacing: Theme.space1

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space1

                BSearchField {
                    id: searchField
                    objectName: "pdfSearchField"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 80
                    size: "small"
                    debounce: 0 // (the model waits 250 ms itself)
                    // Esc closes the search (below), never only clears its text.
                    clearable: false
                    placeholderText: qsTr("Search in PDF")
                    onTextEdited: pdf.searchText = searchField.text
                    onCleared: pdf.searchText = ""
                    onAccepted: root.nextHit((searchField.lastModifiers & Qt.ShiftModifier) !== 0)
                    property int lastModifiers: 0
                    Keys.priority: Keys.BeforeItem
                    Keys.onPressed: (event) => {
                        searchField.lastModifiers = event.modifiers
                        if (event.key === Qt.Key_F3) {
                            root.nextHit((event.modifiers & Qt.ShiftModifier) !== 0)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Escape) {
                            root.closeSearch()
                            event.accepted = true
                        }
                    }
                    // Android: Back while typing closes the search first.
                    Keys.onReleased: (event) => {
                        if (event.key === Qt.Key_Back && root.searchOpen) {
                            root.closeSearch()
                            event.accepted = true
                        }
                    }
                }
                // (The status in every state, also while the line below shows it.)
                UiText {
                    objectName: "pdfSearchStatus"
                    Layout.alignment: Qt.AlignVCenter
                    Layout.maximumWidth: root.width * 0.45
                    visible: text !== "" && !root.searchStatusBelow
                    elide: Text.ElideRight
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                    text: root.searchStatus
                }
                // ‹ › only with hits in the narrow bar (disabled without: room for the field and ×).
                BIconButton {
                    objectName: "pdfSearchPrevious"
                    visible: root.wide || pdf.hitCount > 0
                    size: "small"
                    iconName: "chevron-up"
                    label: qsTr("Previous result")
                    shortcut: "Shift+F3"
                    enabled: pdf.hitCount > 0
                    onClicked: root.nextHit(true)
                }
                BIconButton {
                    objectName: "pdfSearchNext"
                    visible: root.wide || pdf.hitCount > 0
                    size: "small"
                    iconName: "chevron-down"
                    label: qsTr("Next result")
                    shortcut: "F3"
                    enabled: pdf.hitCount > 0
                    onClicked: root.nextHit(false)
                }
                BIconButton {
                    objectName: "pdfSearchClose"
                    size: "small"
                    iconName: "x"
                    label: qsTr("Close search")
                    onClicked: root.closeSearch()
                }
            }
            UiText {
                objectName: "pdfSearchStatusLine"
                Layout.fillWidth: true
                visible: root.searchStatusBelow
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                text: root.searchStatus
            }
        }
    }

    // ---- Pages ----------------------------------------------------------------------------------

    Rectangle {
        anchors.fill: flick
        color: Theme.bgSunken
    }

    Flickable {
        id: flick

        objectName: "pdfFlick"
        anchors.fill: parent
        anchors.topMargin: bar.height + searchBar.height
        visible: root.ready
        clip: true
        contentWidth: Math.max(width, pdf.contentWidth)
        contentHeight: pdf.contentHeight + (refs.visible ? refs.height + Theme.space12 : 0)
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        interactive: !pinch.active && !root.touchSelecting
        TouchFlick.enabled: !pinch.active && !root.touchSelecting
        T.ScrollBar.vertical: BScrollBar {}
        T.ScrollBar.horizontal: BScrollBar {}
        onContentXChanged: pdf.setViewport(flick.contentX, flick.contentY)
        onContentYChanged: pdf.setViewport(flick.contentX, flick.contentY)
        onMovementEnded: pdf.settle()
        Accessible.role: Accessible.Pane
        //: The PDF viewer's pages for screen readers: the zoom ("Zoom 85 %")
        Accessible.name: qsTr("Zoom %1 %").arg(pdf.zoomPercent)

        Item {
            id: pagesLayer
            objectName: "pdfPages"
            width: pdf.contentWidth
            height: pdf.contentHeight
            transform: Scale {
                origin.x: root.previewOrigin.x
                origin.y: root.previewOrigin.y
                xScale: root.previewScale
                yScale: root.previewScale
            }

            Repeater {
                model: pdf.pages
                delegate: Rectangle {
                    id: pageItem
                    required property int page
                    required property real pageX
                    required property real pageY
                    required property real pageWidth
                    required property real pageHeight
                    required property real baseWidth
                    required property real baseHeight
                    required property bool failed
                    // The detail picture (PdfViewModel.detail(): again when the view settled).
                    required property url detailUrl
                    required property rect detailBox

                    objectName: "pdfPage"
                    x: pageX
                    y: pageY
                    width: pageWidth
                    height: pageHeight
                    color: "#ffffff" // a page is white, also in the dark theme
                    border.width: 1
                    border.color: Theme.borderSubtle
                    Accessible.role: Accessible.Graphic
                    Accessible.name: root.pageOf(page)

                    Image {
                        // The base picture: at most a screenful of pixels (PdfViewModel).
                        objectName: "pdfPageImage"
                        anchors.fill: parent
                        anchors.margins: 1
                        source: pdf.pageUrl(pageItem.page)
                        sourceSize: Qt.size(pageItem.baseWidth, pageItem.baseHeight)
                        asynchronous: true
                        cache: false
                        retainWhileLoading: true
                        smooth: true
                    }
                    Image {
                        // The visible part at full resolution, zoomed in, once the view settled.
                        objectName: "pdfPageDetail"
                        id: detailImage
                        visible: pageItem.detailBox.width > 0 && detailImage.status === Image.Ready
                        x: pageItem.detailBox.x
                        y: pageItem.detailBox.y
                        width: pageItem.detailBox.width
                        height: pageItem.detailBox.height
                        source: pageItem.detailUrl
                        sourceSize: Qt.size(width, height)
                        asynchronous: true
                        cache: false
                        retainWhileLoading: true
                        smooth: true
                    }
                    Repeater {
                        // Search hits (fractions of the page).
                        model: pdf.hitRevision >= 0 ? root.hitsOn(pageItem.page) : root.noRows
                        delegate: Rectangle {
                            required property rect box
                            required property bool current
                            objectName: "pdfHit"
                            x: box.x * pageItem.width - 1
                            y: box.y * pageItem.height - 1
                            width: box.width * pageItem.width + 2
                            height: box.height * pageItem.height + 2
                            radius: 2
                            color: Theme.accent.alpha(current ? 0.45 : 0.25)
                            border.width: current ? 2 : 0
                            border.color: Theme.accent
                        }
                    }
                    Repeater {
                        // The selected text (fractions of the page; the page is paper: one blue in
                        // both themes).
                        model: pdf.selectionRevision >= 0 ? root.selectionOn(pageItem.page) : root.noRows
                        delegate: Rectangle {
                            required property rect box
                            objectName: "pdfSelection"
                            x: box.x * pageItem.width
                            y: box.y * pageItem.height
                            width: box.width * pageItem.width
                            height: box.height * pageItem.height
                            color: "#594285f4" // rgba(66, 133, 244, 0.35)
                        }
                    }
                    Repeater {
                        // Links: a page of the PDF, or an address for the browser / mail client.
                        model: pdf.linkRevision >= 0 ? root.linksOn(pageItem.page) : root.noRows
                        delegate: MouseArea {
                            id: linkArea
                            required property rect box
                            required property string url
                            required property int page
                            required property real targetTop
                            objectName: "pdfLink"
                            x: box.x * pageItem.width
                            y: box.y * pageItem.height
                            width: Math.max(1, box.width * pageItem.width)
                            height: Math.max(1, box.height * pageItem.height)
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            Accessible.role: Accessible.Link
                            Accessible.name: url !== "" ? url : root.pageOf(page)
                            Accessible.onPressAction: root.followLink(linkArea.url, linkArea.page, linkArea.targetTop)
                            onClicked: root.followLink(linkArea.url, linkArea.page, linkArea.targetTop)
                            Loader {
                                // Where an address leads, on hover (created only then).
                                active: linkArea.containsMouse && linkArea.url !== ""
                                sourceComponent: BToolTip {
                                    parent: linkArea
                                    visible: true
                                    text: linkArea.url
                                }
                            }
                        }
                    }
                    UiText {
                        visible: pageItem.failed
                        anchors.centerIn: parent
                        width: parent.width - 2 * Theme.space4
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: qsTr("Page %1 cannot be shown.").arg(pageItem.page)
                        color: "#5f6368"
                    }
                }
            }

            // Gestures on the pages (their points are mapped to the view, the preview's scale aside).
            MouseArea {
                // Ctrl+wheel (and a touchpad's pinch on the desktop, sent as Ctrl+wheel): a
                // preview, then one zoom; a double click fits the width ↔ twice that. Other wheel
                // turns go on to the Flickable (scrolling). A press and drag selects text where
                // the renderer can (the Flickable may not steal it then), else pans; a right click
                // opens the menu of the selection. Under the pages' links (z).
                id: pagesMouse
                objectName: "pdfMouse"
                anchors.fill: parent
                z: -1
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                preventStealing: root.selecting
                cursorShape: pdf.selectionAvailable ? Qt.IBeamCursor : root.pannedSideways ? Qt.OpenHandCursor : Qt.ArrowCursor
                onWheel: (wheel) => {
                    if (!(wheel.modifiers & Qt.ControlModifier)) {
                        wheel.accepted = false
                        return
                    }
                    wheel.accepted = true
                    const delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.pixelDelta.y
                    if (delta === 0)
                        return
                    if (root.previewScale === 1) {
                        root._at = flick.mapFromItem(pagesMouse, wheel.x, wheel.y)
                        root.previewOrigin = Qt.point(flick.contentX + root._at.x, flick.contentY + root._at.y)
                    }
                    root.previewScale = root.boundedPreview(root.previewScale * Math.pow(1.0015, delta))
                    wheelCommit.restart()
                }
                // A finger is the TapHandler's and the PinchHandler's (below), never this one's.
                onPressed: (mouse) => {
                    mouse.accepted = mouse.source === Qt.MouseEventNotSynthesized
                    if (!mouse.accepted || !root.ready)
                        return
                    root.forceActiveFocus()
                    if (mouse.button === Qt.RightButton) {
                        root.contextPage = pdf.pageAt(mouse.x, mouse.y)
                        root._at = flick.mapFromItem(pagesMouse, mouse.x, mouse.y)
                        selectionMenu.popup(flick, root._at.x, root._at.y)
                        return
                    }
                    if (pdf.selectionAvailable)
                        root.selecting = root.beginSelection(mouse.x, mouse.y)
                }
                onPositionChanged: (mouse) => {
                    if (root.selecting)
                        root.extendSelection(mouse.x, mouse.y)
                }
                onReleased: root.selecting = false
                onCanceled: root.selecting = false
                onDoubleClicked: (mouse) => {
                    // (A finger's double tap: the TapHandler below.)
                    if (mouse.source !== Qt.MouseEventNotSynthesized)
                        return
                    root.toggleZoomAt(flick.mapFromItem(pagesMouse, mouse.x, mouse.y))
                }
            }
            PinchHandler {
                id: pinch
                target: null
                acceptedDevices: PointerDevice.TouchScreen | PointerDevice.TouchPad
                onActiveChanged: {
                    if (pinch.active) {
                        root._at = flick.mapFromItem(null, pinch.centroid.scenePosition.x, pinch.centroid.scenePosition.y)
                        root.previewOrigin = Qt.point(flick.contentX + root._at.x, flick.contentY + root._at.y)
                    } else {
                        root.commitPreview()
                    }
                }
                onActiveScaleChanged: if (pinch.active) root.previewScale = root.boundedPreview(pinch.activeScale)
            }
            TapHandler {
                // A finger's double tap: fit width ↔ twice that, around the tap. A long press
                // selects the word under it (where the renderer can), and the finger moving on
                // selects on to it (the pages stop scrolling meanwhile: PointHandler below); a
                // tap elsewhere ends the selection.
                id: touchTap
                acceptedDevices: PointerDevice.TouchScreen
                onDoubleTapped: (eventPoint) => {
                    root.toggleZoomAt(flick.mapFromItem(null, eventPoint.scenePosition.x, eventPoint.scenePosition.y))
                }
                onTapped: if (root.hasSelection) pdf.clearSelection()
                onLongPressed: {
                    const p = touchTap.point.position
                    if (pdf.selectionAvailable && root.ready && root.beginSelection(p.x, p.y)) {
                        root.touchSelecting = true
                        pdf.selectWord(root.selectPage, root.selectFrom)
                    }
                }
            }
            PointHandler {
                // The finger after a long press (a passive grab: it sees every move).
                id: touchFinger
                acceptedDevices: PointerDevice.TouchScreen
                onPointChanged: {
                    if (!root.touchSelecting || !touchFinger.active)
                        return
                    // (The word of the long press stays until the finger really moves.)
                    const p = touchFinger.point.position
                    const from = touchFinger.point.pressPosition
                    if (Math.abs(p.x - from.x) + Math.abs(p.y - from.y) > Theme.space2)
                        root.extendSelection(p.x, p.y)
                }
                onActiveChanged: if (!touchFinger.active) root.touchSelecting = false
            }

            BButton {
                // The selection's "Copy", over its first line (scrolls with the page).
                id: copyButton
                objectName: "pdfCopyButton"
                // (The selection's first rectangle, fractions of its page.)
                readonly property rect first: pdf.selectionAnchor
                readonly property bool hasFirst: root.hasSelection && first.width > 0
                readonly property rect pageBox: root.hasSelection && pdf.scale > 0 && pdf.contentWidth > 0
                                                ? pdf.selectionPageRect : Qt.rect(0, 0, 0, 0)
                visible: hasFirst && !root.selecting && !root.touchSelecting
                z: 3
                x: hasFirst ? Math.max(pageBox.x, Math.min(pageBox.x + pageBox.width - width,
                                                           pageBox.x + first.x * pageBox.width)) : 0
                y: hasFirst ? Math.max(0, pageBox.y + first.y * pageBox.height - height - Theme.space2) : 0
                size: "small"
                variant: "secondary"
                iconName: "copy"
                text: qsTr("Copy")
                onClicked: root.copySelection()
            }
        }

        UiText {
            objectName: "pdfTooManyPages"
            visible: pdf.totalPages > pdf.pageCount
            x: Theme.space4
            y: pdf.contentHeight - Theme.space4
            width: flick.width - 2 * Theme.space4
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Showing the first %1 pages.").arg(pdf.pageCount)
        }

        // The notes that embed or link the PDF (not when the wide window's side panel shows them).
        ContentBacklinks {
            id: refs
            objectName: "pdfReferences"
            visible: !empty && !root.refsInSidePanel
            x: Math.max(Theme.space4, (flick.width - width) / 2) + flick.contentX
            y: pdf.contentHeight + Theme.space8
            width: Math.min(flick.width - 2 * Theme.space4, 760)
            path: root.path
            rendererPalette: root.rendererPalette
            compact: Theme.compact || root.inSidePanel
            showUnlinked: false

            Rule { y: -Theme.space4 }
        }
    }

    BMenu {
        id: selectionMenu
        objectName: "pdfSelectionMenu"
        BMenuItem {
            objectName: "pdfMenuCopy"
            text: qsTr("Copy")
            iconName: "copy"
            shortcut: "Ctrl+C"
            enabled: root.hasSelection
            onTriggered: root.copySelection()
        }
        BMenuItem {
            objectName: "pdfMenuSelectPage"
            text: qsTr("Select the whole page")
            iconName: "text-select"
            enabled: pdf.selectionAvailable && root.contextPage > 0
            onTriggered: pdf.selectAll(root.contextPage)
        }
        HidingMenuItem {
            visible: pdf.outline.length > 0
            text: qsTr("Table of contents")
            iconName: "list"
            onTriggered: root.openOutline()
        }
    }

    // ---- Other states ---------------------------------------------------------------------------

    BSpinner {
        anchors.centerIn: flick
        size: Theme.iconSizeLg
        running: pdf.status === PdfViewModel.Loading && root.path !== ""
        visible: running
    }

    // A PDF that cannot be shown here (no renderer, a password, a broken file): the file's name and
    // size, why, the button that opens it elsewhere, and the notes that embed or link it below (a
    // phone has no side panel to list them) — scrolling when they are long.
    Flickable {
        id: messageFlick
        objectName: "pdfMessage"
        visible: pdf.status === PdfViewModel.Failed || pdf.status === PdfViewModel.Unsupported
        anchors.fill: flick
        clip: true
        contentWidth: width
        contentHeight: messageColumn.height
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        TouchFlick.enabled: true
        T.ScrollBar.vertical: BScrollBar {}

        Column {
            id: messageColumn
            width: messageFlick.width
            // Centred while it fits; the references start under the card.
            topPadding: messageRefs.visible ? Theme.space12
                                            : Math.max(Theme.space8, (messageFlick.height - card.height) / 2)
            bottomPadding: Theme.space8
            spacing: Theme.space8

            Column {
                id: card
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(messageFlick.width - 2 * Theme.space6, 440)
                spacing: Theme.space3

                BIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: pdf.status === PdfViewModel.Unsupported ? "file-text" : pdf.locked ? "lock" : "triangle-alert"
                    size: Theme.iconSizeLg * 1.5
                    color: Theme.fgMuted
                }
                UiText {
                    objectName: "pdfMessageName"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.textLg
                    font.weight: Font.DemiBold
                    color: Theme.fg
                    text: root.fileName
                }
                UiText {
                    objectName: "pdfMessageSize"
                    width: parent.width
                    visible: text !== ""
                    horizontalAlignment: Text.AlignHCenter
                    text: root.revision >= 0 && root.path !== "" && messageFlick.visible
                          ? Workspace.fileSizeText(root.path) : ""
                }
                UiText {
                    objectName: "pdfMessageText"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.textBase
                    color: Theme.fg
                    text: pdf.status === PdfViewModel.Failed ? pdf.error
                          : root.browser ? qsTr("Brulion here does not draw PDFs: the browser's own viewer opens it in a new tab (scrolling, zoom, search).")
                          : qsTr("This version of Brulion has no PDF renderer: open it in another app.")
                }
                // A protected PDF: its password (where the renderer opens one; never kept).
                BTextField {
                    id: passwordField
                    objectName: "pdfPasswordField"
                    visible: pdf.status === PdfViewModel.Failed && pdf.locked && pdf.passwordAvailable
                    width: parent.width
                    echoMode: TextInput.Password
                    placeholderText: qsTr("Password")
                    accessibleName: qsTr("The PDF's password")
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhHiddenText | Qt.ImhNoPredictiveText
                                      | Qt.ImhNoAutoUppercase
                    errorText: pdf.passwordRejected ? qsTr("Wrong password. Try again.") : ""
                    onAccepted: root.submitPassword()
                    onVisibleChanged: if (visible && !Theme.touchUi) forceActiveFocus()
                }
                BButton {
                    objectName: "pdfUnlock"
                    visible: passwordField.visible
                    anchors.horizontalCenter: parent.horizontalCenter
                    variant: "primary"
                    iconName: "lock"
                    text: qsTr("Open the PDF")
                    enabled: passwordField.text !== ""
                    onClicked: root.submitPassword()
                }
                UiText {
                    objectName: "pdfPasswordNote"
                    visible: passwordField.visible
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("Brulion does not remember the password: it is asked for each time the PDF is opened.")
                }
                BButton {
                    objectName: "pdfOpenElsewhere"
                    anchors.horizontalCenter: parent.horizontalCenter
                    variant: passwordField.visible ? "secondary" : "primary"
                    iconName: "external-link"
                    text: root.openElsewhereText
                    onClicked: root.openElsewhere()
                }
            }

            // The notes that embed or link the PDF (not when the wide window's side panel shows them).
            Loader {
                id: messageRefs
                anchors.horizontalCenter: parent.horizontalCenter
                active: messageFlick.visible && !root.refsInSidePanel
                visible: active && item !== null && !(item as ContentBacklinks).empty
                sourceComponent: ContentBacklinks {
                    objectName: "pdfMessageReferences"
                    width: Math.min(messageFlick.width - 2 * Theme.space4, 760)
                    path: root.path
                    rendererPalette: root.rendererPalette
                    compact: Theme.compact || root.inSidePanel
                    showUnlinked: false

                    Rule { y: -Theme.space4 }
                }
            }
        }
    }

    // ---- Table of contents ----------------------------------------------------------------------

    BDialog {
        id: outlineDialog
        objectName: "pdfOutline"
        parent: T.Overlay.overlay
        title: qsTr("Table of contents")
        showFooter: false
        preferredWidth: 440
        property int current: -1
        onAboutToShow: {
            // The outline entry of the current page: the last one leading to it or before it.
            outlineDialog.current = pdf.outlineIndexForPage(pdf.currentPage)
            if (outlineDialog.current >= 0)
                outlineLater.restart()
        }
        Later {
            id: outlineLater
            onTriggered: outlineList.positionViewAtIndex(outlineDialog.current, ListView.Center)
        }
        onClosed: root.forceActiveFocus()

        ListView {
            id: outlineList
            objectName: "pdfOutlineList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Math.max(160, root.height * 0.6))
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: outlineDialog.visible ? pdf.outline : root.noRows
            T.ScrollBar.vertical: BScrollBar {}
            delegate: BListItem {
                id: entry
                // PdfViewModel.outline rows (every key always there).
                required property string title
                required property int level
                required property int page
                required property real targetTop
                required property int index
                objectName: "pdfOutlineEntry"
                width: outlineList.width
                dense: true
                text: title !== "" ? title : qsTr("Page %1").arg(page)
                leftPadding: Theme.space3 + Math.min(level, 6) * Theme.space4
                selected: index === outlineDialog.current
                enabled: page > 0
                trailing: UiText { text: entry.page > 0 ? "" + entry.page : "" }
                onClicked: {
                    outlineDialog.close()
                    root.jumpToTarget(entry.page, entry.targetTop)
                }
            }
        }
    }

    // ---- Page jump ------------------------------------------------------------------------------

    BDialog {
        id: jumpDialog
        objectName: "pdfJump"
        parent: T.Overlay.overlay
        title: qsTr("Go to page")
        // The range stays in sight: the field opens filled, so its placeholder seldom shows.
        subtitle: qsTr("%n page(s)", "", pdf.pageCount)
        acceptText: qsTr("Go")
        preferredWidth: 320
        initialFocusItem: jumpField
        /// The page typed (0: none, or not only digits) and whether the PDF has it.
        readonly property int typedPage: Js.test(jumpField.text, "^[0-9]{1,7}$", "") ? Js.parseInt(jumpField.text) : 0
        readonly property bool pageInRange: typedPage >= 1 && typedPage <= pdf.pageCount
        acceptEnabled: pageInRange
        onAccepted: root.jumpTo(jumpDialog.typedPage)
        // The page shown, filled in by openJump() while the field had no focus (the caret at its
        // start), is selected once the field has the focus: a digit typed replaces it instead of
        // going in front of it ("7" on page 3 made 73).
        onOpened: {
            jumpField.forceActiveFocus(Qt.PopupFocusReason)
            jumpField.selectAll()
        }
        onClosed: root.forceActiveFocus()

        BTextField {
            id: jumpField
            objectName: "pdfJumpField"
            Layout.fillWidth: true
            accessibleName: qsTr("Page number")
            placeholderText: "1–%1".arg(pdf.pageCount)
            inputMethodHints: Qt.ImhDigitsOnly
            // A number, whatever its range: a page the PDF has not is said here instead of the key
            // being swallowed. (A regular expression literal does not compile to C++.)
            validator: IntValidator { bottom: 0; top: 9999999 }
            errorText: jumpField.text !== "" && !jumpDialog.pageInRange
                       ? qsTr("Enter a page from 1 to %1.").arg(pdf.pageCount) : ""
            onAccepted: jumpDialog.tryAccept()
        }
    }
}
