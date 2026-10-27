pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Brulion

// DrawingView — route "drawing" ({path}): the drawing editor (docs/UI.md "Drawing editor") on a
// drawing attachment (docs/SYNTAX.md §2 "Drawings": an SVG with an embedded Excalidraw scene).
//
//  * The paper (DrawingCanvas: a stylus draws with its pressure, its eraser end erases; a mouse
//    draws; one finger draws or moves the paper — "Rysuj palcem", on by default until a stylus has
//    been seen on this device —, two fingers move and zoom) and the tool bar (DrawingToolbar:
//    above the paper, at the bottom on phones). Keys: Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y, P / H / E
//    (pen, highlighter, eraser), Space held (move the paper), Ctrl+0 (the whole drawing),
//    Ctrl++ / Ctrl+- (zoom).
//  * Saved by itself: 1.2 s after the last change, when the route changes, the view goes and the
//    app leaves the foreground (Workspace.writeAttachment). The tools' colours and widths, finger
//    drawing and whether a stylus was seen are remembered on the device (AppSettings
//    "drawing/…").
//  * A change of the file from elsewhere (Syncthing, Nextcloud) while it is open is taken in: as
//    it is when nothing changed here, else merged stroke by stroke (DrawingCanvas.takeExternal)
//    and saved; the canvas's own saves coming back are recognised (isKnownFile).
//  * A file Brulion cannot edit is shown as the picture it is, with why: an Excalidraw drawing
//    with shapes or text ("readonly"), a picture without a scene, a scene that cannot be read; a
//    missing file says so. Renames follow the file (the route applied one event-loop turn late, as
//    in PageView); a deleted file is never written again.
// View contract (ShellRouteView): `title` (the file's name), `editing` (no capture button over the
// paper), `toolbarDocked` (phones: the tool bar at the bottom — the bottom navigation makes way),
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
    readonly property string title: root.fileName.replace(/\.svg$/i, "")
    readonly property bool editing: true
    readonly property bool toolbarDocked: Theme.narrow && root.status === ""
    readonly property bool edgeSwipe: true
    readonly property alias canvas: canvas

    /// "" (editable), "loading", "readonly", "notDrawing", "corrupt", "missing".
    property string status: "loading"
    /// The file the paper shows (follows renames; "" none).
    property string loadedPath: ""

    // The tools, remembered on the device.
    property string penColor: AppSettings.value("drawing/penColor", "#1f2430")
    property real penWidth: Number(AppSettings.value("drawing/penWidth", 1)) || 1
    property string highlighterColor: AppSettings.value("drawing/highlighterColor", "#ffd43b")
    property real highlighterWidth: Number(AppSettings.value("drawing/highlighterWidth", 5)) || 5
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

    function flag(value) {
        return value === true || value === "true"
    }

    /// Write the drawing when it changed (true: nothing to write, or written).
    function save() {
        saveTimer.stop()
        if (root.loadedPath === "" || root.status !== "" || !canvas.modified)
            return true
        const bytes = canvas.svg()
        if (!Workspace.writeAttachment(root.loadedPath, bytes))
            return false // (Workspace.errorOccurred: the shell's toast says why)
        canvas.markSaved(bytes)
        return true
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
    }

    function applyTool() {
        canvas.color = canvas.tool === "highlighter" ? root.highlighterColor : root.penColor
        canvas.penWidth = canvas.tool === "highlighter" ? root.highlighterWidth : root.penWidth
    }

    function chooseColor(color) {
        if (canvas.tool === "highlighter") {
            root.highlighterColor = color
            AppSettings.setValue("drawing/highlighterColor", color)
        } else {
            root.penColor = color
            AppSettings.setValue("drawing/penColor", color)
        }
        if (canvas.tool === "eraser")
            canvas.tool = "pen"
        root.applyTool()
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
    Component.onCompleted: applyRoute()
    Component.onDestruction: {
        alive = false
        save()
    }

    Timer {
        id: saveTimer
        interval: 1200
        onTriggered: root.save()
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
            if (path !== root.loadedPath || root.status === "missing")
                return
            const bytes = Workspace.readAttachment(path)
            if (bytes.byteLength === 0 || canvas.isKnownFile(bytes))
                return // its own save, or a file being written
            if (root.status === "") {
                const merged = canvas.modified
                if (canvas.takeExternal(bytes)) {
                    if (merged) {
                        AppController.toast(qsTr("The drawing was changed on another device: both versions are kept in it"))
                        saveTimer.restart()
                    }
                    return
                }
                if (canvas.modified)
                    return // not a drawing any more: this one is written over it (History keeps it)
            }
            root.status = canvas.load(bytes)
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
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
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
            fingerDraws: root.fingerDraws
            onColorChosen: (color) => root.chooseColor(color)
            onWidthChosen: (width) => root.chooseWidth(width)
            onFingerToggled: root.toggleFinger()
            onFitRequested: canvas.fitAll()
            onDone: root.done()
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
                onToolChanged: root.applyTool()
                onContentChanged: if (canvas.modified) saveTimer.restart()
                onStylusSeenChanged: {
                    if (canvas.stylusSeen && !root.stylusKnown) {
                        root.stylusKnown = true
                        AppSettings.setValue("drawing/stylusSeen", true)
                    }
                }
                Component.onCompleted: root.applyTool()
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
                                return qsTr("This drawing comes from Excalidraw and has shapes, text or pictures: Brulion edits only handwriting. Open the file in excalidraw.com to change it.")
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
                        visible: root.status !== "missing" && status === Image.Ready
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(parent.width, natural.width > 0 ? natural.width : parent.width)
                        height: natural.width > 0 ? width * natural.height / natural.width : implicitHeight
                        fillMode: Image.PreserveAspectFit
                        sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio), 0)
                        source: notice.visible && root.status !== "missing" && root.loadedPath !== ""
                                ? Workspace.assetUrl(root.loadedPath) : ""
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
                color: "#535a6a" // on the paper (white in every theme)
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
            fingerDraws: root.fingerDraws
            onColorChosen: (color) => root.chooseColor(color)
            onWidthChosen: (width) => root.chooseWidth(width)
            onFingerToggled: root.toggleFinger()
            onFitRequested: canvas.fitAll()
            onDone: root.done()
        }
    }
}
