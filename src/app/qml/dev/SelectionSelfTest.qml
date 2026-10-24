pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Brulion

// SelectionSelfTest — selecting and copying blocks with real input in the editor playground
// (`brulion --editor --editor-script selectiontest`; ctest tst_editor_qml_selection, desktop, and
// tst_editor_qml_selection_touch, a phone with --touch). QtTest's TestEvent sends keys, mouse and
// touch through the window like a real device; the clipboard is read back.
//   desktop: Esc (Ctrl+Enter then changes the parent alone, not its children) then Shift+↓ twice and
//     Ctrl+C (three blocks with their children), Shift+↑ from an edited block's first line, Shift+↓
//     from inside one, Ctrl+A twice, Ctrl+clicks picking blocks with gaps between them (a band each,
//     the count, Ctrl+C, Tab, Alt+↑ and Więcej's Przenieś w górę refused with the reason, one left out
//     again, Shift+click after them), Shift+click, Delete and Ctrl+Z, the toast's Cofnij (also one whose
//     deletion was undone already: nothing else is undone), Alt+↓ and Tab on a range (undone
//     from another edited block: the range selected again), Ctrl+V over a selection, one copied block
//     pasted into an empty bullet (no second marker), Shift+click on a calculation and a table,
//     in-block Ctrl+C / Ctrl+X keeping no-break spaces, the edited text's right-click menu, the block
//     menu of a selected and of another block, the floating tools, a mouse drag over three paragraphs
//     of a document (the list does not scroll), a drag to the view's edge (the list scrolls, the
//     selection follows), the band after scrolling far away and back, and the floating tools' Więcej
//     with no indents for a document's paragraphs;
//   touch: a long press on a block's text (the selection mode, its tools docked), a tap extending
//     it, Kopiuj, "Pojedynczo" (no grips; taps leave a block out and pick another one with a gap,
//     Kopiuj; off again: a tap moves the end), the end grip dragged, Usuń (still in the selection
//     mode), a tap on the toast's Cofnij,
//     Usuń and Ctrl+Z, a long press
//     dragged to the view's edge (autoscroll), Back, the editing toolbar's "Zaznacz", Esc and
//     Shift+↓ from a keyboard, and the bullet menu's "Zaznacz".
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property var window
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property TestEvent events: TestEvent {}
    property int rowA: -1
    property int rowB: -1
    property int rowC: -1
    property real contentBefore: 0
    property string textBefore: ""
    /// A toast's Cofnij kept after its edit was undone another way.
    property var staleUndo: null

    // ---- Helpers ------------------------------------------------------------------------------------
    readonly property ListView list: editor.listView
    function check(ok, what) { window.check(ok, what) }
    /// A step repeating until `ready()`, then checking it (at most window.waitLimit times: longer
    /// in slow sanitizer builds). Gestures are followed by one: what they do may come a frame later.
    function wait(ready, what) { return window.waitStep(ready, what) }
    /// A step repeating until `ready()` (at most window.waitLimit times), checking nothing: a finger
    /// held down until its gesture took effect.
    function hold(ready) { return window.holdStep(ready) }
    function rowOf(prefix) { return window.rowOf(prefix) }
    function info(row) { return page.blocks.get(row) }
    function textOf(row) { return window.renderedOf(row) }
    function delegateAt(row) { return window.delegateAt(row) }
    function selected() {
        const out = []
        for (const r of page.selectedRows())
            out.push(r)
        return out
    }
    /// The clipboard's text exactly (no-break spaces kept): this app's copies are read as written.
    function clip() { return page.clipboardText() }
    function key(k, modifiers) { EditorInput.sendKey(list, k, modifiers || 0, "") }
    function editKey(k, modifiers) { window.realKey(k, modifiers || 0, "") }
    function edgeOf(row) {
        const item = delegateAt(row)
        return item ? item.selectionEdge : "?"
    }
    function center(row) {
        editor.listView.positionViewAtIndex(row, ListView.Center)
    }
    /// Visible rows that can be selected (not the page's properties).
    function selectableCount() {
        let n = 0
        for (let r = 0; r < page.blocks.count; ++r) {
            const b = info(r)
            if (b.visible && b.view !== "frontmatter")
                ++n
        }
        return n
    }
    /// A finger put on (x, y) of `target()` and held there until `done()` — the long press took
    /// effect (the selection mode, a menu, a selected word), as a user lifts it once that shows —,
    /// then `then` (steps, the finger still down) and the release (unless `then` lifts it:
    /// releasedByThen): steps. (Held for a fixed number of steps, it was lifted too early in slow
    /// sanitizer runs now and then: a tap.)
    function longPress(target, x, y, done, then, releasedByThen) {
        const out = [() => {
            const item = target()
            check(item !== null, "something to hold")
            if (item)
                events.touchEvent(item).press(0, item, x(item), y(item)).commit()
        }, hold(done)]
        if (then)
            out.push(...then)
        if (!releasedByThen) {
            out.push(() => {
                const item = target()
                if (item)
                    events.touchEvent(item).release(0, item, x(item), y(item)).commit()
            })
        }
        return out
    }
    function fingerTap(item, x, y) {
        events.touchEvent(item).press(0, item, x, y).commit()
        events.touchEvent(item).release(0, item, x, y).commit()
    }
    function tap(item) {
        if (!item)
            return
        if (Theme.touchUi)
            fingerTap(item, item.width / 2, item.height / 2)
        else
            events.mouseClick(item, item.width / 2, item.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
    }
    /// A Ctrl+click on the text of `row` (picks it into or out of the selection).
    function ctrlClick(row) {
        const text = textOf(row)
        check(text !== null, "the text of row " + row)
        if (text)
            events.mouseClick(text, 30, text.height / 2, Qt.LeftButton, Qt.ControlModifier, -1)
    }
    function rightClick(item, x, y) {
        if (item)
            events.mouseClick(item, x, y, Qt.RightButton, Qt.NoModifier, -1)
    }
    function toolbar() { return window.findItem(editor, "editToolbarSlot") }
    function shown(item, name) { return window.findShown(item, name) }
    /// Scroll the docked toolbar's row so that `button` is on screen.
    function revealTool(button) {
        const flick = window.findItem(editor, "editToolbarFlick")
        if (!flick || !button)
            return
        const x = button.mapToItem(flick.contentItem, 0, 0).x
        flick.contentX = Math.max(0, Math.min(x - Theme.space8, flick.contentWidth - flick.width))
    }
    /// The shown grip at the end of the selection's band, or null.
    function endGrip() { return window.findShown(delegateAt(page.selectionLastRow), "selectionGripEnd") }
    /// The button of a toast ("Cofnij"), when one is shown (clearToasts() first: one at a time).
    function toastUndo() { return window.findShown(window.toastHost, "toastAction") }
    function clearToasts() { window.toastHost.clear() }

    Component.onCompleted: window.runSteps(Theme.touchUi ? touchSteps() : desktopSteps())

    // ---- Desktop ------------------------------------------------------------------------------------
    function desktopSteps() {
        return [
            wait(() => page.loaded && rowOf("Przygotowanie") > 0, "the outline is open"),
            // ---- Esc, Shift+↓ twice, Ctrl+C -----------------------------------------------------------
            () => {
                test.rowA = rowOf("Przygotowanie")
                center(test.rowA)
                editor.startEditing(test.rowA, -1)
            },
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus, "a block is edited"),
            () => editKey(Qt.Key_Escape),
            () => {
                check(page.selectedRow === test.rowA && page.editingRow < 0, "Esc selects the block")
                check(selected().length === 7, "with its children (folded or not): " + selected().length)
                key(Qt.Key_Return, Qt.ControlModifier)
            },
            () => {
                check(info(test.rowA).taskStatus === "todo" && info(rowOf("Zmierzyć")).taskStatus === "done"
                      && info(rowOf("Zadzwonić")).taskStatus === "doing" && info(rowOf("Wywieźć")).taskStatus === "todo",
                      "Ctrl+Enter on a parent selected alone changes it, not the children it brings along")
                key(Qt.Key_Z, Qt.ControlModifier)
            },
            () => {
                check(info(test.rowA).taskStatus === "" && page.selectedRow === test.rowA && selected().length === 7,
                      "undone, still selected")
                key(Qt.Key_Down, Qt.ShiftModifier)
            },
            () => {
                check(page.selectedRow === rowOf("Prace") && page.selectionAnchorRow === test.rowA,
                      "Shift+↓ jumps over the selected children to the next block: " + page.selectedRow)
                key(Qt.Key_Down, Qt.ShiftModifier)
            },
            () => {
                check(page.selectedRow === rowOf("Notatki z rozmowy"), "Shift+↓ again: " + page.selectedRow)
                check(edgeOf(test.rowA) === "first" && edgeOf(rowOf("Prace")) === "middle"
                      && edgeOf(rowOf("Ekipa może")) === "last", "one band over the range")
                window.lastNotice = ""
                key(Qt.Key_C, Qt.ControlModifier)
            },
            wait(() => clip().startsWith("- Przygotowanie ^przyg\n\t- [x] Zmierzyć ściany") && clip().indexOf("\n- Prace\n") > 0
                       && clip().endsWith("- Notatki z rozmowy z ekipą — zobacz ((przyg))\n\t- Ekipa może zacząć 20 października, cena ==do negocjacji=="),
                 () => "Ctrl+C copies the three blocks with their children as Markdown: " + JSON.stringify(clip().slice(0, 60))),
            () => {
                check(window.lastNotice !== "", "and says so: " + window.lastNotice)
                key(Qt.Key_Escape)
            },
            () => check(page.selectedRow < 0 && page.selectionCount === 0, "Esc clears the selection"),
            // ---- Shift+↑ from the first line of an edited block; Shift+↓ from inside one ---------------
            () => editor.startEditing(rowOf("Skuć"), 0),
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus, "Skuć is edited"),
            () => editKey(Qt.Key_Up, Qt.ShiftModifier),
            () => check(page.editingRow < 0 && page.selectionAnchorRow === rowOf("Skuć") && page.selectedRow === rowOf("Prace"),
                        "Shift+↑ at the start of the first line selects the block and the one above"),
            () => editor.startEditing(rowOf("Położyć"), 3),
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus, "Położyć is edited"),
            () => editKey(Qt.Key_Down, Qt.ShiftModifier),
            () => {
                const edit = editor.activeEditor
                check(page.editingRow === rowOf("Położyć") && edit !== null && edit.selectionStart === 3 && edit.selectionEnd === edit.length,
                      "Shift+↓ on the last line first selects the text to the block's end")
                editKey(Qt.Key_Down, Qt.ShiftModifier)
            },
            () => check(page.editingRow < 0 && page.selectionAnchorRow === rowOf("Położyć") && page.selectedRow === rowOf("Podlać"),
                        "then the next block"),
            // ---- Ctrl+A twice -------------------------------------------------------------------------
            () => editor.startEditing(rowOf("Kolejność"), 2),
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus, "Kolejność is edited"),
            () => editKey(Qt.Key_A, Qt.ControlModifier),
            () => {
                const edit = editor.activeEditor
                check(page.editingRow === rowOf("Kolejność") && edit && edit.selectionStart === 0 && edit.selectionEnd === edit.length,
                      "the first Ctrl+A selects the block's text")
                editKey(Qt.Key_A, Qt.ControlModifier)
            },
            () => check(page.editingRow < 0 && page.selectionCount === selectableCount(),
                        "the second Ctrl+A selects every block: " + page.selectionCount + " of " + selectableCount()),
            () => key(Qt.Key_Escape),
            // ---- Ctrl+click picks single blocks: gaps, copying, no moving, Shift+click after it ------------
            () => {
                center(rowOf("Zadzwonić"))
                ctrlClick(rowOf("Zmierzyć"))
            },
            () => {
                check(page.selectedRow === rowOf("Zmierzyć") && page.editingRow < 0, "Ctrl+click selects a block, nothing edited")
                ctrlClick(rowOf("Zadzwonić"))
            },
            () => ctrlClick(rowOf("Skuć")),
            () => {
                check(page.selectionCount === 3 && page.selectionPicked && !page.selectionContiguous
                      && JSON.stringify(selected()) === JSON.stringify([rowOf("Zmierzyć"), rowOf("Zadzwonić"), rowOf("Skuć")]),
                      "Ctrl+clicks pick three blocks with gaps between them: " + JSON.stringify(selected()))
                check(edgeOf(rowOf("Zmierzyć")) === "single" && edgeOf(rowOf("Kupić płytki")) === ""
                      && edgeOf(rowOf("Zadzwonić")) === "single" && edgeOf(rowOf("Prace")) === ""
                      && edgeOf(rowOf("Skuć")) === "single", "each has its own band, the gaps none")
                const tools = window.findShown(editor.dockHost, "floatingSelectionTools")
                const count = tools ? window.findShown(tools, "selectionCount") : null
                check(count !== null && count.text.indexOf("3") >= 0, "the floating tools count them: " + (count ? count.text : ""))
                window.lastNotice = ""
                key(Qt.Key_C, Qt.ControlModifier)
            },
            wait(() => clip() === "- [x] Zmierzyć ściany i podłogę ✅ 2026-10-12\n- [/] Zadzwonić do hydraulika ⏰ 2026-10-15 18:00 #telefon\n- [ ] Skuć stare płytki 📅 2026-10-13",
                 () => "Ctrl+C copies the picked blocks in their order: " + JSON.stringify(clip())),
            () => {
                test.textBefore = window.pageText()
                window.lastNotice = ""
                key(Qt.Key_Tab)
            },
            () => {
                check(window.pageText() === test.textBefore && window.lastNotice === editor.gapReason,
                      "Tab does not indent blocks with gaps between them, and says why: " + window.lastNotice)
                window.lastNotice = ""
                key(Qt.Key_Up, Qt.AltModifier)
            },
            () => {
                check(window.pageText() === test.textBefore && window.lastNotice === editor.gapReason, "nor does Alt+↑ move them")
                tap(window.findShown(window.findShown(editor.dockHost, "floatingSelectionTools"), "selectionMore"))
            },
            wait(() => {
                const tools = window.findShown(editor.dockHost, "floatingSelectionTools")
                return tools !== null && tools.menu !== null && tools.menu.opened
            }, "the floating tools' Więcej opens"),
            () => {
                const tools = window.findShown(editor.dockHost, "floatingSelectionTools")
                const up = window.menuItemNamed(tools.menu, "selectionMoveUp")
                check(up !== null && up.unavailable === editor.gapReason && up.opacity < 1,
                      "its Przenieś w górę is shown disabled, with the reason")
                window.lastNotice = ""
                tap(up)
            },
            () => check(window.pageText() === test.textBefore && window.lastNotice === editor.gapReason,
                        "a click on it moves nothing and says why"),
            () => ctrlClick(rowOf("Zadzwonić")),
            () => {
                check(JSON.stringify(selected()) === JSON.stringify([rowOf("Zmierzyć"), rowOf("Skuć")]),
                      "a second Ctrl+click leaves a picked block out: " + JSON.stringify(selected()))
                ctrlClick(rowOf("Ekipa może"))
            },
            () => {
                check(page.selectedRow === rowOf("Ekipa może") && page.selectionCount === 3, "another one picked")
                const text = textOf(rowOf("Położyć"))
                events.mouseClick(text, 30, text.height / 2, Qt.LeftButton, Qt.ShiftModifier, -1)
            },
            () => check(!page.selectionPicked && page.selectionAnchorRow === rowOf("Ekipa może") && page.selectedRow === rowOf("Położyć"),
                        "Shift+click then selects a range from the block picked last: " + page.selectionAnchorRow + "–" + page.selectedRow),
            () => key(Qt.Key_Escape),
            // ---- Shift+click; Delete and Ctrl+Z ---------------------------------------------------------
            () => {
                test.rowA = rowOf("Zmierzyć")
                center(test.rowA)
                editor.startEditing(test.rowA, -1)
            },
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus, "Zmierzyć is edited"),
            () => {
                const text = textOf(rowOf("Wywieźć"))
                events.mouseClick(text, 30, text.height / 2, Qt.LeftButton, Qt.ShiftModifier, -1)
            },
            () => {
                check(page.editingRow < 0 && page.selectionAnchorRow === test.rowA && page.selectedRow === rowOf("Wywieźć"),
                      "Shift+click selects from the edited block to the clicked one")
                test.textBefore = window.pageText()
                key(Qt.Key_Delete)
            },
            wait(() => window.pageText().indexOf("Zadzwonić do hydraulika") < 0, "Delete removes the selected blocks"),
            () => {
                check(page.selectedRow === rowOf("Przygotowanie"), "the block before them is selected")
                key(Qt.Key_Z, Qt.ControlModifier)
            },
            wait(() => window.pageText() === test.textBefore, "Ctrl+Z brings them back byte for byte"),
            () => check(page.selectionAnchorRow === rowOf("Zmierzyć") && page.selectedRow === rowOf("Wywieźć"),
                        "selected again"),
            // ---- The toast's "Cofnij" (a click on its button), and a stale one --------------------------
            () => clearToasts(),
            wait(() => toastUndo() === null, "no toast is left"),
            () => {
                window.lastNoticeAction = null
                key(Qt.Key_Delete)
            },
            wait(() => window.pageText().indexOf("Zadzwonić do hydraulika") < 0 && toastUndo() !== null,
                 () => "Delete again: the toast says so and offers Cofnij: “" + window.lastNotice + "”"),
            () => {
                const button = toastUndo()
                check(window.lastNoticeAction !== null && button.text === window.lastNoticeAction.text,
                      "the toast's button is the editor's action: " + window.lastNotice)
                events.mouseClick(button, button.width / 2, button.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
            },
            wait(() => window.pageText() === test.textBefore, "the toast's Cofnij brings them back byte for byte"),
            () => check(page.selectionAnchorRow === rowOf("Zmierzyć") && page.selectedRow === rowOf("Wywieźć"),
                        "and selects them again"),
            () => {
                window.lastNoticeAction = null
                key(Qt.Key_Delete)
            },
            wait(() => window.lastNoticeAction !== null, "deleted once more"),
            () => {
                test.staleUndo = window.lastNoticeAction
                key(Qt.Key_Z, Qt.ControlModifier) // undone already (another way)
            },
            wait(() => window.pageText() === test.textBefore, "Ctrl+Z undoes the deletion"),
            () => {
                window.lastNotice = ""
                test.staleUndo.run()
            },
            () => check(window.pageText() === test.textBefore && window.lastNotice !== "",
                        "a Cofnij whose edit was undone already undoes nothing else, and says so: " + window.lastNotice),
            // ---- Alt+↓ and Tab on a range ------------------------------------------------------------
            () => {
                page.selectRange(rowOf("Skuć"), rowOf("Położyć"))
                list.forceActiveFocus()
                test.textBefore = window.pageText()
                key(Qt.Key_Down, Qt.AltModifier)
            },
            () => {
                check(rowOf("Podlać") < rowOf("Skuć") && rowOf("Skuć") + 1 === rowOf("Położyć"),
                      "Alt+↓ moves the range below its next sibling (with that one's child)")
                check(page.selectionAnchorRow === rowOf("Skuć") && page.selectedRow === rowOf("Położyć"), "the range stays selected")
                key(Qt.Key_Tab)
            },
            () => {
                check(info(rowOf("Skuć")).depth === 2 && info(rowOf("Położyć")).depth === 2, "Tab indents the range")
                editor.startEditing(rowOf("Ekipa może"), -1) // another block edited meanwhile
            },
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus && editor.activeEditor.row === rowOf("Ekipa może"),
                 "another block is edited"),
            () => editKey(Qt.Key_Z, Qt.ControlModifier),
            () => {
                check(page.editingRow < 0 && page.selectionAnchorRow === rowOf("Skuć") && page.selectedRow === rowOf("Położyć")
                      && list.activeFocus && info(rowOf("Skuć")).depth === 1,
                      "Ctrl+Z in it undoes Tab: the range selected again, nothing edited, the keys with the selection")
                key(Qt.Key_Z, Qt.ControlModifier)
            },
            wait(() => window.pageText() === test.textBefore, "undone"),
            // ---- Ctrl+V over a selection ---------------------------------------------------------------
            () => {
                page.selectRange(rowOf("Skuć"), rowOf("Skuć"))
                list.forceActiveFocus()
                key(Qt.Key_C, Qt.ControlModifier)
            },
            wait(() => clip() === "- [ ] Skuć stare płytki 📅 2026-10-13", "Ctrl+C copies one block"),
            () => {
                page.selectedRow = rowOf("Położyć")
                key(Qt.Key_V, Qt.ControlModifier)
            },
            () => {
                check(rowOf("Położyć") < 0 && window.pageText().split("Skuć stare płytki").length === 3,
                      "Ctrl+V replaces the selected block with the copied one")
                key(Qt.Key_Z, Qt.ControlModifier)
            },
            wait(() => window.pageText() === test.textBefore, "undone"),
            // ---- One copied block pasted into an edited empty bullet: its text, no second marker --------
            () => { test.rowA = page.insertBlockAfter(rowOf("Ekipa może"), "") },
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus && editor.activeEditor.row === test.rowA,
                 "a new empty bullet is edited"),
            () => editKey(Qt.Key_V, Qt.ControlModifier),
            wait(() => info(test.rowA).taskStatus === "todo" && info(test.rowA).plainText.indexOf("Skuć stare płytki") === 0
                       && window.pageText().split("\t- [ ] Skuć stare płytki 📅 2026-10-13\n").length === 3,
                 () => "Ctrl+V pastes the copied task into it, its checkbox and no second marker: "
                       + JSON.stringify(info(test.rowA).plainText) + " " + info(test.rowA).taskStatus),
            () => check(page.editingRow === test.rowA && window.pageText().indexOf("- - ") < 0, "the block is still edited"),
            () => {
                page.undo()
                page.undo()
                page.stopEditing()
            },
            wait(() => window.pageText() === test.textBefore, "undone"),
            // ---- Shift+click on a calculation and on a table (frames, not text) ---------------------------
            () => {
                test.rowA = rowOf("Koszty")
                center(window.viewRow("calc"))
                editor.startEditing(test.rowA, -1)
            },
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus && editor.activeEditor.row === test.rowA,
                 "the heading Koszty is edited"),
            () => {
                const calc = window.findItem(delegateAt(window.viewRow("calc")), "calcInput")
                check(calc !== null, "the calculation's lines")
                if (calc)
                    events.mouseClick(calc, 20, 8, Qt.LeftButton, Qt.ShiftModifier, -1)
            },
            () => check(page.editingRow < 0 && page.selectionAnchorRow === test.rowA && page.selectedRow === window.viewRow("calc"),
                        "Shift+click on a calculation selects from the edited heading to it, nothing edited: "
                        + page.selectionAnchorRow + "–" + page.selectedRow),
            () => {
                center(window.viewRow("table"))
                const table = delegateAt(window.viewRow("table"))
                if (table)
                    events.mouseClick(table, table.width / 3, table.height / 2, Qt.LeftButton, Qt.ShiftModifier, -1)
            },
            () => check(page.selectionAnchorRow === test.rowA && page.selectedRow === window.viewRow("table")
                        && editor.editingTable === null && page.editingRow < 0,
                        "Shift+click on a table moves the selection's end there, no cell edited: " + page.selectedRow),
            () => key(Qt.Key_Escape),
            // ---- In-block Ctrl+C / Ctrl+X keep no-break spaces ------------------------------------------
            () => {
                test.rowA = page.insertBlockAfter(rowOf("Ekipa może"), "Jadę w\u00a0góry z\u00a0Anią")
            },
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus && editor.activeEditor.row === test.rowA,
                 "a block with no-break spaces is edited"),
            () => {
                editor.activeEditor.select(5, 13)
                editKey(Qt.Key_C, Qt.ControlModifier)
            },
            wait(() => clip() === "w\u00a0góry z", () => "Ctrl+C copies the exact characters: " + JSON.stringify(clip())),
            () => editKey(Qt.Key_X, Qt.ControlModifier),
            wait(() => window.pageText().indexOf("- Jadę \u00a0Anią\n") > 0 && clip() === "w\u00a0góry z", "Ctrl+X cuts them"),
            // ---- The edited text's menu -------------------------------------------------------------------
            () => {
                const edit = editor.activeEditor
                edit.select(0, 4)
                const r = edit.positionToRectangle(2)
                events.mouseClick(edit, r.x, r.y + r.height / 2, Qt.RightButton, Qt.NoModifier, -1)
            },
            wait(() => editor.textMenu !== null && editor.textMenu.opened, "a right-click in the edited text opens its menu"),
            () => {
                check(editor.activeEditor.selectionStart === 0 && editor.activeEditor.selectionEnd === 4,
                      "the selection stays while the menu is open")
                check(!editor.blockMenu || !editor.blockMenu.opened, "and no block menu")
                tap(window.menuItemNamed(editor.textMenu, "editTextCopy"))
            },
            wait(() => clip() === "Jadę" && !editor.textMenu.opened, "its Kopiuj copies the selected text"),
            () => {
                page.undo()
                page.stopEditing()
            },
            // ---- The block menu: of another block, of a selected one --------------------------------------
            () => {
                center(rowOf("Plan remontu"))
                const text = textOf(rowOf("Plan remontu"))
                rightClick(text, text.width / 2, 10)
            },
            wait(() => editor.blockMenu !== null && editor.blockMenu.opened, "a right-click on a paragraph opens the block menu"),
            () => {
                const copy = window.menuItemNamed(editor.blockMenu, "blockMenuCopy")
                check(copy !== null && !editor.blockMenu.inSelection, "with Kopiuj")
                tap(copy)
            },
            wait(() => clip().indexOf("Plan remontu na październik.") === 0, "Kopiuj copies that block"),
            () => {
                page.selectRange(rowOf("Zadzwonić"), rowOf("Prace"))
                const text = textOf(rowOf("Wywieźć"))
                rightClick(text, 20, text.height / 2)
            },
            wait(() => editor.blockMenu !== null && editor.blockMenu.opened && editor.blockMenu.inSelection,
                 "the menu of a selected block acts on the selection"),
            () => tap(window.menuItemNamed(editor.blockMenu, "blockMenuCopyText")),
            wait(() => clip().indexOf("☐ Zadzwonić do hydraulika") === 0 && clip().indexOf("\n☐ Wywieźć gruz") > 0
                       && clip().indexOf("\n• Prace\n  ☐ Skuć") > 0,
                 () => "Kopiuj jako tekst copies the selection as text: " + JSON.stringify(clip().slice(0, 80))),
            // ---- The floating tools ---------------------------------------------------------------------
            () => {
                const tools = window.findShown(editor.dockHost, "floatingSelectionTools")
                check(tools !== null, "two or more blocks selected: the floating tools")
                tap(window.findShown(tools, "selectionCopy"))
            },
            wait(() => clip().indexOf("- [/] Zadzwonić do hydraulika") === 0, "their Kopiuj copies the selection as Markdown"),
            // ---- The band after scrolling far away and back -----------------------------------------------
            () => list.positionViewAtEnd(),
            () => list.positionViewAtBeginning(),
            () => center(rowOf("Wywieźć")),
            () => check(window.findShown(delegateAt(rowOf("Wywieźć")), "selectionBand") !== null && edgeOf(rowOf("Prace")) !== "",
                        "the band is there again after its blocks were scrolled away"),
            () => key(Qt.Key_Escape),
            // ---- A mouse drag from one block into another; at the view's edge the list scrolls -------------
            () => list.positionViewAtBeginning(),
            () => {
                test.contentBefore = list.contentY
                test.rowA = rowOf("Zmierzyć")
                const text = textOf(test.rowA)
                events.mousePress(text, 40, text.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
                events.mouseMove(text, 60, text.height / 2 + 2, -1, Qt.LeftButton, Qt.NoModifier)
            },
            () => {
                const text = textOf(rowOf("Zadzwonić"))
                events.mouseMove(text, 60, text.height / 2, -1, Qt.LeftButton, Qt.NoModifier)
            },
            () => {
                check(page.editingRow < 0 && page.selectionAnchorRow === test.rowA && page.selectedRow === rowOf("Zadzwonić"),
                      "dragging into another block selects the range")
                check(Math.abs(list.contentY - test.contentBefore) < 1, "a mouse drag does not scroll the list")
                events.mouseMove(list, list.width / 2, list.height - 4, -1, Qt.LeftButton, Qt.NoModifier)
            },
            // Held at the edge until the list has scrolled (a frame at a time: slow in sanitizer builds).
            hold(() => list.contentY > test.contentBefore + 100 && page.selectedRow > rowOf("Notatki z rozmowy")),
            () => {
                check(list.contentY > test.contentBefore + 100, "at the view's bottom edge the list scrolls: " + list.contentY)
                check(page.selectedRow > rowOf("Notatki z rozmowy"), "and the selection follows the pointer: " + page.selectedRow)
                const y = list.height - 4
                events.mouseRelease(list, list.width / 2, y, Qt.LeftButton, Qt.NoModifier, -1)
            },
            () => {
                check(!editor.dragSelecting && page.editingRow < 0 && page.selectionAnchorRow === test.rowA,
                      "the release ends the drag, nothing is edited")
                key(Qt.Key_Escape)
            },
            // ---- A document: a drag over three paragraphs --------------------------------------------------
            () => { page.path = host.documentPath },
            wait(() => page.loaded && page.documentMode && rowOf("Pierwszego dnia") > 0, "the document is open"),
            () => list.positionViewAtBeginning(),
            () => {
                test.rowA = rowOf("Trzy dni")
                test.rowB = rowOf("Pierwszego dnia")
                test.contentBefore = list.contentY
                const text = textOf(test.rowA)
                events.mousePress(text, 30, 10, Qt.LeftButton, Qt.NoModifier, -1)
                events.mouseMove(text, 50, 14, -1, Qt.LeftButton, Qt.NoModifier)
            },
            () => {
                const text = textOf(rowOf("Plan"))
                events.mouseMove(text, 40, text.height / 2, -1, Qt.LeftButton, Qt.NoModifier)
            },
            () => {
                const text = textOf(test.rowB)
                events.mouseMove(text, 80, 10, -1, Qt.LeftButton, Qt.NoModifier)
                events.mouseRelease(text, 80, 10, Qt.LeftButton, Qt.NoModifier, -1)
            },
            () => {
                check(page.selectionAnchorRow === test.rowA && page.selectedRow === test.rowB && page.selectionCount === 3,
                      "a drag over three paragraphs selects them: " + page.selectionCount)
                check(edgeOf(test.rowA) === "first" && edgeOf(rowOf("Plan")) === "middle" && edgeOf(test.rowB) === "last",
                      "on one band")
                check(Math.abs(list.contentY - test.contentBefore) < 1 && page.editingRow < 0, "nothing scrolled, nothing edited")
                key(Qt.Key_C, Qt.ControlModifier)
            },
            wait(() => clip().indexOf("Trzy dni w Zakopanem") === 0 && clip().indexOf("\n\n## Plan\n\nPierwszego dnia") > 0,
                 "copied as Markdown with its blank lines"),
            // A document's paragraphs have no outline: Więcej offers no indents for them.
            () => tap(window.findShown(window.findShown(editor.dockHost, "floatingSelectionTools"), "selectionMore")),
            wait(() => {
                const tools = window.findShown(editor.dockHost, "floatingSelectionTools")
                return tools !== null && tools.menu !== null && tools.menu.opened
            }, "the floating tools' Więcej opens"),
            () => {
                const tools = window.findShown(editor.dockHost, "floatingSelectionTools")
                const indent = window.menuItemNamed(tools.menu, "selectionIndent")
                const outdent = window.menuItemNamed(tools.menu, "selectionOutdent")
                check(indent !== null && !indent.visible && outdent !== null && !outdent.visible,
                      "paragraphs of a document: no Zwiększ / Zmniejsz wcięcie")
                tools.menu.close()
            },
        ]
    }

    // ---- Touch --------------------------------------------------------------------------------------
    // (Qt Quick hands a finger's moves over with the next frame, not when they are sent: whatever a
    // gesture does is waited for, in slow sanitizer runs too.)
    function touchSteps() {
        const textAt = (row) => () => textOf(row())
        return [
            wait(() => page.loaded && rowOf("Przygotowanie") > 0, "the outline is open"),
            () => center(rowOf("Zadzwonić")),
            // ---- A long press on a block's text: the selection mode ----------------------------------------
            ...longPress(textAt(() => rowOf("Zadzwonić")), (t) => t.width / 2, (t) => t.height / 2,
                         () => editor.selectionMode && page.selectedRow === rowOf("Zadzwonić")),
            wait(() => editor.selectionMode && editor.toolbarMode === "selection" && page.selectedRow === rowOf("Zadzwonić"),
                 "a long press on text selects the block (the selection mode)"),
            () => {
                check(page.editingRow < 0, "nothing is edited")
                check(shown(toolbar(), "toolbarSelectionTools") !== null, "the selection tools are docked")
                check(window.findShown(delegateAt(rowOf("Zadzwonić")), "selectionGripStart") !== null, "with grips")
                const text = textOf(rowOf("Położyć"))
                fingerTap(text, text.width / 3, text.height / 2)
            },
            wait(() => page.selectionAnchorRow === rowOf("Zadzwonić") && page.selectedRow === rowOf("Położyć")
                       && page.editingRow < 0 && editor.selectionMode,
                 "a tap on another block extends the selection (no editing)"),
            () => tap(shown(toolbar(), "selectionCopy")),
            wait(() => clip().indexOf("- [/] Zadzwonić do hydraulika") === 0 && clip().indexOf("- [ ] Położyć hydroizolację") > 0,
                 "Kopiuj copies it"),
            // ---- "Pojedynczo": taps pick single blocks ----------------------------------------------------
            () => {
                const pick = shown(toolbar(), "selectionPickMode")
                check(pick !== null && !pick.checked, "the docked tools have Pojedynczo")
                revealTool(pick)
            },
            () => tap(shown(toolbar(), "selectionPickMode")),
            wait(() => editor.pickMode && shown(toolbar(), "selectionPickMode").checked, "a tap turns it on"),
            () => {
                check(window.findShown(delegateAt(page.selectionLastRow), "selectionGripEnd") === null, "no grips while picking")
                const text = textOf(rowOf("Zadzwonić"))
                fingerTap(text, text.width / 3, text.height / 2)
            },
            wait(() => page.selectionPicked && page.editingRow < 0 && selected().indexOf(rowOf("Zadzwonić")) < 0
                       && selected().indexOf(rowOf("Wywieźć")) >= 0,
                 () => "a tap on a selected block leaves it out, the rest stays: " + JSON.stringify(selected())),
            () => center(rowOf("Ekipa może")),
            () => {
                const text = textOf(rowOf("Ekipa może"))
                fingerTap(text, text.width / 3, text.height / 2)
            },
            wait(() => !page.selectionContiguous && page.selectedRow === rowOf("Ekipa może")
                       && selected().indexOf(rowOf("Ekipa może")) >= 0 && selected().indexOf(rowOf("Notatki z rozmowy")) < 0,
                 () => "a tap on another block picks it, with a gap: " + JSON.stringify(selected())),
            () => tap(shown(toolbar(), "selectionCopy")),
            wait(() => clip().indexOf("- [ ] Wywieźć gruz") === 0 && clip().indexOf("\n- Prace\n\t- [ ] Skuć") > 0
                       && clip().endsWith("\n- Ekipa może zacząć 20 października, cena ==do negocjacji=="),
                 () => "Kopiuj copies the picked blocks: " + JSON.stringify(clip())),
            () => tap(shown(toolbar(), "selectionPickMode")),
            wait(() => !editor.pickMode, "a second tap turns Pojedynczo off"),
            () => center(rowOf("Położyć")),
            () => {
                const text = textOf(rowOf("Położyć"))
                fingerTap(text, text.width / 3, text.height / 2)
            },
            wait(() => !page.selectionPicked && page.selectionAnchorRow === rowOf("Ekipa może") && page.selectedRow === rowOf("Położyć")
                       && endGrip() !== null,
                 () => "then a tap moves the selection's end again, from the block picked last (the grips are back): "
                       + page.selectionAnchorRow + "–" + page.selectedRow),
            // ---- The end grip -------------------------------------------------------------------------------
            // The band ends below Prace's children (a selected parent brings them along). Położyć in
            // the middle keeps its grip and Kolejność, where the finger goes, clear of the view's
            // bottom edge, where a finger held on a grip scrolls the list: centred on Zadzwonić, the
            // grip sat there, the list scrolled after the first move and the row the finger ended on
            // depended on how far it had scrolled before Qt Quick handed the next move over.
            () => {
                page.selectRange(rowOf("Zadzwonić"), rowOf("Położyć")) // as before, for the grip
                center(rowOf("Położyć"))
            },
            wait(() => endGrip() !== null, "the band's end has a grip"),
            () => {
                test.contentBefore = list.contentY
                const grip = endGrip()
                events.touchEvent(grip).press(0, grip, grip.width / 2, grip.height / 2).commit()
            },
            wait(() => editor.dragSelecting, "a finger on the grip drags the selection's end"),
            () => {
                const text = textOf(rowOf("Kolejność"))
                events.touchEvent(text).move(0, text, 30, text.height / 2).commit()
            },
            hold(() => page.selectedRow === rowOf("Kolejność")),
            () => {
                const text = textOf(rowOf("Kolejność"))
                events.touchEvent(text).release(0, text, 30, text.height / 2).commit()
            },
            wait(() => !editor.dragSelecting, "the lifted finger ends the drag"),
            () => check(page.selectedRow === rowOf("Kolejność") && page.selectionAnchorRow === rowOf("Zadzwonić")
                        && Math.abs(list.contentY - test.contentBefore) < 1,
                        "dragging the end grip moves the selection's end: " + page.selectionAnchorRow + "–" + page.selectedRow
                        + ", scrolled " + (list.contentY - test.contentBefore)),
            // ---- Usuń keeps the selection mode (the block before them selected); the toast's Cofnij, Ctrl+Z --
            () => clearToasts(),
            wait(() => toastUndo() === null, "no toast is left"),
            () => {
                test.textBefore = window.pageText()
                revealTool(shown(toolbar(), "selectionDelete"))
            },
            () => tap(shown(toolbar(), "selectionDelete")),
            wait(() => window.pageText().indexOf("Zadzwonić do hydraulika") < 0, "Usuń deletes the selected blocks"),
            () => check(editor.selectionMode && editor.toolbarMode === "selection" && page.selectionCount === 1
                        && page.editingRow < 0,
                        "the block before them is selected, in the selection mode with its tools: " + page.selectionCount),
            wait(() => toastUndo() !== null, "the toast offers Cofnij above the docked tools"),
            () => tap(toastUndo()),
            wait(() => window.pageText() === test.textBefore, "a tap on the toast's Cofnij brings them back"),
            () => {
                check(page.selectionAnchorRow === rowOf("Zadzwonić") && page.selectedRow === rowOf("Kolejność"),
                      "selected again")
                tap(shown(toolbar(), "selectionDelete"))
            },
            wait(() => window.pageText().indexOf("Zadzwonić do hydraulika") < 0, "Usuń deletes them again"),
            () => key(Qt.Key_Z, Qt.ControlModifier),
            wait(() => window.pageText() === test.textBefore, "Ctrl+Z brings them back"),
            () => check(editor.selectionMode && page.selectionAnchorRow === rowOf("Zadzwonić") && page.selectedRow === rowOf("Kolejność"),
                        "selected again, in the selection mode"),
            // ---- Back leaves the selection mode -----------------------------------------------------------
            () => key(Qt.Key_Back),
            wait(() => !editor.selectionMode && page.selectionCount === 0 && editor.toolbarMode === "",
                 "Back leaves the selection mode"),
            // ---- A long press dragged to the view's edge: autoscroll ----------------------------------------
            () => list.positionViewAtBeginning(),
            () => { test.contentBefore = list.contentY },
            ...longPress(textAt(() => rowOf("Zmierzyć")), (t) => t.width / 2, (t) => t.height / 2,
                         () => editor.selectionMode && page.selectedRow === rowOf("Zmierzyć"), [
                () => {
                    check(editor.selectionMode, "the long press selected the block")
                    const text = textOf(rowOf("Kupić płytki"))
                    events.touchEvent(text).move(0, text, text.width / 2, text.height / 2).commit()
                },
                wait(() => page.selectedRow === rowOf("Kupić płytki") && Math.abs(list.contentY - test.contentBefore) < 1,
                     () => "moving the finger without lifting it extends the selection (no scrolling): " + page.selectedRow
                           + ", scrolled " + (list.contentY - test.contentBefore)),
                () => events.touchEvent(list).move(0, list, list.width / 2, list.height - 6).commit(),
                // Held at the edge until the list has scrolled (a frame at a time: slow in sanitizer builds).
                hold(() => list.contentY > test.contentBefore + 100 && page.selectedRow > rowOf("Prace")),
                () => {
                    check(list.contentY > test.contentBefore + 100, "at the view's bottom edge the list scrolls: " + list.contentY)
                    check(page.selectedRow > rowOf("Prace"), "the selection follows the finger: " + page.selectedRow)
                    events.touchEvent(list).release(0, list, list.width / 2, list.height - 6).commit()
                },
            ], true),
            wait(() => !editor.dragSelecting && editor.selectionMode && list.interactive
                       && page.selectionAnchorRow === rowOf("Zmierzyć"),
                 "the lifted finger leaves the selection in the selection mode"),
            () => key(Qt.Key_Back),
            // ---- The editing toolbar's "Zaznacz" ------------------------------------------------------------
            () => {
                test.rowA = rowOf("Skuć")
                center(test.rowA)
                editor.startEditing(test.rowA, -1)
            },
            wait(() => editor.toolbarMode === "block" && shown(toolbar(), "toolbarSelect") !== null, "the editing toolbar has Zaznacz"),
            () => revealTool(shown(toolbar(), "toolbarSelect")),
            () => tap(shown(toolbar(), "toolbarSelect")),
            wait(() => editor.selectionMode && page.selectedRow === test.rowA && page.editingRow < 0,
                 "Zaznacz selects the edited block (the selection mode)"),
            () => {
                const done = shown(toolbar(), "selectionDone")
                check(done !== null && done.mapToItem(null, 0, 0).x < 60, "Gotowe is the bar's first tool")
                tap(done)
            },
            wait(() => !editor.selectionMode && page.selectionCount === 0, "Gotowe leaves the selection mode"),
            // ---- A keyboard on a touch UI: Esc and Shift+↓ give the selection its tools -------------------
            () => editor.startEditing(rowOf("Skuć"), -1),
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus && editor.activeEditor.row === rowOf("Skuć"),
                 "Skuć is edited"),
            () => editKey(Qt.Key_Escape),
            wait(() => page.selectedRow === rowOf("Skuć") && page.editingRow < 0 && editor.selectionMode
                       && editor.toolbarMode === "selection", "Esc selects the block with the selection's tools"),
            () => key(Qt.Key_Down, Qt.ShiftModifier),
            wait(() => page.selectionAnchorRow === rowOf("Skuć") && page.selectedRow === rowOf("Położyć")
                       && editor.toolbarMode === "selection", "Shift+↓ extends it, the tools stay"),
            () => key(Qt.Key_Back),
            // ---- The bullet's menu: Zaznacz -----------------------------------------------------------------
            ...longPress(() => delegateAt(rowOf("Kolejność")), (d) => d.bulletX, (d) => d.firstLineCenter,
                         () => editor.blockMenu !== null && editor.blockMenu.opened),
            wait(() => editor.blockMenu !== null && editor.blockMenu.opened, "a bullet's long press opens the block menu"),
            () => tap(window.menuItemNamed(editor.blockMenu, "blockMenuSelect")),
            wait(() => editor.selectionMode && page.selectedRow === rowOf("Kolejność"), "its Zaznacz selects the block"),
            () => key(Qt.Key_Back),
        ]
    }
}
