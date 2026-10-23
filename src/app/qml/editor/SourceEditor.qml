pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// SourceEditor — source mode (Ctrl+E): the whole file in one TextEdit with Markdown
// highlighting ("source" mode: fenced code, frontmatter and comments across lines). Edits go to
// PageController.sourceText (saved like any edit); leaving source mode applies the text to the
// blocks as one undo step. Uses TextEdit's own undo while in this mode.
// The text is read exactly (EditorInput.plainText: no-break spaces kept); the TextEdit shows
// every line break as "\n" and the controller puts the file's "\r\n" back, so the controller's
// text differs from the TextEdit's in CRLF files: only changes made elsewhere are loaded back.
// A mouse selects by dragging; a finger scrolls (on touch UIs a TextEdit that selects by dragging
// keeps the finger and the source could not be scrolled at all). Such a TextEdit leaves taps of a
// finger alone too, so on touch UIs a TapHandler above it places the cursor where a tap lands
// and selects the word under a long press (the system's selection handles, QInputMethod, take it
// on from there where the platform has them).
// Touch UIs: while the text has the keyboard the editor's toolbar is docked on it with the source
// tools ([[, #, /, undo, redo, hide keyboard; BlockEditor.toolbarMode "source"); the view ends at
// the keyboard (the shell lifts it), the caret is kept above the toolbar.
Flickable {
    id: root

    property BlockEditor editor
    property PageController controller
    /// The source's text editor (the touch toolbar inserts into it and uses its undo).
    readonly property alias textEdit: source
    /// Room at the bottom for the toolbar docked on the editor's bottom edge.
    readonly property real toolbarRoom: editor && editor.toolbarVisible && editor.dockHost === editor ? editor.toolbarHeight : 0

    contentWidth: width
    contentHeight: source.height + 2 * Theme.space6
    bottomMargin: toolbarRoom
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    TouchFlick.enabled: true

    function focusEditor() { source.forceActiveFocus() }
    /// "Hide keyboard" on the toolbar: the text gives up the keyboard (a tap brings it back).
    function releaseKeyboard() { source.focus = false }
    /// Keep the caret inside the visible part (above the toolbar), not while the keyboard slides.
    function keepCaretVisible() {
        if (!source.activeFocus || KeyboardInsets.animating)
            return
        const top = source.y + source.cursorRectangle.y
        const bottom = top + source.cursorRectangle.height
        const visibleHeight = root.height - root.toolbarRoom
        if (top < root.contentY + Theme.space4)
            root.contentY = Math.max(0, top - Theme.space4)
        else if (bottom > root.contentY + visibleHeight - Theme.space4)
            root.contentY = bottom - visibleHeight + Theme.space4
    }
    // The view shrinks with the keyboard; the toolbar comes and goes.
    property real lastHeight: 0
    onHeightChanged: {
        const shrank = height < lastHeight - 0.5
        lastHeight = height
        if (shrank)
            Qt.callLater(root.keepCaretVisible)
    }
    onToolbarRoomChanged: Qt.callLater(root.keepCaretVisible)
    readonly property bool keyboardSliding: KeyboardInsets.animating
    onKeyboardSlidingChanged: if (!keyboardSliding && KeyboardInsets.shown) Qt.callLater(root.keepCaretVisible)
    Component.onDestruction: if (editor) editor.sourceEditing = false

    T.ScrollBar.vertical: BScrollBar {}

    // A sunken card around the text; on phones it fills the width (a card would touch the
    // screen edges with its rounded corners and side borders).
    Rectangle {
        x: Theme.compact ? 0 : root.editor.contentX - Theme.space3
        y: Theme.compact ? 0 : Theme.space3
        width: Theme.compact ? root.width : root.editor.contentWidth + 2 * Theme.space3
        height: source.height + (Theme.compact ? 2 * Theme.space6 : 2 * Theme.space3)
        radius: Theme.compact ? 0 : Theme.radiusLg
        color: Theme.bgSunken
        border.width: Theme.compact ? 0 : 1
        border.color: Theme.borderSubtle
    }

    TextEdit {
        id: source
        x: root.editor.contentX
        y: Theme.space6
        width: root.editor.contentWidth
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        tabStopDistance: tabMetrics.advanceWidth("    ")
        // Not on touch UIs: a finger drag scrolls (see above).
        selectByMouse: !Theme.touchUi
        font.family: Theme.fontMono
        font.pixelSize: Theme.textSm + 1
        color: Theme.fg
        selectionColor: Theme.selection
        selectedTextColor: Theme.selectionFg
        Accessible.role: Accessible.EditableText
        Accessible.name: qsTr("Markdown source")

        // TextEdit also reports formatting-only changes (highlighting) as textChanged: only real
        // edits after the initial text is in place reach the controller.
        property bool syncing: true
        property bool pushing: false
        property string lastText: ""

        FontMetrics { id: tabMetrics; font: source.font }

        MarkdownHighlighter {
            textDocument: source.textDocument
            palette: root.editor.highlighterPalette
            mode: "source"
            monoFamily: Theme.fontMono
        }

        Component.onCompleted: {
            syncing = true
            text = root.controller ? root.controller.sourceText : ""
            lastText = EditorInput.plainText(textDocument)
            syncing = false
            cursorPosition = 0
        }
        onTextChanged: {
            if (syncing || !root.controller)
                return
            const t = EditorInput.plainText(textDocument)
            if (t === lastText)
                return
            lastText = t
            pushing = true
            root.controller.sourceText = t
            pushing = false
        }
        onCursorRectangleChanged: root.keepCaretVisible()
        // The editor's toolbar shows the source tools while the text has the keyboard.
        onActiveFocusChanged: if (root.editor) root.editor.sourceEditing = activeFocus

        Connections {
            target: root.controller
            function onSourceTextChanged() {
                // Our own edit coming back (with the file's line endings) is not reloaded.
                if (root.controller.sourceMode && !source.pushing) {
                    source.syncing = true
                    const c = source.cursorPosition
                    source.text = ""
                    source.text = root.controller.sourceText
                    source.lastText = EditorInput.plainText(source.textDocument)
                    source.cursorPosition = Math.min(c, source.length)
                    source.syncing = false
                }
            }
        }

        Keys.onShortcutOverride: (event) => {
            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_E)
                event.accepted = true
        }
        Keys.onPressed: (event) => {
            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_E) {
                root.editor.toggleSourceMode()
                event.accepted = true
            }
        }
    }

    // Touch UIs (see above): taps and long presses on the source, on an item of its own above
    // the TextEdit (BlockText's way): its TapHandler takes the press first, and a drag past the
    // threshold goes to the Flickable.
    Item {
        x: source.x
        y: source.y
        width: source.width
        height: source.height
        visible: Theme.touchUi

        TapHandler {
            id: touchTap
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: (eventPoint) => {
                source.forceActiveFocus()
                source.cursorPosition = source.positionAt(eventPoint.position.x, eventPoint.position.y)
                Qt.inputMethod.show()
            }
            onLongPressed: {
                source.forceActiveFocus()
                source.cursorPosition = source.positionAt(touchTap.point.position.x, touchTap.point.position.y)
                source.selectWord()
            }
        }
    }
}
