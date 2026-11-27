pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Brulion

// AtramentSelfTest — the editor's own self-test (EditorPlayground's `selftest`: its outline and
// keys parts) against the page drawn by Atrament (docs/ATRAMENT.md M2: "the editor's existing
// behaviour tests pass against it"), in the editor playground (`brulion --editor --editor-script
// atramenttest`; ctest tst_editor_qml_atrament, desktop, and tst_editor_qml_atrament_touch, a phone
// with --touch). The BlockEditor draws its rows with Atrament (`forceAtrament`: the device's
// setting is never written) and every key, every typed letter and every word an input method
// composes goes the way a user's does: through the window to the AtramentView, its edited block's
// field and PageView's keys — none of BlockEditor's key functions are called. Checked: a click / tap
// editing at the character, Enter splitting (a task continued), the text an input method sees,
// Tab / Shift+Tab, Backspace removing a task's box then merging, the page's undo and redo,
// Ctrl+Enter, ↑/↓ across blocks, Alt+↑/↓, the `[[` / `#` suggestions under Atrament's caret
// (accepting, "Create page", tags written #[[…]]), Esc selecting the block, Enter editing the
// selection, a fold hiding the rows, Enter at the end of a zoomed paragraph and undoing it, a
// pasted list becoming blocks, no-break spaces kept, a composed word kept out of the page until
// it is committed (Backspace and Enter while composing, completions after a composed word), typing
// into a block scrolled far away, a zoom's end keeping the edited block in view; document mode's
// keys (Enter leaving a list, Backspace removing a marker, Tab only for list items) and its toggle.
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property ApplicationWindow appWindow
    readonly property EditorPlayground window: test.appWindow as EditorPlayground
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property int r: -1
    property int realRow: -1
    property int pasteRow: -1
    property int farRow: -1
    property int zoomRow: -1
    property int countBefore: 0
    property string textBefore: ""

    function check(ok: bool, what: string): void { window.check(ok, what) }
    function info(row: int): DevBlock { return window.info(row) }
    function rowOf(prefix: string): int { return window.rowOf(prefix) }
    /// The AtramentView drawing the page (null before it is made).
    function view(): Item { return editor.atramentPage }
    function editRow(): int {
        const v = test.view()
        return v ? QmlCall.integer(QmlCall.get(v, "editRow")) : -2
    }
    function editText(): string {
        const v = test.view()
        return v ? QmlCall.text(QmlCall.get(v, "editText")) : ""
    }
    function editCursor(): int {
        const v = test.view()
        return v ? QmlCall.integer(QmlCall.get(v, "editCursor")) : -2
    }
    /// `row` is edited in the page drawn by Atrament, which has the keyboard.
    function editing(row: int): bool {
        const at = row
        const v = test.view()
        return v !== null && page.editingRow === at && test.editRow() === at && v.activeFocus
    }
    function rowTop(row: int): real {
        const at = row
        const v = test.view()
        return v ? QmlCall.number(QmlCall.call1(v, "rowTop", at)) : -1
    }
    function rowHeight(row: int): real {
        const at = row
        const v = test.view()
        return v ? QmlCall.number(QmlCall.call1(v, "rowHeight", at)) : -1
    }
    /// The edited block's caret is inside the view (a block taller than its caret's line may
    /// stay partly scrolled away, as InlineEditor's ensureVisible() leaves it).
    function caretInView(): bool {
        const v = test.view() as AtramentView
        return v !== null && v.editCaretRect.height > 0 && v.editCaretRect.y >= -0.5
               && v.editCaretRect.y + v.editCaretRect.height <= v.height + 0.5
    }
    /// The zoom's breadcrumb arrow in the page's header (a QML item hosted over the list), or null.
    function breadcrumbArrow(): Item {
        const v = test.view()
        const header = v ? QmlCall.object(v, "header") as Item : null
        return header ? DevTest.findWhere(header, "iconName", "arrow-left", true) : null
    }
    /// The row is laid out and wholly inside the view.
    function rowInView(row: int): bool {
        const at = row
        const v = test.view()
        const top = test.rowTop(at)
        const height = test.rowHeight(at)
        return v !== null && height > 0 && top >= -0.5 && top + height <= v.height + 0.5
    }
    /// A real key press through the window (to the focused item: the AtramentView).
    function key(k: int, modifiers: int): void {
        const code = k
        const mods = modifiers
        EditorInput.sendKey(test.view(), code, mods, "")
    }
    function typeKeys(s: string): void {
        const text = s
        for (let i = 0; i < text.length; ++i)
            EditorInput.sendKey(test.view(), 0, 0, text[i])
    }
    /// An input method's event: `preedit` composed, `commit` committed.
    function compose(preedit: string, commit: string): void {
        const p = preedit
        const c = commit
        EditorInput.sendInputMethod(test.view(), p, c)
    }
    /// What an input method is told about the text around the caret.
    function surrounding(): string {
        const v = test.view()
        return v ? QmlCall.text(EditorInput.inputMethodQuery(v, Qt.ImSurroundingText)) : "<none>"
    }
    function imCursor(): int {
        const v = test.view()
        return v ? QmlCall.integer(EditorInput.inputMethodQuery(v, Qt.ImCursorPosition)) : -1
    }
    /// The suggestion popup (BlockEditor's).
    function popup(): SuggestionPopup { return editor.suggestions as SuggestionPopup }
    /// How many suggestions the popup lists.
    function suggestionCount(): int { return QmlCall.integer(QmlCall.get(window.suggestionModel(), "count")) }
    function suggestionsShown(): bool {
        const p = test.popup()
        return p !== null && p.visible && test.suggestionCount() > 0
    }
    function suggestionsClosed(): bool {
        const p = test.popup()
        return p === null || !p.opened
    }

    function steps(): void {
        test.reading()
        test.outline()
        test.suggestions()
        test.structure()
        test.realKeys()
        test.composition()
        test.farAway()
        test.document()
    }
    function reading(): void {
        if (window.at()) {
            editor.forceAtrament = true
            test.r = test.rowOf("Wywieźć gruz")
        }
        if (window.at()) window.waitStep(test.view() !== null && test.r > 0 && test.rowTop(test.r) >= 0,
                                         "the page is drawn by Atrament: row " + test.r + " at " + test.rowTop(test.r))
        // A click / tap on a block's text edits it at the character under the pointer (the
        // block scrolled into view first: a phone's screen ends above it).
        if (window.at()) {
            const row = test.rowOf("Ekipa może")
            QmlCall.call5(test.view(), "revealArea", 0, test.rowTop(row), 1, test.rowHeight(row), 24)
        }
        if (window.at()) window.waitStep(test.rowInView(test.rowOf("Ekipa może")), "the block to click is in view")
        if (window.at()) {
            const row = test.rowOf("Ekipa może")
            const v = test.view()
            const x = QmlCall.number(QmlCall.get(v, "contentX")) + QmlCall.number(QmlCall.get(v, "contentWidth")) * 0.4
            window.tapPoint(v, x, test.rowTop(row) + Math.min(test.rowHeight(row) / 2, 14))
        }
        if (window.at()) window.waitStep(test.editing(test.rowOf("Ekipa może")) && test.editCursor() > 0
                                         && test.editCursor() < test.editText().length,
                                         "a click on a block's text edits it at the character: row " + page.editingRow
                                         + ", caret " + test.editCursor() + " of " + test.editText().length)
        if (window.at()) page.stopEditing()
    }
    function outline(): void {
        if (window.at()) editor.startEditing(test.r, -1)
        if (window.at()) window.waitStep(test.editing(test.r), "editing starts with the keyboard: " + page.editingRow + " / " + test.editRow())
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(test.editing(test.r + 1) && test.info(test.r + 1).taskStatus === "todo",
                                         "Enter splits, the new block continues the task and has the keyboard: "
                                         + page.editingRow + " " + test.info(test.r + 1).taskStatus)
        // The keyboard capitalises a sentence's start from the text around the caret: the hidden
        // task marker is no part of it.
        if (window.at()) test.check(test.surrounding() === "" && test.imCursor() === 0,
                                    "a new task block shows the keyboard an empty text: \"" + test.surrounding() + "\" at " + test.imCursor())
        if (window.at()) test.typeKeys("Nowe zadanie ąę")
        if (window.at()) test.check(test.info(test.r + 1).plainText === "Nowe zadanie ąę", "typing reaches the model: " + test.info(test.r + 1).plainText)
        if (window.at()) test.check(test.surrounding() === "Nowe zadanie ąę",
                                    "the keyboard sees the task's text without its marker: " + test.surrounding())
        if (window.at()) test.key(Qt.Key_Tab, Qt.NoModifier)
        if (window.at()) test.check(test.info(test.r + 1).depth === test.info(test.r).depth + 1 && test.editing(test.r + 1), "Tab indents, the keyboard stays")
        if (window.at()) test.key(Qt.Key_Backtab, Qt.ShiftModifier)
        if (window.at()) test.check(test.info(test.r + 1).depth === test.info(test.r).depth, "Shift+Tab outdents")
        if (window.at()) { test.key(Qt.Key_Home, Qt.NoModifier); test.key(Qt.Key_Backspace, Qt.NoModifier) }
        if (window.at()) test.check(test.info(test.r + 1).taskStatus === "" && page.editingRow === test.r + 1,
                                    "Backspace at the start removes the checkbox first")
        if (window.at()) { test.key(Qt.Key_Home, Qt.NoModifier); test.key(Qt.Key_Backspace, Qt.NoModifier) }
        if (window.at()) window.waitStep(test.editing(test.r) && Js.indexOf(test.info(test.r).plainText, "Nowe zadanie ąę") > 0,
                                         "Backspace again merges with the block above: " + test.info(test.r).plainText)
        if (window.at()) test.key(Qt.Key_Z, Qt.ControlModifier)
        if (window.at()) test.check(test.info(test.r + 1).plainText === "Nowe zadanie ąę", "Ctrl+Z undoes the merge")
        if (window.at()) test.key(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier)
        if (window.at()) window.waitStep(Js.indexOf(test.info(test.r).plainText, "Nowe zadanie ąę") > 0 && test.editing(test.r),
                                         "Ctrl+Shift+Z redoes, the merged block edited")
        if (window.at()) test.key(Qt.Key_Return, Qt.ControlModifier)
        if (window.at()) test.check(test.info(test.r).taskStatus === "doing", "Ctrl+Enter cycles the task: " + test.info(test.r).taskStatus)
        if (window.at()) { test.key(Qt.Key_Home, Qt.NoModifier); test.key(Qt.Key_Up, Qt.NoModifier) }
        if (window.at()) window.waitStep(test.editing(page.previousVisibleRow(test.r)), "Up on the first line goes to the block above: " + page.editingRow)
        if (window.at()) test.key(Qt.Key_Down, Qt.NoModifier)
        if (window.at()) window.waitStep(test.editing(test.r), "Down on the last line comes back: " + page.editingRow)
        if (window.at()) test.key(Qt.Key_Up, Qt.AltModifier)
        if (window.at()) window.waitStep(test.editing(test.r - 1) && Js.indexOf(test.info(test.r - 1).plainText, "Wywieźć") === 0,
                                         "Alt+Up moves the block, the keyboard follows: " + page.editingRow)
        if (window.at()) test.key(Qt.Key_Down, Qt.AltModifier)
        if (window.at()) window.waitStep(test.editing(test.r), "Alt+Down moves it back")
    }
    function suggestions(): void {
        if (window.at()) { test.key(Qt.Key_End, Qt.NoModifier); test.typeKeys(" [[Rem") }
        if (window.at()) window.waitStep(test.suggestionsShown(), "[[ opens the page suggestions under Atrament's caret")
        if (window.at()) {
            // Placed at the caret: the popup's top is below the caret's line, inside the window.
            const v = test.view()
            const caret = QmlCall.number(QmlCall.call1(v, "rowTop", test.r)) + 0
            const popupY = window.windowY(test.popup().contentItem, 0, 0)
            test.check(popupY > window.windowY(v, 0, caret), "the suggestions open below the edited block: " + popupY)
        }
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(Js.indexOf(test.info(test.r).plainText, "Remont kuchni") > 0 && test.suggestionsClosed(),
                                         "Enter accepts a suggestion: " + test.info(test.r).plainText)
        if (window.at()) test.typeKeys(" [[Remo")
        if (window.at()) {
            const popup = test.popup()
            window.waitStep(popup.visible && window.suggestionAt(0, "kind") === "create" && window.suggestionAt(0, "title") === "Remo"
                            && test.suggestionCount() > 1 && popup.currentIndex === 1,
                            "Create page first, the first page highlighted: " + popup.currentIndex)
        }
        if (window.at()) test.key(Qt.Key_Up, Qt.NoModifier)
        if (window.at()) test.check(test.popup().currentIndex === 0 && test.editing(test.r),
                                    "Up reaches Create page (the block keeps its caret): " + test.popup().currentIndex)
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(Js.indexOf(test.editText(), "[[Remont kuchni]] [[Remo]]") > 0 && test.suggestionsClosed(),
                                         "Up + Enter links the new page: " + test.editText())
        // `#` offers pages next to the tags; a name with a space is written #[[…]].
        if (window.at()) test.typeKeys(" #remont")
        if (window.at()) window.waitStep(test.suggestionsShown() && DevTest.valueAt(test.popup().trigger, "mode") === "tags"
                                         && window.suggestionAt(0, "icon") === "file-text",
                                         "# offers pages next to the tags")
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(Js.indexOf(test.editText(), " #[[Remont ") > 0 && test.suggestionsClosed(),
                                         "a page accepted after # is written #[[…]]: " + test.editText())
    }
    function structure(): void {
        if (window.at()) test.key(Qt.Key_Escape, Qt.NoModifier)
        if (window.at()) window.waitStep(page.editingRow === -1 && page.selectedRow === test.r && test.editRow() === -1,
                                         "Esc stops editing and selects the block")
        if (window.at()) editor.handleSelectionKeyCode(Qt.Key_Down, Qt.NoModifier)
        if (window.at()) test.check(page.selectedRow === page.nextVisibleRow(test.r), "Down moves the selection")
        if (window.at()) editor.handleSelectionKeyCode(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(test.editing(page.nextVisibleRow(test.r)), "Enter edits the selected block")
        if (window.at()) { page.stopEditing(); page.toggleFold(test.rowOf("Przygotowanie")) }
        if (window.at()) window.waitStep(!test.info(test.r).visible && test.rowTop(test.r) < 0, "folding hides the children: " + test.rowTop(test.r))
        if (window.at()) page.toggleFold(test.rowOf("Przygotowanie"))
        if (window.at()) window.waitStep(test.info(test.r).visible && test.rowTop(test.r) >= 0, "unfolding shows them again")
        // Enter at the end of a zoomed paragraph: the zoom ends and the new block below it is
        // edited, in view; Ctrl+Z zooms back in, Ctrl+Shift+Z ends the zoom again.
        if (window.at()) {
            test.textBefore = page.sourceText
            test.zoomRow = test.rowOf("Plan remontu")
            page.zoomInto(test.zoomRow)
            editor.startEditing(test.zoomRow, -1)
        }
        if (window.at()) window.waitStep(test.zoomRow >= 0 && page.zoomBlock === test.zoomRow && test.editing(test.zoomRow),
                                         "a zoomed paragraph is edited: " + test.zoomRow)
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(page.zoomBlock < 0 && test.editing(test.zoomRow + 1) && test.rowInView(test.zoomRow + 1),
                                         "Enter at the end of a zoomed paragraph ends the zoom and edits the new block below it, in view: zoom "
                                         + page.zoomBlock + ", row " + page.editingRow + ", y " + test.rowTop(test.zoomRow + 1))
        if (window.at()) test.key(Qt.Key_Z, Qt.ControlModifier)
        if (window.at()) window.waitStep(page.zoomBlock === test.zoomRow && test.editing(test.zoomRow) && page.sourceText === test.textBefore,
                                         "Ctrl+Z zooms back into the paragraph: zoom " + page.zoomBlock + ", row " + page.editingRow)
        if (window.at()) test.key(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier)
        if (window.at()) window.waitStep(page.zoomBlock < 0 && test.editing(test.zoomRow + 1),
                                         "Ctrl+Shift+Z ends the zoom again: zoom " + page.zoomBlock + ", row " + page.editingRow)
        if (window.at()) test.typeKeys("Źdźbło")
        if (window.at()) test.check(test.info(test.zoomRow + 1).plainText === "Źdźbło", "typing goes into the new block: " + test.info(test.zoomRow + 1).plainText)
        if (window.at()) test.key(Qt.Key_Z, Qt.ControlModifier) // the typing
        if (window.at()) test.key(Qt.Key_Z, Qt.ControlModifier) // Enter
        if (window.at()) window.waitStep(page.sourceText === test.textBefore && page.zoomBlock === test.zoomRow,
                                         "two Ctrl+Z undo the typing and Enter, back in the zoom: zoom " + page.zoomBlock)
        if (window.at()) { page.zoomTo(-1); page.stopEditing() }
        // A pasted list becomes blocks, the caret in the last one.
        if (window.at()) {
            test.pasteRow = page.insertBlockAfter(test.rowOf("Ekipa może"), "")
            test.countBefore = page.blocks.count
        }
        if (window.at()) window.waitStep(test.editing(test.pasteRow), "a new block is edited")
        if (window.at()) QmlCall.call2(test.view(), "insertAtCaret", "- [ ] pierwsze\n- drugie", false)
        if (window.at()) window.waitStep(page.blocks.count === test.countBefore + 1 && test.info(test.pasteRow).taskStatus === "todo"
                                         && test.info(test.pasteRow + 1).plainText === "drugie" && test.editing(test.pasteRow + 1),
                                         "a pasted list becomes blocks, tasks kept, the caret in the last one: " + page.blocks.count
                                         + " (was " + test.countBefore + "), row " + page.editingRow)
    }
    function realKeys(): void {
        if (window.at()) test.realRow = page.insertBlockAfter(test.rowOf("Ekipa może"), "")
        if (window.at()) window.waitStep(test.editing(test.realRow), "the new block has the keyboard")
        if (window.at()) test.typeKeys("Żółw ąę")
        if (window.at()) test.check(test.info(test.realRow).plainText === "Żółw ąę", "typed keys reach the block: " + test.info(test.realRow).plainText)
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(test.editing(test.realRow + 1), "Enter splits and the new block has the keyboard")
        if (window.at()) test.typeKeys("x")
        if (window.at()) { test.key(Qt.Key_Backspace, Qt.NoModifier); test.key(Qt.Key_Backspace, Qt.NoModifier) }
        if (window.at()) window.waitStep(test.editing(test.realRow) && test.info(test.realRow).plainText === "Żółw ąę",
                                         "Backspace deletes, then merges: " + test.info(test.realRow).plainText)
        // No-break spaces survive editing.
        if (window.at()) { page.setBody(test.realRow, "w\u00a0domu i\u00a0w\u00a0pracy"); editor.startEditing(test.realRow, -1) }
        if (window.at()) window.waitStep(test.editing(test.realRow) && Js.indexOf(test.editText(), "pracy") > 0, "the block's new text is edited")
        if (window.at()) { test.key(Qt.Key_End, Qt.NoModifier); test.typeKeys("!") }
        if (window.at()) test.check(Js.indexOf(page.sourceText, "w\u00a0domu i\u00a0w\u00a0pracy!") >= 0, "no-break spaces are kept when typing")
    }
    function composition(): void {
        // A word an input method composes stays the field's until it is committed: the page
        // never sees half a word.
        if (window.at()) {
            test.realRow = page.insertBlockAfter(test.realRow, "")
            test.countBefore = page.blocks.count
        }
        if (window.at()) window.waitStep(test.editing(test.realRow) && test.surrounding() === "" && test.imCursor() === 0,
                                         "a new block shows the keyboard an empty text")
        if (window.at()) test.compose("kot", "")
        if (window.at()) test.check(test.info(test.realRow).plainText === "" && Js.indexOf(test.surrounding(), "kot") < 0,
                                    "a composed word is the field's, not yet the page's: \"" + test.info(test.realRow).plainText + "\"")
        if (window.at()) test.key(Qt.Key_Backspace, Qt.NoModifier)
        if (window.at()) test.check(page.blocks.count === test.countBefore && page.editingRow === test.realRow
                                    && test.info(test.realRow).plainText === "ko",
                                    "Backspace while composing edits the word, never merges: \"" + test.info(test.realRow).plainText + "\"")
        if (window.at()) test.compose("pies", "")
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(test.info(test.realRow).plainText === "kopies" && test.editing(test.realRow + 1),
                                         "Enter while composing keeps the word and splits after it: " + test.info(test.realRow).plainText)
        if (window.at()) test.compose("", "[[")
        if (window.at()) test.compose("Rem", "")
        if (window.at()) window.waitStep(test.suggestionsShown(), "the suggestions follow a composed word")
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(Js.indexOf(test.info(test.realRow + 1).plainText, "Remont kuchni") >= 0,
                                         "accepting a suggestion commits the composed word: " + test.info(test.realRow + 1).plainText)
        if (window.at()) page.stopEditing()
    }
    function farAway(): void {
        // Typing into a block scrolled far away brings it back; a lone modifier does not.
        if (window.at()) {
            let md = ""
            for (let i = 0; i < 150; ++i)
                md += "- Wiersz " + i + " źdźbło\n"
            page.insertMarkdown(page.lastVisibleRow(), md)
        }
        if (window.at()) {
            test.farRow = test.rowOf("Kupić płytki")
            editor.startEditing(test.farRow, -1)
        }
        if (window.at()) window.waitStep(test.editing(test.farRow) && test.rowInView(test.farRow), "a block at the page's top is edited")
        if (window.at()) QmlCall.call5(test.view(), "revealArea", 0, 100000, 1, 1, 0)
        if (window.at()) window.waitStep(!test.rowInView(test.farRow) && test.rowInView(page.lastVisibleRow()),
                                         "the page scrolls to its end, the edited block far away: " + test.rowTop(test.farRow))
        if (window.at()) test.key(Qt.Key_Control, Qt.ControlModifier)
        if (window.at()) test.check(!test.rowInView(test.farRow), "a lone modifier key does not scroll back")
        if (window.at()) test.typeKeys("Q")
        if (window.at()) window.waitStep(Js.indexOf(test.info(test.farRow).plainText, "Q") > 0 && test.editing(test.farRow)
                                         && test.caretInView(),
                                         "typing goes into the block scrolled away and brings its caret back: " + test.info(test.farRow).plainText
                                         + ", y " + test.rowTop(test.farRow))
        // Zooming out while a block far down the page is edited keeps it in view.
        if (window.at()) {
            test.zoomRow = test.rowOf("Wiersz 140")
            page.zoomInto(test.zoomRow)
            editor.startEditing(test.zoomRow, -1)
        }
        if (window.at()) window.waitStep(page.zoomBlock === test.zoomRow && test.editing(test.zoomRow) && test.rowInView(test.zoomRow),
                                         "a zoomed block far down the page is edited: " + test.zoomRow)
        if (window.at()) page.zoomOut()
        if (window.at()) window.waitStep(page.zoomBlock < 0 && test.editing(test.zoomRow) && test.rowInView(test.zoomRow),
                                         "zooming out keeps the edited block in view: y " + test.rowTop(test.zoomRow))
        if (window.at()) test.typeKeys("ż")
        if (window.at()) test.check(Js.indexOf(test.info(test.zoomRow).plainText, "źdźbłoż") > 0, "and it takes the keys: " + test.info(test.zoomRow).plainText)
        // The breadcrumb's arrow (the page's header: a QML item hosted over the list) clicked while
        // a child of a folded zoom root is edited: the zoom ends, the fold opens over the child,
        // which stays edited and in view, and the next key typed goes into it.
        if (window.at()) {
            page.stopEditing()
            test.textBefore = page.sourceText
            test.zoomRow = test.rowOf("Wiersz 145")
            page.zoomInto(test.zoomRow)
            editor.startEditing(test.zoomRow, -1)
        }
        if (window.at()) window.waitStep(test.editing(test.zoomRow), "a zoomed block is edited")
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(test.editing(test.zoomRow + 1), "Enter in the zoom root adds a child")
        if (window.at()) test.typeKeys("Dziecko ąę")
        if (window.at()) page.setFolded(test.zoomRow, true)
        if (window.at()) {
            const folded = test.info(test.zoomRow).collapsed
            const shown = test.info(test.zoomRow + 1).visible
            const text = test.info(test.zoomRow + 1).plainText
            test.check(page.zoomBlock === test.zoomRow && folded && page.editingRow === test.zoomRow + 1 && shown
                       && text === "Dziecko ąę", "a child of a folded zoom root is edited")
        }
        if (window.at()) {
            const arrow = test.breadcrumbArrow()
            test.check(arrow !== null, "the breadcrumb's arrow is in the page's header")
            if (arrow)
                window.tapPoint(arrow, arrow.width / 2, arrow.height / 2)
        }
        if (window.at()) {
            const zoom = page.zoomBlock
            const row = page.editingRow
            const shown = test.info(row).visible
            const folded = test.info(test.zoomRow).collapsed
            const ok = zoom < 0 && test.editing(test.zoomRow + 1) && shown && !folded && test.caretInView()
            window.waitStep(ok, "the breadcrumb's arrow clicked: the zoom ends, the edited child stays shown, edited and in view: zoom "
                            + zoom + ", row " + row)
        }
        if (window.at()) {
            const target = window.activeFocusItem
            EditorInput.sendKey(target, 0, 0, "ż")
        }
        if (window.at()) {
            const text = test.info(test.zoomRow + 1).plainText
            const focus = "" + window.activeFocusItem
            const ok = text === "Dziecko ąęż" && test.editing(test.zoomRow + 1)
            test.check(ok, "a key typed after the click goes into the edited child: " + text + " (focus " + focus + ")")
        }
        if (window.at()) {
            for (let i = 0; i < 6 && page.sourceText !== test.textBefore; ++i)
                page.undo()
            test.check(page.sourceText === test.textBefore, "undo takes the child away again")
            page.zoomTo(-1)
            page.stopEditing()
        }
    }

    // Document mode (selftest's document part): list items, paragraphs, Enter leaving a list,
    // Backspace removing a marker, Tab only for list items, ↑/↓ as in a text, the mode's toggle.
    function document(): void {
        if (window.at()) { page.stopEditing(); page.save(); page.path = host.documentPath }
        if (window.at()) {
            const heading = test.rowOf("Plan")
            const top = test.rowTop(heading)
            const mode = page.mode
            window.waitStep(page.loaded && mode === "document" && editor.documentMode && heading >= 0 && top >= 0,
                            "the document sample opens as a document drawn by Atrament: " + mode + ", heading " + heading + " at " + top)
        }
        if (window.at()) editor.startEditing(test.rowOf("Niedziela"), -1)
        if (window.at()) window.waitStep(test.editing(test.rowOf("Niedziela")), "a list item is edited")
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(test.editing(test.rowOf("Niedziela") + 1) && test.info(page.editingRow).kind === "listItem",
                                         "Enter in a list item adds a list item")
        if (window.at()) test.typeKeys("Powrót ąę")
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) test.realRow = page.editingRow
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(test.editing(test.realRow) && test.info(test.realRow).kind === "paragraph",
                                         "Enter on an empty list item leaves the list: " + test.info(test.realRow).kind)
        if (window.at()) test.typeKeys("Akapit po liście")
        if (window.at()) test.key(Qt.Key_Tab, Qt.NoModifier)
        if (window.at()) test.check(test.info(test.realRow).kind === "paragraph" && test.info(test.realRow).depth === 0
                                    && test.editing(test.realRow), "Tab in a paragraph does nothing")
        if (window.at()) editor.startEditing(test.rowOf("Powrót ąę"), 0)
        if (window.at()) window.waitStep(test.editing(test.rowOf("Powrót ąę")) && test.editCursor() === 0, "a list item is edited at its start")
        if (window.at()) test.key(Qt.Key_Backspace, Qt.NoModifier)
        if (window.at()) test.check(test.info(test.rowOf("Powrót ąę")).kind === "paragraph" && page.editingRow === test.rowOf("Powrót ąę"),
                                    "Backspace at the start of a list item removes its marker")
        if (window.at()) test.key(Qt.Key_Z, Qt.ControlModifier)
        if (window.at()) test.check(test.info(test.rowOf("Powrót ąę")).kind === "listItem", "Ctrl+Z brings the list item back")
        if (window.at()) editor.startEditing(test.rowOf("Z powrotem bryczką"), 0)
        if (window.at()) window.waitStep(test.editing(test.rowOf("Z powrotem bryczką")), "a nested list item is edited")
        if (window.at()) test.key(Qt.Key_Tab, Qt.NoModifier)
        if (window.at()) test.check(test.info(test.rowOf("Z powrotem bryczką")).depth === 2, "Tab indents a list item")
        if (window.at()) test.key(Qt.Key_Backtab, Qt.ShiftModifier)
        if (window.at()) test.check(test.info(test.rowOf("Z powrotem bryczką")).depth === 1, "Shift+Tab outdents it")
        if (window.at()) editor.startEditing(test.rowOf("Pierwszego dnia"), 0)
        if (window.at()) window.waitStep(test.editing(test.rowOf("Pierwszego dnia")), "a paragraph is edited")
        if (window.at()) test.key(Qt.Key_Up, Qt.NoModifier)
        if (window.at()) window.waitStep(test.editing(test.rowOf("Plan")), "Up goes to the previous block like in a text: " + page.editingRow)
        if (window.at()) test.key(Qt.Key_Down, Qt.NoModifier)
        if (window.at()) window.waitStep(test.editing(test.rowOf("Pierwszego dnia")), "Down comes back")
        if (window.at()) { page.stopEditing(); page.toggleMode() }
        if (window.at()) window.waitStep(page.mode === "outline" && editor.gutter > 0 && Js.indexOf(host.file(page.path), "brulion: outline") > 0
                                         && test.rowTop(test.rowOf("Niedziela")) >= 0,
                                         "the toggle switches to an outline (drawn again) and writes only the key")
        if (window.at()) page.toggleMode()
        if (window.at()) window.waitStep(page.mode === "document" && Js.indexOf(host.file(page.path), "brulion: document") > 0
                                         && Js.indexOf(host.file(page.path), "Akapit po liście") > 0,
                                         "and back to a document")
    }

    Component.onCompleted: window.runSteps("atramenttest")
}
