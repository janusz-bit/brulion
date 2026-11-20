pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import Brulion

// CanvasView — route "canvas" ({path}, optional `line`): a whiteboard in the JSON Canvas format
// (docs/UI.md "Canvas view", docs/SYNTAX.md §2 "Canvases"; Obsidian's `.canvas` files) — text cards,
// notes, pictures, drawings, PDFs, other files, web addresses and groups laid out on an endless
// canvas, connected by edges.
//
//  * The board (CanvasEditor, C++): every pointer gesture, the view (pan and zoom: the wheel, a
//    touchpad, a pinch, the keys), the selection, the undo history. Cards are QML items of the
//    cards in view only (CanvasEditor.nodes), in an item the board scales and moves; edges are
//    curves (Qt Quick Shapes) in the same world.
//  * What cards show (CanvasCards / CanvasCardContent, C++): a text card's Markdown and a note's
//    page rendered as embeds are (escaped rich text, never a note's HTML); pictures, drawings and
//    PDFs by their previews; web addresses as a card (nothing is fetched).
//  * Editing: a double click (double tap) on a text card types its Markdown in place
//    (CanvasTextEditor), on a note or a file opens it, on a web address opens it in the browser, on
//    a group renames it, on an edge labels it, on empty canvas adds a card there; Enter edits the
//    selected card. The selection's bar (CanvasSelectionBar) and the context menu (a right click,
//    a long press) colour, duplicate, group, align, arrange and delete. The tool bar adds cards,
//    notes (the page picker), files (the system's file picker: imported into the notebook), web
//    addresses and groups. Files dropped from another app are imported and added; a page dragged
//    from the sidebar's tree onto the board is added as a note card where it is dropped
//    (AppController.fileDroppedOnView). Paste: cards copied on a canvas (Brulion's or Obsidian's),
//    files, a picture, a web address or text.
//  * Keys (none while text is typed): Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y, Delete / Backspace, Ctrl+A,
//    Escape (a gesture dropped, else the selection), Ctrl+C / Ctrl+X / Ctrl+V, Ctrl+D (duplicate),
//    Ctrl+G / Ctrl+Shift+G (group, ungroup), Enter (edit), the arrows (move the selection 10, with
//    Shift 1; with nothing selected the canvas), Ctrl+] / Ctrl+[ / Ctrl+Shift+] / Ctrl+Shift+[
//    (z-order), Shift+1 (the whole canvas), Shift+2 (the selection), Ctrl+0 (100 %), Ctrl++ /
//    Ctrl+- (zoom), Space held (the mouse moves the canvas).
//  * Saved by itself: a moment after the last change, when the route changes, the view goes and
//    the app leaves the foreground (Workspace.writeAttachment), in the file's own layout — only
//    what changed is written differently (canvas::writeCanvas). A change of the file from elsewhere
//    (Syncthing, Nextcloud, Obsidian) is taken in: as it is when nothing changed here, else merged
//    card by card (CanvasEditor.takeExternal); every save first reads the file, so another
//    program's version is merged in, never written over. A file that is no canvas is shown with
//    why and never written. Renames follow the file; a deleted file is never written again.
// View contract (ShellRouteView): `title` (the canvas's name), `editing` (a card's text is typed),
// `toolbarDocked` and `replacesBottomNav` (phones: the tool bar at the bottom), `edgeSwipe` (no
// drawer edge strip: a drag from the left edge moves the canvas).
FocusScope {
    id: root

    property var route: ({})
    property bool inSidePanel: false

    focus: true

    readonly property string routePath: AppController.routeText(route, "path")
    readonly property string fileName: Js.sliceFrom(root.loadedPath, Js.lastIndexOf(root.loadedPath, "/") + 1)
    readonly property string title: Js.replaceRegExp(root.fileName, "\\.canvas$", "i", "")
    readonly property bool editing: textEditor.visible
    readonly property bool toolbarDocked: Theme.narrow && root.status === ""
    readonly property bool replacesBottomNav: toolbarDocked
    readonly property bool edgeSwipe: true
    readonly property alias board: board

    /// "" (editable), "loading", "corrupt", "tooLarge", "missing".
    property string status: "loading"
    /// The file the board shows (follows renames; "" none).
    property string loadedPath: ""
    property bool _writing: false
    property var _bytes
    property var _disk
    property bool alive: true
    /// A map a call returned (compiled calls read keys of a held map only).
    readonly property MapHolder held: MapHolder {}
    /// Where a card added by the tool bar or the menu goes (canvas units).
    property point addAt: Qt.point(0, 0)
    /// The node a context menu or a dialog is about.
    property string menuNode: ""
    property string menuEdge: ""

    readonly property var rendererPalette: ({
        link: Theme.link, unresolved: Theme.linkMissing, muted: Theme.fgMuted,
        codeBackground: Theme.bgSunken, highlightBackground: Theme.mark,
        tagFg: Theme.tagFg, tagBg: Theme.tagBg,
        dueFg: Theme.chipDueFg, dueBg: Theme.chipDueBg,
        overdueFg: Theme.chipOverdueFg, overdueBg: Theme.chipOverdueBg,
        scheduledFg: Theme.chipScheduledFg, scheduledBg: Theme.chipScheduledBg,
        reminderFg: Theme.chipReminderFg, reminderBg: Theme.chipReminderBg,
        alarmFg: Theme.chipAlarmFg, alarmBg: Theme.chipAlarmBg,
        recurrenceFg: Theme.chipRecurrenceFg, recurrenceBg: Theme.chipRecurrenceBg,
        doneFg: Theme.chipDoneFg, doneBg: Theme.chipDoneBg,
        neutralFg: Theme.chipNeutralFg, neutralBg: Theme.chipNeutralBg
    })

    // ---- the file ------------------------------------------------------------------------------

    /// Write the canvas when it changed (true: nothing to write, or written); a text being typed
    /// goes in first.
    function save(): bool {
        saveTimer.stop()
        textEditor.commit()
        if (root.loadedPath === "" || root.status !== "" || !board.modified)
            return true
        root._bytes = board.fileBytes()
        // Compared with what the file holds now, right before the write: another program's
        // version written a moment ago is merged in first, never written over.
        for (let checks = 0; checks < 3 && Workspace.exists(root.loadedPath); ++checks) {
            root._disk = Workspace.readAttachment(root.loadedPath)
            if (!board.hasBytes(root._disk) || board.isKnownFile(root._disk))
                break
            if (!root.takeIn())
                return true
            if (!board.modified)
                return true
            root._bytes = board.fileBytes()
        }
        root._writing = true
        const written = Workspace.writeAttachment(root.loadedPath, root._bytes)
        root._writing = false
        if (!written)
            return false // (Workspace.errorOccurred: the shell's toast says why)
        board.markSaved(root._bytes)
        return true
    }

    /// Another program's version of the open canvas (root._disk): merged with what changed here, or
    /// — no canvas any more — shown with why, what was done here kept in a copy beside it.
    function takeIn(): bool {
        const merged = board.modified
        if (board.takeExternal(root._disk)) {
            if (merged) {
                AppController.toast(qsTr("The canvas was changed elsewhere: both versions are kept in it"))
                saveTimer.restart()
            }
            return true
        }
        saveTimer.stop()
        if (board.modified)
            root.keepCopy()
        root.status = board.load(root._disk)
        return false
    }
    function keepCopy(): void {
        const slash = Js.lastIndexOf(root.loadedPath, "/")
        const folder = slash < 0 ? "" : Js.slice(root.loadedPath, 0, slash + 1)
        root._bytes = board.fileBytes()
        const path = Workspace.createAttachment(folder + qsTr("%1 (this device)").arg(root.title) + ".canvas",
                                                root._bytes)
        if (path !== "")
            AppController.toast(qsTr("The canvas was changed elsewhere into a file Brulion cannot read. What you did here is kept in %1").arg(root.baseName(path)))
    }
    /// The last part of a path.
    function baseName(path: string): string {
        return Js.sliceFrom(path, Js.lastIndexOf(path, "/") + 1)
    }

    function load(path: string): void {
        root.loadedPath = path
        textEditor.nodeId = ""
        if (!Workspace.exists(path)) {
            board.load(Workspace.readAttachment(""))
            root.status = "missing"
            return
        }
        root.status = board.load(Workspace.readAttachment(path))
        root.forceActiveFocus()
    }

    function applyRoute(): void {
        if (root.routePath === "")
            return
        if (root.routePath !== root.loadedPath) {
            root.save()
            root.load(root.routePath)
        }
        // A search hit's or a backlink's line: its card, selected and in view.
        const line = AppController.routeInt(root.route, "line", -1)
        if (line >= 0 && root.status === "")
            board.focusLine(line)
    }

    // ---- adding ---------------------------------------------------------------------------------

    function addTextAt(x: real, y: real): void {
        const id = board.addText(x, y, "")
        if (id !== "")
            textEditor.open(id)
    }
    function addText(): void {
        root.addAt = board.viewCenter()
        root.addTextAt(root.addAt.x, root.addAt.y)
    }
    function addNote(): void {
        root.addAt = board.viewCenter()
        notePicker.open()
    }
    function addNoteAt(x: real, y: real): void {
        root.addAt = Qt.point(x, y)
        notePicker.open()
    }
    function addFile(): void {
        root.addAt = board.viewCenter()
        if (filePicker.available)
            filePicker.open()
        else
            AppController.toast(qsTr("Adding files needs the system's file picker; drop or paste the file instead."))
    }
    function addLink(): void {
        root.addAt = board.viewCenter()
        textDialog.ask("link", "", qsTr("Add a web address"), "https://", "https://…")
    }
    function addGroup(): void {
        const id = board.addGroup("")
        if (id !== "" && board.selectedNodes.length === 1)
            textDialog.ask("group", id, qsTr("Group label"), "", qsTr("Label"))
    }
    function paste(): void {
        root.held.map = board.paste(-1, -1)
        if (UiModels.numberField(root.held.map, "pasted", 0) === 0)
            AppController.toast(qsTr("The clipboard holds nothing to put on a canvas."))
    }
    function exportPicture(format: string): void {
        root.save()
        root._bytes = board.exportPicture(format, Theme.dark)
        if (board.nodeCount === 0)
            return
        const slash = Js.lastIndexOf(root.loadedPath, "/")
        const folder = slash < 0 ? "" : Js.slice(root.loadedPath, 0, slash + 1)
        const path = Workspace.createAttachment(folder + root.title + "." + format, root._bytes)
        if (path !== "")
            AppController.toast(qsTr("Saved the canvas as %1").arg(root.baseName(path)), "success")
    }

    // ---- opening ---------------------------------------------------------------------------------

    /// A card's file (a note, a picture, a PDF, another canvas…) or web address opened.
    function openNode(id: string, newPane: bool): void {
        root.held.map = board.nodeData(id)
        OpenIn.begin(newPane ? "tab" : "")
        const opened = cards.openNode(root.held.map)
        OpenIn.end()
        if (!opened)
            AppController.toast(qsTr("The file of this card is not in the notebook."))
    }
    /// What a double click / tap does on a card, an edge or the canvas.
    function activate(nodeId: string, edgeId: string, x: real, y: real): void {
        if (nodeId !== "") {
            root.held.map = board.nodeData(nodeId)
            const type = UiModels.stringField(root.held.map, "type")
            if (type === "group")
                textDialog.ask("group", nodeId, qsTr("Group label"), UiModels.stringField(root.held.map, "label"), qsTr("Label"))
            else if (type === "file" || type === "link")
                root.openNode(nodeId, false)
            else
                textEditor.open(nodeId)
            return
        }
        if (edgeId !== "") {
            root.held.map = board.edgeData(edgeId)
            textDialog.ask("edge", edgeId, qsTr("Edge label"), UiModels.stringField(root.held.map, "label"), qsTr("Label"))
            return
        }
        root.addTextAt(x, y)
    }
    /// A tap on a card: a link in its text followed.
    function followTap(nodeId: string, x: real, y: real, modifiers: int): void {
        if (nodeId === "")
            return
        root.openLinkText(root.linkOf(nodeId, x, y), (modifiers & Qt.ControlModifier) !== 0)
    }
    /// The link at (x, y) (canvas units) of the card `nodeId`, "" none.
    function linkOf(nodeId: string, x: real, y: real): string {
        const row = board.nodes.rowOf(nodeId)
        if (row < 0)
            return ""
        const item = cardRepeater.itemAt(row) as CanvasCard
        if (!item)
            return ""
        const lx = x - item.x
        const ly = y - item.y
        return item.linkAt(lx, ly)
    }
    function openLinkText(link: string, newPane: bool): void {
        if (link === "")
            return
        if (Js.startsWith(link, "ext:"))
            Workspace.openUrl(Js.sliceFrom(link, 4))
        else
            cards.openLink(link, newPane)
    }
    /// The wheel over a selected card whose content is longer than it.
    function scrollCard(nodeId: string, dy: real): void {
        const row = board.nodes.rowOf(nodeId)
        const item = row >= 0 ? cardRepeater.itemAt(row) as CanvasCard : null
        if (item)
            item.scrollBy(dy)
    }
    function showMenu(nodeId: string, edgeId: string, x: real, y: real): void {
        root.menuNode = nodeId
        root.menuEdge = edgeId
        root.addAt = board.toCanvas(x, y)
        contextMenu.popup(board, x, y)
    }
    /// What a menu entry or a dialog chose.
    function textChosen(purpose: string, target: string, text: string): void {
        if (purpose === "link")
            board.addLink(text, root.addAt.x, root.addAt.y)
        else if (purpose === "url")
            board.setNodeUrl(target, text)
        else if (purpose === "group")
            board.setGroupLabel(target, text)
        else if (purpose === "edge")
            board.setEdgeLabel(target, text)
        root.forceActiveFocus()
    }
    /// A page dragged from the sidebar's tree and dropped at (sceneX, sceneY).
    function dropFile(path: string, sceneX: real, sceneY: real): void {
        if (root.status !== "" || !root.visible || !board.containsScene(sceneX, sceneY))
            return
        root.addAt = board.canvasAtScene(sceneX, sceneY)
        board.addFile(path, "", root.addAt.x, root.addAt.y)
        root.forceActiveFocus()
    }

    // One event-loop turn late (as PageView): on a rename the shell rewrites its routes before
    // Workspace.pathRenamed reaches this view.
    onRouteChanged: routeLater.restart()
    Later {
        id: routeLater
        onTriggered: if (root.alive) root.applyRoute()
    }
    Component.onCompleted: root.applyRoute()
    Component.onDestruction: {
        root.alive = false
        root.save()
    }

    Timer {
        id: saveTimer
        interval: 800
        onTriggered: {
            if (board.editingNode === "" && !board.busy)
                root.save()
            else
                saveTimer.restart()
        }
    }
    Connections {
        target: Workspace
        function onPathRenamed(oldPath: string, newPath: string) {
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
            if (!board.hasBytes(root._disk) || board.isKnownFile(root._disk))
                return
            if (root.status === "")
                root.takeIn()
            else
                root.status = board.load(root._disk)
        }
    }
    Connections {
        target: Application
        function onStateChanged() {
            if (Application.state !== Qt.ApplicationActive)
                root.save()
        }
    }
    Connections {
        target: AppController
        function onFileDroppedOnView(path: string, sceneX: real, sceneY: real) {
            root.dropFile(path, sceneX, sceneY)
        }
    }

    CanvasCards {
        id: cards
        host: Workspace
        path: root.loadedPath
        palette: root.rendererPalette
        locale: DatesHelper.locale
        now: DatesHelper.fixedNow
    }

    // ---- keys -------------------------------------------------------------------------------------

    function arrowDelta(key: int, axis: int): int {
        switch (key) {
        case Qt.Key_Left: return axis === 0 ? -1 : 0
        case Qt.Key_Right: return axis === 0 ? 1 : 0
        case Qt.Key_Up: return axis === 0 ? 0 : -1
        case Qt.Key_Down: return axis === 0 ? 0 : 1
        default: return 0
        }
    }
    Keys.onPressed: (event) => {
        if (textEditor.visible || root.status !== "")
            return
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        const arrow = event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Up
                      || event.key === Qt.Key_Down
        if (event.matches(StandardKey.Undo)) {
            board.undo()
        } else if (event.matches(StandardKey.Redo) || (ctrl && event.key === Qt.Key_Y)) {
            board.redo()
        } else if (event.key === Qt.Key_Space && !ctrl) {
            if (!event.isAutoRepeat)
                board.panMode = true
        } else if (ctrl && event.key === Qt.Key_0) {
            board.setZoom(1)
        } else if (ctrl && (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)) {
            board.zoomBy(1.25, -1, -1)
        } else if (ctrl && event.key === Qt.Key_Minus) {
            board.zoomBy(0.8, -1, -1)
        } else if (shift && !ctrl && (event.key === Qt.Key_1 || event.key === Qt.Key_Exclam)) {
            board.zoomToFit()
        } else if (shift && !ctrl && (event.key === Qt.Key_2 || event.key === Qt.Key_At || event.key === Qt.Key_QuoteDbl)) {
            board.zoomToSelection()
        } else if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) && board.selectionCount > 0) {
            board.deleteSelection()
        } else if (event.matches(StandardKey.Copy) && board.selectionCount > 0) {
            board.copySelection()
        } else if (event.matches(StandardKey.Cut) && board.selectionCount > 0) {
            board.cutSelection()
        } else if (event.matches(StandardKey.Paste)) {
            root.paste()
        } else if (event.matches(StandardKey.SelectAll)) {
            board.selectAll()
        } else if (ctrl && !shift && event.key === Qt.Key_D) {
            board.duplicateSelection()
        } else if (ctrl && event.key === Qt.Key_G) {
            if (shift)
                board.ungroupSelection()
            else
                root.addGroup()
        } else if (ctrl && (event.key === Qt.Key_BracketRight || event.key === Qt.Key_BraceRight)) {
            board.reorderSelection(shift ? "front" : "forward")
        } else if (ctrl && (event.key === Qt.Key_BracketLeft || event.key === Qt.Key_BraceLeft)) {
            board.reorderSelection(shift ? "back" : "backward")
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && board.selectedNodes.length === 1) {
            root.activate(board.selectedNodes[0], "", 0, 0)
        } else if (event.key === Qt.Key_Escape && (board.busy || board.selectionCount > 0)) {
            if (!board.cancelGesture())
                board.clearSelection()
        } else if (arrow && !ctrl) {
            const step = shift ? 1 : 10
            if (board.selectedNodes.length > 0)
                board.nudgeSelection(root.arrowDelta(event.key, 0) * step, root.arrowDelta(event.key, 1) * step)
            else
                board.panBy(root.arrowDelta(event.key, 0) * step * 8, root.arrowDelta(event.key, 1) * step * 8)
        } else {
            return
        }
        event.accepted = true
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            board.panMode = false
            event.accepted = true
        }
    }

    // ---- the layout --------------------------------------------------------------------------------

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        CanvasToolbar {
            id: topBar
            objectName: "canvasToolbar"
            Layout.fillWidth: true
            visible: !Theme.narrow && root.status === ""
            board: board
            onAddText: root.addText()
            onAddNote: root.addNote()
            onAddFile: root.addFile()
            onAddLink: root.addLink()
            onAddGroup: root.addGroup()
            onPasteRequested: root.paste()
            onExportRequested: (format) => root.exportPicture(format)
        }

        Item {
            id: boardArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Rectangle {
                anchors.fill: parent
                color: Theme.bg
            }

            // A faint grid that moves and zooms with the canvas (every 20 units; every 100 when
            // that is too dense).
            Item {
                id: grid
                anchors.fill: parent
                visible: root.status === ""
                readonly property real unit: board.zoom * 20 >= 12 ? 20 : 100
                readonly property real step: board.zoom * unit
                readonly property bool shown: step >= 12
                readonly property real offsetX: board.panX - Math.floor(board.panX / step) * step
                readonly property real offsetY: board.panY - Math.floor(board.panY / step) * step
                Repeater {
                    model: grid.shown ? Math.ceil(grid.width / grid.step) + 1 : 0
                    delegate: Rectangle {
                        required property int index
                        x: grid.offsetX + index * grid.step
                        width: 1
                        height: grid.height
                        color: Theme.borderSubtle
                        opacity: 0.6
                    }
                }
                Repeater {
                    model: grid.shown ? Math.ceil(grid.height / grid.step) + 1 : 0
                    delegate: Rectangle {
                        required property int index
                        y: grid.offsetY + index * grid.step
                        height: 1
                        width: grid.width
                        color: Theme.borderSubtle
                        opacity: 0.6
                    }
                }
            }

            CanvasEditor {
                id: board
                objectName: "canvasBoard"
                anchors.fill: parent
                visible: root.status === ""
                host: Workspace
                path: root.loadedPath
                animate: !Theme.reducedMotion
                handleRadius: Theme.touchUi ? 18 : 9
                focus: true
                onContentChanged: if (board.modified) saveTimer.restart()
                onTapped: (nodeId, x, y, button, modifiers) => root.followTap(nodeId, x, y, modifiers)
                onDoubleTapped: (nodeId, edgeId, x, y) => root.activate(nodeId, edgeId, x, y)
                onContextMenuRequested: (nodeId, edgeId, x, y) => root.showMenu(nodeId, edgeId, x, y)
                onCardScrollRequested: (nodeId, dy) => root.scrollCard(nodeId, dy)
                onEditRequested: (nodeId) => textEditor.open(nodeId)
            }

            // The canvas's world: cards and edges in canvas units, scaled and moved by the board.
            Item {
                id: world
                objectName: "canvasWorld"
                visible: root.status === ""
                x: board.panX
                y: board.panY
                scale: board.zoom
                transformOrigin: Item.TopLeft
                width: 1
                height: 1

                Repeater {
                    id: edgeRepeater
                    model: board.edges
                    delegate: CanvasEdge { zoom: board.zoom }
                }
                Repeater {
                    model: board.edges
                    delegate: CanvasEdgeLabel { zoom: board.zoom }
                }
                Repeater {
                    id: cardRepeater
                    objectName: "canvasCards"
                    model: board.nodes
                    delegate: CanvasCard {
                        board: board
                        cards: cards
                        editing: board.editingNode === nodeId
                    }
                }

                // An edge being dragged out of a card.
                Shape {
                    z: 5
                    visible: board.connecting
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        strokeColor: Theme.accent
                        strokeWidth: Math.max(2.5, 1.5 / Math.max(0.01, board.zoom))
                        fillColor: "transparent"
                        strokeStyle: ShapePath.DashLine
                        dashPattern: [4, 3]
                        PathSvg { path: board.connectPath }
                    }
                    ShapePath {
                        strokeColor: "transparent"
                        strokeWidth: 0
                        fillColor: Theme.accent
                        PathSvg { path: board.connectArrow }
                    }
                }

                // The side handles of the card under the pointer (and the selected one): drag one
                // to connect.
                component SideHandle: Rectangle {
                    required property real cx
                    required property real cy
                    readonly property real r: (Theme.touchUi ? 9 : 6) / Math.max(0.01, board.zoom)
                    x: cx - r
                    y: cy - r
                    width: 2 * r
                    height: 2 * r
                    radius: r
                    color: Theme.bgElevated
                    border.width: 1.5 / Math.max(0.01, board.zoom)
                    border.color: Theme.accent
                    z: 6
                }
                Item {
                    id: hoverHandles
                    z: 6
                    readonly property rect r: board.singleRect.width > 0 && !Theme.touchUi ? board.hoverRect.width > 0 ? board.hoverRect : board.singleRect
                                                                                   : board.singleRect.width > 0 ? board.singleRect : board.hoverRect
                    visible: r.width > 0 && !board.busy && board.editingNode === ""
                    SideHandle { cx: hoverHandles.r.x + hoverHandles.r.width / 2; cy: hoverHandles.r.y }
                    SideHandle { cx: hoverHandles.r.x + hoverHandles.r.width; cy: hoverHandles.r.y + hoverHandles.r.height / 2 }
                    SideHandle { cx: hoverHandles.r.x + hoverHandles.r.width / 2; cy: hoverHandles.r.y + hoverHandles.r.height }
                    SideHandle { cx: hoverHandles.r.x; cy: hoverHandles.r.y + hoverHandles.r.height / 2 }
                }
                // The one selected card's corners (resize).
                component CornerHandle: Rectangle {
                    required property real cx
                    required property real cy
                    readonly property real r: (Theme.touchUi ? 7 : 4.5) / Math.max(0.01, board.zoom)
                    x: cx - r
                    y: cy - r
                    width: 2 * r
                    height: 2 * r
                    radius: r / 3
                    color: Theme.accent
                    z: 6
                }
                Item {
                    id: corners
                    z: 6
                    readonly property rect r: board.singleRect
                    visible: r.width > 0 && !board.busy
                    CornerHandle { cx: corners.r.x; cy: corners.r.y }
                    CornerHandle { cx: corners.r.x + corners.r.width; cy: corners.r.y }
                    CornerHandle { cx: corners.r.x; cy: corners.r.y + corners.r.height }
                    CornerHandle { cx: corners.r.x + corners.r.width; cy: corners.r.y + corners.r.height }
                }

                CanvasTextEditor {
                    id: textEditor
                    board: board
                    cards: cards
                    onClosed: root.forceActiveFocus()
                }
            }

            // The rubber band.
            Rectangle {
                readonly property rect band: board.rubberBand
                visible: band.width > 0 || band.height > 0
                x: band.x
                y: band.y
                width: band.width
                height: band.height
                color: Theme.accent.alpha(0.08)
                border.width: 1
                border.color: Theme.accent
            }

            CanvasSelectionBar {
                id: selectionBar
                objectName: "canvasSelectionBar"
                board: board
                onEditRequested: {
                    if (board.selectedNodes.length === 1)
                        root.activate(board.selectedNodes[0], "", 0, 0)
                }
                onOpenRequested: (newPane) => {
                    if (board.selectedNodes.length === 1)
                        root.openNode(board.selectedNodes[0], newPane)
                }
                onLabelRequested: {
                    if (board.selectedEdges.length === 1)
                        root.activate("", board.selectedEdges[0], 0, 0)
                }
            }

            // An empty canvas says what to do.
            BEmptyState {
                visible: root.status === "" && board.nodeCount === 0 && !board.busy && !textEditor.visible
                anchors.centerIn: parent
                iconName: "layout-dashboard"
                title: qsTr("An empty canvas")
                description: Theme.touchUi
                             ? qsTr("Double-tap to add a card, or add notes, files and web addresses from the tool bar. Drag from a card's side to connect it.")
                             : qsTr("Double-click to add a card, or add notes, files and web addresses from the tool bar, drop files or paste. Drag from a card's side to connect it.")
                actionText: qsTr("Add a card")
                actionIcon: "sticky-note"
                onActionTriggered: root.addText()
            }

            // Files dropped from another app: imported, added where they land.
            DropArea {
                id: fileDrop
                anchors.fill: parent
                enabled: root.status === ""
                keys: ["text/uri-list"]
                onEntered: (drag) => drag.accepted = drag.hasUrls
                onDropped: (drop) => {
                    if (!drop.hasUrls)
                        return
                    root.addAt = board.toCanvas(drop.x, drop.y)
                    board.addDroppedFiles(drop.urls, root.addAt.x, root.addAt.y)
                    root.forceActiveFocus()
                    drop.acceptProposedAction()
                }
            }

            // A file that is no canvas (or none).
            Column {
                visible: root.status !== "" && root.status !== "loading"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space4, 520)
                spacing: Theme.space4
                BIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: root.status === "missing" ? "circle-alert" : "layout-dashboard"
                    size: Theme.iconSizeLg
                    color: Theme.fgMuted
                }
                BText {
                    objectName: "canvasNoticeText"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    variant: "muted"
                    text: {
                        switch (root.status) {
                        case "corrupt": return qsTr("This file cannot be read as a canvas (JSON Canvas). Brulion shows it as it is and never writes it.")
                        case "tooLarge": return qsTr("This canvas is too large for Brulion to open.")
                        case "missing": return qsTr("The canvas %1 is not in the notebook.").arg(root.fileName !== "" ? root.fileName : root.routePath)
                        }
                        return ""
                    }
                }
                BButton {
                    visible: root.status === "corrupt"
                    anchors.horizontalCenter: parent.horizontalCenter
                    iconName: "file-text"
                    text: qsTr("Open as text")
                    onClicked: AppController.navigate({ view: "page", path: root.loadedPath, asFile: true })
                }
            }
        }

        CanvasToolbar {
            id: bottomBar
            objectName: "canvasToolbarBottom"
            Layout.fillWidth: true
            visible: Theme.narrow && root.status === ""
            compact: true
            board: board
            onAddText: root.addText()
            onAddNote: root.addNote()
            onAddFile: root.addFile()
            onAddLink: root.addLink()
            onAddGroup: root.addGroup()
            onPasteRequested: root.paste()
            onExportRequested: (format) => root.exportPicture(format)
        }
    }

    // ---- files dropped and picked --------------------------------------------------------------------

    FilePicker {
        id: filePicker
        title: qsTr("Add a file to the canvas")
        fileMode: FilePicker.OpenFile
        dark: Theme.dark
        colors: Theme.colors
        onAccepted: {
            const path = "" + Workspace.importAttachment(filePicker.selectedFile)
            if (path === "")
                AppController.toast(qsTr("The file could not be added to the notebook."))
            else
                board.addFile(path, "", root.addAt.x, root.addAt.y)
            root.forceActiveFocus()
        }
    }

    BQuickSwitcher {
        id: notePicker
        objectName: "canvasNotePicker"
        placeholderText: qsTr("Add a note to the canvas…")
        pages: notePicker.visible && Workspace.namesRevision >= 0 ? Workspace.switcherPages(notePicker.query, 60) : []
        onPagePathChosen: (path) => {
            if (path !== "")
                board.addFile(path, "", root.addAt.x, root.addAt.y)
            root.forceActiveFocus()
        }
        onCreateRequested: (name) => {
            const path = Workspace.createPage(name)
            if (path !== "")
                board.addFile(path, "", root.addAt.x, root.addAt.y)
            root.forceActiveFocus()
        }
    }

    CanvasTextDialog {
        id: textDialog
        objectName: "canvasTextDialog"
        onChosen: (purpose, target, text) => root.textChosen(purpose, target, text)
    }

    BMenu {
        id: contextMenu
        objectName: "canvasContextMenu"
        readonly property bool onItem: root.menuNode !== "" || root.menuEdge !== ""
        BMenuItem {
            visible: !contextMenu.onItem
            height: visible ? implicitHeight : 0
            text: qsTr("Add a card here")
            iconName: "sticky-note"
            onTriggered: root.addTextAt(root.addAt.x, root.addAt.y)
        }
        BMenuItem {
            visible: !contextMenu.onItem
            height: visible ? implicitHeight : 0
            text: qsTr("Add a note here…")
            iconName: "file-text"
            onTriggered: root.addNoteAt(root.addAt.x, root.addAt.y)
        }
        BMenuItem {
            visible: root.menuNode !== "" && board.selectionKind === "text" && board.selectedNodes.length === 1
            height: visible ? implicitHeight : 0
            text: qsTr("Edit text")
            iconName: "pencil"
            onTriggered: root.activate(root.menuNode, "", 0, 0)
        }
        BMenuItem {
            visible: root.menuNode !== "" && (board.selectionKind === "file" || board.selectionKind === "link") && board.selectedNodes.length === 1
            height: visible ? implicitHeight : 0
            text: qsTr("Open")
            iconName: "external-link"
            onTriggered: root.openNode(root.menuNode, false)
        }
        BMenuItem {
            visible: root.menuNode !== "" && board.selectionKind === "file" && board.selectedNodes.length === 1 && OpenIn.tabs
            height: visible ? implicitHeight : 0
            text: qsTr("Open in a new tab")
            onTriggered: root.openNode(root.menuNode, true)
        }
        BMenuItem {
            visible: root.menuNode !== "" && board.selectionKind === "link" && board.selectedNodes.length === 1
            height: visible ? implicitHeight : 0
            text: qsTr("Change the address…")
            iconName: "globe"
            onTriggered: {
                root.held.map = board.nodeData(root.menuNode)
                textDialog.ask("url", root.menuNode, qsTr("Web address"), UiModels.stringField(root.held.map, "url"), "https://…")
            }
        }
        BMenuItem {
            visible: root.menuEdge !== "" && root.menuNode === ""
            height: visible ? implicitHeight : 0
            text: qsTr("Label…")
            iconName: "type"
            onTriggered: root.activate("", root.menuEdge, 0, 0)
        }
        BMenuItem {
            visible: contextMenu.onItem
            height: visible ? implicitHeight : 0
            text: qsTr("Copy")
            iconName: "copy"
            shortcut: "Ctrl+C"
            onTriggered: board.copySelection()
        }
        BMenuItem {
            visible: contextMenu.onItem
            height: visible ? implicitHeight : 0
            text: qsTr("Cut")
            iconName: "scissors"
            shortcut: "Ctrl+X"
            onTriggered: board.cutSelection()
        }
        BMenuItem {
            text: qsTr("Paste")
            iconName: "clipboard-paste"
            shortcut: "Ctrl+V"
            onTriggered: root.paste()
        }
        BMenuItem {
            visible: root.menuNode !== ""
            height: visible ? implicitHeight : 0
            text: qsTr("Duplicate")
            iconName: "copy-plus"
            shortcut: "Ctrl+D"
            onTriggered: board.duplicateSelection()
        }
        BMenuItem {
            visible: root.menuNode !== ""
            height: visible ? implicitHeight : 0
            text: board.selectionKind === "group" ? qsTr("Ungroup") : qsTr("Group")
            iconName: board.selectionKind === "group" ? "ungroup" : "group"
            onTriggered: {
                if (board.selectionKind === "group")
                    board.ungroupSelection()
                else
                    root.addGroup()
            }
        }
        BMenuItem {
            visible: root.menuNode !== ""
            height: visible ? implicitHeight : 0
            text: qsTr("Bring to front")
            iconName: "bring-to-front"
            onTriggered: board.reorderSelection("front")
        }
        BMenuItem {
            visible: root.menuNode !== ""
            height: visible ? implicitHeight : 0
            text: qsTr("Send to back")
            iconName: "send-to-back"
            onTriggered: board.reorderSelection("back")
        }
        BMenuItem {
            visible: !contextMenu.onItem
            height: visible ? implicitHeight : 0
            text: qsTr("Select all")
            iconName: "text-select"
            shortcut: "Ctrl+A"
            onTriggered: board.selectAll()
        }
        BMenuItem {
            visible: !contextMenu.onItem
            height: visible ? implicitHeight : 0
            text: qsTr("Show the whole canvas")
            iconName: "maximize"
            shortcut: "Shift+1"
            onTriggered: board.zoomToFit()
        }
        BMenuSeparator { visible: contextMenu.onItem; height: visible ? implicitHeight : 0 }
        BMenuItem {
            visible: contextMenu.onItem
            height: visible ? implicitHeight : 0
            text: qsTr("Delete")
            iconName: "trash-2"
            danger: true
            shortcut: "Del"
            onTriggered: board.deleteSelection()
        }
    }
}
