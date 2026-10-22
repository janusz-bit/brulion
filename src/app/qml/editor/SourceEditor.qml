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
// keeps the finger and the source could not be scrolled at all) and selects with the system's
// selection handles (a long press: the input method's, QInputMethod), a tap places the cursor.
Flickable {
    id: root

    property BlockEditor editor
    property PageController controller

    contentWidth: width
    contentHeight: source.height + 2 * Theme.space6
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    TouchFlick.enabled: true

    function focusEditor() { source.forceActiveFocus() }

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
        onCursorRectangleChanged: {
            const top = y + cursorRectangle.y
            if (top < root.contentY + Theme.space4)
                root.contentY = Math.max(0, top - Theme.space4)
            else if (top + cursorRectangle.height > root.contentY + root.height - Theme.space4)
                root.contentY = top + cursorRectangle.height - root.height + Theme.space4
        }

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
}
