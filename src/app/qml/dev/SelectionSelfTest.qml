pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
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
//     selection follows), the band after scrolling far away and back, the scroll bar's thumb dragged
//     (the list scrolls, nothing selected) and the wheel over it, and the floating tools' Więcej
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

    required property ApplicationWindow appWindow
    readonly property EditorPlayground window: test.appWindow as EditorPlayground
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property DevEvents events: DevEvents {}
    property int rowA: -1
    property int rowB: -1
    property int rowC: -1
    property real contentBefore: 0
    /// Where the scroll bar's thumb was pressed (in the bar).
    property point barPress
    /// A point mapped to another item (read through a typed property).
    property point mapped
    property string textBefore: ""
    /// A toast's Cofnij kept after its edit was undone another way.
    property EditorToastAction staleUndo: null
    /// A caret's rectangle (read through a typed property).
    property rect caret

    // ---- Helpers ------------------------------------------------------------------------------------
    readonly property ListView list: test.editor.listView
    function check(ok: bool, what: string): void { window.check(ok, what) }
    function rowOf(prefix: string): int { return window.rowOf(prefix) }
    function info(row: int): DevBlock { return window.info(row) }
    function textOf(row: int): Item { return window.renderedOf(row) }
    function delegateAt(row: int): BlockDelegate { return window.delegateAt(row) }
    function edit(): InlineEditor { return editor.activeEditor }
    function menuItem(menu: T.Menu, name: string): T.MenuItem { return window.menuItemNamed(menu, name) }
    function selected(): list<int> { return page.selectedRows() }
    /// The selected rows as JSON.stringify wrote them ("[3,4]").
    function selectedText(): string { return "[" + selected().join(",") + "]" }
    /// The clipboard's text exactly (no-break spaces kept): this app's copies are read as written.
    function clip(): string { return page.clipboardText() }
    function key(k: int, modifiers: int): void { EditorInput.sendKey(list, k, modifiers, "") }
    function editKey(k: int, modifiers: int): void { window.realKey(k, modifiers, "") }
    function edgeOf(row: int): string {
        const item = delegateAt(row)
        return item ? item.selectionEdge : "?"
    }
    function center(row: int): void {
        list.positionViewAtIndex(row, ListView.Center)
    }
    /// Visible rows that can be selected (not the page's properties).
    function selectableCount(): int {
        let n = 0
        for (let r = 0; r < page.blocks.count; ++r) {
            const b = info(r)
            if (b.visible && b.view !== "frontmatter")
                ++n
        }
        return n
    }
    /// A finger put on the middle of the text of `row` (a long press: the step after it holds the
    /// finger until the press took effect — the selection mode, a menu —, as a user lifts it once
    /// that shows; then releaseText()). (Held for a fixed number of steps, it was lifted too early
    /// in slow sanitizer runs now and then: a tap.)
    function pressText(row: int): void {
        const item = textOf(row)
        check(item !== null, "something to hold")
        if (item)
            events.touchPress(item, item.width / 2, item.height / 2)
    }
    function releaseText(row: int): void {
        const item = textOf(row)
        if (item)
            events.touchRelease(item, item.width / 2, item.height / 2)
    }
    /// The same on the bullet of `row`.
    function pressBullet(row: int): void {
        const item = delegateAt(row)
        check(item !== null, "something to hold")
        if (item)
            events.touchPress(item, item.bulletX, item.firstLineCenter)
    }
    function releaseBullet(row: int): void {
        const item = delegateAt(row)
        if (item)
            events.touchRelease(item, item.bulletX, item.firstLineCenter)
    }
    function fingerTap(item: Item, x: real, y: real): void {
        events.touchPress(item, x, y)
        events.touchRelease(item, x, y)
    }
    function tap(item: Item): void {
        if (!item)
            return
        if (Theme.touchUi)
            fingerTap(item, item.width / 2, item.height / 2)
        else
            events.mouseClick(item, item.width / 2, item.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
    }
    /// A Ctrl+click on the text of `row` (picks it into or out of the selection).
    function ctrlClick(row: int): void {
        const text = textOf(row)
        check(text !== null, "the text of row " + row)
        if (text)
            events.mouseClick(text, 30, text.height / 2, Qt.LeftButton, Qt.ControlModifier, -1)
    }
    function rightClick(item: Item, x: real, y: real): void {
        if (item)
            events.mouseClick(item, x, y, Qt.RightButton, Qt.NoModifier, -1)
    }
    /// The x of `item` in the window.
    function windowX(item: Item): real {
        test.mapped = item.mapToItem(null, 0, 0)
        return test.mapped.x
    }
    function partsOf(text: string, separator: string): list<string> { return Js.split(text, separator) }
    /// The picked blocks (touch) are on the clipboard.
    function pickedCopied(): bool {
        return Js.indexOf(clip(), "- [ ] Wywieźć gruz") === 0 && Js.indexOf(clip(), "\n- Prace\n\t- [ ] Skuć") > 0
               && Js.endsWith(clip(), "\n- Ekipa może zacząć 20 października, cena ==do negocjacji==")
    }
    function textMenu(): BMenu { return editor.textMenu }
    function blockMenu(): BlockMenu { return editor.blockMenu }
    function highlightMenu(): BMenu { return editor.highlightMenu }
    function scrollBar(): T.ScrollBar { return editor.scrollBar }
    function toolbar(): Item { return window.findItem(editor, "editToolbarSlot") }
    function shown(item: Item, name: string): Item { return window.findShown(item, name) }
    function shownText(item: Item, name: string): Text { return window.findShown(item, name) as Text }
    /// The floating selection tools (desktop), their menu.
    function floatingTools(): SelectionToolbar { return window.findShown(editor.dockHost, "floatingSelectionTools") as SelectionToolbar }
    function toolsMenu(): BMenu {
        const tools = floatingTools()
        return tools ? tools.menu : null
    }
    function toolsMenuOpen(): bool {
        const tools = floatingTools()
        return tools !== null && toolsMenu() !== null && toolsMenu().opened
    }
    /// Scroll the docked toolbar's row so that `button` is on screen.
    function revealTool(button: Item): void {
        const flick = window.findItem(editor, "editToolbarFlick") as Flickable
        if (!flick || !button)
            return
        test.mapped = button.mapToItem(flick.contentItem, 0, 0)
        flick.contentX = Math.max(0, Math.min(test.mapped.x - Theme.space8, flick.contentWidth - flick.width))
    }
    /// The shown grip at the end of the selection's band, or null.
    function endGrip(): Item { return window.findShown(delegateAt(page.selectionLastRow), "selectionGripEnd") }
    /// The button of a toast ("Cofnij"), when one is shown (clearToasts() first: one at a time).
    function toastUndo(): T.AbstractButton { return window.findShown(window.toastHost, "toastAction") as T.AbstractButton }
    function clearToasts(): void { window.toastHost.clear() }

    function steps(): void {
        if (window.touchRun)
            touchSteps()
        else
            desktopSteps()
    }
    Component.onCompleted: window.runSteps("selectiontest")

    // ---- Desktop ------------------------------------------------------------------------------------
    function desktopSteps(): void {
        if (window.at()) window.waitStep(page.loaded && rowOf("Przygotowanie") > 0, "the outline is open")
        // ---- Esc, Shift+↓ twice, Ctrl+C -----------------------------------------------------------
        if (window.at()) {
            test.rowA = rowOf("Przygotowanie")
            center(test.rowA)
            editor.startEditing(test.rowA, -1)
        }
        if (window.at()) window.waitStep(edit() !== null && edit().activeFocus, "a block is edited")
        if (window.at()) editKey(Qt.Key_Escape, 0)
        if (window.at()) {
            check(page.selectedRow === test.rowA && page.editingRow < 0, "Esc selects the block")
            check(selected().length === 7, "with its children (folded or not): " + selected().length)
            key(Qt.Key_Return, Qt.ControlModifier)
        }
        if (window.at()) {
            check(info(test.rowA).taskStatus === "todo" && info(rowOf("Zmierzyć")).taskStatus === "done"
                  && info(rowOf("Zadzwonić")).taskStatus === "doing" && info(rowOf("Wywieźć")).taskStatus === "todo",
                  "Ctrl+Enter on a parent selected alone changes it, not the children it brings along")
            key(Qt.Key_Z, Qt.ControlModifier)
        }
        if (window.at()) {
            check(info(test.rowA).taskStatus === "" && page.selectedRow === test.rowA && selected().length === 7,
                  "undone, still selected")
            key(Qt.Key_Down, Qt.ShiftModifier)
        }
        if (window.at()) {
            check(page.selectedRow === rowOf("Prace") && page.selectionAnchorRow === test.rowA,
                  "Shift+↓ jumps over the selected children to the next block: " + page.selectedRow)
            key(Qt.Key_Down, Qt.ShiftModifier)
        }
        if (window.at()) {
            check(page.selectedRow === rowOf("Notatki z rozmowy"), "Shift+↓ again: " + page.selectedRow)
            check(edgeOf(test.rowA) === "first" && edgeOf(rowOf("Prace")) === "middle"
                  && edgeOf(rowOf("Ekipa może")) === "last", "one band over the range")
            window.lastNotice = ""
            key(Qt.Key_C, Qt.ControlModifier)
        }
        if (window.at() && !window.waiting(Js.startsWith(clip(), "- Przygotowanie ^przyg\n\t- [x] Zmierzyć ściany") && Js.indexOf(clip(), "\n- Prace\n") > 0
                   && Js.endsWith(clip(), "- Notatki z rozmowy z ekipą — zobacz ((przyg))\n\t- Ekipa może zacząć 20 października, cena ==do negocjacji==")))
            window.checkWait("Ctrl+C copies the three blocks with their children as Markdown: " + Js.stringify(Js.slice(clip(), 0, 60)))
        if (window.at()) {
            check(window.lastNotice !== "", "and says so: " + window.lastNotice)
            key(Qt.Key_Escape, 0)
        }
        if (window.at()) check(page.selectedRow < 0 && page.selectionCount === 0, "Esc clears the selection")
        // ---- Shift+↑ from the first line of an edited block; Shift+↓ from inside one ---------------
        if (window.at()) editor.startEditing(rowOf("Skuć"), 0)
        if (window.at()) window.waitStep(edit() !== null && edit().activeFocus, "Skuć is edited")
        if (window.at()) editKey(Qt.Key_Up, Qt.ShiftModifier)
        if (window.at()) check(page.editingRow < 0 && page.selectionAnchorRow === rowOf("Skuć") && page.selectedRow === rowOf("Prace"),
                    "Shift+↑ at the start of the first line selects the block and the one above")
        if (window.at()) editor.startEditing(rowOf("Położyć"), 3)
        if (window.at()) window.waitStep(edit() !== null && edit().activeFocus, "Położyć is edited")
        if (window.at()) editKey(Qt.Key_Down, Qt.ShiftModifier)
        if (window.at()) {
            const ed = edit()
            check(page.editingRow === rowOf("Położyć") && ed !== null && ed.selectionStart === 3 && ed.selectionEnd - ed.length === 0,
                  "Shift+↓ on the last line first selects the text to the block's end")
            editKey(Qt.Key_Down, Qt.ShiftModifier)
        }
        if (window.at()) check(page.editingRow < 0 && page.selectionAnchorRow === rowOf("Położyć") && page.selectedRow === rowOf("Podlać"),
                    "then the next block")
        // ---- Ctrl+A twice -------------------------------------------------------------------------
        if (window.at()) editor.startEditing(rowOf("Kolejność"), 2)
        if (window.at()) window.waitStep(edit() !== null && edit().activeFocus, "Kolejność is edited")
        if (window.at()) editKey(Qt.Key_A, Qt.ControlModifier)
        if (window.at()) {
            const ed = edit()
            check(page.editingRow === rowOf("Kolejność") && ed !== null && ed.selectionStart === 0 && ed.selectionEnd - ed.length === 0,
                  "the first Ctrl+A selects the block's text")
            editKey(Qt.Key_A, Qt.ControlModifier)
        }
        if (window.at()) check(page.editingRow < 0 && page.selectionCount === selectableCount(),
                    "the second Ctrl+A selects every block: " + page.selectionCount + " of " + selectableCount())
        if (window.at()) key(Qt.Key_Escape, 0)
        // ---- Ctrl+click picks single blocks: gaps, copying, no moving, Shift+click after it ------------
        if (window.at()) {
            center(rowOf("Zadzwonić"))
            ctrlClick(rowOf("Zmierzyć"))
        }
        if (window.at()) {
            check(page.selectedRow === rowOf("Zmierzyć") && page.editingRow < 0, "Ctrl+click selects a block, nothing edited")
            ctrlClick(rowOf("Zadzwonić"))
        }
        if (window.at()) ctrlClick(rowOf("Skuć"))
        if (window.at()) {
            check(page.selectionCount === 3 && page.selectionPicked && !page.selectionContiguous
                  && selectedText() === "[" + rowOf("Zmierzyć") + "," + rowOf("Zadzwonić") + "," + rowOf("Skuć") + "]",
                  "Ctrl+clicks pick three blocks with gaps between them: " + selectedText())
            check(edgeOf(rowOf("Zmierzyć")) === "single" && edgeOf(rowOf("Kupić płytki")) === ""
                  && edgeOf(rowOf("Zadzwonić")) === "single" && edgeOf(rowOf("Prace")) === ""
                  && edgeOf(rowOf("Skuć")) === "single", "each has its own band, the gaps none")
            const tools = floatingTools()
            const count = tools ? shownText(tools, "selectionCount") : null
            check(count !== null && Js.indexOf(count.text, "3") >= 0, "the floating tools count them: " + (count ? count.text : ""))
            window.lastNotice = ""
            key(Qt.Key_C, Qt.ControlModifier)
        }
        if (window.at() && !window.waiting(clip() === "- [x] Zmierzyć ściany i podłogę ✅ 2026-10-12\n- [/] Zadzwonić do hydraulika ⏰ 2026-10-15 18:00 #telefon\n- [ ] Skuć stare płytki 📅 2026-10-13"))
            window.checkWait("Ctrl+C copies the picked blocks in their order: " + Js.stringify(clip()))
        if (window.at()) {
            test.textBefore = window.pageText()
            window.lastNotice = ""
            key(Qt.Key_Tab, 0)
        }
        if (window.at()) {
            check(window.pageText() === test.textBefore && window.lastNotice === editor.gapReason,
                  "Tab does not indent blocks with gaps between them, and says why: " + window.lastNotice)
            window.lastNotice = ""
            key(Qt.Key_Up, Qt.AltModifier)
        }
        if (window.at()) {
            check(window.pageText() === test.textBefore && window.lastNotice === editor.gapReason, "nor does Alt+↑ move them")
            tap(window.findShown(floatingTools(), "selectionMore"))
        }
        if (window.at()) window.waitStep(toolsMenuOpen(), "the floating tools' Więcej opens")
        if (window.at()) {
            const tools = floatingTools()
            const up = menuItem(toolsMenu(), "selectionMoveUp")
            check(up !== null && "" + DevTest.property(up, "unavailable") === editor.gapReason && up.opacity < 1,
                  "its Przenieś w górę is shown disabled, with the reason")
            window.lastNotice = ""
            tap(up)
        }
        if (window.at()) check(window.pageText() === test.textBefore && window.lastNotice === editor.gapReason,
                    "a click on it moves nothing and says why")
        if (window.at()) ctrlClick(rowOf("Zadzwonić"))
        if (window.at()) {
            check(selectedText() === "[" + rowOf("Zmierzyć") + "," + rowOf("Skuć") + "]",
                  "a second Ctrl+click leaves a picked block out: " + selectedText())
            ctrlClick(rowOf("Ekipa może"))
        }
        if (window.at()) {
            check(page.selectedRow === rowOf("Ekipa może") && page.selectionCount === 3, "another one picked")
            const text = textOf(rowOf("Położyć"))
            events.mouseClick(text, 30, text.height / 2, Qt.LeftButton, Qt.ShiftModifier, -1)
        }
        if (window.at()) check(!page.selectionPicked && page.selectionAnchorRow === rowOf("Ekipa może") && page.selectedRow === rowOf("Położyć"),
                    "Shift+click then selects a range from the block picked last: " + page.selectionAnchorRow + "–" + page.selectedRow)
        if (window.at()) key(Qt.Key_Escape, 0)
        // ---- Shift+click; Delete and Ctrl+Z ---------------------------------------------------------
        if (window.at()) {
            test.rowA = rowOf("Zmierzyć")
            center(test.rowA)
            editor.startEditing(test.rowA, -1)
        }
        if (window.at()) window.waitStep(edit() !== null && edit().activeFocus, "Zmierzyć is edited")
        if (window.at()) {
            const text = textOf(rowOf("Wywieźć"))
            events.mouseClick(text, 30, text.height / 2, Qt.LeftButton, Qt.ShiftModifier, -1)
        }
        if (window.at()) {
            check(page.editingRow < 0 && page.selectionAnchorRow === test.rowA && page.selectedRow === rowOf("Wywieźć"),
                  "Shift+click selects from the edited block to the clicked one")
            test.textBefore = window.pageText()
            key(Qt.Key_Delete, 0)
        }
        if (window.at()) window.waitStep(Js.indexOf(window.pageText(), "Zadzwonić do hydraulika") < 0, "Delete removes the selected blocks")
        if (window.at()) {
            check(page.selectedRow === rowOf("Przygotowanie"), "the block before them is selected")
            key(Qt.Key_Z, Qt.ControlModifier)
        }
        if (window.at()) window.waitStep(window.pageText() === test.textBefore, "Ctrl+Z brings them back byte for byte")
        if (window.at()) check(page.selectionAnchorRow === rowOf("Zmierzyć") && page.selectedRow === rowOf("Wywieźć"),
                    "selected again")
        // ---- The toast's "Cofnij" (a click on its button), and a stale one --------------------------
        if (window.at()) clearToasts()
        if (window.at()) window.waitStep(toastUndo() === null, "no toast is left")
        if (window.at()) {
            window.lastNoticeAction = null
            key(Qt.Key_Delete, 0)
        }
        if (window.at() && !window.waiting(Js.indexOf(window.pageText(), "Zadzwonić do hydraulika") < 0 && toastUndo() !== null))
            window.checkWait("Delete again: the toast says so and offers Cofnij: “" + window.lastNotice + "”")
        if (window.at()) {
            const button = toastUndo()
            check(window.lastNoticeAction !== null && button.text === window.lastNoticeAction.text,
                  "the toast's button is the editor's action: " + window.lastNotice)
            events.mouseClick(button, button.width / 2, button.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
        }
        if (window.at()) window.waitStep(window.pageText() === test.textBefore, "the toast's Cofnij brings them back byte for byte")
        if (window.at()) check(page.selectionAnchorRow === rowOf("Zmierzyć") && page.selectedRow === rowOf("Wywieźć"),
                    "and selects them again")
        if (window.at()) {
            window.lastNoticeAction = null
            key(Qt.Key_Delete, 0)
        }
        if (window.at()) window.waitStep(window.lastNoticeAction !== null, "deleted once more")
        if (window.at()) {
            test.staleUndo = window.lastNoticeAction
            key(Qt.Key_Z, Qt.ControlModifier) // undone already (another way)
        }
        if (window.at()) window.waitStep(window.pageText() === test.textBefore, "Ctrl+Z undoes the deletion")
        if (window.at()) {
            window.lastNotice = ""
            test.staleUndo.run()
        }
        if (window.at()) check(window.pageText() === test.textBefore && window.lastNotice !== "",
                    "a Cofnij whose edit was undone already undoes nothing else, and says so: " + window.lastNotice)
        // ---- Alt+↓ and Tab on a range ------------------------------------------------------------
        if (window.at()) {
            page.selectRange(rowOf("Skuć"), rowOf("Położyć"))
            list.forceActiveFocus()
            test.textBefore = window.pageText()
            key(Qt.Key_Down, Qt.AltModifier)
        }
        if (window.at()) {
            check(rowOf("Podlać") < rowOf("Skuć") && rowOf("Skuć") + 1 === rowOf("Położyć"),
                  "Alt+↓ moves the range below its next sibling (with that one's child)")
            check(page.selectionAnchorRow === rowOf("Skuć") && page.selectedRow === rowOf("Położyć"), "the range stays selected")
            key(Qt.Key_Tab, 0)
        }
        if (window.at()) {
            check(info(rowOf("Skuć")).depth === 2 && info(rowOf("Położyć")).depth === 2, "Tab indents the range")
            editor.startEditing(rowOf("Ekipa może"), -1) // another block edited meanwhile
        }
        if (window.at()) window.waitStep(edit() !== null && edit().activeFocus && edit().row === rowOf("Ekipa może"), "another block is edited")
        if (window.at()) editKey(Qt.Key_Z, Qt.ControlModifier)
        if (window.at()) {
            check(page.editingRow < 0 && page.selectionAnchorRow === rowOf("Skuć") && page.selectedRow === rowOf("Położyć")
                  && list.activeFocus && info(rowOf("Skuć")).depth === 1,
                  "Ctrl+Z in it undoes Tab: the range selected again, nothing edited, the keys with the selection")
            key(Qt.Key_Z, Qt.ControlModifier)
        }
        if (window.at()) window.waitStep(window.pageText() === test.textBefore, "undone")
        // ---- Ctrl+V over a selection ---------------------------------------------------------------
        if (window.at()) {
            page.selectRange(rowOf("Skuć"), rowOf("Skuć"))
            list.forceActiveFocus()
            key(Qt.Key_C, Qt.ControlModifier)
        }
        if (window.at()) window.waitStep(clip() === "- [ ] Skuć stare płytki 📅 2026-10-13", "Ctrl+C copies one block")
        if (window.at()) {
            page.selectedRow = rowOf("Położyć")
            key(Qt.Key_V, Qt.ControlModifier)
        }
        if (window.at()) {
            check(rowOf("Położyć") < 0 && partsOf(window.pageText(), "Skuć stare płytki").length === 3,
                  "Ctrl+V replaces the selected block with the copied one")
            key(Qt.Key_Z, Qt.ControlModifier)
        }
        if (window.at()) window.waitStep(window.pageText() === test.textBefore, "undone")
        // ---- One copied block pasted into an edited empty bullet: its text, no second marker --------
        if (window.at()) { test.rowA = page.insertBlockAfter(rowOf("Ekipa może"), "") }
        if (window.at()) window.waitStep(edit() !== null && edit().activeFocus && edit().row === test.rowA, "a new empty bullet is edited")
        if (window.at()) editKey(Qt.Key_V, Qt.ControlModifier)
        if (window.at() && !window.waiting(info(test.rowA).taskStatus === "todo" && Js.indexOf(info(test.rowA).plainText, "Skuć stare płytki") === 0
                   && partsOf(window.pageText(), "\t- [ ] Skuć stare płytki 📅 2026-10-13\n").length === 3))
            window.checkWait("Ctrl+V pastes the copied task into it, its checkbox and no second marker: "
                   + Js.stringify(info(test.rowA).plainText) + " " + info(test.rowA).taskStatus)
        if (window.at()) check(page.editingRow === test.rowA && Js.indexOf(window.pageText(), "- - ") < 0, "the block is still edited")
        if (window.at()) {
            page.undo()
            page.undo()
            page.stopEditing()
        }
        if (window.at()) window.waitStep(window.pageText() === test.textBefore, "undone")
        // ---- Shift+click on a calculation and on a table (frames, not text) ---------------------------
        if (window.at()) {
            test.rowA = rowOf("Koszty")
            center(window.viewRow("calc"))
            editor.startEditing(test.rowA, -1)
        }
        if (window.at()) window.waitStep(edit() !== null && edit().activeFocus && edit().row === test.rowA, "the heading Koszty is edited")
        if (window.at()) {
            const calc = window.findItem(delegateAt(window.viewRow("calc")), "calcInput") as Item
            check(calc !== null, "the calculation's lines")
            if (calc)
                events.mouseClick(calc, 20, 8, Qt.LeftButton, Qt.ShiftModifier, -1)
        }
        if (window.at()) check(page.editingRow < 0 && page.selectionAnchorRow === test.rowA && page.selectedRow === window.viewRow("calc"),
                    "Shift+click on a calculation selects from the edited heading to it, nothing edited: "
                    + page.selectionAnchorRow + "–" + page.selectedRow)
        if (window.at()) {
            center(window.viewRow("table"))
            const table = delegateAt(window.viewRow("table"))
            if (table)
                events.mouseClick(table, table.width / 3, table.height / 2, Qt.LeftButton, Qt.ShiftModifier, -1)
        }
        if (window.at()) check(page.selectionAnchorRow === test.rowA && page.selectedRow === window.viewRow("table")
                    && editor.editingTable === null && page.editingRow < 0,
                    "Shift+click on a table moves the selection's end there, no cell edited: " + page.selectedRow)
        if (window.at()) key(Qt.Key_Escape, 0)
        // ---- In-block Ctrl+C / Ctrl+X keep no-break spaces ------------------------------------------
        if (window.at()) {
            test.rowA = page.insertBlockAfter(rowOf("Ekipa może"), "Jadę w\u00a0góry z\u00a0Anią")
        }
        if (window.at()) window.waitStep(edit() !== null && edit().activeFocus && edit().row === test.rowA, "a block with no-break spaces is edited")
        if (window.at()) {
            edit().select(5, 13)
            editKey(Qt.Key_C, Qt.ControlModifier)
        }
        if (window.at() && !window.waiting(clip() === "w\u00a0góry z"))
            window.checkWait("Ctrl+C copies the exact characters: " + Js.stringify(clip()))
        if (window.at()) editKey(Qt.Key_X, Qt.ControlModifier)
        if (window.at()) window.waitStep(Js.indexOf(window.pageText(), "- Jadę \u00a0Anią\n") > 0 && clip() === "w\u00a0góry z", "Ctrl+X cuts them")
        // ---- The edited text's menu -------------------------------------------------------------------
        if (window.at()) {
            const ed = edit()
            ed.select(0, 4)
            test.caret = ed.positionToRectangle(2)
            events.mouseClick(ed, test.caret.x, test.caret.y + test.caret.height / 2, Qt.RightButton, Qt.NoModifier, -1)
        }
        if (window.at()) window.waitStep(textMenu() !== null && textMenu().opened, "a right-click in the edited text opens its menu")
        if (window.at()) {
            check(edit().selectionStart === 0 && edit().selectionEnd === 4,
                  "the selection stays while the menu is open")
            check(!blockMenu() || !blockMenu().opened, "and no block menu")
            tap(window.menuItemNamed(textMenu(), "editTextCopy"))
        }
        if (window.at()) window.waitStep(clip() === "Jadę" && !textMenu().opened, "its Kopiuj copies the selected text")
        // ---- Its "Zakreśl…" (docs/UI.md "Coloured highlights") -----------------------------------
        if (window.at()) {
            const ed = edit()
            ed.select(0, 4)
            test.caret = ed.positionToRectangle(2)
            events.mouseClick(ed, test.caret.x, test.caret.y + test.caret.height / 2, Qt.RightButton, Qt.NoModifier, -1)
        }
        if (window.at()) window.waitStep(textMenu() !== null && textMenu().opened, "the edited text's menu again")
        if (window.at()) tap(window.menuItemNamed(textMenu(), "editTextHighlight"))
        if (window.at()) window.waitStep(highlightMenu() !== null && highlightMenu().opened && !textMenu().opened, "Zakreśl… opens the colours")
        if (window.at()) {
            check(window.menuItemNamed(highlightMenu(), "highlight-default") !== null
                  && window.menuItemNamed(highlightMenu(), "highlight-remove") !== null,
                  "with the default colour and Usuń zakreślenie")
            tap(window.menuItemNamed(highlightMenu(), "highlight-red"))
        }
        if (window.at() && !window.waiting(Js.indexOf(window.pageText(), "- <mark style=\"background: #FF5582A6;\">Jadę</mark> \u00a0Anią\n") > 0))
            window.checkWait("Czerwony highlights the selection: " + Js.stringify(Js.slice(window.pageText(), 0, 160)))
        if (window.at()) window.waitStep(edit() !== null && edit().selectedText === "Jadę", "the highlighted text stays selected")
        if (window.at()) page.undo()
        if (window.at()) window.waitStep(Js.indexOf(window.pageText(), "- Jadę \u00a0Anią\n") > 0, "the highlight is one undo step")
        if (window.at()) {
            page.undo()
            page.stopEditing()
        }
        // ---- The block menu: of another block, of a selected one --------------------------------------
        if (window.at()) {
            center(rowOf("Plan remontu"))
            const text = textOf(rowOf("Plan remontu"))
            rightClick(text, 8, 10) // (on "Plan", not on a link: a link has a menu of its own)
        }
        if (window.at()) window.waitStep(window.blockMenuOpen(), "a right-click on a paragraph opens the block menu")
        if (window.at()) {
            const copy = menuItem(blockMenu(), "blockMenuCopy")
            check(copy !== null && !blockMenu().inSelection, "with Kopiuj")
            tap(copy)
        }
        if (window.at()) window.waitStep(Js.indexOf(clip(), "Plan remontu na październik.") === 0, "Kopiuj copies that block")
        if (window.at()) {
            page.selectRange(rowOf("Zadzwonić"), rowOf("Prace"))
            const text = textOf(rowOf("Wywieźć"))
            rightClick(text, 20, text.height / 2)
        }
        if (window.at()) window.waitStep(window.blockMenuOpen() && blockMenu().inSelection, "the menu of a selected block acts on the selection")
        if (window.at()) tap(menuItem(blockMenu(), "blockMenuCopyText"))
        if (window.at() && !window.waiting(Js.indexOf(clip(), "☐ Zadzwonić do hydraulika") === 0 && Js.indexOf(clip(), "\n☐ Wywieźć gruz") > 0
                   && Js.indexOf(clip(), "\n• Prace\n  ☐ Skuć") > 0))
            window.checkWait("Kopiuj jako tekst copies the selection as text: " + Js.stringify(Js.slice(clip(), 0, 80)))
        // ---- The floating tools ---------------------------------------------------------------------
        if (window.at()) {
            const tools = floatingTools()
            check(tools !== null, "two or more blocks selected: the floating tools")
            tap(window.findShown(tools, "selectionCopy"))
        }
        if (window.at()) window.waitStep(Js.indexOf(clip(), "- [/] Zadzwonić do hydraulika") === 0, "their Kopiuj copies the selection as Markdown")
        // ---- The band after scrolling far away and back -----------------------------------------------
        if (window.at()) list.positionViewAtEnd()
        if (window.at()) list.positionViewAtBeginning()
        if (window.at()) center(rowOf("Wywieźć"))
        if (window.at()) check(window.findShown(delegateAt(rowOf("Wywieźć")), "selectionBand") !== null && edgeOf(rowOf("Prace")) !== "",
                    "the band is there again after its blocks were scrolled away")
        if (window.at()) key(Qt.Key_Escape, 0)
        // ---- A mouse drag from one block into another; at the view's edge the list scrolls -------------
        if (window.at()) list.positionViewAtBeginning()
        if (window.at()) {
            test.contentBefore = list.contentY
            test.rowA = rowOf("Zmierzyć")
            const text = textOf(test.rowA)
            events.mousePress(text, 40, text.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
            events.mouseMove(text, 60, text.height / 2 + 2, -1, Qt.LeftButton, Qt.NoModifier)
        }
        if (window.at()) {
            const text = textOf(rowOf("Zadzwonić"))
            events.mouseMove(text, 60, text.height / 2, -1, Qt.LeftButton, Qt.NoModifier)
        }
        if (window.at()) {
            check(page.editingRow < 0 && page.selectionAnchorRow === test.rowA && page.selectedRow === rowOf("Zadzwonić"),
                  "dragging into another block selects the range")
            check(Math.abs(list.contentY - test.contentBefore) < 1, "a mouse drag does not scroll the list")
            events.mouseMove(list, list.width / 2, list.height - 4, -1, Qt.LeftButton, Qt.NoModifier)
        }
        // Held at the edge until the list has scrolled (a frame at a time: slow in sanitizer builds).
        if (window.at()) window.waiting(list.contentY > test.contentBefore + 100 && page.selectedRow > rowOf("Notatki z rozmowy"))
        if (window.at()) {
            check(list.contentY > test.contentBefore + 100, "at the view's bottom edge the list scrolls: " + list.contentY)
            check(page.selectedRow > rowOf("Notatki z rozmowy"), "and the selection follows the pointer: " + page.selectedRow)
            const y = list.height - 4
            events.mouseRelease(list, list.width / 2, y, Qt.LeftButton, Qt.NoModifier, -1)
        }
        if (window.at()) {
            check(!editor.dragSelecting && page.editingRow < 0 && page.selectionAnchorRow === test.rowA,
                  "the release ends the drag, nothing is edited")
            key(Qt.Key_Escape, 0)
        }
        // ---- The scroll bar (BRowScrollBar): its thumb dragged, the wheel over it --------------------
        if (window.at()) list.positionViewAtBeginning()
        if (window.at()) {
            const bar = scrollBar()
            check(bar !== null && bar.objectName === "rowScrollBar" && bar.visible && bar.size < 1,
                  "the page has a row scroll bar")
            test.contentBefore = list.contentY
            test.barPress = bar.mapFromItem(bar.contentItem, bar.contentItem.width / 2, bar.contentItem.height / 2)
            events.mousePress(bar, test.barPress.x, test.barPress.y, Qt.LeftButton, Qt.NoModifier, -1)
        }
        if (window.at()) events.mouseMove(scrollBar(), test.barPress.x, test.barPress.y + 30, -1, Qt.LeftButton, Qt.NoModifier)
        if (window.at()) events.mouseMove(scrollBar(), test.barPress.x, test.barPress.y + 60, -1, Qt.LeftButton, Qt.NoModifier)
        if (window.at()) window.waitStep(list.contentY > test.contentBefore + 30, "dragging the scroll bar's thumb scrolls the list")
        if (window.at()) {
            check(scrollBar().pressed && page.selectionCount === 0 && !editor.dragSelecting && page.editingRow < 0,
                  "and selects nothing")
            events.mouseRelease(scrollBar(), test.barPress.x, test.barPress.y + 60, Qt.LeftButton, Qt.NoModifier, -1)
        }
        if (window.at()) {
            check(!scrollBar().pressed && page.selectionCount === 0, "the release selects nothing either")
            test.contentBefore = list.contentY
            const bar = scrollBar()
            events.mouseWheel(bar, bar.width / 2, bar.height / 2, Qt.NoButton, Qt.NoModifier, 0, -120, -1)
        }
        if (window.at()) window.waitStep(list.contentY > test.contentBefore + 10, "the wheel over the scroll bar scrolls the list")
        // ---- A document: a drag over three paragraphs --------------------------------------------------
        if (window.at()) { page.path = host.documentPath }
        if (window.at()) window.waitStep(page.loaded && page.documentMode && rowOf("Pierwszego dnia") > 0, "the document is open")
        if (window.at()) list.positionViewAtBeginning()
        if (window.at()) {
            test.rowA = rowOf("Trzy dni")
            test.rowB = rowOf("Pierwszego dnia")
            test.contentBefore = list.contentY
            const text = textOf(test.rowA)
            events.mousePress(text, 30, 10, Qt.LeftButton, Qt.NoModifier, -1)
            events.mouseMove(text, 50, 14, -1, Qt.LeftButton, Qt.NoModifier)
        }
        if (window.at()) {
            const text = textOf(rowOf("Plan"))
            events.mouseMove(text, 40, text.height / 2, -1, Qt.LeftButton, Qt.NoModifier)
        }
        if (window.at()) {
            const text = textOf(test.rowB)
            events.mouseMove(text, 80, 10, -1, Qt.LeftButton, Qt.NoModifier)
            events.mouseRelease(text, 80, 10, Qt.LeftButton, Qt.NoModifier, -1)
        }
        if (window.at()) {
            check(page.selectionAnchorRow === test.rowA && page.selectedRow === test.rowB && page.selectionCount === 3,
                  "a drag over three paragraphs selects them: " + page.selectionCount)
            check(edgeOf(test.rowA) === "first" && edgeOf(rowOf("Plan")) === "middle" && edgeOf(test.rowB) === "last",
                  "on one band")
            check(Math.abs(list.contentY - test.contentBefore) < 1 && page.editingRow < 0, "nothing scrolled, nothing edited")
            key(Qt.Key_C, Qt.ControlModifier)
        }
        if (window.at()) window.waitStep(Js.indexOf(clip(), "Trzy dni w Zakopanem") === 0 && Js.indexOf(clip(), "\n\n## Plan\n\nPierwszego dnia") > 0, "copied as Markdown with its blank lines")
        // A document's paragraphs have no outline: Więcej offers no indents for them.
        if (window.at()) tap(window.findShown(floatingTools(), "selectionMore"))
        if (window.at()) window.waitStep(toolsMenuOpen(), "the floating tools' Więcej opens")
        if (window.at()) {
            const tools = floatingTools()
            const indent = menuItem(toolsMenu(), "selectionIndent")
            const outdent = menuItem(toolsMenu(), "selectionOutdent")
            check(indent !== null && !indent.visible && outdent !== null && !outdent.visible,
                  "paragraphs of a document: no Zwiększ / Zmniejsz wcięcie")
            toolsMenu().close()
        }
    }

    // ---- Touch --------------------------------------------------------------------------------------
    // (Qt Quick hands a finger's moves over with the next frame, not when they are sent: whatever a
    // gesture does is waited for, in slow sanitizer runs too.)
    function touchSteps(): void {
        if (window.at()) window.waitStep(page.loaded && rowOf("Przygotowanie") > 0, "the outline is open")
        if (window.at()) center(rowOf("Zadzwonić"))
        // ---- A long press on a block's text: the selection mode ----------------------------------------
        if (window.at()) pressText(rowOf("Zadzwonić"))
        if (window.at()) window.waiting(editor.selectionMode && page.selectedRow === rowOf("Zadzwonić"))
        if (window.at()) releaseText(rowOf("Zadzwonić"))
        if (window.at()) window.waitStep(editor.selectionMode && editor.toolbarMode === "selection" && page.selectedRow === rowOf("Zadzwonić"), "a long press on text selects the block (the selection mode)")
        if (window.at()) {
            check(page.editingRow < 0, "nothing is edited")
            check(shown(toolbar(), "toolbarSelectionTools") !== null, "the selection tools are docked")
            check(window.findShown(delegateAt(rowOf("Zadzwonić")), "selectionGripStart") !== null, "with grips")
            const text = textOf(rowOf("Położyć"))
            fingerTap(text, text.width / 3, text.height / 2)
        }
        if (window.at()) window.waitStep(page.selectionAnchorRow === rowOf("Zadzwonić") && page.selectedRow === rowOf("Położyć")
                   && page.editingRow < 0 && editor.selectionMode, "a tap on another block extends the selection (no editing)")
        if (window.at()) tap(shown(toolbar(), "selectionCopy"))
        if (window.at()) window.waitStep(Js.indexOf(clip(), "- [/] Zadzwonić do hydraulika") === 0 && Js.indexOf(clip(), "- [ ] Położyć hydroizolację") > 0, "Kopiuj copies it")
        // ---- "Pojedynczo": taps pick single blocks ----------------------------------------------------
        if (window.at()) {
            const pick = shown(toolbar(), "selectionPickMode") as T.AbstractButton
            check(pick !== null && !pick.checked, "the docked tools have Pojedynczo")
            revealTool(pick)
        }
        if (window.at()) tap(shown(toolbar(), "selectionPickMode"))
        if (window.at()) window.waitStep(editor.pickMode && (shown(toolbar(), "selectionPickMode") as T.AbstractButton).checked, "a tap turns it on")
        if (window.at()) {
            check(window.findShown(delegateAt(page.selectionLastRow), "selectionGripEnd") === null, "no grips while picking")
            const text = textOf(rowOf("Zadzwonić"))
            fingerTap(text, text.width / 3, text.height / 2)
        }
        if (window.at() && !window.waiting(page.selectionPicked && page.editingRow < 0 && selected().indexOf(rowOf("Zadzwonić")) < 0
                   && selected().indexOf(rowOf("Wywieźć")) >= 0))
            window.checkWait("a tap on a selected block leaves it out, the rest stays: " + selectedText())
        if (window.at()) center(rowOf("Ekipa może"))
        if (window.at()) {
            const text = textOf(rowOf("Ekipa może"))
            fingerTap(text, text.width / 3, text.height / 2)
        }
        if (window.at() && !window.waiting(!page.selectionContiguous && page.selectedRow === rowOf("Ekipa może")
                   && selected().indexOf(rowOf("Ekipa może")) >= 0 && selected().indexOf(rowOf("Notatki z rozmowy")) < 0))
            window.checkWait("a tap on another block picks it, with a gap: " + selectedText())
        if (window.at()) tap(shown(toolbar(), "selectionCopy"))
        if (window.at() && !window.waiting(pickedCopied()))
            window.checkWait("Kopiuj copies the picked blocks: " + Js.stringify(clip()))
        if (window.at()) tap(shown(toolbar(), "selectionPickMode"))
        if (window.at()) window.waitStep(!editor.pickMode, "a second tap turns Pojedynczo off")
        if (window.at()) center(rowOf("Położyć"))
        if (window.at()) {
            const text = textOf(rowOf("Położyć"))
            fingerTap(text, text.width / 3, text.height / 2)
        }
        if (window.at() && !window.waiting(!page.selectionPicked && page.selectionAnchorRow === rowOf("Ekipa może") && page.selectedRow === rowOf("Położyć")
                   && endGrip() !== null))
            window.checkWait("then a tap moves the selection's end again, from the block picked last (the grips are back): "
                   + page.selectionAnchorRow + "–" + page.selectedRow)
        // ---- The end grip -------------------------------------------------------------------------------
        // The band ends below Prace's children (a selected parent brings them along). Położyć in
        // the middle keeps its grip and Kolejność, where the finger goes, clear of the view's
        // bottom edge, where a finger held on a grip scrolls the list: centred on Zadzwonić, the
        // grip sat there, the list scrolled after the first move and the row the finger ended on
        // depended on how far it had scrolled before Qt Quick handed the next move over.
        if (window.at()) {
            page.selectRange(rowOf("Zadzwonić"), rowOf("Położyć")) // as before, for the grip
            center(rowOf("Położyć"))
        }
        if (window.at()) window.waitStep(endGrip() !== null, "the band's end has a grip")
        if (window.at()) {
            test.contentBefore = list.contentY
            const grip = endGrip()
            events.touchPress(grip, grip.width / 2, grip.height / 2)
        }
        if (window.at()) window.waitStep(editor.dragSelecting, "a finger on the grip drags the selection's end")
        if (window.at()) {
            const text = textOf(rowOf("Kolejność"))
            events.touchMove(text, 30, text.height / 2)
        }
        if (window.at()) window.waiting(page.selectedRow === rowOf("Kolejność"))
        if (window.at()) {
            const text = textOf(rowOf("Kolejność"))
            events.touchRelease(text, 30, text.height / 2)
        }
        if (window.at()) window.waitStep(!editor.dragSelecting, "the lifted finger ends the drag")
        if (window.at()) check(page.selectedRow === rowOf("Kolejność") && page.selectionAnchorRow === rowOf("Zadzwonić")
                    && Math.abs(list.contentY - test.contentBefore) < 1,
                    "dragging the end grip moves the selection's end: " + page.selectionAnchorRow + "–" + page.selectedRow
                    + ", scrolled " + (list.contentY - test.contentBefore))
        // ---- Usuń keeps the selection mode (the block before them selected); the toast's Cofnij, Ctrl+Z --
        if (window.at()) clearToasts()
        if (window.at()) window.waitStep(toastUndo() === null, "no toast is left")
        if (window.at()) {
            test.textBefore = window.pageText()
            revealTool(shown(toolbar(), "selectionDelete"))
        }
        if (window.at()) tap(shown(toolbar(), "selectionDelete"))
        if (window.at()) window.waitStep(Js.indexOf(window.pageText(), "Zadzwonić do hydraulika") < 0, "Usuń deletes the selected blocks")
        if (window.at()) check(editor.selectionMode && editor.toolbarMode === "selection" && page.selectionCount === 1
                    && page.editingRow < 0,
                    "the block before them is selected, in the selection mode with its tools: " + page.selectionCount)
        if (window.at()) window.waitStep(toastUndo() !== null, "the toast offers Cofnij above the docked tools")
        if (window.at()) tap(toastUndo())
        if (window.at()) window.waitStep(window.pageText() === test.textBefore, "a tap on the toast's Cofnij brings them back")
        if (window.at()) {
            check(page.selectionAnchorRow === rowOf("Zadzwonić") && page.selectedRow === rowOf("Kolejność"),
                  "selected again")
            tap(shown(toolbar(), "selectionDelete"))
        }
        if (window.at()) window.waitStep(Js.indexOf(window.pageText(), "Zadzwonić do hydraulika") < 0, "Usuń deletes them again")
        if (window.at()) key(Qt.Key_Z, Qt.ControlModifier)
        if (window.at()) window.waitStep(window.pageText() === test.textBefore, "Ctrl+Z brings them back")
        if (window.at()) check(editor.selectionMode && page.selectionAnchorRow === rowOf("Zadzwonić") && page.selectedRow === rowOf("Kolejność"),
                    "selected again, in the selection mode")
        // ---- Back leaves the selection mode -----------------------------------------------------------
        if (window.at()) key(Qt.Key_Back, 0)
        if (window.at()) window.waitStep(!editor.selectionMode && page.selectionCount === 0 && editor.toolbarMode === "", "Back leaves the selection mode")
        // ---- A long press dragged to the view's edge: autoscroll ----------------------------------------
        if (window.at()) list.positionViewAtBeginning()
        if (window.at()) { test.contentBefore = list.contentY }
        if (window.at()) pressText(rowOf("Zmierzyć"))
        if (window.at()) window.waiting(editor.selectionMode && page.selectedRow === rowOf("Zmierzyć"))
        if (window.at()) {
                check(editor.selectionMode, "the long press selected the block")
                const text = textOf(rowOf("Kupić płytki"))
                events.touchMove(text, text.width / 2, text.height / 2)
            }
        if (window.at() && !window.waiting(page.selectedRow === rowOf("Kupić płytki") && Math.abs(list.contentY - test.contentBefore) < 1))
            window.checkWait("moving the finger without lifting it extends the selection (no scrolling): " + page.selectedRow
                       + ", scrolled " + (list.contentY - test.contentBefore))
        if (window.at()) events.touchMove(list, list.width / 2, list.height - 6)
        // Held at the edge until the list has scrolled (a frame at a time: slow in sanitizer builds).
        if (window.at()) window.waiting(list.contentY > test.contentBefore + 100 && page.selectedRow > rowOf("Prace"))
        if (window.at()) {
                check(list.contentY > test.contentBefore + 100, "at the view's bottom edge the list scrolls: " + list.contentY)
                check(page.selectedRow > rowOf("Prace"), "the selection follows the finger: " + page.selectedRow)
                events.touchRelease(list, list.width / 2, list.height - 6)
            }
        if (window.at()) window.waitStep(!editor.dragSelecting && editor.selectionMode && list.interactive
                   && page.selectionAnchorRow === rowOf("Zmierzyć"), "the lifted finger leaves the selection in the selection mode")
        if (window.at()) key(Qt.Key_Back, 0)
        // ---- The editing toolbar's "Zaznacz" ------------------------------------------------------------
        if (window.at()) {
            test.rowA = rowOf("Skuć")
            center(test.rowA)
            editor.startEditing(test.rowA, -1)
        }
        if (window.at()) window.waitStep(editor.toolbarMode === "block" && shown(toolbar(), "toolbarSelect") !== null, "the editing toolbar has Zaznacz")
        if (window.at()) revealTool(shown(toolbar(), "toolbarSelect"))
        if (window.at()) tap(shown(toolbar(), "toolbarSelect"))
        if (window.at()) window.waitStep(editor.selectionMode && page.selectedRow === test.rowA && page.editingRow < 0, "Zaznacz selects the edited block (the selection mode)")
        if (window.at()) {
            const done = shown(toolbar(), "selectionDone")
            check(done !== null && windowX(done) < 60, "Gotowe is the bar's first tool")
            tap(done)
        }
        if (window.at()) window.waitStep(!editor.selectionMode && page.selectionCount === 0, "Gotowe leaves the selection mode")
        // ---- A keyboard on a touch UI: Esc and Shift+↓ give the selection its tools -------------------
        if (window.at()) editor.startEditing(rowOf("Skuć"), -1)
        if (window.at()) window.waitStep(edit() !== null && edit().activeFocus && edit().row === rowOf("Skuć"), "Skuć is edited")
        if (window.at()) editKey(Qt.Key_Escape, 0)
        if (window.at()) window.waitStep(page.selectedRow === rowOf("Skuć") && page.editingRow < 0 && editor.selectionMode
                   && editor.toolbarMode === "selection", "Esc selects the block with the selection's tools")
        if (window.at()) key(Qt.Key_Down, Qt.ShiftModifier)
        if (window.at()) window.waitStep(page.selectionAnchorRow === rowOf("Skuć") && page.selectedRow === rowOf("Położyć")
                   && editor.toolbarMode === "selection", "Shift+↓ extends it, the tools stay")
        if (window.at()) key(Qt.Key_Back, 0)
        // ---- The bullet's menu: Zaznacz -----------------------------------------------------------------
        if (window.at()) pressBullet(rowOf("Kolejność"))
        if (window.at()) window.waiting(window.blockMenuOpen())
        if (window.at()) releaseBullet(rowOf("Kolejność"))
        if (window.at()) window.waitStep(window.blockMenuOpen(), "a bullet's long press opens the block menu")
        if (window.at()) tap(menuItem(blockMenu(), "blockMenuSelect"))
        if (window.at()) window.waitStep(editor.selectionMode && page.selectedRow === rowOf("Kolejność"), "its Zaznacz selects the block")
        if (window.at()) key(Qt.Key_Back, 0)
    }
}
