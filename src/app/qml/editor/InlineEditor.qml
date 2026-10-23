pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// InlineEditor — the TextEdit of a block being edited: raw Markdown (the block's `editText`:
// without the task marker / fence lines the delegate draws itself), MarkdownHighlighter, the
// fixed line height of the rendered text, keys handled by BlockEditor.handleEditorKey, typing
// forwarded to PageController.setEditText (one undo step per block), autocomplete triggers.
//
// Text blocks create it while editing (BlockText); code-like blocks keep a `persistent` one that
// is read-only while the block is not edited, so nothing moves when editing starts.
// Android keyboards may commit a line break as text instead of a key press: in single-line
// blocks it splits the block like Enter.
// The text is read with EditorInput.plainText(), not `text`: TextEdit.text turns no-break spaces
// and U+2028 into spaces / line feeds, and the block must keep every character it had (so do
// Ctrl+C / Ctrl+X: BlockEditor.copyEditSelection). The selection stays shown while a menu or a
// dialog has the keyboard; a right-click (mouse, touchpad, stylus) opens Wytnij / Kopiuj / Wklej /
// Zaznacz wszystko (BlockEditor.openTextMenu).
TextEdit {
    id: edit

    property BlockDelegate block
    readonly property BlockEditor editor: block ? block.editor : null
    readonly property int row: block ? block.index : -1
    readonly property bool active: block !== null && block.editing
    /// Stays alive (read-only) while the block is not edited.
    property bool persistent: false
    property real lineHeightPx: 0
    /// Enter inserts a line break (code, quotes) instead of splitting the block.
    property bool multiline: false
    /// Inserted after a line break (quotes: "> ").
    property string continuation: ""
    /// Autocomplete popup for links, tags, slash commands, dates.
    property bool completions: !multiline || continuation !== ""
    property string highlightMode: "inline"
    property string placeholderText

    // Until the initial text is in place, textChanged (TextEdit also reports formatting-only
    // changes) is ignored. lastText: the exact text last synchronised with the controller.
    property bool syncing: true
    property bool completed: false
    property string lastText: ""
    property bool softBreak: false
    readonly property string modelText: block ? block.model.editText : ""

    textFormat: TextEdit.PlainText
    wrapMode: TextEdit.Wrap
    tabStopDistance: tabMetrics.advanceWidth("    ")
    readOnly: !active
    selectByMouse: active
    activeFocusOnPress: active
    // The selected text stays while a menu or dialog has the keyboard (the block is still edited;
    // editing ends deselect it, onActiveChanged).
    persistentSelection: active
    color: Theme.fg
    selectionColor: Theme.selection
    selectedTextColor: Theme.selectionFg
    Accessible.role: Accessible.EditableText
    Accessible.name: qsTr("Block text")

    FontMetrics { id: tabMetrics; font: edit.font }

    MarkdownHighlighter {
        textDocument: edit.textDocument
        palette: edit.editor ? edit.editor.highlighterPalette : ({})
        mode: edit.highlightMode
        monoFamily: Theme.fontMono
        lineHeight: edit.lineHeightPx
    }

    Text {
        visible: edit.active && edit.length === 0 && edit.preeditText === "" && edit.placeholderText !== ""
        width: edit.width
        textFormat: Text.RichText
        text: TextHelper.escapeHtml(edit.placeholderText)
        font: edit.font
        lineHeightMode: Text.FixedHeight
        lineHeight: edit.lineHeightPx > 0 ? edit.lineHeightPx : 1
        color: Theme.fgFaint
        elide: Text.ElideRight
    }

    /// The exact text of the document (no-break spaces kept), see EditorInput.plainText().
    function plain() {
        return EditorInput.plainText(edit.textDocument)
    }
    /// Show `t` exactly. TextEdit.text ignores a new text that equals its current one after
    /// normalisation (a space for a no-break space), so such a text is set from scratch.
    function setPlain(t) {
        if (plain() === t)
            return
        text = t
        if (plain() !== t) {
            text = ""
            text = t
        }
    }

    /// Take the keyboard: text from the model, caret from a click, Up/Down navigation or the
    /// controller.
    function activate() {
        if (!editor || !editor.controller)
            return
        syncing = true
        setPlain(modelText)
        lastText = plain()
        let c = -1
        if (editor.pendingClickRow === row) {
            c = positionAt(editor.pendingClick.x, editor.pendingClick.y)
            editor.pendingClickRow = -1
        } else if (editor.navigationEdge !== "") {
            // navigationX is in the editor's coordinates: blocks are indented differently.
            const x = editor.navigationX - mapToItem(editor, 0, 0).x
            c = positionAt(Math.max(0, x), editor.navigationEdge === "bottom" ? contentHeight - 2 : 2)
            editor.navigationEdge = ""
        } else {
            c = editor.controller.cursorPosition
        }
        cursorPosition = c < 0 ? length : Math.min(c, length)
        syncing = false
        editor.controller.cursorPosition = cursorPosition
        forceActiveFocus()
        // A delegate created while rows are being inserted is not in the scene yet: focus again
        // once it is.
        Qt.callLater(edit.ensureFocus)
    }

    function ensureFocus() {
        if (active && !activeFocus)
            forceActiveFocus()
    }

    /// The controller changed the text (undo, completion, normalisation…).
    function reset(newText, cursor) {
        syncing = true
        setPlain(newText)
        lastText = plain()
        cursorPosition = Math.max(0, Math.min(cursor < 0 ? length : cursor, length))
        syncing = false
        if (editor)
            editor.updateCompletion(edit)
    }

    function insertLineBreak() {
        if (selectedText !== "")
            remove(selectionStart, selectionEnd)
        softBreak = true
        insert(cursorPosition, "\n" + continuation)
    }

    Component.onCompleted: {
        completed = true
        if (active) {
            activate()
        } else {
            setPlain(modelText)
            syncing = false
        }
    }
    onActiveChanged: {
        if (!completed)
            return // bindings are still being set up; Component.onCompleted activates
        if (active) {
            activate()
        } else {
            deselect()
            syncing = true
            setPlain(modelText)
            syncing = false
        }
    }
    onModelTextChanged: {
        if (completed && !active) {
            syncing = true
            setPlain(modelText)
            syncing = false
        }
    }

    onTextChanged: {
        if (syncing || !active || !editor || !editor.controller)
            return
        const t = plain()
        if (t === lastText)
            return
        if (!multiline && !softBreak) {
            // Text with line breaks that did not come from Shift+Enter: an input method's Enter
            // (Android commits "\n" as text) splits the block, pasted or dropped Markdown
            // becomes blocks.
            let start = 0
            while (start < t.length && start < lastText.length && t.charAt(start) === lastText.charAt(start))
                ++start
            let endNew = t.length
            let endOld = lastText.length
            while (endNew > start && endOld > start && t.charAt(endNew - 1) === lastText.charAt(endOld - 1)) {
                --endNew
                --endOld
            }
            const inserted = t.slice(start, endNew)
            if (inserted.indexOf("\n") >= 0) {
                const before = lastText
                syncing = true
                setPlain(before)
                cursorPosition = start
                syncing = false
                if (inserted === "\n")
                    editor.controller.split(row, start)
                else
                    editor.controller.pasteText(row, start, endOld, inserted)
                return
            }
        }
        softBreak = false
        lastText = t
        editor.controller.setEditText(row, t, cursorPosition)
        editor.updateCompletion(edit)
    }

    onCursorPositionChanged: {
        if (syncing || !active || !editor || !editor.controller)
            return
        editor.controller.cursorPosition = cursorPosition
        editor.updateCompletion(edit)
    }
    onCursorRectangleChanged: if (activeFocus && editor) Qt.callLater(editor.ensureCaretVisible, edit)
    // Android keyboards compose every word as preedit: completions follow it while it is typed.
    onPreeditTextChanged: if (editor && active) editor.updateCompletion(edit)

    onActiveFocusChanged: {
        if (!editor)
            return
        if (activeFocus) {
            editor.activeEditor = edit
            Qt.callLater(editor.ensureCaretVisible, edit)
        } else {
            editor.editorFocusLost()
        }
    }

    Connections {
        target: edit.editor ? edit.editor.controller : null
        function onEditTextReset(r, t, c) {
            if (r === edit.row && edit.active)
                edit.reset(t, c)
        }
    }

    Keys.onShortcutOverride: (event) => {
        // Editor shortcuts win over application-wide ones while typing.
        if ((event.modifiers & Qt.ControlModifier) && [Qt.Key_E, Qt.Key_Period, Qt.Key_Z, Qt.Key_Y, Qt.Key_Return, Qt.Key_Enter].indexOf(event.key) >= 0)
            event.accepted = true
    }
    Keys.onPressed: (event) => {
        if (edit.active && edit.editor && edit.editor.handleEditorKey(event, edit))
            event.accepted = true
    }

    // The text menu: a right-click in the edited text (its press is the text's: the editor's
    // right-click on the block opens no block menu as well).
    TapHandler {
        enabled: edit.active && !Theme.touchUi
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.WithinBounds
        onTapped: (eventPoint) => edit.editor.openTextMenu(edit, eventPoint.position.x, eventPoint.position.y)
    }
    // Read-only (not editing) persistent editors: a click starts editing there.
    TapHandler {
        enabled: !edit.active && edit.persistent
        onTapped: (eventPoint, button) => edit.editor.editAt(edit.row, eventPoint.position.x, eventPoint.position.y)
    }
    HoverHandler {
        enabled: !edit.active && edit.persistent && !Theme.touchUi
        cursorShape: Qt.IBeamCursor
    }
}
