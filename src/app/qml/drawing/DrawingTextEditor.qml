pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import Brulion

// DrawingTextEditor — writing a text or a shape's label on a drawing (DrawingView; docs/UI.md
// "Drawing editor"): opened by the canvas (DrawingCanvas.textEditRequested: the text tool's tap, a
// double tap on a text or a shape with the select tool) where the text is, in its font, size
// (× zoom), colour (as the paper shows it) and alignment, as wide as the text — or its shape's
// inside, wrapped. Enter: a new line; Escape, Ctrl+Enter, a press on the paper beside it
// (textCommitRequested) or "Gotowe" (touch screens): written (DrawingCanvas.commitText; emptied: the
// text deleted) — one undo step. On a phone the view moves so the text stays above the keyboard.
// Its parent is the paper's area; the editor takes the keyboard's focus while it is open and gives
// it back (closed()).
Item {
    id: root

    required property DrawingCanvas canvas
    /// The request being edited (DrawingCanvas.textEditRequested), null when closed.
    property var request: null
    readonly property bool active: request !== null
    readonly property alias editor: edit

    /// Written or dropped: the view takes the focus back.
    signal closed()

    visible: active

    /// The text's top left (a label: its shape's left edge at its middle) on the paper, following
    /// the view.
    readonly property point anchor: !root.active ? Qt.point(0, 0)
        : Qt.point((root.request.sceneX - root.canvas.centerX) * root.canvas.zoom + root.canvas.width / 2,
                   (root.request.sceneY - root.canvas.centerY) * root.canvas.zoom + root.canvas.height / 2)
    readonly property real fixedWidth: active && request.width > 0 ? request.width * canvas.zoom : 0
    /// How much of the paper the on-screen keyboard covers.
    readonly property real keyboardCover: {
        if (KeyboardInsets.overlap <= 0 || !root.Window.window)
            return 0
        const bottom = root.canvas.mapToItem(null, 0, root.canvas.height).y
        return Math.max(0, bottom - (root.Window.height - KeyboardInsets.overlap))
    }

    function open(r) {
        root.request = r
        edit.text = r.text
        edit.cursorPosition = edit.length
        edit.forceActiveFocus()
        Qt.callLater(root.keepInView)
    }
    function commit() {
        if (!root.active)
            return
        const r = root.request
        const text = edit.text + edit.preeditText
        root.release()
        root.canvas.commitText(r, text)
        root.closed()
    }
    function cancel() {
        if (!root.active)
            return
        root.release()
        root.canvas.cancelText()
        root.closed()
    }
    /// Closed: the hidden editor keeps no focus (its scope would hand the keys back to it).
    function release() {
        root.request = null
        edit.focus = false
    }
    /// The text on screen above the keyboard.
    function keepInView() {
        if (!root.active)
            return
        const z = root.canvas.zoom
        root.canvas.ensureVisible(Qt.rect(root.request.sceneX - 4 / z, (edit.y - 4 - root.anchor.y) / z + root.request.sceneY,
                                          (edit.width + 8 + (doneButton.visible ? doneButton.width : 0)) / z,
                                          (edit.height + 8) / z),
                                  root.keyboardCover)
    }

    onKeyboardCoverChanged: Qt.callLater(root.keepInView)

    Connections {
        target: root.canvas
        function onTextEditRequested(r) { root.open(r) }
        function onTextCommitRequested() { root.commit() }
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
        readonly property string align: root.active ? root.request.align : "left"
        x: root.fixedWidth > 0 ? root.anchor.x
           : align === "center" ? root.anchor.x - width / 2 : align === "right" ? root.anchor.x - width : root.anchor.x
        y: root.active && root.request.valign === "middle" ? root.anchor.y - height / 2 : root.anchor.y
        width: root.fixedWidth > 0 ? root.fixedWidth : Math.max(implicitWidth, font.pixelSize / 2)
        textFormat: TextEdit.PlainText
        wrapMode: root.fixedWidth > 0 ? TextEdit.Wrap : TextEdit.NoWrap
        font.family: root.active ? root.request.fontFamily : Theme.fontUi
        font.pixelSize: root.active ? Math.max(1, Math.round(root.request.fontSize * root.canvas.zoom)) : 20
        color: root.active ? root.request.color : Theme.fg
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
        onHeightChanged: if (root.active) Qt.callLater(root.keepInView)
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
