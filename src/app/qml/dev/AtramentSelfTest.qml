pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import Brulion

// AtramentSelfTest — the editor's own self-test (EditorPlayground's `selftest`: its outline and
// keys parts) against the page drawn by Atrament (docs/ATRAMENT.md M2: "the editor's existing
// behaviour tests pass against it"), in the editor playground (`brulion --editor --editor-script
// atramenttest`; ctest tst_editor_qml_atrament, desktop, and tst_editor_qml_atrament_touch, a phone
// with --touch). The BlockEditor draws its rows with Atrament (`--atrament-editor`: the device's
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
// keys (Enter leaving a list, Backspace removing a marker, Tab only for list items) and its toggle;
// reading mode's taps (the toast and its "Edit"); "/" commands setting a task's status; the
// gestures on chips, tags, links and task boxes (also in reading mode); the edited text's menu (a
// right-click in it: Copy, Cut, Paste, Select all, Highlight…, a misspelled word's corrections; on
// a phone what Android's selection handles and floating toolbar give it through the input method);
// the menus of chips and links inside an embedded block and a query result; a link's preview while
// the mouse pointer rests on it.
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
    // (Cast only once it exists: the popup's type is loaded with it.)
    function popup(): SuggestionPopup { return editor.suggestions !== null ? editor.suggestions as SuggestionPopup : null }
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
        test.readingMode()
        test.slashCommands()
        test.chips()
        test.sources()
        test.spelling()
        test.taskMenu()
        if (window.touchRun)
            test.touchTextTools()
        else
            test.textMenus()
        test.embedMenus()
    }
    function reading(): void {
        if (window.at()) {
            test.check(DevTools.atramentEditor, "the app was started with --atrament-editor")
            test.r = test.rowOf("Wywieźć gruz")
        }
        if (window.at()) window.waitStep(test.view() !== null && test.r > 0 && test.rowTop(test.r) >= 0,
                                         "the page is drawn by Atrament: row " + test.r + " at " + test.rowTop(test.r))
        // A click / tap on a block's text edits it at the character under the pointer (the
        // block scrolled into view first: a phone's screen ends above it).
        if (window.at()) {
            const row = test.rowOf("Ekipa może")
            editor.ensureRowVisible(row)
        }
        if (window.at()) {
            const row = test.rowOf("Ekipa może")
            const top = test.rowTop(row)
            const height = test.rowHeight(row)
            const view = test.view().height
            window.waitStep(test.rowInView(row), "the block to click is in view: " + top + " + " + height + " in " + view)
        }
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
        // The selection's keys are the Atrament page's (pageselection.cpp), real keys through the window.
        if (window.at()) test.key(Qt.Key_Down, Qt.NoModifier)
        if (window.at()) window.waitStep(page.selectedRow === page.nextVisibleRow(test.r), "Down moves the selection")
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
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

    // Reading mode (ReadingSelfTest's taps): a tap on a block's text edits nothing and the toast
    // says why, its "Edit" leaving the mode; off again a tap edits; on again while editing, the
    // editing ends with the text kept and no toast.
    function tapRow(row: int): void {
        const at = row
        const v = test.view()
        const x = QmlCall.number(QmlCall.get(v, "contentX")) + QmlCall.number(QmlCall.get(v, "contentWidth")) * 0.3
        const y = test.rowTop(at) + Math.min(test.rowHeight(at) / 2, 14)
        window.tapPoint(v, x, y)
    }
    property EditorToastAction leave: null
    // The playground's host part: "Edit" in the toast leaves reading mode (the app: AppSettings).
    property Connections leaveWatch: Connections {
        target: test.editor
        function onReadOnlyLeaveRequested() { test.page.readOnly = false }
    }
    readonly property string readingContent: "- [ ] Zadanie źdźbło\n- Zwykły akapit do czytania, żółw i łódź\n- Ostatni blok\n"
    function readingMode(): void {
        if (window.at()) {
            page.stopEditing()
            page.save()
            host.setFile("Do czytania.md", test.readingContent)
            page.path = "Do czytania.md"
        }
        if (window.at()) window.waitStep(page.loaded && test.rowOf("Zwykły") >= 0 && test.rowInView(test.rowOf("Zwykły")),
                                         "the page to read is drawn")
        if (window.at()) {
            page.readOnly = true
            window.lastNotice = ""
            test.check(editor.readOnly, "the editor reads the controller's reading mode")
        }
        if (window.at()) test.tapRow(test.rowOf("Zwykły"))
        if (window.at()) window.waitStep(window.lastNotice === editor.readOnlyReason && window.lastNotice !== "",
                                         "a tap on text says reading mode is on: " + window.lastNotice)
        if (window.at()) {
            test.check(page.editingRow < 0 && test.editRow() < 0, "and no block is edited")
            test.leave = window.lastNoticeAction
            test.check(test.leave !== null && test.leave.kind === "leaveReadOnly", "the toast's button leaves reading mode")
        }
        if (window.at()) {
            if (test.leave)
                test.leave.run()
            test.check(!page.readOnly && !editor.readOnly, "“Edit” in the toast leaves reading mode")
        }
        if (window.at()) test.tapRow(test.rowOf("Zwykły"))
        if (window.at()) window.waitStep(test.editing(test.rowOf("Zwykły")), "a tap edits the block again")
        if (window.at()) {
            window.lastNotice = ""
            page.readOnly = true
            test.check(page.editingRow < 0, "reading mode ends the editing")
        }
        if (window.at()) window.waitStep(test.editRow() < 0, "the field goes")
        if (window.at()) {
            test.check(window.lastNotice === "", "and no toast says an edit was refused: " + window.lastNotice)
            test.check(host.file("Do czytania.md") === test.readingContent, "the file is still as written")
            page.readOnly = false
        }
    }

    // "/" commands typed with real keys (the taskstatus test's): the status set, the command's
    // text gone, the caret kept, one undo step; a paragraph becoming a task; a Logseq keyword.
    property int taskRow: -1
    function slashCommands(): void {
        if (window.at()) {
            page.stopEditing()
            page.save()
            host.setFile("Zadania.md", "- Ekipa może zacząć\n")
            page.path = "Zadania.md"
        }
        if (window.at()) window.waitStep(page.loaded && test.rowOf("Ekipa może") === 0, "the page of tasks opens")
        if (window.at()) test.taskRow = page.insertBlockAfter(test.rowOf("Ekipa może"), "")
        if (window.at()) window.waitStep(test.editing(test.taskRow), "a new block is edited")
        if (window.at()) test.typeKeys("Zadzwonić do Żanety /czeka")
        if (window.at()) window.waitStep(test.suggestionsShown() && window.suggestionAt(0, "command") === "taskWaiting",
                                         "“/czeka” offers “Zadanie: oczekujące” first")
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) {
            const status = test.info(test.taskRow).taskStatus
            const text = test.editText()
            const caret = test.editCursor()
            const ok = status === "deferred" && text === "Zadzwonić do Żanety " && caret === 20 && test.suggestionsClosed()
            window.waitStep(ok, "the command sets the status, its text goes, the caret stays: \"" + text + "\" at " + caret)
        }
        if (window.at()) page.undo()
        if (window.at()) {
            const text = test.editText()
            const ok = text === "Zadzwonić do Żanety /czeka" && test.info(test.taskRow).taskStatus === ""
            window.waitStep(ok, "one undo step brings the command text back: " + text)
        }
        if (window.at()) {
            test.key(Qt.Key_End, Qt.NoModifier)
            for (let i = 0; i < 5; ++i)
                test.key(Qt.Key_Backspace, Qt.NoModifier)
            test.typeKeys("zrob")
        }
        if (window.at()) window.waitStep(test.suggestionsShown() && window.suggestionAt(0, "command") === "task",
                                         "“/zrob” offers the statuses (to do first)")
        if (window.at()) {
            let done = -1
            const count = test.suggestionCount()
            for (let i = 0; i < count; ++i) {
                if (window.suggestionAt(i, "command") === "taskDone")
                    done = i
            }
            test.check(done > 0, "“Zadanie: zrobione” is listed")
            for (let i = 0; i < done; ++i)
                test.key(Qt.Key_Down, Qt.NoModifier)
            test.key(Qt.Key_Return, Qt.NoModifier)
        }
        if (window.at()) {
            const ok = test.info(test.taskRow).taskStatus === "done"
                       && Js.indexOf(host.file("Zadania.md"), "- [x] Zadzwonić do Żanety ✅ 2026-10-15") >= 0
            window.waitStep(ok, "“Zadanie: zrobione” completes the task: " + host.file("Zadania.md"))
        }
        if (window.at()) test.typeKeys(" /bez")
        if (window.at()) window.waitStep(test.suggestionsShown() && window.suggestionAt(0, "command") === "taskNone",
                                         "“/bez” offers “Zadanie: bez statusu”")
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) {
            const ok = test.info(test.taskRow).taskStatus === "" && Js.indexOf(host.file("Zadania.md"), "- Zadzwonić do Żanety") >= 0
                       && Js.indexOf(host.file("Zadania.md"), "✅") < 0
            window.waitStep(ok, "“Zadanie: bez statusu” removes the checkbox and its date: " + host.file("Zadania.md"))
        }
        test.slashCommandsOnOtherBlocks()
    }
    // (Its second half: one function took qmlcachegen over its 30 s on a loaded machine.)
    function slashCommandsOnOtherBlocks(): void {
        if (window.at()) {
            page.stopEditing()
            test.taskRow = page.insertMarkdown(page.lastVisibleRow(), "Akapit o płytkach")
        }
        if (window.at()) editor.startEditing(test.rowOf("Akapit o płytkach"), -1)
        if (window.at()) window.waitStep(test.editing(test.rowOf("Akapit o płytkach")), "a paragraph is edited")
        if (window.at()) test.typeKeys(" /zadanie")
        if (window.at()) window.waitStep(test.suggestionsShown() && window.suggestionAt(0, "command") === "task",
                                         "“/zadanie” offers “Zadanie: do zrobienia”")
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) {
            const r = test.rowOf("Akapit o płytkach")
            const ok = r >= 0 && test.info(r).taskStatus === "todo" && test.info(r).kind === "listItem"
                       && Js.indexOf(host.file("Zadania.md"), "- [ ] Akapit o płytkach") >= 0
            window.waitStep(ok, "a paragraph becomes a task: " + host.file("Zadania.md"))
        }
        if (window.at()) {
            page.stopEditing()
            test.taskRow = page.insertBlockAfter(test.rowOf("Ekipa może"), "TODO Odebrać kafelki")
        }
        if (window.at()) window.waitStep(test.editing(test.taskRow) && test.info(test.taskRow).taskStatus === "todo", "a Logseq TODO is a task")
        if (window.at()) { test.key(Qt.Key_End, Qt.NoModifier); test.typeKeys(" /toku") }
        if (window.at()) window.waitStep(test.suggestionsShown() && window.suggestionAt(0, "command") === "taskDoing",
                                         "“/toku” offers “Zadanie: w toku”")
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) {
            const ok = test.info(test.taskRow).taskStatus === "doing" && Js.indexOf(host.file("Zadania.md"), "- DOING Odebrać kafelki") >= 0
            window.waitStep(ok, "the Logseq keyword follows: " + host.file("Zadania.md"))
        }
        if (window.at()) page.stopEditing()
    }

    // Links, chips and task boxes in drawn rows (the datechips and reading tests' gestures): a
    // right-click or a finger held on a 📅 chip opens DateChipMenu — never the block menu, no
    // block selected —; a plain click / tap opens the date dialog on it, editing nothing; the
    // same gesture on a #tag opens the link menu; a click on a [[link]] opens its page; the task's
    // box toggles it (one undo step) — and in reading mode the box changes nothing while the link
    // still opens.
    property int navigations: 0
    property int navigationsBefore: 0
    property Connections navigationWatch: Connections {
        target: test.host
        function onNavigated(route: var, newPane: bool) { test.navigations += 1 }
    }
    // (BMenu, as the editor declares them: DateChipMenu and LinkMenu are loaded when first used, and
    // an `as` to them before that meets a type without its meta-object — UBSan in CI's build.)
    function dateMenu(): BMenu { return editor.dateMenu }
    function dateMenuOpen(): bool { return test.dateMenu() !== null && test.dateMenu().opened }
    function blockMenu(): BMenu { return editor.blockMenu }
    function blockMenuOpen(): bool { return test.blockMenu() !== null && test.blockMenu().opened }
    function linkMenu(): BMenu { return editor.linkMenu }
    function linkMenuOpen(): bool { return test.linkMenu() !== null && test.linkMenu().opened }
    function dialogs(): EditorDialogs { return editor.dialogsNow() as EditorDialogs }
    function dateDialog(): BDialog {
        const d = test.dialogs()
        return d ? d.dateDialog : null
    }
    function dateDialogOpen(): bool { return test.dateDialog() !== null && test.dateDialog().opened }
    property point spot: Qt.point(-1, -1)
    /// A px of jitter for the hover's moves (each a move the window delivers).
    property int hoverJitter: 0
    /// Finds the link of `row` whose href has `hrefPart` (spot), false when it is not drawn.
    function findLink(row: int, hrefPart: string): bool {
        const at = row
        const part = hrefPart
        const v = test.view() as AtramentView
        if (!v)
            return false
        test.spot = v.linkPoint(at, part)
        return test.spot.x >= 0
    }
    function findTaskBox(row: int): bool {
        const at = row
        const v = test.view() as AtramentView
        if (!v)
            return false
        test.spot = v.taskBoxPoint(at)
        return test.spot.x >= 0
    }
    function tapSpot(): void { window.tapPoint(test.view(), test.spot.x, test.spot.y) }
    /// Steps: a right-click on the spot, or a finger held there until `opened` (then lifted).
    function menuGesture(what: string, opened: bool): void {
        const shown = opened
        if (!window.touchRun) {
            if (window.at())
                window.rightClick(test.view(), test.spot.x, test.spot.y)
            return
        }
        if (window.at())
            window.taskEvents.touchPress(test.view(), test.spot.x, test.spot.y)
        if (window.at())
            window.waiting(shown)
        if (window.at())
            window.taskEvents.touchRelease(test.view(), test.spot.x, test.spot.y)
    }
    readonly property string chipsContent: "- [ ] Kupić płytki 📅 2026-10-16 ⏫ #zakupy\n- Link do [[Inna strona]] tutaj\n"
    function chips(): void {
        if (window.at()) {
            page.stopEditing()
            page.save()
            host.setFile("Inna strona.md", "- tam\n")
            host.setFile("Terminy.md", test.chipsContent)
            page.path = "Terminy.md"
        }
        if (window.at()) window.waitStep(page.loaded && test.findLink(0, "date:") && test.findLink(1, "page:"),
                                         "the chips and links are drawn")
        if (window.at()) test.findLink(0, "date:")
        test.menuGesture("date", test.dateMenuOpen())
        if (window.at()) window.waitStep(test.dateMenuOpen() && !test.blockMenuOpen() && page.selectedRow < 0 && page.editingRow < 0,
                                         (window.touchRun ? "a finger held" : "a right-click") + " on a 📅 chip opens its menu, no block menu, nothing selected")
        if (window.at()) test.dateMenu().close()
        if (window.at()) window.waitStep(!test.dateMenuOpen(), "the chip's menu closes")
        if (window.at()) { test.findLink(0, "date:"); test.tapSpot() }
        if (window.at()) window.waitStep(test.dateDialogOpen() && page.editingRow < 0, "a click on the 📅 chip opens the date dialog, editing nothing")
        if (window.at()) test.dateDialog().close()
        if (window.at()) window.waitStep(!test.dateDialogOpen(), "the dialog closes")
        if (window.at()) test.findLink(0, "tag:")
        test.menuGesture("tag", test.linkMenuOpen())
        if (window.at()) window.waitStep(test.linkMenuOpen() && !test.blockMenuOpen() && page.editingRow < 0,
                                         "the same gesture on a #tag opens the link menu")
        if (window.at()) test.linkMenu().close()
        if (window.at()) window.waitStep(!test.linkMenuOpen(), "the link menu closes")
        // The mouse pointer resting on the [[link]]: its preview (LinkPreviews), closed when it leaves.
        if (!window.touchRun) {
            // (The move sent again while it waits: one that came while a menu was closing went to the
            // menu's overlay.)
            if (window.at()) {
                test.findLink(1, "page:")
                test.hoverJitter = 1 - test.hoverJitter
                if (!LinkPreviews.shown)
                    window.taskEvents.mouseMove(test.view(), test.spot.x + test.hoverJitter, test.spot.y, -1, Qt.NoButton, Qt.NoModifier)
                window.waitStep(LinkPreviews.shown && Js.indexOf(LinkPreviews.href, "Inna") >= 0,
                                "the mouse pointer resting on a [[link]] opens its preview: " + LinkPreviews.href)
            }
            if (window.at()) window.taskEvents.mouseMove(test.view(), test.spot.x, test.spot.y + 160, -1, Qt.NoButton, Qt.NoModifier)
            if (window.at()) window.waitStep(!LinkPreviews.shown, "and the preview closes when the pointer leaves the link")
        }
        if (window.at()) {
            test.navigationsBefore = test.navigations
            test.findLink(1, "page:")
            test.tapSpot()
        }
        if (window.at()) window.waitStep(test.navigations > test.navigationsBefore && page.editingRow < 0,
                                         "a click on a [[link]] opens its page and edits nothing")
        if (window.at()) { test.findTaskBox(0); test.tapSpot() }
        if (window.at()) window.waitStep(test.info(0).taskStatus === "done" && Js.indexOf(host.file("Terminy.md"), "- [x] Kupić płytki") === 0,
                                         "the task's box completes it: " + host.file("Terminy.md"))
        if (window.at()) page.undo()
        if (window.at()) window.waitStep(test.info(0).taskStatus === "todo", "one undo step opens it again")
        if (window.at()) {
            page.save()
            page.readOnly = true
        }
        if (window.at()) { test.findTaskBox(0); test.tapSpot() }
        if (window.at()) window.waitStep(true, "the box tapped in reading mode")
        if (window.at()) test.check(test.info(0).taskStatus === "todo" && host.file("Terminy.md") === test.chipsContent,
                                    "in reading mode the box changes nothing")
        if (window.at()) {
            test.navigationsBefore = test.navigations
            test.findLink(1, "page:")
            test.tapSpot()
        }
        if (window.at()) window.waitStep(test.navigations > test.navigationsBefore, "and a link still opens")
        if (window.at()) page.readOnly = false
    }

    // Code, a calculation: their sources take "[[" and "#" as text — no suggestions (their QML
    // views offer none); Enter is a line break there.
    function sources(): void {
        if (window.at()) {
            page.stopEditing()
            page.save()
            host.setFile("Źródła.md", "```luau\nlocal gęś = 1\n```\n\n```calc\ncena = 89\n```\n")
            page.path = "Źródła.md"
        }
        if (window.at()) window.waitStep(page.loaded && page.blocks.count === 2, "the page of sources opens")
        if (window.at()) editor.startEditing(0, -1)
        if (window.at()) window.waitStep(test.editing(0), "a code block is edited in the Atrament field")
        if (window.at()) { test.key(Qt.Key_End, Qt.NoModifier); test.typeKeys(" -- [[Rem #tag") }
        if (window.at()) window.waitStep(Js.indexOf(test.editText(), "[[Rem #tag") > 0, "the code takes the text")
        if (window.at()) test.check(test.suggestionsClosed() && !(test.popup() !== null && test.popup().visible),
                                    "and opens no suggestions")
        if (window.at()) test.key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(page.blocks.count === 2 && Js.indexOf(test.editText(), "\n") > 0,
                                         "Enter is a line break in the code block")
        if (window.at()) editor.startEditing(1, -1)
        if (window.at()) window.waitStep(test.editing(1), "a calculation is edited")
        if (window.at()) test.typeKeys(" [[x")
        if (window.at()) test.check(test.suggestionsClosed() && !(test.popup() !== null && test.popup().visible),
                                    "a calculation opens no suggestions either")
        if (window.at()) page.stopEditing()
    }

    // Spell checking in the Atrament field (SpellingSelfTest's correction, on word lists): the
    // misspelled word marked (not a known one, not in code, a link or a tag), a right-click on it
    // (a tap with a finger) opening the spelling's menu with its corrections first, a correction
    // replacing the word with the caret after it, one undo step bringing it back.
    // (The word lists of the spelling's and the text menu's steps.)
    readonly property list<string> polishWords: ["ala", "ma", "kota", "kot", "psa", "i", "zapisz", "jest", "fajny", "notatnik"]
    readonly property list<string> englishWords: ["meeting", "team"]
    function spellMenu(): BMenu { return editor.textMenu }
    function spellMenuOpen(): bool { return test.spellMenu() !== null && test.spellMenu().opened }

    // The edited text's menu (InlineEditor's right-click, BlockEditor.openTextMenu) on the page drawn
    // by Atrament: a right-click in the edited text opens it and no block menu, the selection kept;
    // its Copy copies the exact characters, Cut cuts them (no-break spaces kept), Paste puts them
    // back, Select all selects the block's text, Highlight… opens the colours (a colour highlights
    // the selection, one undo step); on a misspelled word its corrections come first, a correction
    // replaces the word.
    property int menuRow: -1
    readonly property string menuText: "Jadę w\u00a0góry z\u00a0Anią"
    function textMenu(): BMenu { return editor.textMenu }
    function textMenuOpen(): bool { return test.textMenu() !== null && test.textMenu().opened }
    function highlightMenu(): BMenu { return editor.highlightMenu }
    function highlightMenuOpen(): bool { return test.highlightMenu() !== null && test.highlightMenu().opened }
    function clip(): string { return page.clipboardText() }
    /// The edited field's selection as "from–to".
    function editSelection(): string {
        const v = test.view()
        return v ? QmlCall.integer(QmlCall.get(v, "editSelectionStart")) + "–"
                   + QmlCall.integer(QmlCall.get(v, "editSelectionEnd")) : ""
    }
    function selectEdited(from: int, to: int): void {
        const a = from
        const b = to
        QmlCall.call3(test.view(), "selectInEdited", test.menuRow, a, b)
    }
    /// A right-click at the edited text's caret (the end of its selection: the menu keeps it).
    function rightClickAtCaret(): void {
        const v = test.view() as AtramentView
        if (v)
            window.rightClick(v, v.editCaretRect.x + 0.5, v.editCaretRect.y + v.editCaretRect.height / 2)
    }
    function menuItem(name: string): Item { return window.menuItemNamed(test.textMenu(), name) }
    /// The text menu's correction `text` (null: not offered).
    function suggestion(text: string): T.MenuItem {
        const menu = test.textMenu()
        for (let i = 0; menu && i < menu.count; ++i) {
            const it = menu.itemAt(i) as T.MenuItem
            if (it && it.objectName === "spellSuggestion" && it.text === text)
                return it
        }
        return null
    }
    /// The middle of `word`'s first occurrence in the edited text (spot), false when it is not there.
    function findWord(word: string): bool {
        const w = word
        const at = Js.indexOf(test.editText(), w)
        const v = test.view() as AtramentView
        if (at < 0 || !v)
            return false
        test.spot = v.editPoint(at + Math.floor(w.length / 2))
        return test.spot.x >= 0
    }
    function wordMarked(word: string): bool {
        const v = test.view() as AtramentView
        return test.findWord(word) && v !== null && v.misspelledWordAt(test.spot.x, test.spot.y) !== ""
    }
    function spelling(): void {
        if (window.at()) {
            page.stopEditing()
            page.save()
            SpellCheck.useWordLists(Js.withKey(Js.withKey({}, "pl", test.polishWords), "en", test.englishWords))
            SpellCheck.languageSetting = "pl,en"
            SpellCheck.enabled = true
            host.setFile("Pisownia.md", "- Ala ma kotta i psa\n- Zapisz `kotta` w [[kotta]] #kotta\n")
            page.path = "Pisownia.md"
        }
        if (window.at()) window.waitStep(page.loaded && test.rowOf("Ala ma") === 0 && SpellCheck.active, "the page to check opens")
        if (window.at()) editor.startEditing(0, 0)
        if (window.at()) window.waitStep(test.editing(0) && test.wordMarked("kotta"), "“kotta” is marked in the edited Atrament field")
        if (window.at()) test.check(!test.wordMarked("Ala") && !test.wordMarked("psa"), "known words are not marked")
        if (window.at()) {
            test.findWord("kotta")
            if (window.touchRun)
                window.tapPoint(test.view(), test.spot.x, test.spot.y)
            else
                window.rightClick(test.view(), test.spot.x, test.spot.y)
        }
        if (window.at()) window.waitStep(test.spellMenuOpen() && test.suggestion("kota") !== null,
                                         (window.touchRun ? "a tap" : "a right-click") + " on the marked word offers “kota”")
        if (window.at()) {
            test.check(window.menuIndexOf(test.spellMenu(), "spellSuggestion") === 0, "corrections come first")
            const target = test.suggestion("kota")
            if (target)
                window.tapItem(target)
        }
        if (window.at()) window.waitStep(!test.spellMenuOpen() && Js.indexOf(host.file("Pisownia.md"), "Ala ma kota i psa") >= 0
                                         && test.editText() === "Ala ma kota i psa",
                                         "the word is replaced: " + test.editText())
        if (window.at()) test.check(test.editing(0) && test.editCursor() === 11, "the caret is after it: " + test.editCursor())
        if (window.at()) test.key(Qt.Key_Z, Qt.ControlModifier)
        if (window.at()) window.waitStep(test.editText() === "Ala ma kotta i psa", "one undo step brings it back: " + test.editText())
        if (window.at()) editor.startEditing(1, 0)
        if (window.at()) window.waitStep(test.editing(1), "the block of code, a link and a tag is edited")
        if (window.at()) test.check(!test.wordMarked("kotta"), "a word in code, a link or a tag is not marked")
        if (window.at()) {
            page.stopEditing()
            SpellCheck.enabled = false
        }
    }

    // The task box's status menu (the taskstatus test's first part): a right-click or a finger
    // held on the box opens it — the task not toggled, its status checked —, a status chosen is
    // written (one undo step), a plain click still toggles, "No status" removes the box.
    function statusMenu(): BMenu { return editor.taskMenu }
    function statusMenuOpen(): bool { return test.statusMenu() !== null && test.statusMenu().visible }
    function statusItem(name: string): T.MenuItem { return window.menuItemNamed(test.statusMenu(), name) }
    function taskMenu(): void {
        if (window.at()) {
            page.stopEditing()
            page.save()
            host.setFile("Statusy.md", "- [ ] Kupić płytki 📅 2026-10-16\n- zwykły blok\n")
            page.path = "Statusy.md"
        }
        if (window.at()) window.waitStep(page.loaded && test.findTaskBox(0), "the task's box is drawn")
        if (window.at()) test.findTaskBox(0)
        test.menuGesture("task", test.statusMenuOpen())
        if (window.at()) window.waitStep(test.statusMenuOpen() && !test.blockMenuOpen(),
                                         (window.touchRun ? "a finger held" : "a right-click") + " on the box opens the status menu")
        if (window.at()) {
            test.check(test.info(0).taskStatus === "todo", "the gesture does not toggle the task")
            const todo = test.statusItem("taskStatus_todo")
            const doing = test.statusItem("taskStatus_doing")
            test.check(todo !== null && todo.checked && doing !== null && !doing.checked, "the current status is checked")
            if (doing)
                window.tapItem(doing)
        }
        if (window.at()) window.waitStep(!test.statusMenuOpen() && test.info(0).taskStatus === "doing"
                                         && Js.indexOf(host.file("Statusy.md"), "- [/] Kupić płytki") === 0,
                                         "the chosen status is written: " + host.file("Statusy.md"))
        if (window.at()) page.undo()
        if (window.at()) window.waitStep(test.info(0).taskStatus === "todo", "one undo step puts the status back")
        if (window.at()) { test.findTaskBox(0); test.tapSpot() }
        if (window.at()) window.waitStep(test.info(0).taskStatus === "done" && !test.statusMenuOpen(),
                                         "a plain click still toggles, without a menu")
        if (window.at()) page.undo()
        if (window.at()) test.findTaskBox(0)
        test.menuGesture("task", test.statusMenuOpen())
        if (window.at()) window.waitStep(test.statusMenuOpen(), "the menu opens again")
        if (window.at()) {
            const none = test.statusItem("taskStatus_none")
            if (none)
                window.tapItem(none)
        }
        if (window.at()) window.waitStep(!test.statusMenuOpen() && test.info(0).taskStatus === ""
                                         && Js.indexOf(host.file("Statusy.md"), "- Kupić płytki") === 0,
                                         "“No status” removes the box, the text stays a list item")
        if (window.at()) page.undo()
        if (window.at()) window.waitStep(test.info(0).taskStatus === "todo", "undo brings the box back")
        // Ctrl+Shift+Enter while editing: the menu at the caret, its keys, the caret kept.
        if (window.at()) editor.startEditing(0, 3)
        if (window.at()) window.waitStep(test.editing(0) && test.editCursor() === 3, "the task is edited, the caret at 3")
        if (window.at()) test.key(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier)
        if (window.at()) window.waitStep(test.statusMenu() !== null && test.statusMenu().opened && test.info(0).taskStatus === "todo",
                                         "Ctrl+Shift+Enter opens the status menu of the edited block, the status not cycled")
        if (window.at()) window.realKey(Qt.Key_Escape, Qt.NoModifier, "")
        if (window.at()) window.waitStep(!test.statusMenuOpen() && test.editing(0) && test.editCursor() === 3,
                                         "Escape closes the menu, the block keeps its caret: " + test.editCursor())
        if (window.at()) test.key(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier)
        if (window.at()) window.waitStep(test.statusMenu() !== null && test.statusMenu().opened, "the menu opens again")
        if (window.at()) {
            const doing = test.statusItem("taskStatus_doing")
            if (doing)
                window.tapItem(doing)
        }
        if (window.at()) window.waitStep(!test.statusMenuOpen() && test.info(0).taskStatus === "doing" && test.editing(0)
                                         && test.editCursor() === 3 && test.editText() === "Kupić płytki 📅 2026-10-16",
                                         "a status from the menu: the block edited on, the caret and the text kept: "
                                         + test.editCursor() + " " + test.editText())
        if (window.at()) page.undo()
        // A heading has no status: Ctrl+Shift+Enter opens nothing, changes nothing.
        if (window.at()) {
            page.stopEditing()
            page.insertMarkdown(page.lastVisibleRow(), "## Koszty")
        }
        if (window.at()) editor.startEditing(test.rowOf("Koszty"), -1)
        if (window.at()) window.waitStep(test.editing(test.rowOf("Koszty")), "a heading is edited")
        if (window.at()) {
            test.textBefore = page.sourceText
            test.key(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier)
        }
        if (window.at()) window.waitStep(true, "Ctrl+Shift+Enter pressed in the heading")
        if (window.at()) {
            const open = test.statusMenuOpen()
            const same = page.sourceText === test.textBefore
            const ok = !open && same && test.editing(test.rowOf("Koszty"))
            test.check(ok, "Ctrl+Shift+Enter in a heading opens no status menu, changes nothing: menu " + open
                       + ", text kept " + same + ", " + test.editText())
        }
        if (window.at()) page.stopEditing()
    }

    function sourceHas(text: string): bool { return Js.indexOf(page.sourceText, text) >= 0 }
    function openMenuPage(): void {
        if (window.at()) {
            page.stopEditing()
            page.save()
            host.setFile("Menu tekstu.md", "- " + test.menuText + "\n- Ala ma kotta i psa\n")
            page.path = "Menu tekstu.md"
        }
        if (window.at()) window.waitStep(page.loaded && test.rowOf("Jadę") === 0 && test.rowTop(0) >= 0,
                                         "the page for the text menu opens")
        if (window.at()) {
            test.menuRow = 0
            editor.startEditing(0, -1)
        }
        if (window.at()) window.waitStep(test.editing(0), "its block is edited")
    }
    function textMenus(): void {
        test.openMenuPage()
        if (window.at()) test.selectEdited(0, 4)
        if (window.at()) test.rightClickAtCaret()
        if (window.at()) window.waitStep(test.textMenuOpen() && !test.blockMenuOpen(),
                                         "a right-click in the edited text opens its menu, no block menu")
        if (window.at()) {
            test.check(test.editSelection() === "0–4", "the selection stays while the menu is open: " + test.editSelection())
            test.check(test.menuItem("editTextCut") !== null && test.menuItem("editTextCut").enabled
                       && test.menuItem("editTextHighlight") !== null, "with Wytnij and Zakreśl…")
            window.tapItem(test.menuItem("editTextCopy"))
        }
        if (window.at()) window.waitStep(test.clip() === "Jadę" && !test.textMenuOpen() && test.editing(0),
                                         "its Kopiuj copies the selected text, the block stays edited: " + test.clip())
        // Cut and Paste: the exact characters, no-break spaces kept.
        if (window.at()) test.selectEdited(5, 13)
        if (window.at()) test.rightClickAtCaret()
        if (window.at()) window.waitStep(test.textMenuOpen(), "the menu over another selection")
        if (window.at()) window.tapItem(test.menuItem("editTextCut"))
        if (window.at()) window.waitStep(test.clip() === "w\u00a0góry z" && test.sourceHas("- Jadę \u00a0Anią\n"),
                                         "Wytnij cuts the exact characters: " + Js.stringify(test.clip()))
        if (window.at()) test.rightClickAtCaret()
        if (window.at()) window.waitStep(test.textMenuOpen() && !test.menuItem("editTextCut").enabled,
                                         "the menu at the caret, Wytnij off with nothing selected")
        if (window.at()) window.tapItem(test.menuItem("editTextPaste"))
        if (window.at()) window.waitStep(test.sourceHas("- " + test.menuText + "\n") && test.editing(0),
                                         "Wklej puts them back: " + Js.stringify(Js.slice(page.sourceText, 0, 40)))
        if (window.at()) test.rightClickAtCaret()
        if (window.at()) window.waitStep(test.textMenuOpen(), "the menu once more")
        if (window.at()) window.tapItem(test.menuItem("editTextSelectAll"))
        if (window.at()) window.waitStep(test.editSelection() === "0–" + test.menuText.length,
                                         "Zaznacz wszystko selects the block's text: " + test.editSelection())
        // Highlight…: the colours, then the colour on the selection.
        if (window.at()) test.selectEdited(0, 4)
        if (window.at()) test.rightClickAtCaret()
        if (window.at()) window.waitStep(test.textMenuOpen(), "the menu for Zakreśl…")
        if (window.at()) window.tapItem(test.menuItem("editTextHighlight"))
        if (window.at()) window.waitStep(test.highlightMenuOpen() && !test.textMenuOpen(), "Zakreśl… opens the colours")
        if (window.at()) window.tapItem(window.menuItemNamed(test.highlightMenu(), "highlight-red"))
        if (window.at()) window.waitStep(test.sourceHas("- <mark style=\"background: #FF5582A6;\">Jadę</mark> w") && test.editing(0)
                                         && test.editSelection() !== "0–0",
                                         "Czerwony highlights the selection, which stays selected: "
                                         + Js.stringify(Js.slice(page.sourceText, 0, 60)) + ", " + test.editSelection())
        if (window.at()) page.undo()
        if (window.at()) window.waitStep(test.sourceHas("- " + test.menuText + "\n"), "the highlight is one undo step")
        // A misspelled word: its corrections first, then the text's items; a correction replaces it.
        if (window.at()) {
            SpellCheck.useWordLists(Js.withKey(Js.withKey({}, "pl", test.polishWords), "en", test.englishWords))
            SpellCheck.languageSetting = "pl,en"
            SpellCheck.enabled = true
            test.menuRow = 1
            editor.startEditing(1, 9)
        }
        if (window.at()) window.waitStep(test.editing(1) && SpellCheck.active && SpellCheck.idle(),
                                         "a block with a misspelled word is edited, its words checked")
        if (window.at()) test.selectEdited(9, 9)
        if (window.at()) test.rightClickAtCaret()
        if (window.at()) window.waitStep(test.textMenuOpen() && window.menuIndexOf(test.textMenu(), "spellSuggestion") === 0
                                         && test.menuItem("editTextCut") !== null && test.menuItem("editTextCut").visible,
                                         "on a misspelled word the menu offers its corrections first, the text's items after them")
        if (window.at()) window.waitStep(test.suggestion("kota") !== null, "“kota” is offered")
        if (window.at()) window.tapItem(test.suggestion("kota"))
        if (window.at()) window.waitStep(test.sourceHas("- Ala ma kota i psa\n") && !test.textMenuOpen(),
                                         "the correction replaces the word: " + Js.stringify(page.sourceText))
        if (window.at()) {
            SpellCheck.enabled = false
            page.stopEditing()
        }
    }

    // A phone: the text menu is the platform's — Android's long press selects the word through the
    // input method (QAndroidInputContext::longPress: a selection back from the word's end), its
    // handles and floating toolbar ask the field what is selected and whether it is read-only, and
    // the toolbar's Cut / Copy / Paste / Select all come as key shortcuts without text
    // (QAndroidInputContext::sendShortcut) — the same through the window to the page drawn by
    // Atrament: the controller's exact copy, cut and paste.
    function touchTextTools(): void {
        test.openMenuPage()
        if (window.at()) EditorInput.sendInputMethodSelection(test.view(), 4, -4)
        if (window.at()) {
            const v = test.view()
            const selected = QmlCall.text(EditorInput.inputMethodQuery(v, Qt.ImCurrentSelection))
            const readOnly = QmlCall.isTrue(EditorInput.inputMethodQuery(v, Qt.ImReadOnly))
            test.check(test.editSelection() === "0–4" && selected === "Jadę" && !readOnly,
                       "the input method's long press selects a word the toolbar sees, editable: " + test.editSelection()
                       + " “" + selected + "”")
        }
        if (window.at()) test.key(Qt.Key_C, Qt.ControlModifier)
        if (window.at()) window.waitStep(test.clip() === "Jadę" && test.editing(0), "the toolbar's Copy copies it: " + test.clip())
        if (window.at()) EditorInput.sendInputMethodSelection(test.view(), 13, -8)
        if (window.at()) test.key(Qt.Key_X, Qt.ControlModifier)
        if (window.at()) window.waitStep(test.clip() === "w\u00a0góry z" && test.sourceHas("- Jadę \u00a0Anią\n"),
                                         "its Cut cuts the exact characters: " + Js.stringify(test.clip()))
        if (window.at()) test.key(Qt.Key_V, Qt.ControlModifier)
        if (window.at()) window.waitStep(test.sourceHas("- " + test.menuText + "\n") && test.editing(0),
                                         "its Paste puts them back: " + Js.stringify(Js.slice(page.sourceText, 0, 40)))
        if (window.at()) test.key(Qt.Key_A, Qt.ControlModifier)
        if (window.at()) window.waitStep(test.editSelection() === "0–" + test.menuText.length, "its Select all selects the text: "
                                         + test.editSelection())
        if (window.at()) page.stopEditing()
    }

    // Chips and links inside an embedded block and a query result (TransclusionView's,
    // QueryBlockView's): a right-click or a finger held on a 📅 chip opens DateChipMenu, on a
    // [[link]] the link menu — never the block menu —, whose "Otwórz" opens the page.
    readonly property string embedsContent: "- ![[Przepisy#Zupa]]\n\n```query\ntype:tasks tag:zakupy\n```\n"
    function embedMenus(): void {
        if (window.at()) {
            page.stopEditing()
            page.save()
            host.setFile("Przepisy.md", "# Zupa\n- Pokrój marchew 📅 2026-10-16 i [[Seler]]\n")
            host.setFile("Seler.md", "- korzeń\n")
            host.setFile("Sklep.md", "- [ ] Kupić chleb w [[Piekarnia]] 📅 2026-10-17 #zakupy\n")
            host.setFile("Osadzenia.md", test.embedsContent)
            page.path = "Osadzenia.md"
        }
        if (window.at()) window.waitStep(page.loaded && test.findLink(0, "Seler") && test.findLink(1, "Piekarnia"),
                                         "the embedded block's and the query result's links are drawn")
        if (window.at()) test.findLink(0, "date:")
        test.menuGesture("date", test.dateMenuOpen())
        if (window.at()) window.waitStep(test.dateMenuOpen() && !test.blockMenuOpen() && page.selectedRow < 0,
                                         (window.touchRun ? "a finger held" : "a right-click")
                                         + " on an embedded block's 📅 chip opens its menu, no block menu")
        if (window.at()) test.dateMenu().close()
        if (window.at()) window.waitStep(!test.dateMenuOpen(), "the chip's menu closes")
        if (window.at()) test.findLink(0, "Seler")
        test.menuGesture("link", test.linkMenuOpen())
        if (window.at()) window.waitStep(test.linkMenuOpen() && !test.blockMenuOpen(), "the same on its [[link]] opens the link menu")
        if (window.at()) {
            test.navigationsBefore = test.navigations
            window.tapItem(window.menuItemNamed(test.linkMenu(), "linkMenuOpen"))
        }
        if (window.at()) window.waitStep(test.navigations > test.navigationsBefore && !test.linkMenuOpen(),
                                         "its Otwórz opens the page")
        if (window.at()) test.findLink(1, "date:")
        test.menuGesture("result date", test.dateMenuOpen())
        if (window.at()) window.waitStep(test.dateMenuOpen() && !test.blockMenuOpen(),
                                         "a query result's 📅 chip opens its menu, no block menu")
        if (window.at()) test.dateMenu().close()
        if (window.at()) test.findLink(1, "Piekarnia")
        test.menuGesture("result link", test.linkMenuOpen())
        if (window.at()) window.waitStep(test.linkMenuOpen() && !test.blockMenuOpen(), "a query result's [[link]] opens the link menu")
        if (window.at()) test.linkMenu().close()
        if (window.at()) window.waitStep(!test.linkMenuOpen(), "the link menu closes")
    }

    Component.onCompleted: window.runSteps("atramenttest")
}
