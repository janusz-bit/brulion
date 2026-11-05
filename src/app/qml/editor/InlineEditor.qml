pragma ComponentBehavior: Bound
import QtQuick
import Brulion
import Brulion as B

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
// Zaznacz wszystko (BlockEditor.openTextMenu), with the spelling's corrections at its top on a word
// marked as misspelled; on touch UIs a tap on such a word opens only those (docs/UI.md "Spell
// checking"). Spell checking (SpellCheck) runs in the edited prose of text blocks, headings and
// quotes — not in code, calculations or queries ("code" highlighting).
TextEdit {
    id: edit

    final property BlockDelegate block
    final property BlockEditor editor: block ? block.editor : null
    // (final, also the bindings: BlockEditor and the popups look them up through a typed `edit`.)
    final property int row: block ? block.index : -1
    final property bool active: block !== null && block.editing
    /// Stays alive (read-only) while the block is not edited.
    final property bool persistent: false
    final property real lineHeightPx: 0
    /// Enter inserts a line break (code, quotes) instead of splitting the block.
    final property bool multiline: false
    /// Inserted after a line break (quotes: "> ").
    property string continuation: ""
    /// Autocomplete popup for links, tags, slash commands, dates.
    final property bool completions: !multiline || continuation !== ""
    /// cursorRectangle, typed for the editor (TextEdit's own cannot be looked into from there).
    final property rect caretRect: cursorRectangle
    /// This editor as the module's type, for calls into the editor (inside its own file the type is
    /// another one for the compiler).
    readonly property B.InlineEditor self: edit as B.InlineEditor
    property string highlightMode: "inline"
    property string placeholderText
    /// Misspelled words are underlined while the block is edited.
    readonly property bool spellChecked: active && highlightMode !== "code" && SpellCheck.active

    // Until the initial text is in place, textChanged (TextEdit also reports formatting-only
    // changes) is ignored. lastText: the exact text last synchronised with the controller.
    property bool syncing: true
    property bool completed: false
    property string lastText: ""
    property bool softBreak: false
    readonly property color noColor: "transparent"
    readonly property string modelText: block ? block.rowData.editText : ""

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
        id: highlighter
        textDocument: edit.textDocument
        palette: edit.editor ? edit.editor.highlighterPalette : ({})
        mode: edit.highlightMode
        monoFamily: Theme.fontMono
        lineHeight: edit.lineHeightPx
        readonly property NoteFind find: edit.editor ? edit.editor.find : null
        findQuery: find ? find.appliedQuery : ""
        findMatchCase: find ? find.matchCase : false
        findMatchDiacritics: find ? find.matchDiacritics : false
        findWholeWord: find ? find.wholeWord : false
        findColor: edit.editor ? edit.editor.findHitColor : edit.noColor
        spellCheck: edit.spellChecked ? SpellCheck : null
        spellCaret: edit.spellChecked ? edit.cursorPosition : -1
        spellColor: Theme.danger
    }

    /// The word marked as misspelled at `position` ({from, to, word, text}; {} when none).
    function misspelledAt(position: int): var {
        return edit.spellChecked ? highlighter.misspelledAt(position) : ({})
    }
    /// Replace the misspelled word [from, to) with `text`: one undo step, the caret after it
    /// (PageController.replaceRange → editTextReset).
    function replaceWord(from: int, to: int, text: string): void {
        if (editor && editor.controller && active)
            editor.controller.replaceRange(row, from, to, text)
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
    function plain(): string {
        return EditorInput.plainText(edit.textDocument)
    }
    /// Show `t` exactly. TextEdit.text ignores a new text that equals its current one after
    /// normalisation (a space for a no-break space), so such a text is set from scratch.
    function setPlain(t: string): void {
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
    function activate(): void {
        if (!editor || !editor.controller)
            return
        syncing = true
        setPlain(modelText)
        lastText = plain()
        let c = -1
        if (editor.pendingClickRow === row) {
            const click = editor.pendingClick
            c = positionAt(click.x, click.y)
            editor.pendingClickRow = -1
        } else if (editor.navigationEdge !== "") {
            // navigationX is in the editor's coordinates: blocks are indented differently.
            edit.mapped = mapToItem(editor, 0, 0)
            const x = editor.navigationX - edit.mapped.x
            c = positionAt(Math.max(0, x), editor.navigationEdge === "bottom" ? contentHeight - 2 : 2)
            editor.navigationEdge = ""
        } else {
            c = editor.controller.cursorPosition
        }
        cursorPosition = c < 0 ? length : Math.min(c, length)
        syncing = false
        editor.controller.cursorPosition = cursorPosition
        edit.selectFindMatch()
        // The find bar keeps the keyboard while it goes from match to match.
        if (editor.findKeepsKeyboard) {
            editor.activeEditor = edit.self
            return
        }
        forceActiveFocus()
        // A delegate created while rows are being inserted is not in the scene yet: focus again
        // once it is.
        focusLater.restart()
    }
    /// A point mapped into the editor, read (mapToItem()'s result cannot be looked into).
    property point mapped
    Timer {
        id: focusLater
        interval: 0
        onTriggered: edit.ensureFocus()
    }

    function ensureFocus(): void {
        if (active && !activeFocus && !(editor && editor.findKeepsKeyboard))
            forceActiveFocus()
    }

    /// The find bar's current match is in this block: selected, once — a click back into the block
    /// later (activate() again) must not select it again, typing would replace it.
    function selectFindMatch(): void {
        if (editor && editor.findSelectionRow >= 0 && editor.findSelectionRow === row && active) {
            select(editor.findSelectionFrom, editor.findSelectionTo)
            editor.clearFindSelection()
        }
    }

    /// The controller changed the text (undo, completion, normalisation…).
    function reset(newText: string, cursor: int): void {
        syncing = true
        setPlain(newText)
        lastText = plain()
        cursorPosition = Math.max(0, Math.min(cursor < 0 ? length : cursor, length))
        syncing = false
        if (editor)
            editor.updateCompletion(edit.self)
    }

    function insertLineBreak(): void {
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
            // The changed range: [start, endNew) of the new text, [start, endOld) of the old one.
            edit.changed = edit.changedRange(lastText, t)
            const start = edit.changed[0] + 0 // (typed copies: passed on after other calls)
            const endNew = edit.changed[1] + 0
            const endOld = edit.changed[2] + 0
            const inserted = Js.slice(t, start, endNew) + ""
            if (Js.indexOf(inserted, "\n") >= 0) {
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
        editor.updateCompletion(edit.self)
    }

    /// EditorInput.changedRange(before, after): [start, end in after, end in before].
    function changedRange(before: string, after: string): list<int> {
        return EditorInput.changedRange(before, after)
    }
    /// The last text change's range (onTextChanged).
    property list<int> changed

    onCursorPositionChanged: {
        if (syncing || !active || !editor || !editor.controller)
            return
        editor.controller.cursorPosition = cursorPosition
        editor.updateCompletion(edit.self)
    }
    onCursorRectangleChanged: if (activeFocus && editor) caretLater.restart()
    /// editor.ensureCaretVisible(edit) once the event loop is idle (Qt.callLater merged the calls).
    Timer {
        id: caretLater
        interval: 0
        onTriggered: if (edit.editor) edit.editor.ensureCaretVisible(edit.self)
    }
    // Android keyboards compose every word as preedit: completions follow it while it is typed.
    onPreeditTextChanged: if (editor && active) editor.updateCompletion(edit.self)

    onActiveFocusChanged: {
        if (!editor)
            return
        if (activeFocus) {
            editor.findKeepsKeyboard = false
            editor.activeEditor = edit.self
            caretLater.restart()
        } else {
            editor.editorFocusLost()
        }
    }

    Connections {
        target: edit.editor ? edit.editor.controller : null
        function onEditTextReset(r: int, t: string, c: int) {
            if (r === edit.row && edit.active)
                edit.reset(t, c)
        }
    }

    Connections {
        target: edit.editor
        function onFindSelectionChanged() { edit.selectFindMatch() }
    }

    Keys.onShortcutOverride: (event) => {
        // Editor shortcuts win over application-wide ones while typing.
        if (!(event.modifiers & Qt.ControlModifier))
            return
        switch (event.key) {
        case Qt.Key_E: case Qt.Key_Period: case Qt.Key_Z: case Qt.Key_Y: case Qt.Key_Return: case Qt.Key_Enter:
            event.accepted = true
        }
    }
    Keys.onPressed: (event) => {
        if (edit.active && edit.editor && edit.editor.handleEditorKey(event, edit.self))
            event.accepted = true
    }

    // Touch UIs: a tap on a word marked as misspelled opens its corrections (the tap places the
    // caret there first, as on any text: this handler only watches).
    TapHandler {
        enabled: edit.active && Theme.touchUi && edit.spellChecked
        acceptedDevices: PointerDevice.TouchScreen
        onTapped: (eventPoint) => {
            const x = eventPoint.position.x
            const y = eventPoint.position.y
            const pos = edit.positionAt(x, y)
            misspelling.map = edit.misspelledAt(pos)
            if (misspelling.map.word !== undefined)
                edit.editor.openSpellingMenu(edit.self, pos, x, y)
        }
    }
    // The text menu: a right-click in the edited text (its press is the text's: the editor's
    // right-click on the block opens no block menu as well).
    TapHandler {
        enabled: edit.active && !Theme.touchUi
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.WithinBounds
        onTapped: (eventPoint) => edit.editor.openTextMenu(edit.self, eventPoint.position.x, eventPoint.position.y)
    }
    // Read-only (not editing) persistent editors (code, calculations): a click starts editing
    // there, Shift+click selects blocks; a finger held on it selects the block (touch UIs).
    TapHandler {
        id: persistentTap
        enabled: !edit.active && edit.persistent
        onTapped: (eventPoint, button) => edit.editor.editAtWith(edit.row, eventPoint.position.x, eventPoint.position.y,
                                                                 persistentTap.point.modifiers)
    }
    TapHandler {
        enabled: !edit.active && edit.persistent && Theme.touchUi
        acceptedDevices: PointerDevice.TouchScreen
        onLongPressed: edit.editor.longPressText(edit.row)
    }
    HoverHandler {
        enabled: !edit.active && edit.persistent && !Theme.touchUi
        cursorShape: Qt.IBeamCursor
    }
    /// The word marked as misspelled under a tap.
    MapHolder { id: misspelling }
}
