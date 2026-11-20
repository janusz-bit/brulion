pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CanvasTextEditor — a text card's Markdown typed in place (CanvasView; docs/UI.md "Canvas view"):
// over the card, in the board's scaled world, the card's own size (growing down while the text is
// longer). Escape or a click outside ends it and keeps what was typed (as Obsidian does); the
// card grows to hold the text. One undo step per edit (CanvasEditor.setNodeText).
Item {
    id: editor

    required property CanvasEditor board
    /// What the card will show (its rows measure the height the text needs).
    property CanvasCards cards: null
    /// The node being edited ("" none).
    property string nodeId: ""
    property string original: ""
    readonly property alias area: area
    readonly property MapHolder holder: MapHolder {}

    signal closed()

    visible: nodeId !== ""
    z: 10

    /// Edit the text card `id` (the view zooms to it first when it is too small to type in).
    function open(id: string): void {
        if (editor.nodeId !== "")
            editor.commit()
        editor.holder.map = editor.board.nodeData(id)
        if (UiModels.stringField(editor.holder.map, "id") === "")
            return
        editor.x = UiModels.numberField(editor.holder.map, "x", 0)
        editor.y = UiModels.numberField(editor.holder.map, "y", 0)
        editor.width = UiModels.numberField(editor.holder.map, "width", 250)
        editor.cardHeight = UiModels.numberField(editor.holder.map, "height", 60)
        editor.original = UiModels.stringField(editor.holder.map, "text")
        area.text = editor.original
        editor.nodeId = id
        editor.board.editingNode = id
        area.forceActiveFocus()
        area.cursorPosition = area.length
    }
    /// What was typed goes into the card; the editor closes.
    function commit(): void {
        if (editor.nodeId === "")
            return
        const id = editor.nodeId
        const text = area.text
        // The height the rendered text needs (the card's rows), at least what the editor shows.
        const grow = Math.max(area.contentHeight + 2 * Theme.space3, measure.contentHeight + 2 * Theme.space2 + Theme.space1)
        editor.nodeId = ""
        editor.board.editingNode = ""
        if (text !== editor.original || grow > editor.cardHeight + 1)
            editor.board.setNodeTextFitted(id, text, grow)
        editor.closed()
    }

    property real cardHeight: 60
    height: Math.max(cardHeight, area.contentHeight + 2 * Theme.space3)

    // The text as the card will render it, unseen: its height.
    CanvasCardContent {
        id: measureContent
        cards: editor.cards
        active: editor.nodeId !== ""
        kind: "text"
        text: area.text
    }
    CanvasCardRows {
        id: measure
        visible: false
        width: editor.width - 2 * Theme.space3
        entries: measureContent.rows
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusLg
        color: Theme.bgElevated
        border.width: 2.5 / Math.max(0.01, editor.board.zoom)
        border.color: Theme.accent
    }
    BTextArea {
        id: area
        objectName: "canvasTextArea"
        anchors.fill: parent
        anchors.margins: 2
        leftPadding: Theme.space3
        rightPadding: Theme.space3
        topPadding: Theme.space2
        bottomPadding: Theme.space2
        wrapMode: TextEdit.Wrap
        font.family: Theme.fontEditor
        font.pixelSize: Theme.textEditor
        background: Item {}
        placeholderText: qsTr("Type Markdown…")
        Keys.onEscapePressed: editor.commit()
        Keys.onPressed: (event) => {
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                editor.commit()
                event.accepted = true
            }
        }
        onActiveFocusChanged: if (!activeFocus && editor.nodeId !== "") editor.commit()
    }
}
