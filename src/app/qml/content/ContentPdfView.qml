pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import Brulion

// ContentPdfView — a PDF of the notebook opened as a page (route "page" with a PDF's path; docs/UI.md
// "PDF viewer"): its pages one under another (PdfViewModel lays them out exactly and keeps only those
// near the view; their pictures come from the image provider "pdfview", drawn off the GUI thread at
// the screen's ratio with a memory cap — a page's detail picture when zoomed in), zoom (pinch, double
// tap, Ctrl+wheel, Ctrl+± and the bar's buttons, fit width), "Strona 3 z 12" with a jump to a page,
// search with highlights where the renderer extracts text (Workspace.pdfSearchable: hidden
// otherwise), "Otwórz w innej aplikacji", "Rysuj na tej stronie" (a drawing whose backdrop is the
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
// a touch screen, by a long press (a word) and moving the finger on — Ctrl+C, the floating "Kopiuj",
// the right-click menu, Ctrl+A for the current page —; the PDF's table of contents ("Spis treści": a
// list that jumps); its links (a page of the PDF, or an http(s) / mailto address through
// Workspace.openUrl); a password asked for a protected PDF (never kept); two pages side by side
// (remembered on the device: AppSettings "pdf/twoPages", "pdf/coverAlone").
// Where no renderer is built in (the browser, a desktop build without one) it shows the file's name
// and size, explains that and offers "Otwórz w nowej karcie" / "Otwórz w innej aplikacji" (a real
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

    readonly property alias model: pdf
    readonly property string fileName: {
        const i = root.path.lastIndexOf("/")
        return i < 0 ? root.path : root.path.slice(i + 1)
    }
    /// Zoomed in beyond the view's width: the drawer's edge strip yields to panning (PageView).
    readonly property bool pannedSideways: pdf.status === PdfViewModel.Ready && pdf.contentWidth > flick.width + 1
    /// Room for the full bar (else the narrow one: page "3/12", search, ⋯).
    readonly property bool wide: root.width >= Theme.compactBreakpoint && !root.inSidePanel
    readonly property bool ready: pdf.status === PdfViewModel.Ready
    readonly property bool browser: Qt.platform.os === "wasm"
    readonly property string openElsewhereText: root.browser ? qsTr("Open in a new tab") : qsTr("Open in another app")
    property bool searchOpen: false
    /// "Rysuj na tej stronie" being prepared (DrawingBackdrops' job) and its page.
    property int drawJob: 0
    property int drawPage: 0
    /// The drawings of the current page the note embeds already (the draw menu offers them).
    property var existingDrawings: []
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

    Connections {
        target: Workspace
        function onFileChanged(path) {
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
        twoPages: String(AppSettings.value("pdf/twoPages", false)) === "true"
        coverAlone: String(AppSettings.value("pdf/coverAlone", false)) === "true"
        // (Once the bindings on the status, `ready` among them, followed it.)
        onStatusChanged: if (status === PdfViewModel.Ready) Qt.callLater(root.whenReady)
        onCurrentPageChanged: if (root.ready) writeRoute.restart()
    }

    // ---- Route ------------------------------------------------------------------------------

    function whenReady() {
        if (!root || !root.ready)
            return
        root._readyPath = root.path
        root.applyRouteScale()
        root.applyRoutePage()
        root.openRequested()
    }

    /// The page a route names: pdfPage, else Obsidian's "#page=N" kept as a heading.
    function routePage(r) {
        if (r && r.pdfPage > 0)
            return r.pdfPage
        const m = r && r.heading ? /(?:^|&)\s*page\s*=\s*(\d{1,7})\s*(?:&|$)/i.exec(r.heading) : null
        return m ? parseInt(m[1]) : 1
    }
    onRouteChanged: Qt.callLater(root.applyRouteFromOutside)
    function applyRouteFromOutside() {
        root.applyRouteScale()
        root.applyRoutePage()
    }
    /// route.pdfScale (a zoom in percent): once per path and value, before the page is jumped to.
    function applyRouteScale() {
        if (!root || !root.ready || root._readyPath !== root.path || !root.route || !(root.route.pdfScale > 0))
            return
        const key = root.path + "\n" + root.route.pdfScale
        if (key === root._scaled)
            return
        root._scaled = key
        root.moveTo(pdf.setScaleAround(root.route.pdfScale / 100 * 96 / 72, Qt.point(flick.width / 2, 0)))
    }
    function applyRoutePage() {
        if (!root || !root.ready || root._readyPath !== root.path)
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
        onTriggered: {
            const current = AppController.currentRoute
            if (root.inSidePanel || !root.paneActive || !root.ready || root._readyPath !== root.path || !current
                    || current.path !== root.path)
                return
            const page = pdf.currentPage
            if (page < 1 || root.routePage(current) === page)
                return
            root._routePage = page
            const next = Object.assign({}, current, { pdfPage: page })
            delete next.heading
            AppController.replaceRoute(next)
        }
    }
    /// route.open: once per path and value.
    function openRequested() {
        if (!root.route || !root.route.open || !root.ready || root._readyPath !== root.path)
            return
        const key = root.path + "\n" + root.route.open
        if (key === root._opened)
            return
        root._opened = key
        switch (root.route.open) {
        case "search":
            root.openSearch()
            if (root.route.search) {
                searchField.text = root.route.search
                pdf.searchText = root.route.search
            }
            break
        case "jump":
            root.openJump()
            break
        case "draw":
            root.drawOnPage()
            break
        }
    }

    // ---- Moving and zooming -------------------------------------------------------------------

    function moveTo(x, y) {
        if (typeof x === "object") { // a point
            y = x.y
            x = x.x
        }
        if (!isFinite(x) || !isFinite(y))
            return
        flick.contentX = Math.max(0, x)
        flick.contentY = Math.max(0, y)
    }
    function jumpTo(page) {
        if (!root.ready)
            return
        const p = Math.max(1, Math.min(pdf.pageCount, page))
        root.moveTo(pdf.positionForPage(p, 0))
        pdf.pinPage(p) // (named also when it cannot reach the view's top)
        pdf.settle()
    }
    function scrollBy(dx, dy) {
        const maxX = Math.max(0, flick.contentWidth - flick.width)
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        flick.contentX = Math.max(0, Math.min(maxX, flick.contentX + dx))
        flick.contentY = Math.max(0, Math.min(maxY, flick.contentY + dy))
    }
    readonly property point centre: Qt.point(flick.width / 2, flick.height / 2)
    function zoomBy(factor, point) {
        if (root.ready)
            root.moveTo(pdf.zoomAround(factor, point || root.centre))
    }
    function zoomTo(percent) {
        if (root.ready)
            root.moveTo(pdf.setScaleAround(percent / 100 * 96 / 72, root.centre))
    }
    /// A double tap / click: fit width ↔ twice that, around `point` (in the view).
    function toggleZoomAt(point) {
        if (!root.ready)
            return
        if (pdf.fitWidth)
            root.zoomBy(2, point)
        else
            root.moveTo(pdf.fitToWidth(point))
    }
    function fitWidth() {
        if (root.ready)
            root.moveTo(pdf.fitToWidth(Qt.point(0, 0)))
    }
    /// The gesture's preview committed: one zoom around where it happened.
    function commitPreview() {
        if (root.previewScale === 1)
            return
        const factor = root.previewScale
        const point = Qt.point(root.previewOrigin.x - flick.contentX, root.previewOrigin.y - flick.contentY)
        root.previewScale = 1
        root.zoomBy(factor, point)
    }
    /// A preview factor that keeps the committed zoom within the model's limits.
    function boundedPreview(factor) {
        return Math.max(pdf.minScale / pdf.scale, Math.min(pdf.maxScale / pdf.scale, factor))
    }
    Timer {
        id: wheelCommit
        interval: 150
        onTriggered: root.commitPreview()
    }

    // ---- Search, jump, open elsewhere, draw -----------------------------------------------------

    function openSearch() {
        if (!pdf.searchAvailable || !root.ready)
            return
        root.searchOpen = true
        searchField.forceActiveFocus()
        searchField.selectAll()
    }
    function closeSearch() {
        root.searchOpen = false
        searchField.text = ""
        pdf.clearSearch()
        root.forceActiveFocus()
    }
    function nextHit(backwards) {
        const at = backwards ? pdf.previousHit() : pdf.nextHit()
        if (isFinite(at.x))
            root.moveTo(at)
    }
    function openJump() {
        if (!root.ready)
            return
        jumpField.text = String(pdf.currentPage)
        jumpDialog.open()
    }
    function openElsewhere() {
        Workspace.openInOtherApp(root.path)
    }
    function drawOnPage() {
        if (!root.ready || root.drawJob !== 0)
            return
        const page = Math.max(1, pdf.currentPage)
        const existing = Workspace.drawingsOnPdfPage(root.path, page, root.route && root.route.from ? root.route.from : "")
        if (existing.length > 0) {
            root.existingDrawings = existing
            root.drawPage = page
            drawMenu.popup(drawButtonAnchor(), 0, drawButtonAnchor().height)
        } else {
            root.startDrawing(page)
        }
    }
    function drawButtonAnchor() {
        return root.wide ? drawButton : moreButton
    }
    /// The page shown, into the route now (the view is about to be left: Back returns here).
    function rememberPage() {
        writeRoute.stop()
        writeRoute.triggered()
    }
    /// A drawing of this page the note has already, opened.
    function openExisting(i) {
        root.rememberPage()
        AppController.navigate({ view: "drawing", path: root.existingDrawings[i].path })
    }
    function startDrawing(page) {
        root.rememberPage()
        root.drawPage = page
        root.drawJob = DrawingBackdrops.prepare(root.path, [page], true)
    }
    Connections {
        target: DrawingBackdrops
        enabled: root.drawJob !== 0
        function onFinished(job, items, cancelled) {
            if (job !== root.drawJob)
                return
            root.drawJob = 0
            if (cancelled)
                return
            if (items.length === 0 || items[0].error !== "") {
                AppController.toast(DrawingBackdrops.errorText(items.length > 0 ? items[0].error : "pdf"), "error")
                return
            }
            const out = Workspace.addDrawingOnPdfPage(root.path, items[0], root.route && root.route.from ? root.route.from : "",
                                                      root.route && root.route.fromLine !== undefined ? root.route.fromLine : -1)
            if (!out.path)
                return
            if (out.journal)
                AppController.toast(qsTr("The drawing was added to today's journal."), "info")
            AppController.navigate({ view: "drawing", path: out.path })
        }
    }

    // ---- Selection, links, outline, password, two pages -------------------------------------------

    /// The page at a point of the pages layer (content coordinates) and the point in fractions of it.
    function pagePoint(x, y, page) {
        const p = page > 0 ? page : pdf.pageAt(x, y)
        const r = pdf.pageRect(p)
        if (p < 1 || !(r.width > 0) || !(r.height > 0))
            return null
        return { page: p, at: Qt.point((x - r.x) / r.width, (y - r.y) / r.height) }
    }
    /// A selection starts at a point of the pages layer.
    function beginSelection(x, y) {
        const hit = root.pagePoint(x, y, 0)
        if (!hit)
            return false
        root.selectPage = hit.page
        root.selectFrom = hit.at
        pdf.clearSelection()
        return true
    }
    /// …and goes on to another point (fractions of the press's page, beyond it too).
    function extendSelection(x, y) {
        const hit = root.pagePoint(x, y, root.selectPage)
        if (hit)
            pdf.selectText(root.selectPage, root.selectFrom, hit.at)
    }
    function copySelection() {
        if (pdf.copySelection())
            AppController.toast(qsTr("Copied to the clipboard"), "success")
    }
    function selectAllOnPage() {
        if (pdf.selectionAvailable && root.ready)
            pdf.selectAll(Math.max(1, pdf.currentPage))
    }
    /// A link of the PDF: a page of it (and a place there), or an address for the browser / mail.
    function followLink(link) {
        if (link.url)
            Workspace.openUrl(link.url) // (only http, https, mailto)
        else if (link.page > 0)
            root.jumpToTarget(link.page, link.top)
    }
    function jumpToTarget(page, top) {
        if (!root.ready || page < 1)
            return
        root.moveTo(pdf.positionForTarget(page, top))
        pdf.pinPage(page)
        pdf.settle()
    }
    function openOutline() {
        if (root.ready && pdf.outline.length > 0)
            outlineDialog.open()
    }
    function setTwoPages(on) {
        pdf.twoPages = on
        AppSettings.setValue("pdf/twoPages", on)
    }
    function setCoverAlone(on) {
        pdf.coverAlone = on
        AppSettings.setValue("pdf/coverAlone", on)
    }
    function submitPassword() {
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

        Rectangle {
            // A hairline under the bar.
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.borderSubtle
        }

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
            Text {
                objectName: "pdfName"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
                text: root.fileName
                textFormat: Text.PlainText // a file name from anywhere (sync, imports) is no markup
                elide: Text.ElideMiddle
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: Theme.fgMuted
            }
            BButton {
                id: pageButton
                objectName: "pdfPageButton"
                visible: root.ready
                size: "small"
                variant: "ghost"
                //: The PDF viewer's page indicator ("Strona 3 z 12"); a click jumps to a page
                text: root.wide ? qsTr("Page %1 of %2").arg(Math.max(1, pdf.currentPage)).arg(pdf.pageCount)
                                : "%1/%2".arg(Math.max(1, pdf.currentPage)).arg(pdf.pageCount)
                Accessible.name: qsTr("Page %1 of %2").arg(Math.max(1, pdf.currentPage)).arg(pdf.pageCount)
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
                onClicked: root.searchOpen ? root.closeSearch() : root.openSearch()
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
        BMenuItem {
            visible: pdf.twoPages
            height: visible ? implicitHeight : 0
            text: qsTr("Cover page alone")
            checkable: true
            checked: pdf.coverAlone
            onTriggered: root.setCoverAlone(!pdf.coverAlone)
        }
    }

    BMenu {
        id: moreMenu
        BMenuItem {
            visible: root.ready && !root.wide
            height: visible ? implicitHeight : 0
            text: qsTr("Zoom in")
            iconName: "zoom-in"
            onTriggered: root.zoomBy(1.25)
        }
        BMenuItem {
            visible: root.ready && !root.wide
            height: visible ? implicitHeight : 0
            text: qsTr("Zoom out")
            iconName: "zoom-out"
            onTriggered: root.zoomBy(0.8)
        }
        BMenuItem {
            visible: root.ready && !root.wide
            height: visible ? implicitHeight : 0
            text: qsTr("Fit width")
            iconName: "maximize"
            onTriggered: root.fitWidth()
        }
        BMenuItem {
            objectName: "pdfMenuTwoPages"
            visible: root.ready && !root.wide
            height: visible ? implicitHeight : 0
            text: qsTr("Two pages side by side")
            iconName: "columns-2"
            checkable: true
            checked: pdf.twoPages
            onTriggered: root.setTwoPages(!pdf.twoPages)
        }
        BMenuItem {
            visible: root.ready && !root.wide && pdf.twoPages
            height: visible ? implicitHeight : 0
            text: qsTr("Cover page alone")
            checkable: true
            checked: pdf.coverAlone
            onTriggered: root.setCoverAlone(!pdf.coverAlone)
        }
        BMenuItem {
            objectName: "pdfMenuDraw"
            visible: root.ready && !root.wide && !pdf.unlocked
            height: visible ? implicitHeight : 0
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

    // "Rysuj na tej stronie" when the note has drawings of this page already: open one, or a new one.
    BMenu {
        id: drawMenu
        objectName: "pdfDrawMenu"
        BMenuItem {
            objectName: "pdfDrawExisting"
            visible: root.existingDrawings.length > 0
            height: visible ? implicitHeight : 0
            //: "Rysuj na tej stronie": a drawing of this page already in the note
            text: visible ? qsTr("Open the drawing “%1”").arg(root.existingDrawings[0].name) : ""
            iconName: "pen-tool"
            onTriggered: root.openExisting(0)
        }
        BMenuItem {
            visible: root.existingDrawings.length > 1
            height: visible ? implicitHeight : 0
            text: visible ? qsTr("Open the drawing “%1”").arg(root.existingDrawings[1].name) : ""
            iconName: "pen-tool"
            onTriggered: root.openExisting(1)
        }
        BMenuItem {
            visible: root.existingDrawings.length > 2
            height: visible ? implicitHeight : 0
            text: visible ? qsTr("Open the drawing “%1”").arg(root.existingDrawings[2].name) : ""
            iconName: "pen-tool"
            onTriggered: root.openExisting(2)
        }
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
        height: visible ? searchRow.implicitHeight + Theme.space2 * 2 : 0
        color: Theme.bg
        z: 2

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.borderSubtle
        }

        RowLayout {
            id: searchRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.space3
            anchors.rightMargin: Theme.space2
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
                onTextEdited: pdf.searchText = text
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
            Text {
                objectName: "pdfSearchStatus"
                Layout.alignment: Qt.AlignVCenter
                Layout.maximumWidth: root.width * 0.45
                elide: Text.ElideRight
                textFormat: Text.PlainText
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: Theme.fgMuted
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                text: {
                    if (pdf.searchText.trim() === "")
                        return ""
                    if (pdf.hitCount > 0)
                        //: Search hits in the PDF viewer: "3 z 12" (+ while still searching or capped)
                        return qsTr("%1 of %2").arg(pdf.currentHit + 1).arg(pdf.hitCount)
                               + (pdf.hitsCapped || pdf.searching ? "+" : "")
                    if (pdf.searching)
                        return pdf.searchedPages > 0 ? qsTr("Searching… page %1 of %2").arg(pdf.searchedPages).arg(pdf.pageCount)
                                                     : ""
                    return pdf.textless ? qsTr("This PDF has no text to search (it may be a scan).") : qsTr("No results")
                }
            }
            BIconButton {
                objectName: "pdfSearchPrevious"
                size: "small"
                iconName: "chevron-up"
                label: qsTr("Previous result")
                shortcut: "Shift+F3"
                enabled: pdf.hitCount > 0
                onClicked: root.nextHit(true)
            }
            BIconButton {
                objectName: "pdfSearchNext"
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
    }

    // ---- Pages ----------------------------------------------------------------------------------

    Rectangle {
        anchors.fill: flick
        color: Theme.bgSunken
    }

    Flickable {
        id: flick

        objectName: "pdfFlick"
        anchors.top: searchBar.visible ? searchBar.bottom : bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
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
        onContentXChanged: pdf.setViewport(contentX, contentY)
        onContentYChanged: pdf.setViewport(contentX, contentY)
        onMovementEnded: pdf.settle()
        Accessible.role: Accessible.Pane
        //: The PDF viewer's pages for screen readers: the zoom ("Powiększenie 85 %")
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

                    objectName: "pdfPage"
                    x: pageX
                    y: pageY
                    width: pageWidth
                    height: pageHeight
                    color: "#ffffff" // a page is white, also in the dark theme
                    border.width: 1
                    border.color: Theme.borderSubtle
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("Page %1 of %2").arg(page).arg(pdf.pageCount)

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
                        readonly property var d: pdf.detailRevision >= 0 ? pdf.detail(pageItem.page) : ({})
                        visible: !!d.url && status === Image.Ready
                        x: d.x || 0
                        y: d.y || 0
                        width: d.width || 0
                        height: d.height || 0
                        source: d.url || ""
                        sourceSize: Qt.size(width, height)
                        asynchronous: true
                        cache: false
                        retainWhileLoading: true
                        smooth: true
                    }
                    Repeater {
                        // Search hits (fractions of the page).
                        model: pdf.hitRevision >= 0 ? pdf.hitRects(pageItem.page) : []
                        delegate: Rectangle {
                            required property var modelData
                            objectName: "pdfHit"
                            x: modelData.x * pageItem.width - 1
                            y: modelData.y * pageItem.height - 1
                            width: modelData.width * pageItem.width + 2
                            height: modelData.height * pageItem.height + 2
                            radius: 2
                            color: Theme.alpha(Theme.accent, modelData.current ? 0.45 : 0.25)
                            border.width: modelData.current ? 2 : 0
                            border.color: Theme.accent
                        }
                    }
                    Repeater {
                        // The selected text (fractions of the page; the page is paper: one blue in
                        // both themes).
                        model: pdf.selectionRevision >= 0 ? pdf.selectionRects(pageItem.page) : []
                        delegate: Rectangle {
                            required property var modelData
                            objectName: "pdfSelection"
                            x: modelData.x * pageItem.width
                            y: modelData.y * pageItem.height
                            width: modelData.width * pageItem.width
                            height: modelData.height * pageItem.height
                            color: Qt.rgba(66 / 255, 133 / 255, 244 / 255, 0.35)
                        }
                    }
                    Repeater {
                        // Links: a page of the PDF, or an address for the browser / mail client.
                        model: pdf.linkRevision >= 0 ? pdf.linkRects(pageItem.page) : []
                        delegate: MouseArea {
                            id: linkArea
                            required property var modelData
                            objectName: "pdfLink"
                            x: modelData.x * pageItem.width
                            y: modelData.y * pageItem.height
                            width: Math.max(1, modelData.width * pageItem.width)
                            height: Math.max(1, modelData.height * pageItem.height)
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            Accessible.role: Accessible.Link
                            Accessible.name: modelData.url !== "" ? modelData.url
                                                                  : qsTr("Page %1 of %2").arg(modelData.page).arg(pdf.pageCount)
                            Accessible.onPressAction: root.followLink(modelData)
                            onClicked: root.followLink(modelData)
                            Loader {
                                // Where an address leads, on hover (created only then).
                                active: linkArea.containsMouse && linkArea.modelData.url !== ""
                                sourceComponent: BToolTip {
                                    parent: linkArea
                                    visible: true
                                    text: linkArea.modelData.url
                                }
                            }
                        }
                    }
                    Text {
                        visible: pageItem.failed
                        anchors.centerIn: parent
                        width: parent.width - 2 * Theme.space4
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: qsTr("Page %1 cannot be shown.").arg(pageItem.page)
                        textFormat: Text.PlainText
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
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
                        const p = flick.mapFromItem(this, wheel.x, wheel.y)
                        root.previewOrigin = Qt.point(flick.contentX + p.x, flick.contentY + p.y)
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
                        const p = flick.mapFromItem(this, mouse.x, mouse.y)
                        selectionMenu.popup(flick, p.x, p.y)
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
                    root.toggleZoomAt(flick.mapFromItem(this, mouse.x, mouse.y))
                }
            }
            PinchHandler {
                id: pinch
                target: null
                acceptedDevices: PointerDevice.TouchScreen | PointerDevice.TouchPad
                onActiveChanged: {
                    if (active) {
                        const p = flick.mapFromItem(null, centroid.scenePosition.x, centroid.scenePosition.y)
                        root.previewOrigin = Qt.point(flick.contentX + p.x, flick.contentY + p.y)
                    } else {
                        root.commitPreview()
                    }
                }
                onActiveScaleChanged: if (active) root.previewScale = root.boundedPreview(activeScale)
            }
            TapHandler {
                // A finger's double tap: fit width ↔ twice that, around the tap. A long press
                // selects the word under it (where the renderer can), and the finger moving on
                // selects on to it (the pages stop scrolling meanwhile: PointHandler below); a
                // tap elsewhere ends the selection.
                id: touchTap
                acceptedDevices: PointerDevice.TouchScreen
                onDoubleTapped: (eventPoint) => root.toggleZoomAt(
                                    flick.mapFromItem(null, eventPoint.scenePosition.x, eventPoint.scenePosition.y))
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
                onActiveChanged: if (!active) root.touchSelecting = false
            }

            BButton {
                // The selection's "Kopiuj", over its first line (scrolls with the page).
                id: copyButton
                objectName: "pdfCopyButton"
                readonly property var first: root.hasSelection && pdf.selectionRevision >= 0
                                             ? pdf.selectionRects(pdf.selectionPage)[0] : undefined
                readonly property rect pageBox: root.hasSelection && pdf.scale > 0 && pdf.contentWidth > 0
                                                ? pdf.pageRect(pdf.selectionPage) : Qt.rect(0, 0, 0, 0)
                visible: first !== undefined && !root.selecting && !root.touchSelecting
                z: 3
                x: first ? Math.max(pageBox.x, Math.min(pageBox.x + pageBox.width - width,
                                                         pageBox.x + first.x * pageBox.width)) : 0
                y: first ? Math.max(0, pageBox.y + first.y * pageBox.height - height - Theme.space2) : 0
                size: "small"
                variant: "secondary"
                iconName: "copy"
                text: qsTr("Copy")
                onClicked: root.copySelection()
            }
        }

        Text {
            objectName: "pdfTooManyPages"
            visible: pdf.totalPages > pdf.pageCount
            x: Theme.space4
            y: pdf.contentHeight - Theme.space4
            width: flick.width - 2 * Theme.space4
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Showing the first %1 pages.").arg(pdf.pageCount)
            textFormat: Text.PlainText
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            color: Theme.fgMuted
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

            Rectangle {
                y: -Theme.space4
                width: parent.width
                height: 1
                color: Theme.borderSubtle
            }
        }
    }
    /// The wide window's side panel lists the references (PageView sets it).
    property bool refsInSidePanel: false
    /// The page a right click was on (the selection menu's "Zaznacz całą stronę").
    property int contextPage: 0

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
        BMenuItem {
            visible: pdf.outline.length > 0
            height: visible ? implicitHeight : 0
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
                Text {
                    objectName: "pdfMessageName"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textLg
                    font.weight: Font.DemiBold
                    color: Theme.fg
                    text: root.fileName
                }
                Text {
                    objectName: "pdfMessageSize"
                    width: parent.width
                    visible: text !== ""
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgMuted
                    text: root.revision >= 0 && root.path !== "" && messageFlick.visible
                          ? Workspace.fileSizeText(root.path) : ""
                }
                Text {
                    objectName: "pdfMessageText"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
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
                Text {
                    objectName: "pdfPasswordNote"
                    visible: passwordField.visible
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgMuted
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
                visible: active && item !== null && !item.empty
                sourceComponent: ContentBacklinks {
                    objectName: "pdfMessageReferences"
                    width: Math.min(messageFlick.width - 2 * Theme.space4, 760)
                    path: root.path
                    rendererPalette: root.rendererPalette
                    compact: Theme.compact || root.inSidePanel
                    showUnlinked: false

                    Rectangle {
                        y: -Theme.space4
                        width: parent.width
                        height: 1
                        color: Theme.borderSubtle
                    }
                }
            }
        }
    }

    // ---- Table of contents ----------------------------------------------------------------------

    /// The outline entry of the current page: the last one leading to it or before it.
    function currentOutlineIndex() {
        let best = -1
        for (let i = 0; i < pdf.outline.length; ++i) {
            const page = pdf.outline[i].page
            if (page > 0 && page <= pdf.currentPage)
                best = i
        }
        return best
    }

    BDialog {
        id: outlineDialog
        objectName: "pdfOutline"
        parent: T.Overlay.overlay
        title: qsTr("Table of contents")
        showFooter: false
        preferredWidth: 440
        property int current: -1
        onAboutToShow: {
            current = root.currentOutlineIndex()
            if (current >= 0)
                Qt.callLater(() => outlineList.positionViewAtIndex(outlineDialog.current, ListView.Center))
        }
        onClosed: root.forceActiveFocus()

        ListView {
            id: outlineList
            objectName: "pdfOutlineList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Math.max(160, root.height * 0.6))
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: outlineDialog.visible ? pdf.outline : []
            T.ScrollBar.vertical: BScrollBar {}
            delegate: BListItem {
                id: entry
                required property var modelData
                required property int index
                objectName: "pdfOutlineEntry"
                width: ListView.view.width
                dense: true
                text: modelData.title !== "" ? modelData.title : qsTr("Page %1").arg(modelData.page)
                leftPadding: Theme.space3 + Math.min(modelData.level, 6) * Theme.space4
                selected: index === outlineDialog.current
                enabled: modelData.page > 0
                trailing: Text {
                    text: entry.modelData.page > 0 ? String(entry.modelData.page) : ""
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgMuted
                }
                onClicked: {
                    outlineDialog.close()
                    root.jumpToTarget(modelData.page, modelData.top)
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
        acceptText: qsTr("Go")
        preferredWidth: 320
        initialFocusItem: jumpField
        acceptEnabled: jumpField.acceptableInput
        onAccepted: root.jumpTo(parseInt(jumpField.text))
        onClosed: root.forceActiveFocus()

        BTextField {
            id: jumpField
            objectName: "pdfJumpField"
            Layout.fillWidth: true
            accessibleName: qsTr("Page number")
            placeholderText: "1–%1".arg(pdf.pageCount)
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 1; top: Math.max(1, pdf.pageCount) }
            onAccepted: if (acceptableInput) jumpDialog.accept()
        }
    }
}
