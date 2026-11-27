pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import Brulion

// AtramentTableSelfTest — TableSelfTest's checks (rows and columns of a table with real input)
// against the page drawn by Atrament (docs/ATRAMENT.md M2, step 5, tables: "the editor's existing
// behaviour tests pass against it"), in the editor playground (`brulion --editor --editor-script
// atramenttabletest --atrament-editor`; ctest tst_editor_qml_atrament_table, desktop, and
// tst_editor_qml_atrament_table_touch, a phone with --touch). The table is Atrament's — its cells,
// their field, the cell selection, the filter row — and BlockEditor reaches it as it reaches a
// TableBlockView (AtramentView.tableAt: an AtramentTableItem, the same names): every click, tap and
// key goes through the window to the AtramentView. Checked: a block menu not opened from a cell
// has no Row or Column, a cell's has (a right-click, also on the edited cell; a tap and the touch
// toolbar's "More"); a new row's or column's cell edited after the menu is gone (BlockEditor.
// editTableCell), typing reaching the document; Alt+arrows and Ctrl+Enter with the cell edited on;
// the header's items; the alignment ticked; deleting a row and a column, the formulas following;
// sorting from the header; the filter row (typing "> 40" hides a row, nothing is written, the rows
// counted, "Clear filters"); "Copy as CSV"; with a keyboard (desktop) the arrows, Tab and Home / End
// between cells, Shift+arrows and Shift+click selecting a rectangle, Ctrl+C / Ctrl+X / Delete /
// Ctrl+V on it, "Copy cells as Markdown", a typed character editing the cell, Esc; on a phone the
// toolbar's next cell, undo and "hide keyboard"; each change one undo step, the page's text back
// byte for byte. (TableSelfTest's chart part is the chart's, drawn by Atrament too: not here.)
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property ApplicationWindow appWindow
    readonly property EditorPlayground window: test.appWindow as EditorPlayground
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property int row: -1
    property string before: ""
    /// Alt+Left / Alt+Right, as AppShell's Back and Forward take them, fired.
    property int appShortcuts: 0
    /// A CSV line break (a property: a "\r" in a compiled function's literal breaks the C++).
    readonly property string crlf: "\r\n"
    /// A point mapped to the window (read through a typed property).
    property point scene
    property Component shortcutComponent: Component {
        Shortcut {
            onActivated: test.appShortcuts += 1
        }
    }

    function check(ok: bool, what: string): void { test.window.check(ok, what) }
    function findItem(item: Item, name: string): Item { return test.window.findItem(item, name) }
    function menuItem(menu: T.Menu, name: string): T.MenuItem { return test.window.menuItemNamed(menu, name) }
    /// The AtramentView drawing the page, and the table as BlockEditor reaches it.
    function view(): Item { return test.editor.atramentPage }
    function table(): Item { return test.row >= 0 ? test.editor.tableAt(test.row) : null }
    function num(name: string): int {
        const t = test.table()
        return t ? QmlCall.integer(QmlCall.get(t, name)) : -9
    }
    function flag(name: string): bool {
        const t = test.table()
        return t !== null && QmlCall.isTrue(QmlCall.get(t, name))
    }
    function call0(name: string): int {
        const t = test.table()
        return t ? QmlCall.integer(QmlCall.call(t, name)) : -9
    }
    function rowCount(): int { return test.call0("cellRowCount") }
    function columns(): int { return test.num("columns") }
    function raw(r: int, c: int): string {
        const t = test.table()
        return t ? QmlCall.text(QmlCall.call2(t, "rawText", r, c)) : "<none>"
    }
    function header(c: int): string {
        const t = test.table()
        return t ? QmlCall.text(QmlCall.call1(t, "headerText", c)) : ""
    }
    function align(c: int): string {
        const t = test.table()
        return t ? QmlCall.text(QmlCall.call1(t, "cellAlignOf", c)) : ""
    }
    function rowShown(r: int): bool {
        const t = test.table()
        return t !== null && QmlCall.isTrue(QmlCall.call1(t, "isRowShown", r))
    }
    function shownCount(): int { return test.call0("shownRowCount") }
    function cellText(): string {
        const t = test.table()
        return t ? QmlCall.text(QmlCall.call(t, "cellText")) : ""
    }
    function cellCursor(): int { return test.call0("cellCursor") }
    function cellLength(): int { return test.call0("cellLength") }
    /// The middle of cell (r, c) in the AtramentView (r -1 the header, -3 a filter field, -4 the
    /// "Clear filters" button).
    function cellX(r: int, c: int): real {
        const t = test.table()
        return t ? QmlCall.number(QmlCall.call2(t, "cellCenterX", r, c)) : -1
    }
    function cellY(r: int, c: int): real {
        const t = test.table()
        return t ? QmlCall.number(QmlCall.call2(t, "cellCenterY", r, c)) : -1
    }
    function tapCell(r: int, c: int): void {
        const v = test.view()
        if (Theme.touchUi) {
            test.window.taskEvents.touchPress(v, test.cellX(r, c), test.cellY(r, c))
            test.window.taskEvents.touchRelease(v, test.cellX(r, c), test.cellY(r, c))
        } else {
            test.window.taskEvents.mouseClick(v, test.cellX(r, c), test.cellY(r, c), Qt.LeftButton, Qt.NoModifier, -1)
        }
    }
    function rightClickCell(r: int, c: int): void {
        test.window.taskEvents.mouseClick(test.view(), test.cellX(r, c), test.cellY(r, c), Qt.RightButton, Qt.NoModifier, -1)
    }
    function shiftClickCell(r: int, c: int): void {
        test.window.taskEvents.mouseClick(test.view(), test.cellX(r, c), test.cellY(r, c), Qt.LeftButton, Qt.ShiftModifier, -1)
    }
    /// Cell (r, c) is edited, its field has the keys, no menu is open.
    function editing(r: int, c: int): bool {
        const t = test.table()
        return t !== null && test.editor.editingTable === t && test.num("editRow") === r && test.num("editColumn") === c
               && test.flag("cellHasKeys") && !test.menuOpen()
    }
    function editingNone(): bool { return test.editor.editingTable === null }
    function keyClick(k: int, modifiers: int): void { test.window.taskEvents.keyClick(k, modifiers, -1) }
    function typeKeys(s: string): void {
        const text = s
        for (let i = 0; i < text.length; ++i)
            EditorInput.sendKey(test.view(), 0, 0, text[i])
    }
    function menu(): BlockMenu { return test.editor.blockMenu as BlockMenu }
    function menuOpen(): bool { return test.menu() !== null && test.menu().opened }
    /// The item of the block menu that opens its submenu `name`.
    function subItem(name: string): T.MenuItem {
        const m = test.menu()
        for (let i = 0; m && i < m.count; ++i) {
            const it = m.itemAt(i) as T.MenuItem
            if (it && it.subMenu && it.subMenu.objectName === name)
                return it
        }
        return null
    }
    function subMenu(name: string): T.Menu {
        const s = test.subItem(name)
        return s ? s.subMenu : null
    }
    function item(sub: string, name: string): T.MenuItem { return test.menuItem(test.subMenu(sub), name) }
    function cellMore(): Item { return test.window.findShown(test.findItem(test.editor, "editToolbarSlot"), "toolbarCellMore") }
    function toolbarTool(name: string): Item { return test.window.findShown(test.findItem(test.editor, "editToolbarSlot"), name) }
    /// Steps: the block menu of cell (r, c): a right-click; on touch UIs a tap on the cell and the
    /// toolbar's "More".
    function openOn(r: int, c: int): void {
        if (!test.window.touchRun) {
            if (test.window.at())
                test.rightClickCell(r, c)
        } else {
            if (test.window.at())
                test.tapCell(r, c)
            if (test.window.at())
                test.window.waitStep(test.editor.toolbarMode === "cell" && test.cellMore() !== null, "a cell's toolbar has “More”")
            if (test.window.at()) {
                const flick = test.findItem(test.editor, "editToolbarFlick") as Flickable
                flick.contentX = Math.max(0, flick.contentWidth - flick.width)
            }
            if (test.window.at())
                test.window.tapItem(test.cellMore())
        }
        if (test.window.at())
            test.window.waitStep(test.menuOpen() && test.menu().cellRow === r && test.menu().cellColumn === c,
                                 "the menu of cell " + r + "," + c + " opens")
    }
    /// Steps: choose item `name` of the submenu `sub` and wait until the menu is gone.
    function choose(sub: string, name: string): void {
        if (test.window.at())
            test.window.tapItem(test.subItem(sub))
        if (test.window.at())
            test.window.waitStep(test.subMenu(sub) !== null && test.subMenu(sub).opened, "the submenu " + sub + " opens")
        if (test.window.at()) {
            check(test.item(sub, name) !== null && test.item(sub, name).enabled, name + " can be chosen")
            test.window.tapItem(test.item(sub, name))
        }
        if (test.window.at())
            test.window.waitStep(!test.menuOpen(), "choosing " + name + " closes the menu")
    }
    /// Steps: item `name` of the block menu itself: tapped, the menu gone.
    function chooseTop(name: string): void {
        if (test.window.at()) {
            const it = test.menuItem(test.menu(), name)
            check(it !== null && it.visible && it.enabled, name + " is in the menu")
            test.window.tapItem(it)
        }
        if (test.window.at())
            test.window.waitStep(!test.menuOpen(), "choosing " + name + " closes the menu")
    }
    /// A step: `n` undo steps bring the page's text back.
    function undoes(n: int, what: string): void {
        for (let i = 0; i < n; ++i)
            test.page.undo()
        check(test.window.pageText() === test.before, what)
    }
    /// A step: the edited cell (if any) closed.
    function closeCell(): void {
        const t = test.table()
        if (t && test.num("editRow") > -2)
            QmlCall.call(t, "closeCell")
    }
    function clipboard(): string { return test.window.clipboardText() }
    function selected(top: int, left: int, bottom: int, right: int): bool {
        return test.flag("hasSelection") && test.call0("selectionTop") === top && test.call0("selectionLeft") === left
               && test.call0("selectionBottom") === bottom && test.call0("selectionRight") === right
    }
    function selecting(): bool { return test.flag("hasSelection") && test.editingNone() && test.view().activeFocus }
    /// The cell selection, the edited cell and the keyboard, for a failed step's message.
    function state(): string {
        return "selected " + test.flag("hasSelection") + " " + test.call0("selectionTop") + "," + test.call0("selectionLeft") + "…"
               + test.call0("selectionBottom") + "," + test.call0("selectionRight") + ", edited " + test.num("editRow") + ","
               + test.num("editColumn") + ", keys " + test.view().activeFocus + " " + test.window.activeFocusItem
    }
    function filterFocused(c: int): bool {
        const t = test.table()
        return t !== null && QmlCall.isTrue(QmlCall.call1(t, "filterFocused", c)) && test.view().activeFocus
    }
    function filterStatus(): string {
        const t = test.table()
        return t ? QmlCall.text(QmlCall.call(t, "filterStatusText")) : ""
    }
    function csvCopied(): bool {
        return Js.indexOf(test.clipboard(), "Pozycja,Ilość,Cena,Razem" + test.crlf) === 0
               || Js.indexOf(test.clipboard(), "Pozycja,Ilość,Cena,Razem\n") === 0
    }

    function start(): void {
        test.shortcutComponent.createObject(test.window, { sequence: "Alt+Left" })
        test.shortcutComponent.createObject(test.window, { sequence: "Alt+Right" })
        test.window.runSteps("atramenttabletest")
    }

    function steps(): void {
        stepsPart1()
        stepsPart2()
        stepsPart3()
        stepsPart5()
        stepsPart6()
        stepsPart7()
        stepsPart8()
        stepsPart9()
        stepsTouch()
    }
    function stepsPart1(): void {
        if (test.window.at()) {
            test.row = test.window.viewRow("table")
            check(test.row >= 0, "the sample has a table")
            QmlCall.call1(test.view(), "revealRow", test.row)
            test.before = test.window.pageText()
        }
        if (test.window.at())
            test.window.waitStep(test.table() !== null && test.cellY(-1, 0) > 0, "the table is drawn by Atrament")
        // ---- A block menu not from a cell ---------------------------------------------------------
        if (test.window.at()) {
            test.scene = test.view().mapToItem(null, test.cellX(-1, 0), test.cellY(-1, 0))
            test.editor.openBlockMenuAt(test.row, Qt.point(test.scene.x, test.scene.y))
        }
        if (test.window.at()) test.window.waitStep(test.menuOpen(), "the block menu without a cell opens")
        if (test.window.at()) {
            check(!test.menu().inCell && !test.subItem("tableRowMenu").visible && !test.subItem("tableColumnMenu").visible,
                  "no Row or Column without a cell")
            test.menu().close()
        }
        if (test.window.at()) test.window.waitStep(!test.menuOpen(), "closed")
        // ---- Insert a row below: its cell edited, typing reaches the document -------------------
        test.openOn(0, 1)
        if (test.window.at()) check(test.subItem("tableRowMenu").visible && test.subItem("tableColumnMenu").visible, "Row and Column are shown")
        test.choose("tableRowMenu", "tableRowBelow")
        if (test.window.at()) test.window.waitStep(test.editing(1, 1), "the new row's cell is edited, with the keys")
        if (test.window.at()) {
            check(test.rowCount() === 5, "one row more")
            check(test.raw(2, 3) === "=B3*C3", "the formula below follows its cells: " + test.raw(2, 3))
        }
        if (test.window.at()) test.typeKeys("ąć")
        if (test.window.at()) test.window.waitStep(test.raw(1, 1) === "ąć", "typing reaches the cell")
        test.keySteps()
        // ---- The header's menu --------------------------------------------------------------------
        test.openOn(-1, 1)
        if (test.window.at()) {
            check(!test.item("tableRowMenu", "tableRowAbove").enabled && test.item("tableRowMenu", "tableRowBelow").enabled,
                  "nothing goes above the header, a row below it can")
            check(!test.item("tableRowMenu", "tableRowUp").enabled && !test.item("tableRowMenu", "tableRowDown").enabled
                  && !test.item("tableRowMenu", "tableRowDelete").enabled, "the header does not move or go")
            check(test.item("tableColumnMenu", "tableColumnDelete").enabled && test.item("tableColumnMenu", "tableColumnMoveLeft").enabled,
                  "its column does")
            test.menu().close()
        }
        if (test.window.at()) test.window.waitStep(!test.menuOpen(), "closed")
        if (test.window.at()) test.closeCell()
        // ---- Insert a column: its header edited ---------------------------------------------------
        test.openOn(0, 1)
        test.choose("tableColumnMenu", "tableColumnRight")
        if (test.window.at()) test.window.waitStep(test.editing(-1, 2), "the new column's header is edited")
        if (test.window.at()) {
            check(test.cellText() === "Kolumna 5" && test.columns() === 5, "called " + test.cellText())
            check(test.raw(1, 4) === "=B2*D2", "formulas follow: " + test.raw(1, 4))
        }
        if (test.window.at()) test.closeCell()
        if (test.window.at()) test.undoes(1, "one undo step takes the column back")
    }
    /// Keys in a cell: rows and columns move, the cell stays edited (desktop); on touch UIs the
    /// cell closed and the row taken back.
    function keySteps(): void {
        if (test.window.touchRun) {
            if (test.window.at()) test.closeCell()
            if (test.window.at()) test.undoes(2, "two undo steps take back the typing and the row")
            return
        }
        if (test.window.at()) test.keyClick(Qt.Key_Down, Qt.AltModifier)
        if (test.window.at()) test.window.waitStep(test.editing(2, 1), "Alt+Down moves the row, the cell goes on being edited")
        if (test.window.at()) {
            check(test.raw(2, 1) === "ąć" && test.cellText() === "ąć", "with its text")
            check(test.raw(1, 3) === "=B2*C2", "the formula of the row it passed: " + test.raw(1, 3))
        }
        if (test.window.at()) test.keyClick(Qt.Key_Up, Qt.AltModifier)
        if (test.window.at()) test.window.waitStep(test.editing(1, 1), "Alt+Up moves it back")
        if (test.window.at()) test.keyClick(Qt.Key_Right, Qt.AltModifier)
        if (test.window.at()) test.window.waitStep(test.editing(1, 2), "Alt+Right moves the column")
        if (test.window.at()) check(test.appShortcuts === 0, "the app's Forward does not take it")
        if (test.window.at()) {
            check(test.raw(1, 2) === "ąć" && test.header(1) === "Cena" && test.header(2) === "Ilość", "the column moved, its header too")
            check(test.raw(2, 3) === "=C3*B3", "formulas follow it: " + test.raw(2, 3))
        }
        if (test.window.at()) test.keyClick(Qt.Key_Left, Qt.AltModifier)
        if (test.window.at()) test.window.waitStep(test.editing(1, 1), "Alt+Left moves it back")
        if (test.window.at()) test.keyClick(Qt.Key_Return, Qt.ControlModifier)
        if (test.window.at()) test.window.waitStep(test.editing(2, 1) && test.rowCount() === 6, "Ctrl+Enter inserts a row below, its cell edited")
        if (test.window.at()) check(test.raw(1, 1) === "ąć" && test.raw(3, 3) === "=B4*C4", "the rows below it move down: " + test.raw(3, 3))
        if (test.window.at()) test.keyClick(Qt.Key_Escape, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editingNone() && test.page.selectedRow === test.row, "Esc ends the editing, the table selected")
        if (test.window.at()) test.undoes(7, "seven undo steps take back the keys, the typing and the row")
        if (test.window.at()) test.keyClick(Qt.Key_Left, Qt.AltModifier)
        if (test.window.at()) test.window.waitStep(test.appShortcuts === 1, "outside a cell Alt+Left is the app's Back")
    }
    function stepsPart2(): void {
        // ---- The alignment ------------------------------------------------------------------------
        test.openOn(0, 1)
        if (test.window.at())
            check(test.item("tableColumnMenu", "tableAlignRight").checked && !test.item("tableColumnMenu", "tableAlignCenter").checked,
                  "the column's alignment is ticked")
        test.choose("tableColumnMenu", "tableAlignRight") // the one it has: nothing changes
        if (test.window.at()) test.closeCell()
        test.openOn(0, 1)
        if (test.window.at()) check(test.item("tableColumnMenu", "tableAlignRight").checked, "still ticked after choosing it again")
        test.choose("tableColumnMenu", "tableAlignCenter")
        if (test.window.at()) check(test.align(1) === "center" && Js.indexOf(test.window.pageText(), ":---:") > 0, "centred, in the delimiter row")
        if (test.window.at()) test.closeCell()
        test.openOn(0, 1)
        if (test.window.at())
            check(test.item("tableColumnMenu", "tableAlignCenter").checked && !test.item("tableColumnMenu", "tableAlignRight").checked,
                  "the new one is ticked")
        if (test.window.at()) test.menu().close()
        if (test.window.at()) test.window.waitStep(!test.menuOpen(), "closed")
        if (test.window.at()) test.closeCell()
        if (test.window.at()) test.undoes(1, "one undo step takes the alignment back")
        // ---- Delete a row, a column ------------------------------------------------------------------
        test.openOn(0, 1)
        test.choose("tableRowMenu", "tableRowDelete")
        if (test.window.at()) test.window.waitStep(test.rowCount() === 3, "the row is gone")
        if (test.window.at()) check(test.raw(0, 3) === "=B1*C1" && Js.indexOf(test.window.pageText(), "Płytki") < 0, "formulas follow: " + test.raw(0, 3))
        if (test.window.at()) test.closeCell()
        if (test.window.at()) test.undoes(1, "one undo step brings the row back")
        test.openOn(0, 1)
        test.choose("tableColumnMenu", "tableColumnDelete")
        if (test.window.at()) test.window.waitStep(test.columns() === 3, "the column is gone")
        if (test.window.at()) check(test.raw(1, 2) === "=REF!*B2", "a reference to it: " + test.raw(1, 2))
        if (test.window.at()) test.closeCell()
        if (test.window.at()) test.undoes(1, "one undo step brings the column back")
        // ---- A right-click on the cell being edited (desktop) -------------------------------------
        if (test.window.touchRun)
            return
        if (test.window.at()) test.tapCell(0, 1)
        if (test.window.at()) test.window.waitStep(test.editing(0, 1), "a click edits the cell")
        if (test.window.at()) test.rightClickCell(0, 1)
        if (test.window.at())
            test.window.waitStep(test.menuOpen() && test.menu().inCell && test.menu().cellRow === 0 && test.menu().cellColumn === 1,
                                 "a right-click on the edited cell opens the cell's menu")
        if (test.window.at()) {
            check(test.editingNone(), "its editing ended")
            test.menu().close()
        }
        if (test.window.at()) test.window.waitStep(!test.menuOpen(), "closed")
    }
    function stepsPart3(): void {
        // ---- Sorting from the header ----------------------------------------------------------------
        test.openOn(-1, 2)
        if (test.window.at()) check(test.item("tableColumnMenu", "tableSortAscending").enabled, "the header's column sorts")
        test.choose("tableColumnMenu", "tableSortAscending")
        if (test.window.at() && !test.window.waiting(Js.indexOf(test.raw(0, 0), "Fuga") === 0 && Js.indexOf(test.raw(2, 0), "**Płytki**") === 0))
            test.window.checkWait("sorted by the price: " + test.raw(0, 0) + " / " + test.raw(2, 0))
        if (test.window.at()) {
            check(test.raw(3, 0) === "Suma", "the summary row stays at the end")
            check(test.raw(1, 3) === "=B2*C2", "a formula of a row that stayed: " + test.raw(1, 3))
        }
        if (test.window.at()) test.closeCell()
        if (test.window.at()) test.undoes(1, "one undo step takes the sort back")
        // ---- Filters: the filter row is Atrament's ------------------------------------------------
        test.openOn(0, 2)
        test.chooseTop("tableFilterRows")
        if (test.window.at())
            test.window.waitStep(test.flag("filtering") && test.filterFocused(2), "the filter row shows, the keys in the column's field")
        if (test.window.at()) test.typeKeys("> 40")
        if (test.window.at() && !test.window.waiting(test.shownCount() === 3))
            test.window.checkWait("a row under 40 is hidden: " + test.shownCount())
        if (test.window.at()) {
            check(!test.rowShown(2) && test.rowShown(3), "Fuga hidden, the summary row shown")
            check(test.window.pageText() === test.before, "filtering writes nothing")
            check(Js.indexOf(test.filterStatus(), "3") >= 0, "the rows shown are counted: " + test.filterStatus())
        }
        if (test.window.at()) test.keyClick(Qt.Key_Return, Qt.NoModifier)
        if (test.window.at() && !test.window.waiting(!test.filterFocused(2) && test.editor.toolbarMode !== "cell"))
            test.window.checkWait("the filter field is left")
        if (test.window.at()) QmlCall.call5(test.view(), "revealArea", 0, test.cellY(-4, 0) - 20, 10, 40, 8)
        if (test.window.at())
            test.window.waitStep(test.cellY(-4, 0) > 0 && test.cellY(-4, 0) < test.view().height, "“Clear filters” is in view: " + test.cellY(-4, 0))
        if (test.window.at()) {
            const v = test.view()
            if (Theme.touchUi) {
                test.window.taskEvents.touchPress(v, test.cellX(-4, 0), test.cellY(-4, 0))
                test.window.taskEvents.touchRelease(v, test.cellX(-4, 0), test.cellY(-4, 0))
            } else {
                test.window.taskEvents.mouseClick(v, test.cellX(-4, 0), test.cellY(-4, 0), Qt.LeftButton, Qt.NoModifier, -1)
            }
        }
        if (test.window.at() && !test.window.waiting(!test.flag("filtering") && test.shownCount() === 4))
            test.window.checkWait("Clear filters shows every row: " + test.flag("filtering") + " " + test.shownCount())
        // ---- CSV -----------------------------------------------------------------------------------
        test.openOn(0, 1)
        test.chooseTop("tableCopyCsv")
        if (test.window.at() && !test.window.waiting(test.csvCopied()))
            test.window.checkWait("the table copied as CSV: " + Js.slice(test.clipboard(), 0, 40))
        if (test.window.at())
            check(Js.indexOf(test.clipboard(), "=[") < 0 && Js.indexOf(test.clipboard(), "Fuga #zakupy,2,38,76") > 0,
                  "formulas as their results")
        if (test.window.at()) test.closeCell()
        if (test.window.at()) check(test.window.pageText() === test.before, "the page is as it was")
    }

    /// Between cells with the keys (desktop).
    function stepsPart5(): void {
        if (test.window.touchRun)
            return
        if (test.window.at()) test.tapCell(0, 1)
        if (test.window.at()) test.window.waitStep(test.editing(0, 1), "a click edits the cell")
        if (test.window.at()) test.keyClick(Qt.Key_Down, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editing(1, 1), "Down edits the cell below")
        if (test.window.at()) test.keyClick(Qt.Key_Up, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editing(0, 1), "Up edits the cell above")
        if (test.window.at()) test.keyClick(Qt.Key_Up, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editing(-1, 1), "Up goes on to the header")
        if (test.window.at()) test.keyClick(Qt.Key_End, Qt.NoModifier)
        if (test.window.at()) check(test.editing(-1, 1) && test.cellCursor() === test.cellLength(), "End goes to the end of the cell's text")
        if (test.window.at()) test.keyClick(Qt.Key_Home, Qt.NoModifier)
        if (test.window.at()) check(test.editing(-1, 1) && test.cellCursor() === 0, "Home goes to the start of it")
        if (test.window.at()) test.keyClick(Qt.Key_Left, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editing(-1, 0), "Left at the start of the text edits the cell on the left")
        if (test.window.at()) check(test.cellCursor() === test.cellLength(), "…the caret at its end")
        if (test.window.at()) test.keyClick(Qt.Key_Right, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editing(-1, 1), "Right at the end of the text edits the cell on the right")
        if (test.window.at()) check(test.cellCursor() === 0, "…the caret at its start")
        if (test.window.at()) test.keyClick(Qt.Key_Tab, Qt.NoModifier)
        if (test.window.at()) test.keyClick(Qt.Key_Tab, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editing(-1, 3), "Tab moves on")
        if (test.window.at()) test.keyClick(Qt.Key_Tab, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editing(0, 0), "Tab in the header's last cell goes to the next row")
        if (test.window.at()) test.keyClick(Qt.Key_Backtab, Qt.ShiftModifier)
        if (test.window.at()) test.window.waitStep(test.editing(-1, 3), "Shift+Tab goes back")
        if (test.window.at()) test.keyClick(Qt.Key_Escape, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editingNone() && test.page.selectedRow === test.row, "Esc ends the editing")
        if (test.window.at()) check(test.window.pageText() === test.before, "moving between cells wrote nothing")
        // ---- Enter on the selected table edits its first cell -------------------------------------
        if (test.window.at()) test.keyClick(Qt.Key_Return, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editing(0, 0), "Enter on the selected table edits its first cell")
        if (test.window.at()) test.keyClick(Qt.Key_Escape, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editingNone(), "closed")
    }

    /// Selecting cells: Shift+arrows, copy as TSV, the block menu's Markdown (desktop).
    function stepsPart6(): void {
        if (test.window.touchRun)
            return
        if (test.window.at()) test.tapCell(0, 1)
        if (test.window.at()) test.window.waitStep(test.editing(0, 1), "a click edits the cell")
        if (test.window.at()) test.keyClick(Qt.Key_Down, Qt.ShiftModifier)
        if (test.window.at()) test.window.waitStep(test.selecting() && test.selected(0, 1, 1, 1), "Shift+Down leaves the cell and selects two")
        if (test.window.at()) test.keyClick(Qt.Key_Right, Qt.ShiftModifier)
        if (test.window.at()) test.window.waitStep(test.selected(0, 1, 1, 2) && test.call0("selectedCellCount") === 4, "Shift+Right adds a column")
        if (test.window.at()) test.keyClick(Qt.Key_Left, Qt.ShiftModifier)
        if (test.window.at()) test.window.waitStep(test.selected(0, 1, 1, 1), "Shift+Left takes it back")
        if (test.window.at()) test.keyClick(Qt.Key_Right, Qt.ShiftModifier)
        if (test.window.at()) test.window.waitStep(test.selected(0, 1, 1, 2), "selected again")
        if (test.window.at()) test.keyClick(Qt.Key_C, Qt.ControlModifier)
        if (test.window.at() && !test.window.waiting(test.clipboard() === "6,5\t89\n4\t45"))
            test.window.checkWait("Ctrl+C copies the cells as TSV: " + test.clipboard())
        if (test.window.at()) test.rightClickCell(1, 2)
        if (test.window.at())
            test.window.waitStep(test.menuOpen() && test.menu().cellsPicked && test.menuItem(test.menu(), "tableCopyCellsMarkdown") !== null
                                 && test.menuItem(test.menu(), "tableCopyCellsMarkdown").visible,
                                 "a right-click on a selected cell offers the cells' commands")
        test.chooseTop("tableCopyCellsMarkdown")
        if (test.window.at() && !test.window.waiting(Js.indexOf(test.clipboard(), "| Ilość | Cena |") === 0))
            test.window.checkWait("the cells as a Markdown table: " + test.clipboard())
        if (test.window.at()) check(Js.indexOf(test.clipboard(), "6,5 |") > 0 && Js.indexOf(test.clipboard(), "---:") > 0, "with its rows and alignments: " + test.clipboard())
        if (test.window.at()) check(test.window.pageText() === test.before, "copying wrote nothing")
    }
    function stepsPart7(): void {
        if (test.window.touchRun)
            return
        // ---- Paste over the selection -----------------------------------------------------------------
        if (test.window.at()) test.tapCell(0, 1)
        if (test.window.at()) test.window.waitStep(test.editing(0, 1), "a click edits the cell")
        if (test.window.at()) test.keyClick(Qt.Key_Down, Qt.ShiftModifier)
        if (test.window.at()) test.window.waitStep(test.selecting(), "selecting")
        if (test.window.at()) test.keyClick(Qt.Key_Right, Qt.ShiftModifier)
        if (test.window.at()) test.page.copyText("ż\tó\nł\tć\n")
        if (test.window.at()) test.keyClick(Qt.Key_V, Qt.ControlModifier)
        if (test.window.at()) test.window.waitStep(test.raw(0, 1) === "ż" && test.raw(0, 2) === "ó" && test.raw(1, 1) === "ł" && test.raw(1, 2) === "ć",
                                                   "Ctrl+V pastes the grid from the selection's top-left cell")
        if (test.window.at()) test.window.waitStep(test.selecting() && test.selected(0, 1, 1, 2), "the pasted cells are selected: " + test.state())
        if (test.window.at()) check(test.raw(1, 3) === "=B2*C2" && test.rowCount() === 4, "other cells are as they were")
        if (test.window.at()) test.undoes(1, "one undo step takes the paste back")
        if (test.window.at()) test.page.copyText("0")
        if (test.window.at()) test.keyClick(Qt.Key_V, Qt.ControlModifier)
        if (test.window.at()) test.window.waitStep(test.raw(0, 1) === "0" && test.raw(0, 2) === "0" && test.raw(1, 1) === "0" && test.raw(1, 2) === "0",
                                                   "one value fills the whole selection")
        if (test.window.at()) test.undoes(1, "one undo step takes the fill back")
        // ---- Delete, cut ------------------------------------------------------------------------------
        if (test.window.at()) test.keyClick(Qt.Key_Delete, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.raw(0, 1) === "" && test.raw(1, 2) === "" && test.raw(2, 1) === "2", "Delete empties the selected cells only")
        if (test.window.at()) test.undoes(1, "one undo step brings them back")
        if (test.window.at()) test.keyClick(Qt.Key_X, Qt.ControlModifier)
        if (test.window.at()) test.window.waitStep(test.raw(0, 1) === "" && test.raw(1, 1) === "", "Ctrl+X empties them")
        if (test.window.at()) check(test.clipboard() === "6,5\t89\n4\t45", "…after copying them: " + test.clipboard())
        if (test.window.at()) test.undoes(1, "one undo step brings them back")
    }
    function stepsPart8(): void {
        if (test.window.touchRun)
            return
        // ---- Typing over a cell, Enter, the arrows of the selection -------------------------------------
        if (test.window.at()) test.keyClick(Qt.Key_Up, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.selecting() && test.selected(0, 2, 0, 2), "an arrow key moves the selection (one cell now): " + test.state())
        if (test.window.at()) test.keyClick(Qt.Key_Left, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.selected(0, 1, 0, 1), "Left moves it")
        if (test.window.at()) test.keyClick(Qt.Key_Return, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editing(0, 1) && test.cellText() === "6,5", "Enter edits the cell")
        if (test.window.at()) test.keyClick(Qt.Key_Escape, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editingNone(), "closed")
        if (test.window.at()) test.tapCell(0, 0)
        if (test.window.at()) test.window.waitStep(test.editing(0, 0), "a click edits the cell")
        if (test.window.at()) test.keyClick(Qt.Key_Down, Qt.ShiftModifier)
        if (test.window.at()) test.window.waitStep(test.selecting(), "selecting")
        if (test.window.at()) test.keyClick(Qt.Key_Up, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.selected(0, 0, 0, 0), "one cell")
        if (test.window.at()) test.typeKeys("Ż")
        if (test.window.at()) test.window.waitStep(test.editing(0, 0) && test.cellText() === "Ż", "a typed character edits the cell with that text")
        if (test.window.at()) check(test.raw(0, 0) === "Ż", "the typed character reaches the document")
        if (test.window.at()) test.keyClick(Qt.Key_Escape, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editingNone(), "Esc cancels the cell's typing")
        if (test.window.at()) check(test.window.pageText() === test.before, "the page is as it was")
    }
    function stepsPart9(): void {
        if (test.window.touchRun)
            return
        // ---- Shift+click, Ctrl+A, Tab at the end, Esc ---------------------------------------------------
        if (test.window.at()) test.tapCell(0, 0)
        if (test.window.at()) test.window.waitStep(test.editing(0, 0), "a click edits the cell")
        if (test.window.at()) test.shiftClickCell(2, 2)
        if (test.window.at()) test.window.waitStep(test.selecting() && test.selected(0, 0, 2, 2) && test.call0("selectedCellCount") === 9,
                                                   "Shift+click selects the cells up to it: " + test.state())
        if (test.window.at()) test.shiftClickCell(1, 3)
        if (test.window.at()) test.window.waitStep(test.selected(0, 0, 1, 3), "Shift+click again moves the end")
        if (test.window.at()) test.keyClick(Qt.Key_A, Qt.ControlModifier)
        if (test.window.at()) test.window.waitStep(test.selected(-1, 0, 3, 3), "Ctrl+A selects every cell")
        if (test.window.at()) test.keyClick(Qt.Key_Tab, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.rowCount() === 5 && test.selected(4, 0, 4, 0), "Tab at the last cell adds a row, its first cell selected")
        if (test.window.at()) test.undoes(1, "one undo step takes the row back")
        if (test.window.at()) test.keyClick(Qt.Key_Escape, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(!test.flag("hasSelection") && test.page.selectedRow === test.row, "Esc ends the selection, the table selected")
        // ---- A paste from the cell being edited: rows added below the last one --------------------------
        if (test.window.at()) test.tapCell(3, 0)
        if (test.window.at()) test.window.waitStep(test.editing(3, 0), "a click edits the last row's cell")
        if (test.window.at()) test.page.copyText("x\ny\nz")
        if (test.window.at()) test.keyClick(Qt.Key_V, Qt.ControlModifier)
        if (test.window.at()) test.window.waitStep(test.rowCount() === 6 && test.raw(3, 0) === "x" && test.raw(5, 0) === "z",
                                                   "Ctrl+V of several lines: rows are added below the last one")
        if (test.window.at()) test.window.waitStep(test.selecting() && test.selected(3, 0, 5, 0), "the pasted cells are selected: " + test.state())
        if (test.window.at()) test.undoes(1, "one undo step takes the paste and its rows back")
        if (test.window.at()) test.keyClick(Qt.Key_Escape, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(!test.flag("hasSelection"), "ended")
        // A single line is the field's own paste, at the caret.
        if (test.window.at()) test.tapCell(0, 0)
        if (test.window.at()) test.window.waitStep(test.editing(0, 0), "a click edits the cell")
        if (test.window.at()) test.page.copyText("ąę")
        if (test.window.at()) test.keyClick(Qt.Key_V, Qt.ControlModifier)
        if (test.window.at()) test.window.waitStep(test.editing(0, 0) && Js.indexOf(test.cellText(), "ąę") >= 0 && !test.flag("hasSelection"),
                                                   "a line of text is pasted into the cell's field")
        if (test.window.at()) test.keyClick(Qt.Key_Escape, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editingNone(), "closed")
        if (test.window.at()) check(test.window.pageText() === test.before, "the page is as it was")
    }
    /// The touch toolbar's cell tools (a phone): the next cell, undo with the cell edited on, "hide
    /// keyboard"; the edited cell above the toolbar.
    function stepsTouch(): void {
        if (!test.window.touchRun)
            return
        if (test.window.at()) test.tapCell(0, 1)
        if (test.window.at())
            test.window.waitStep(test.editor.toolbarMode === "cell" && test.editing(0, 1) && test.toolbarTool("toolbarNextCell") !== null,
                                 "a tapped cell is edited, the toolbar has the cell's tools")
        if (test.window.at()) {
            const v = test.view()
            check(test.cellY(0, 1) > 0 && test.cellY(0, 1) < v.height, "the edited cell is above the toolbar: " + test.cellY(0, 1) + " / " + v.height)
            check(test.editor.typingHasKeys(), "the toolbar knows the cell has the keys")
        }
        if (test.window.at()) test.window.tapItem(test.toolbarTool("toolbarNextCell"))
        if (test.window.at()) test.window.waitStep(test.editing(0, 2), "the toolbar's next cell moves to the next cell")
        if (test.window.at()) test.typeKeys("7")
        if (test.window.at()) test.window.waitStep(test.raw(0, 2) === "897", "typing reaches the cell")
        if (test.window.at()) test.window.tapItem(test.toolbarTool("toolbarCellUndo"))
        if (test.window.at()) test.window.waitStep(test.editing(0, 2) && test.raw(0, 2) === "89", "the toolbar's undo takes it back, the cell edited on")
        if (test.window.at()) test.window.tapItem(test.toolbarTool("toolbarCellRedo"))
        if (test.window.at()) test.window.waitStep(test.editing(0, 2) && test.raw(0, 2) === "897", "redo brings it again")
        if (test.window.at()) test.window.tapItem(test.toolbarTool("toolbarHideKeyboard"))
        if (test.window.at()) test.window.waitStep(test.editingNone() && !test.editor.toolbarVisible, "hide keyboard ends the cell's editing and the toolbar")
        if (test.window.at()) test.undoes(1, "one undo step takes the typing back")
    }

    Component.onCompleted: test.start()
}
