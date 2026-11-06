pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import Brulion

// DrawingTextEditor — writing a text or a shape's label on a drawing (DrawingView; docs/UI.md
// "Drawing editor"): opened by the canvas (DrawingCanvas.textEditRequested: the text tool's tap, a
// double tap on a text or a shape with the select tool) where the text is, in its font, size
// (× zoom), colour (as the paper shows it) and alignment, as wide as the text — or its shape's
// inside, wrapped. Enter: a new line; Escape, Ctrl+Enter, a press on the paper beside it
// (textCommitRequested) or "Done" (touch screens): written (DrawingCanvas.commitText; emptied: the
// text deleted) — one undo step. On a phone the view moves so the text stays above the keyboard.
// Its parent is the paper's area; the editor takes the keyboard's focus while it is open and gives
// it back (closed()).
Item {
    id: root

    required property DrawingCanvas canvas
    /// A request is being edited (DrawingCanvas.textEditRequested: its map is canvas.textRequest).
    property bool active: false
    readonly property alias editor: edit

    /// Written or dropped: the view takes the focus back.
    signal closed()

    visible: active

    /// The text's top left (a label: its shape's left edge at its middle) on the paper, following
    /// the view.
    readonly property point anchor: !root.active ? Qt.point(0, 0)
        : Qt.point((root.canvas.textRequest.sceneX - root.canvas.centerX) * root.canvas.zoom + root.canvas.width / 2,
                   (root.canvas.textRequest.sceneY - root.canvas.centerY) * root.canvas.zoom + root.canvas.height / 2)
    readonly property real fixedWidth: root.active && root.canvas.textRequest.width > 0
                                       ? root.canvas.textRequest.width * root.canvas.zoom : 0
    /// The request's text colour (as the paper shows it), alignments and font family.
    readonly property string requestColor: root.active ? root.requestText("color") : ""
    readonly property string requestAlign: root.active ? root.requestText("align") : "left"
    readonly property string requestValign: root.active ? root.requestText("valign") : ""
    readonly property string requestFont: root.active ? root.requestText("fontFamily") : Theme.fontUi
    function requestText(key: string): string {
        return AppController.routeText(root.canvas.textRequest, key)
    }
    /// How much of the paper the on-screen keyboard covers (updateKeyboardCover(): the keyboard,
    /// the window's and the paper's height changing).
    property real keyboardCover: 0
    property point _canvasBottom: Qt.point(0, 0)
    function updateKeyboardCover(): void {
        if (KeyboardInsets.overlap <= 0 || !root.Window.window) {
            root.keyboardCover = 0
            return
        }
        root._canvasBottom = root.canvas.mapToItem(null, 0, root.canvas.height)
        root.keyboardCover = Math.max(0, root._canvasBottom.y - (root.Window.height - KeyboardInsets.overlap))
    }
    Connections {
        target: KeyboardInsets
        function onOverlapChanged() { root.updateKeyboardCover() }
    }
    Connections {
        target: root.Window.window
        function onHeightChanged() { root.updateKeyboardCover() }
    }
    onWindowChanged: root.updateKeyboardCover()
    Component.onCompleted: root.updateKeyboardCover()
    // Deferred like Qt.callLater (repeated calls merged).
    Later {
        id: keepLater
        onTriggered: root.keepInView()
    }

    function open(): void {
        root.active = true
        edit.text = root.requestText("text")
        edit.cursorPosition = edit.length
        edit.forceActiveFocus()
        keepLater.restart()
    }
    function commit(): void {
        if (!root.active)
            return
        const text = edit.text + edit.preeditText
        root.release()
        root.canvas.commitText(root.canvas.textRequest, text)
        root.closed()
    }
    function cancel(): void {
        if (!root.active)
            return
        root.release()
        root.canvas.cancelText()
        root.closed()
    }
    /// Closed: the hidden editor keeps no focus (its scope would hand the keys back to it).
    function release(): void {
        root.active = false
        edit.focus = false
    }
    /// The text on screen above the keyboard.
    function keepInView(): void {
        if (!root.active)
            return
        const z = root.canvas.zoom
        root.canvas.ensureVisible(Qt.rect(root.canvas.textRequest.sceneX - 4 / z,
                                          (edit.y - 4 - root.anchor.y) / z + root.canvas.textRequest.sceneY,
                                          (edit.width + 8 + (doneButton.visible ? doneButton.width : 0)) / z,
                                          (edit.height + 8) / z),
                                  root.keyboardCover)
    }

    onKeyboardCoverChanged: keepLater.restart()

    Connections {
        target: root.canvas
        function onTextEditRequested(request: var) { root.open() }
        function onTextCommitRequested() { root.commit() }
        // The paper shrinking under the text (a browser's keyboard resizes the page: it reports
        // no overlap then) moves the view as the keyboard's cover does.
        function onHeightChanged() {
            root.updateKeyboardCover()
            if (root.active)
                keepLater.restart()
        }
        function onTextEditingChanged() {
            // The canvas ended it (another drawing loaded): nothing to write.
            if (!root.canvas.textEditing && root.active) {
                root.release()
                root.closed()
            }
        }
    }

    Rectangle {
        // Where the text is written: a thin frame in the selection's colour.
        x: edit.x - 4
        y: edit.y - 4
        width: edit.width + 8
        height: edit.height + 8
        radius: Theme.radius
        color: "transparent"
        border.width: 1
        border.color: root.canvas.selectionColor
    }

    TextEdit {
        id: edit
        objectName: "drawingTextEditor"
        readonly property string align: root.requestAlign
        x: root.fixedWidth > 0 ? root.anchor.x
           : align === "center" ? root.anchor.x - width / 2 : align === "right" ? root.anchor.x - width : root.anchor.x
        y: root.active && root.requestValign === "middle" ? root.anchor.y - height / 2 : root.anchor.y
        width: root.fixedWidth > 0 ? root.fixedWidth : Math.max(implicitWidth, font.pixelSize / 2)
        textFormat: TextEdit.PlainText
        wrapMode: root.fixedWidth > 0 ? TextEdit.Wrap : TextEdit.NoWrap
        font.family: root.requestFont
        font.pixelSize: root.active ? Math.max(1, Math.round(root.canvas.textRequest.fontSize * root.canvas.zoom)) : 20
        color: root.active ? root.requestColor : Theme.fg
        selectionColor: Theme.selection
        selectedTextColor: Theme.fg
        horizontalAlignment: align === "center" ? TextEdit.AlignHCenter : align === "right" ? TextEdit.AlignRight
                                                                                         : TextEdit.AlignLeft
        selectByMouse: true
        inputMethodHints: Qt.ImhNone
        Accessible.name: qsTr("Text")
        Keys.onPressed: (event) => {
            const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
            if (event.key === Qt.Key_Escape || (ctrl && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter))) {
                root.commit()
                event.accepted = true
            }
        }
        onHeightChanged: if (root.active) keepLater.restart()
    }

    BIconButton {
        id: doneButton
        objectName: "drawingTextDone"
        visible: Theme.touchUi
        x: edit.x + edit.width + 8
        y: edit.y - 4
        size: "small"
        variant: "primary"
        iconName: "check"
        label: qsTr("Done")
        focusPolicy: Qt.NoFocus
        onClicked: root.commit()
    }
}
