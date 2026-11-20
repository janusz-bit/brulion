pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// SourceEditor — source mode (Ctrl+E): the whole file in one TextEdit with Markdown
// highlighting ("source" mode: fenced code, frontmatter and comments across lines). Edits go to
// PageController.sourceText (saved like any edit); leaving source mode applies the text to the
// blocks as one undo step. Uses TextEdit's own undo while in this mode.
// Each edit is handed over alone (SourceTextSync: where the text changed, not the whole page per
// keystroke), exactly (no-break spaces kept); the TextEdit shows every line break as "\n" and the
// controller puts the file's "\r\n" back, so the controller's text differs from the TextEdit's
// in CRLF files: only changes made elsewhere are loaded back.
// A mouse selects by dragging; a finger scrolls (on touch UIs a TextEdit that selects by dragging
// keeps the finger and the source could not be scrolled at all). Such a TextEdit leaves taps of a
// finger alone too, so on touch UIs a TapHandler above it places the cursor where a tap lands
// and selects the word under a long press (the system's selection handles, QInputMethod, take it
// on from there where the platform has them).
// Touch UIs: while the text has the keyboard the editor's toolbar is docked on it with the source
// tools ([[, #, /, undo, redo, hide keyboard; BlockEditor.toolbarMode "source"); the view ends at
// the keyboard (the shell lifts it), the caret is kept above the toolbar — with the editor's
// typewriter scrolling in the middle of the view.
// Vim keys (docs/UI.md "Vim mode"): the TextEdit is BlockEditor.vimSession's source buffer
// (VimSourceBuffer) while it exists; normal and visual mode make it read-only with a block caret.
Flickable {
    id: root

    /// The editor as an Item, for the editor itself to pass (a file cannot give its own root to a
    /// property of its own type in compiled code); `editor` follows it unless set directly.
    property Item editorItem
    final property BlockEditor editor: editorItem as BlockEditor
    final property PageController controller
    /// The source's text editor (the touch toolbar inserts into it and uses its undo).
    readonly property alias textEdit: source
    /// Room at the bottom for the toolbar docked on the editor's bottom edge.
    readonly property real toolbarRoom: editor && editor.toolbarVisible && editor.dockHost === editor ? editor.toolbarHeight : 0

    contentWidth: width
    contentHeight: source.height + 2 * Theme.space6
    // (Typewriter scrolling: room under the last line to bring it to the middle.)
    bottomMargin: toolbarRoom + (editor && editor.typewriter ? height / 2 : 0)
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    TouchFlick.enabled: true

    function focusEditor(): void { source.forceActiveFocus() }
    /// Vim's normal / visual mode in the source (BlockEditor.vimCommands).
    readonly property bool vimCommands: editor !== null && editor.vimCommands
    onVimCommandsChanged: {
        const caret = source.cursorPosition
        source.readOnly = root.vimCommands
        source.cursorPosition = Math.min(caret, source.length)
        root.updateVimCaret()
    }
    Component.onCompleted: source.readOnly = root.vimCommands
    /// The block caret (InlineEditor's way: set when the caret, the text or the mode change).
    property rect vimCaretBox
    property rect box
    property rect nextBox
    function updateVimCaret(): void {
        if (!vimCommands)
            return
        root.box = source.positionToRectangle(source.cursorPosition)
        let w = caretMetrics.averageCharacterWidth
        if (source.cursorPosition < source.length) {
            root.nextBox = source.positionToRectangle(source.cursorPosition + 1)
            if (Math.abs(root.nextBox.y - root.box.y) < 1 && root.nextBox.x > root.box.x)
                w = root.nextBox.x - root.box.x
        }
        root.vimCaretBox = Qt.rect(root.box.x, root.box.y, Math.max(2, w), root.box.height)
    }
    FontMetrics { id: caretMetrics; font: source.font }
    /// The find bar's current match: selected and scrolled to; the bar may keep the keyboard.
    function showRange(from: int, to: int, keepKeyboard: bool): void {
        source.select(from, to)
        if (!keepKeyboard)
            source.forceActiveFocus()
        root.rect = source.positionToRectangle(from)
        const top = source.y + root.rect.y
        const visibleHeight = root.height - root.toolbarRoom
        if (top < root.contentY + Theme.space4 || top + root.rect.height > root.contentY + visibleHeight - Theme.space4)
            root.contentY = Math.max(0, Math.min(top - visibleHeight / 3, root.contentHeight - visibleHeight))
    }
    /// The find bar's replaces: one step of the text's own undo history.
    function replaceRanges(ranges: list<var>, text: string): void {
        EditorInput.replaceRanges(source.textDocument, ranges, text)
    }
    /// "Hide keyboard" on the toolbar: the text gives up the keyboard (a tap brings it back).
    function releaseKeyboard(): void { source.focus = false }
    /// A rectangle of the text, read (positionToRectangle()'s and cursorRectangle's results
    /// cannot be looked into).
    property rect rect
    /// Keep the caret inside the visible part (above the toolbar), not while the keyboard slides.
    function keepCaretVisible(): void {
        if (!source.activeFocus || KeyboardInsets.animating)
            return
        root.rect = source.cursorRectangle
        const top = source.y + root.rect.y
        const bottom = top + root.rect.height
        const visibleHeight = root.height - root.toolbarRoom
        if (root.editor && root.editor.typewriter) {
            // Typewriter scrolling: the caret's line in the middle.
            const target = Math.max(0, Math.min(root.contentHeight + root.bottomMargin - root.height,
                                                (top + bottom) / 2 - visibleHeight / 2))
            if (Math.abs(target - root.contentY) >= 1)
                root.contentY = target
            return
        }
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
            caretLater.restart()
    }
    onToolbarRoomChanged: caretLater.restart()
    readonly property bool keyboardSliding: KeyboardInsets.animating
    onKeyboardSlidingChanged: if (!keyboardSliding && KeyboardInsets.shown) caretLater.restart()
    /// keepCaretVisible() once the event loop is idle (repeated requests are one).
    Later {
        id: caretLater
        onTriggered: root.keepCaretVisible()
    }
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
        // Vim's normal and visual mode: keys are commands (docs/UI.md "Vim mode"); switched with
        // the caret kept (a TextEdit puts it at the end whenever readOnly changes).
        readOnly: false
        font.family: Theme.fontMono
        font.pixelSize: Theme.textSm + 1
        color: Theme.fg
        selectionColor: Theme.selection
        selectedTextColor: Theme.selectionFg
        // The find bar's match stays selected while the bar has the keyboard.
        persistentSelection: root.editor !== null && root.editor.find !== null
        Accessible.role: Accessible.EditableText
        Accessible.name: qsTr("Markdown source")

        FontMetrics { id: tabMetrics; font: source.font }

        // The edits after the text is in place reach the controller (filling it is none).
        SourceTextSync {
            id: sync
            textDocument: source.textDocument
            controller: root.controller
        }

        MarkdownHighlighter {
            textDocument: source.textDocument
            palette: root.editor.highlighterPalette
            mode: "source"
            monoFamily: Theme.fontMono
            readonly property NoteFind find: root.editor.find
            findQuery: find ? find.appliedQuery : ""
            findMatchCase: find ? find.matchCase : false
            findMatchDiacritics: find ? find.matchDiacritics : false
            findWholeWord: find ? find.wholeWord : false
            findColor: root.editor.findHitColor
        }

        Component.onCompleted: {
            text = root.controller ? root.controller.sourceText : ""
            sync.active = true
            cursorPosition = 0
            if (root.editor)
                root.editor.vimSession.sourceEdit = source
        }
        Component.onDestruction: {
            if (root.editor && root.editor.vimSession.sourceEdit === source)
                root.editor.vimSession.sourceEdit = null
        }

        // Vim's block caret (normal mode).
        Rectangle {
            objectName: "vimSourceCaret"
            visible: root.vimCommands && source.activeFocus && source.selectedText === ""
            x: root.vimCaretBox.x
            y: root.vimCaretBox.y
            width: root.vimCaretBox.width
            height: root.vimCaretBox.height
            color: Theme.accent
            opacity: 0.45
            radius: 1
        }
        onCursorRectangleChanged: {
            root.keepCaretVisible()
            root.updateVimCaret()
        }
        // The editor's toolbar shows the source tools while the text has the keyboard.
        onActiveFocusChanged: if (root.editor) root.editor.sourceEditing = activeFocus

        Connections {
            target: root.controller
            function onSourceTextChanged() {
                // Our own edit coming back (with the file's line endings) is not reloaded.
                if (root.controller.sourceMode && !sync.pushing) {
                    sync.active = false
                    const c = source.cursorPosition
                    source.text = ""
                    source.text = root.controller.sourceText
                    source.cursorPosition = Math.min(c, source.length)
                    sync.active = true
                }
            }
        }

        Keys.onShortcutOverride: (event) => {
            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_E)
                event.accepted = true
            else if (root.vimCommands && root.editor.vimOwnsShortcut(event.key, event.modifiers))
                event.accepted = true
        }
        Keys.onPressed: (event) => {
            // Vim's keys first (docs/UI.md "Vim mode").
            if (root.editor && root.editor.handleVimSourceKey(event, source)) {
                event.accepted = true
                return
            }
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
        anchors.fill: source
        visible: Theme.touchUi

        TapHandler {
            id: touchTap
            objectName: "sourceTouchTap" // the shell self-test holds a long press until it selected
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: (eventPoint) => {
                const x = eventPoint.position.x
                const y = eventPoint.position.y
                source.forceActiveFocus()
                source.cursorPosition = source.positionAt(x, y)
                DeviceInfo.showInputMethod()
            }
            onLongPressed: {
                source.forceActiveFocus()
                source.cursorPosition = source.positionAt(touchTap.point.position.x, touchTap.point.position.y)
                source.selectWord()
            }
        }
    }
}
