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
//    the eraser, the selection (a lasso or a tap; DrawingSelectionBar over the selection), shapes
//    (rectangle, ellipse, diamond, line, arrow), text (DrawingTextEditor writes it). Keys (none
//    while a text is written): Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y, P / H / E (pen, highlighter,
//    eraser), V (select), R / O / D / L / A (rectangle, ellipse, diamond, line, arrow), T (text),
//    Delete / Backspace (the selection), Ctrl+D (duplicate), Ctrl+A (select all), Escape (a shape
//    or a lasso in progress dropped, else the selection cleared), the arrows (move the selection
//    1, with Shift 10), Ctrl+] / Ctrl+[ (one step up / down), Ctrl+Shift+] / Ctrl+Shift+[ (to the
//    front / back), Space held (move the paper), Ctrl+0 (the whole drawing), Ctrl++ / Ctrl+-
//    (zoom).
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
//    it is) and puts it — a PDF's page chosen in DrawingBackdropDialog — under the drawing; "Inna
//    strona PDF…" chooses another page of the same PDF, "Usuń podkład" removes it. Undo takes each
//    back.
// route.open (screenshots): "style" (the colours and widths) and "more" on phones, "paper" (the
// paper's menu) on wide windows, "theme" (the background's menu; on phones "Więcej"), "shape"
// (the shapes' menu), "select" (everything selected: the selection bar), "backdrop" (the page
// picker of the backdrop's PDF; a picture's backdrop: the paper's menu).
// View contract (ShellRouteView): `title` (the file's name), `editing` (no capture button over the
// paper), `toolbarDocked` and `replacesBottomNav` (phones: the tool bar at the bottom — the
// bottom navigation makes way; no keyboard involved, unlike an editor's toolbar),
// `edgeSwipe` (no drawer edge strip: a stroke from the left edge stays a stroke).
FocusScope {
    id: root

    property var route: ({})
    property bool inSidePanel: false

    focus: true

    readonly property string routePath: route && route.path ? route.path : ""
    readonly property string fileName: {
        const i = root.loadedPath.lastIndexOf("/")
        return i < 0 ? root.loadedPath : root.loadedPath.slice(i + 1)
    }
    /// The drawing file's extension as written (".svg", ".excalidraw", ".excalidraw.md"; the title
    /// goes without it, a kept copy gets it).
    readonly property string extension: {
        const m = /(\.excalidraw\.md|\.excalidraw|\.svg|\.md)$/i.exec(root.fileName)
        return m ? m[1] : ".svg"
    }
    readonly property string title: root.fileName.endsWith(root.extension)
                                    ? root.fileName.slice(0, root.fileName.length - root.extension.length)
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
    /// The file the paper shows (follows renames; "" none).
    property string loadedPath: ""
    property bool _writing: false

    // The tools, remembered on the device.
    property string penColor: AppSettings.value("drawing/penColor", "#1f2430")
    property real penWidth: Number(AppSettings.value("drawing/penWidth", 1)) || 1
    property string highlighterColor: AppSettings.value("drawing/highlighterColor", "#ffd43b")
    property real highlighterWidth: Number(AppSettings.value("drawing/highlighterWidth", 5)) || 5
    /// The shape tool the shape button stands for.
    property string shapeTool: AppSettings.value("drawing/shape", "rectangle")
    property real fontSize: Number(AppSettings.value("drawing/fontSize", 20)) || 20
    /// "auto" (a finger draws until a stylus has been seen), "on", "off".
    property string fingerMode: AppSettings.value("drawing/finger", "auto")
    property bool stylusKnown: flag(AppSettings.value("drawing/stylusSeen", false))
    readonly property bool fingerDraws: fingerMode === "on" || (fingerMode === "auto" && !stylusKnown)

    readonly property var penColors: [
        { color: "#1f2430", label: qsTr("Ink") },
        { color: "#2f6fdd", label: qsTr("Blue") },
        { color: "#e03131", label: qsTr("Red") },
        { color: "#2f9e44", label: qsTr("Green") },
        { color: "#f08c00", label: qsTr("Orange") },
        { color: "#7048e8", label: qsTr("Violet") }
    ]
    readonly property var highlighterColors: [
        { color: "#ffd43b", label: qsTr("Yellow") },
        { color: "#8ce99a", label: qsTr("Green") },
        { color: "#faa2c1", label: qsTr("Pink") },
        { color: "#74c0fc", label: qsTr("Blue") },
        { color: "#ffa94d", label: qsTr("Orange") }
    ]
    readonly property var penWidths: [
        { value: 0.5, label: qsTr("Fine") },
        { value: 1, label: qsTr("Medium") },
        { value: 2, label: qsTr("Thick") }
    ]
    readonly property var highlighterWidths: [
        { value: 3, label: qsTr("Narrow") },
        { value: 5, label: qsTr("Medium") },
        { value: 8, label: qsTr("Wide") }
    ]
    /// Excalidraw's text sizes.
    readonly property var fontSizes: [
        { value: 16, label: qsTr("Small") },
        { value: 20, label: qsTr("Medium", "text size") },
        { value: 28, label: qsTr("Large") },
        { value: 36, label: qsTr("Extra large") }
    ]

    function flag(value) {
        return value === true || value === "true"
    }

    /// Write the drawing when it changed (true: nothing to write, or written); a text being written
    /// is committed first.
    function save() {
        saveTimer.stop()
        if (textEditor)
            textEditor.commit() // (a text being written goes in first)
        if (root.loadedPath === "" || root.status !== "" || !canvas.modified)
            return true
        // What the file holds now, on disk: another program may have written it a moment ago,
        // before its change was reported (a folder watch that missed it, Android's shared
        // storage) — taken in first, never written over.
        if (Workspace.exists(root.loadedPath)) {
            const disk = Workspace.readAttachment(root.loadedPath)
            if (disk.byteLength > 0 && !canvas.isKnownFile(disk) && !root.takeIn(disk))
                return true // shown as it is now; what was drawn here kept in a copy beside it
        }
        const bytes = canvas.fileBytes()
        // The vault reports the write at once (Workspace.fileChanged, below): that is this save,
        // not another device's change to merge.
        root._writing = true
        const written = Workspace.writeAttachment(root.loadedPath, bytes)
        root._writing = false
        if (!written)
            return false // (Workspace.errorOccurred: the shell's toast says why)
        canvas.markSaved(bytes)
        return true
    }

    /// Another program's version of the open, editable file: taken in — merged with what was
    /// drawn here (DrawingCanvas.takeExternal) — or, when it is no drawing Brulion can edit any
    /// more (a frame or an embedded page added in excalidraw.com), shown as it is, with what was
    /// drawn here and not yet saved kept in a copy beside it. True when the paper still edits it.
    function takeIn(bytes) {
        const merged = canvas.modified
        if (canvas.takeExternal(bytes)) {
            if (merged) {
                AppController.toast(qsTr("The drawing was changed on another device: both versions are kept in it"))
                saveTimer.restart()
            }
            return true
        }
        saveTimer.stop()
        if (canvas.modified)
            root.keepCopy()
        root.status = canvas.load(bytes)
        return false
    }

    /// What was drawn here, written beside the drawing as a new file in its format ("Rysunek (to
    /// urządzenie).svg", "Szkic (to urządzenie).excalidraw.md").
    function keepCopy() {
        const slash = root.loadedPath.lastIndexOf("/")
        const folder = slash < 0 ? "" : root.loadedPath.slice(0, slash + 1)
        const path = Workspace.createAttachment(folder + qsTr("%1 (this device)").arg(root.title) + root.extension,
                                                canvas.fileBytes())
        if (path !== "")
            AppController.toast(qsTr("The drawing was changed elsewhere into one Brulion cannot edit. What you drew here is kept in %1").arg(path.slice(path.lastIndexOf("/") + 1)))
    }

    function load(path) {
        root.loadedPath = path
        if (!Workspace.exists(path)) {
            canvas.load(new ArrayBuffer(0))
            root.status = "missing"
            return
        }
        root.status = canvas.load(Workspace.readAttachment(path))
        root.forceActiveFocus()
    }

    function applyRoute() {
        if (root.routePath === "" || root.routePath === root.loadedPath)
            return
        root.save()
        root.load(root.routePath)
        // route.open (screenshots): "style" | "more" (phones), "paper" (wide windows), "theme".
        const open = root.route && root.route.open ? root.route.open : ""
        if (open !== "" && root.status === "")
            Qt.callLater(() => {
                const bar = Theme.narrow ? bottomBar : topBar
                if (open === "style")
                    bar.openStyle()
                else if (open === "more")
                    bar.openMore()
                else if (open === "paper")
                    bar.openBackground()
                else if (open === "theme")
                    bar.openTheme()
                else if (open === "shape")
                    bar.openShapes()
                else if (open === "select") {
                    canvas.tool = "select"
                    canvas.selectAll()
                } else if (open === "backdrop") {
                    if (canvas.backdropPage > 0 && canvas.backdropPath !== "" && DrawingBackdrops.pdfAvailable)
                        root.otherBackdropPage()
                    else if (Theme.narrow)
                        bar.openMore()
                    else
                        bar.openBackground()
                }
            })
    }

    function applyTool() {
        // (Shapes and text in the pen's colour; shapes twice the pen's width.)
        canvas.color = canvas.tool === "highlighter" ? root.highlighterColor : root.penColor
        canvas.penWidth = canvas.tool === "highlighter" ? root.highlighterWidth : root.penWidth
        canvas.fontSize = root.fontSize
    }

    function chooseColor(color) {
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

    function chooseFontSize(size) {
        root.fontSize = size
        AppSettings.setValue("drawing/fontSize", size)
        root.applyTool()
    }

    function chooseShape(tool) {
        root.shapeTool = tool
        AppSettings.setValue("drawing/shape", tool)
    }

    function useShape(tool) {
        canvas.tool = tool
        root.chooseShape(tool)
    }

    function chooseWidth(width) {
        if (canvas.tool === "highlighter") {
            root.highlighterWidth = width
            AppSettings.setValue("drawing/highlighterWidth", width)
        } else {
            root.penWidth = width
            AppSettings.setValue("drawing/penWidth", width)
        }
        root.applyTool()
    }

    function toggleFinger() {
        root.fingerMode = root.fingerDraws ? "off" : "on"
        AppSettings.setValue("drawing/finger", root.fingerMode)
    }

    /// "Podkład z obrazu lub PDF…": a picture or a PDF picked and imported into the notebook.
    function pickBackdrop() {
        if (root.status !== "")
            return
        const pick = DrawingBackdrops.pickInBrowser()
        if (pick > 0)
            root.backdropPick = pick // (the browser's picker: picked() below)
        else
            backdropPicker.open()
    }
    /// The backdrop's file (a vault path): a PDF's page is chosen, a picture prepared at once.
    function useBackdropFile(path) {
        if (path === "")
            return
        if (DrawingBackdrops.isPdf(path))
            root.openPageDialog(path, 1)
        else
            root.backdropJob = DrawingBackdrops.prepare(path, [])
    }
    function openPageDialog(path, page) {
        if (!root.backdropDialog)
            root.backdropDialog = backdropDialogComponent.createObject(root) as DrawingBackdropDialog
        root.backdropDialog.openFor(path, "single", page)
    }
    /// "Inna strona PDF…": the backdrop's PDF again (renames do not follow it into the drawing).
    function otherBackdropPage() {
        const path = canvas.backdropPath
        if (path === "" || !Workspace.exists(path)) {
            AppController.toast(qsTr("The file %1 is not in the notebook any more.").arg(path.slice(path.lastIndexOf("/") + 1)))
            return
        }
        root.openPageDialog(path, canvas.backdropPage)
    }
    function applyBackdrop(items) {
        const item = items.length > 0 ? items[0] : null
        if (!item)
            return
        if (item.error !== "")
            AppController.toast(DrawingBackdrops.errorText(item.error))
        else
            canvas.setBackdrop(item)
        root.forceActiveFocus()
    }

    /// Gotowe: written, back to where the drawing was opened from.
    function done() {
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
    // Text on drawings in the app's fonts (the previews' too).
    readonly property string _fonts: Theme.fontUi + "\n" + Theme.fontMono
    on_FontsChanged: DrawingFonts.set(Theme.fontUi, Theme.fontMono)
    Component.onCompleted: {
        DrawingFonts.set(Theme.fontUi, Theme.fontMono)
        applyRoute()
    }
    Component.onDestruction: {
        alive = false
        save()
    }

    Timer {
        id: saveTimer
        interval: 1200
        // (Not while a text is written: the editor's close restarts it.)
        onTriggered: if (!canvas.textEditing) root.save()
    }

    Connections {
        target: Workspace
        function onPathRenamed(oldPath, newPath) {
            if (oldPath === root.loadedPath)
                root.loadedPath = newPath
        }
        function onPathRemoved(path) {
            if (path !== root.loadedPath)
                return
            saveTimer.stop()
            root.loadedPath = ""
            root.status = "missing"
        }
        function onFileChanged(path) {
            if (path !== root.loadedPath || root.status === "missing" || root._writing)
                return
            const bytes = Workspace.readAttachment(path)
            if (bytes.byteLength === 0 || canvas.isKnownFile(bytes))
                return // its own save, or a file being written
            if (root.status === "")
                root.takeIn(bytes)
            else
                root.status = canvas.load(bytes)
        }
    }
    Connections {
        target: DrawingBackdrops
        function onPicked(pick, path) {
            if (pick !== root.backdropPick)
                return
            root.backdropPick = 0
            root.useBackdropFile(path)
        }
        function onPickFailed(pick, name) {
            if (pick !== root.backdropPick)
                return
            root.backdropPick = 0
            AppController.toast(DrawingBackdrops.errorText("unreadable"))
        }
        function onFinished(job, items, cancelled) {
            if (job !== root.backdropJob)
                return
            root.backdropJob = 0
            if (!cancelled)
                root.applyBackdrop(items)
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
            const path = Workspace.importAttachment(selectedFile)
            if (path === "")
                AppController.toast(DrawingBackdrops.errorText("unreadable"))
            root.useBackdropFile(path)
        }
    }
    Component {
        id: backdropDialogComponent
        DrawingBackdropDialog {
            onPrepared: (items) => root.applyBackdrop(items)
            onRejected: root.forceActiveFocus()
        }
    }

    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state !== Qt.ApplicationActive)
                root.save()
        }
    }

    Keys.onPressed: (event) => {
        if (canvas.textEditing)
            return // (the text editor has the keys)
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        const plain = event.modifiers === Qt.NoModifier
        const arrows = { [Qt.Key_Left]: [-1, 0], [Qt.Key_Right]: [1, 0], [Qt.Key_Up]: [0, -1], [Qt.Key_Down]: [0, 1] }
        if (event.matches(StandardKey.Undo)) {
            canvas.undo()
        } else if (event.matches(StandardKey.Redo) || (ctrl && event.key === Qt.Key_Y)) {
            canvas.redo()
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
        } else if (ctrl && !shift && event.key === Qt.Key_D) {
            canvas.duplicateSelection()
        } else if (event.matches(StandardKey.SelectAll)) {
            canvas.tool = "select"
            canvas.selectAll()
        } else if (event.key === Qt.Key_Escape && (canvas.busy || canvas.selectionCount > 0)) {
            if (!canvas.cancelGesture())
                canvas.clearSelection()
        } else if (arrows[event.key] !== undefined && canvas.selectionCount > 0 && !ctrl) {
            const step = shift ? 10 : 1
            canvas.nudgeSelection(arrows[event.key][0] * step, arrows[event.key][1] * step)
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
            onFingerToggled: root.toggleFinger()
            onFitRequested: canvas.fitAll()
            onDone: root.done()
            onBackdropRequested: root.pickBackdrop()
            onBackdropPageRequested: root.otherBackdropPage()
            onBackdropRemoved: canvas.removeBackdrop()
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
                onToolChanged: root.applyTool()
                onContentChanged: if (canvas.modified) saveTimer.restart()
                onTextEditingChanged: if (!canvas.textEditing && canvas.modified) saveTimer.restart()
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
                                return qsTr("The drawing %1 is not in the notebook.").arg(root.fileName || root.routePath)
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
                        readonly property size natural: root.loadedPath !== "" && notice.visible
                                                        ? Workspace.pictureSize(root.loadedPath) : Qt.size(0, 0)
                        // (Also while the app's other theme loads: the shown picture stays.)
                        visible: root.status !== "missing"
                                 && (status === Image.Ready || (status === Image.Loading && implicitWidth > 0))
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
                color: canvas.dark ? "#9ba1af" : "#535a6a"
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
            onFingerToggled: root.toggleFinger()
            onFitRequested: canvas.fitAll()
            onDone: root.done()
            onBackdropRequested: root.pickBackdrop()
            onBackdropPageRequested: root.otherBackdropPage()
            onBackdropRemoved: canvas.removeBackdrop()
        }
    }
}
