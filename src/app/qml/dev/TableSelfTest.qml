pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import Brulion

// TableSelfTest — rows and columns of a table with real input, in the editor playground (`brulion
// --editor --editor-script tabletest`; ctest tst_editor_qml_table, desktop, and
// tst_editor_qml_table_touch, a phone with --touch): the block menu of a cell has Row and Column
// (a right-click on the cell, also the one being edited; a tap on the cell and the toolbar's
// "More"), a block menu not opened from a cell has neither; a new row or column has its cell
// edited with the keyboard's focus after the menu is gone, typing reaches the document; with keys
// Alt+arrows move the cell's row or column with the cell still edited, Ctrl+Enter inserts a row; the
// header's items that make no sense are disabled; the alignment is ticked, also after choosing the
// one it already has; deleting a row and a column; the formulas' references follow (=B2*C2, =REF!);
// each step is undone and the page's text comes back byte for byte. Sorting from the header's menu
// (the summary row stays, the rows keep their lines, one undo step; rows already in order: nothing
// written), the filter row (typing "> 40" hides a row, nothing is written, "Clear filters"), "Copy
// as CSV" (formulas as their results) and a ```chart of the table following its cells.
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property ApplicationWindow appWindow
    readonly property EditorPlayground window: test.appWindow as EditorPlayground
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property int row: -1
    property int chartRow: -1
    property string before: ""
    /// Alt+Left / Alt+Right, as AppShell's Back and Forward take them, fired.
    property int appShortcuts: 0
    /// A CSV line break. (A "\r" in a compiled function's string literal is written raw into the
    /// C++ qmlcachegen makes, which then does not compile: a property holds it instead.)
    readonly property string crlf: "\r\n"
    /// A point mapped to the window (read through a typed property).
    property point scene
    property Component shortcutComponent: Component {
        Shortcut {
            onActivated: test.appShortcuts += 1
        }
    }

    function check(ok: bool, what: string): void { test.window.check(ok, what) }
    // Typed reads through functions of this file (a member of what another component's member
    // holds does not compile: docs/UI.md "Compiled QML").
    function delegateAt(row: int): BlockDelegate { return test.window.delegateAt(row) }
    function list(): ListView { return test.editor.listView }
    function findItem(item: Item, name: string): Item { return test.window.findItem(item, name) }
    function menuItem(menu: T.Menu, name: string): T.MenuItem { return test.window.menuItemNamed(menu, name) }
    function view(): TableBlockView {
        const item = test.delegateAt(test.row)
        return item ? item.contentItem as TableBlockView : null
    }
    function rows(): list<var> { return test.view().rows }
    function header(): list<string> { return test.view().header }
    function aligns(): list<string> { return test.view().aligns }
    function shown(): list<int> { return test.view().shownRows }
    function menu(): BlockMenu { return test.editor.blockMenu }
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
    function item(sub: string, name: string): T.MenuItem {
        return test.menuItem(test.subMenu(sub), name)
    }
    function raw(r: int, c: int): string { return test.view().rawText(r, c) }
    /// The middle of cell (r, c) in the table's coordinates (r -1 = the header).
    function cellX(c: int): real {
        const t = test.view()
        return t.columnX(c) + t.columnWidth(c) / 2
    }
    function cellY(r: int): real {
        const t = test.view()
        return t.rowY(r) + t.rowHeight / 2
    }
    function tapCell(r: int, c: int): void {
        const t = test.view()
        if (Theme.touchUi) {
            test.window.taskEvents.touchPress(t, test.cellX(c), test.cellY(r))
            test.window.taskEvents.touchRelease(t, test.cellX(c), test.cellY(r))
        } else {
            test.window.taskEvents.mouseClick(t, test.cellX(c), test.cellY(r), Qt.LeftButton, Qt.NoModifier, -1)
        }
    }
    function rightClickCell(r: int, c: int): void {
        test.window.taskEvents.mouseClick(test.view(), test.cellX(c), test.cellY(r), Qt.RightButton, Qt.NoModifier, -1)
    }
    function cellInput(): T.TextField {
        const t = test.view()
        return t ? t.cellInput : null
    }
    function editing(r: int, c: int): bool {
        const t = test.view()
        return t !== null && test.editor.editingTable === t && t.editRow === r && t.editColumn === c
                && test.cellInput() !== null && test.cellInput().activeFocus && !test.menuOpen()
    }
    function keyClick(k: int, modifiers: int): void {
        test.window.taskEvents.keyClick(k, modifiers, -1)
    }
    /// Steps: the block menu of cell (r, c): a right-click; on touch UIs a tap on the cell and the
    /// toolbar's "More".
    function openOn(r: int, c: int): void {
        if (!window.touchRun) {
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
    function cellMore(): Item {
        return test.window.findShown(test.findItem(test.editor, "editToolbarSlot"), "toolbarCellMore")
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
    /// Steps: item `name` of the block menu itself (not of a submenu): tapped, the menu gone.
    function chooseTop(name: string): void {
        if (test.window.at()) {
            const it = test.menuItem(test.menu(), name)
            check(it !== null && it.visible && it.enabled, name + " is in the menu")
            test.window.tapItem(it)
        }
        if (test.window.at())
            test.window.waitStep(!test.menuOpen(), "choosing " + name + " closes the menu")
    }
    function filterField(c: int): Item { return test.findItem(test.view(), "tableFilter" + c) }
    function filterStatus(): Text { return test.findItem(test.view(), "tableFilterStatus") as Text }
    function chartView(): ChartView {
        const item = test.chartRow >= 0 ? test.delegateAt(test.chartRow) : null
        return item ? test.findItem(item, "chartView") as ChartView : null
    }
    function shownRows(): string {
        return test.shown().join(",")
    }
    /// A step: `n` undo steps bring the page's text back.
    function undoes(n: int, what: string): void {
        for (let i = 0; i < n; ++i)
            test.page.undo()
        check(test.window.pageText() === test.before, what)
    }
    /// A step: the edited cell (if any) closed.
    function closeCell(): void {
        const t = test.view()
        if (t.cellInput)
            t.closeCell()
    }
    function clipboard(): string { return test.window.clipboardText() }

    /// Keys in a cell: rows and columns move, the cell stays edited (desktop); on touch UIs the
    /// cell closed and the row taken back.
    function keySteps(): void {
        if (window.touchRun) {
            if (test.window.at())
                test.closeCell()
            if (test.window.at())
                test.undoes(2, "two undo steps take back the typing and the row")
            return
        }
        // ---- Keys in a cell: rows and columns move, the cell stays edited ----------------------
        if (test.window.at()) test.keyClick(Qt.Key_Down, Qt.AltModifier)
        if (test.window.at()) test.window.waitStep(test.editing(2, 1), "Alt+Down moves the row, the cell goes on being edited")
        if (test.window.at()) {
            check(test.raw(2, 1) === "ąć" && test.cellInput().text === "ąć", "with its text")
            check(test.raw(1, 3) === "=B2*C2", "the formula of the row it passed: " + test.raw(1, 3))
        }
        if (test.window.at()) test.keyClick(Qt.Key_Up, Qt.AltModifier)
        if (test.window.at()) test.window.waitStep(test.editing(1, 1), "Alt+Up moves it back")
        if (test.window.at()) test.keyClick(Qt.Key_Right, Qt.AltModifier)
        if (test.window.at()) test.window.waitStep(test.editing(1, 2), "Alt+Right moves the column")
        if (test.window.at()) check(test.appShortcuts === 0, "the app's Forward does not take it")
        if (test.window.at()) {
            check(test.raw(1, 2) === "ąć" && test.header()[1] === "Cena" && test.header()[2] === "Ilość", "the column moved, its header too")
            check(test.raw(2, 3) === "=C3*B3", "formulas follow it: " + test.raw(2, 3))
        }
        if (test.window.at()) test.keyClick(Qt.Key_Left, Qt.AltModifier)
        if (test.window.at()) test.window.waitStep(test.editing(1, 1), "Alt+Left moves it back")
        if (test.window.at()) test.keyClick(Qt.Key_Return, Qt.ControlModifier)
        if (test.window.at()) test.window.waitStep(test.editing(2, 1) && test.rows().length === 6, "Ctrl+Enter inserts a row below, its cell edited")
        if (test.window.at()) check(test.raw(1, 1) === "ąć" && test.raw(3, 3) === "=B4*C4", "the rows below it move down: " + test.raw(3, 3))
        if (test.window.at()) test.keyClick(Qt.Key_Escape, Qt.NoModifier)
        if (test.window.at()) test.window.waitStep(test.editor.editingTable === null && test.page.selectedRow === test.row, "Esc ends the editing, the table selected")
        if (test.window.at()) test.undoes(7, "seven undo steps take back the keys, the typing and the row")
        if (test.window.at()) test.keyClick(Qt.Key_Left, Qt.AltModifier)
        if (test.window.at()) test.window.waitStep(test.appShortcuts === 1, "outside a cell Alt+Left is the app's Back")
    }
    /// A right-click on the cell being edited (desktop).
    function editedCellMenuSteps(): void {
        if (window.touchRun)
            return
        // ---- A right-click on the cell being edited ---------------------------------------------
        if (test.window.at()) test.tapCell(0, 1)
        if (test.window.at()) test.window.waitStep(test.editing(0, 1), "a click edits the cell")
        if (test.window.at()) test.rightClickCell(0, 1)
        if (test.window.at())
            test.window.waitStep(test.menuOpen() && test.menu().inCell && test.menu().cellRow === 0 && test.menu().cellColumn === 1,
                                 "a right-click on the edited cell opens the cell's menu")
        if (test.window.at()) {
            check(test.editor.editingTable === null, "its editing ended")
            test.menu().close()
        }
        if (test.window.at()) test.window.waitStep(!test.menuOpen(), "closed")
    }

    function start(): void {
        test.shortcutComponent.createObject(test.window, { sequence: "Alt+Left" })
        test.shortcutComponent.createObject(test.window, { sequence: "Alt+Right" })
        test.window.runSteps("tabletest")
    }

    function steps(): void {
        if (test.window.at()) {
            test.row = test.window.viewRow("table")
            check(test.row >= 0, "the sample has a table")
            test.list().positionViewAtIndex(test.row, ListView.Center)
            test.before = test.window.pageText()
        }
        // ---- A block menu not from a cell ---------------------------------------------------------
        if (test.window.at()) {
            test.scene = test.view().mapToItem(null, 4, 4)
            test.editor.openBlockMenuAt(test.row, Qt.point(test.scene.x, test.scene.y))
        }
        if (test.window.at()) test.window.waitStep(test.menuOpen(), "the block menu without a cell opens")
        if (test.window.at()) {
            check(!test.menu().inCell && !test.subItem("tableRowMenu").visible && !test.subItem("tableColumnMenu").visible,
                  "no Row or Column without a cell")
            check(test.subItem("copyLinkMenu").visible, "Copy link stays")
            test.menu().close()
        }
        if (test.window.at()) test.window.waitStep(!test.menuOpen(), "closed")
        // ---- Insert a row below: its cell edited, typing reaches the document -------------------
        test.openOn(0, 1)
        if (test.window.at()) check(test.subItem("tableRowMenu").visible && test.subItem("tableColumnMenu").visible, "Row and Column are shown")
        test.choose("tableRowMenu", "tableRowBelow")
        if (test.window.at()) test.window.waitStep(test.editing(1, 1), "the new row's cell is edited, with the keys")
        if (test.window.at()) {
            check(test.rows().length === 5, "one row more")
            check(test.raw(2, 3) === "=B3*C3", "the formula below follows its cells: " + test.raw(2, 3))
            check(test.editing(1, 1), "the cell keeps the keys")
        }
        if (test.window.at()) test.window.typeKeys("ąć")
        if (test.window.at()) check(test.raw(1, 1) === "ąć", "typing reaches the cell")
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
        // ---- Insert a column: its header edited ---------------------------------------------------
        test.openOn(0, 1)
        test.choose("tableColumnMenu", "tableColumnRight")
        if (test.window.at()) test.window.waitStep(test.editing(-1, 2), "the new column's header is edited")
        if (test.window.at()) {
            check(test.cellInput().text === "Kolumna 5" && test.view().columns === 5, "called " + test.cellInput().text)
            check(test.raw(1, 4) === "=B2*D2", "formulas follow: " + test.raw(1, 4))
        }
        if (test.window.at()) test.closeCell()
        if (test.window.at()) test.undoes(1, "one undo step takes the column back")
        // ---- The alignment --------------------------------------------------------------------------
        test.openOn(0, 1)
        if (test.window.at())
            check(test.item("tableColumnMenu", "tableAlignRight").checked && !test.item("tableColumnMenu", "tableAlignCenter").checked,
                  "the column's alignment is ticked")
        test.choose("tableColumnMenu", "tableAlignRight") // the one it has: nothing changes
        test.openOn(0, 1)
        if (test.window.at()) check(test.item("tableColumnMenu", "tableAlignRight").checked, "still ticked after choosing it again")
        test.choose("tableColumnMenu", "tableAlignCenter")
        if (test.window.at()) check(test.aligns()[1] === "center" && Js.indexOf(test.window.pageText(), ":---:") > 0, "centred, in the delimiter row")
        test.openOn(0, 1)
        if (test.window.at())
            check(test.item("tableColumnMenu", "tableAlignCenter").checked && !test.item("tableColumnMenu", "tableAlignRight").checked,
                  "the new one is ticked")
        if (test.window.at()) {
            test.menu().close()
        }
        if (test.window.at()) test.window.waitStep(!test.menuOpen(), "closed")
        if (test.window.at()) test.closeCell()
        if (test.window.at()) test.undoes(1, "one undo step takes the alignment back")
        // ---- Delete a row, a column ------------------------------------------------------------------
        test.openOn(0, 1)
        test.choose("tableRowMenu", "tableRowDelete")
        if (test.window.at()) test.window.waitStep(test.rows().length === 3, "the row is gone")
        if (test.window.at()) check(test.raw(0, 3) === "=B1*C1" && Js.indexOf(test.window.pageText(), "Płytki") < 0, "formulas follow: " + test.raw(0, 3))
        if (test.window.at()) test.closeCell()
        if (test.window.at()) test.undoes(1, "one undo step brings the row back")
        test.openOn(0, 1)
        test.choose("tableColumnMenu", "tableColumnDelete")
        if (test.window.at()) test.window.waitStep(test.view().columns === 3, "the column is gone")
        if (test.window.at()) {
            check(test.raw(1, 2) === "=REF!*B2", "a reference to it: " + test.raw(1, 2))
            check(DevTest.valueAt(test.view().rows, "1.2.display") === "#ERR" && DevTest.valueAt(test.view().rows, "1.2.error") !== "",
                  "shown as an error")
        }
        if (test.window.at()) test.closeCell()
        if (test.window.at()) test.undoes(1, "one undo step brings the column back")
        test.editedCellMenuSteps()
        // ---- Sorting from the header ----------------------------------------------------------------
        test.openOn(-1, 2)
        if (test.window.at()) check(test.item("tableColumnMenu", "tableSortAscending").enabled, "the header's column sorts")
        test.choose("tableColumnMenu", "tableSortAscending")
        if (test.window.at() && !test.window.waiting(Js.indexOf(test.raw(0, 0), "Fuga") === 0 && Js.indexOf(test.raw(2, 0), "**Płytki**") === 0))
            test.window.checkWait("sorted by the price: " + test.raw(0, 0) + " / " + test.raw(2, 0))
        if (test.window.at()) {
            check(test.raw(3, 0) === "Suma", "the summary row stays at the end")
            check(test.raw(1, 3) === "=B2*C2", "a formula of a row that stayed: " + test.raw(1, 3))
            check(Js.indexOf(test.window.pageText(), "| Fuga #zakupy | 2 | 38 | =[Ilość]*[Cena] |\n| Klej *elastyczny* | 4 | 45 | =B2*C2 |\n| **Płytki**") > 0,
                  "the rows keep their lines")
        }
        if (test.window.at()) test.closeCell()
        if (test.window.at()) test.undoes(1, "one undo step takes the sort back")
        test.openOn(-1, 2)
        test.choose("tableColumnMenu", "tableSortDescending")
        if (test.window.at()) check(test.window.pageText() === test.before, "rows in that order already: nothing written")
        // ---- Filters -------------------------------------------------------------------------------
        test.openOn(0, 2)
        test.chooseTop("tableFilterRows")
        if (test.window.at())
            test.window.waitStep(test.view().filtering && test.filterField(2) !== null && test.filterField(2).activeFocus,
                                 "the filter row shows, the keys in the column's field")
        if (test.window.at()) {
            const typed = "> 40"
            for (let i = 0; i < typed.length; ++i)
                EditorInput.sendKey(test.filterField(2), 0, 0, typed[i])
        }
        if (test.window.at() && !test.window.waiting(test.shown().length === 3))
            test.window.checkWait("a row under 40 is hidden: " + test.shownRows())
        if (test.window.at()) {
            check(test.shown().indexOf(2) < 0 && test.shown().indexOf(3) >= 0, "Fuga hidden, the summary row shown")
            check(test.window.pageText() === test.before, "filtering writes nothing")
            check(Js.indexOf(test.filterStatus().text, "3") >= 0, "the rows shown are counted")
        }
        if (test.window.at()) {
            // The field gives the keys back (on touch UIs the keyboard and its toolbar go).
            test.filterField(2).focus = false
            test.list().forceActiveFocus()
            test.list().positionViewAtIndex(test.row, ListView.Beginning) // the buttons above the bottom bar
        }
        if (test.window.at() && !test.window.waiting(!test.filterField(2).activeFocus && test.editor.toolbarMode !== "cell"))
            test.window.checkWait("the filter field is left: " + test.filterField(2).activeFocus + " " + test.editor.toolbarMode + " " + test.window.activeFocusItem)
        if (test.window.at()) test.window.tapItem(test.findItem(test.view(), "tableClearFilters"))
        if (test.window.at() && !test.window.waiting(!test.view().filtering && test.shown().length === 4))
            test.window.checkWait("Clear filters shows every row: " + test.view().filtering + " " + test.shownRows()
                  + " " + test.findItem(test.view(), "tableClearFilters"))
        // ---- CSV -----------------------------------------------------------------------------------
        test.openOn(0, 1)
        test.chooseTop("tableCopyCsv")
        if (test.window.at() && !test.window.waiting(test.csvCopied()))
            test.window.checkWait("the table copied as CSV: " + Js.slice(test.clipboard(), 0, 40))
        if (test.window.at())
            check(Js.indexOf(test.clipboard(), "=[") < 0 && Js.indexOf(test.clipboard(), "Fuga #zakupy,2,38,76") > 0,
                  "formulas as their results")
        if (test.window.at()) test.closeCell()
        // ---- A chart of the table ----------------------------------------------------------------------
        if (test.window.at()) {
            test.chartRow = test.page.insertBlockAfter(test.row, "```chart\ntype: bar\ny: Razem\n```")
            test.page.stopEditing()
            test.list().positionViewAtIndex(test.chartRow, ListView.Center)
        }
        if (test.window.at() && !test.window.waiting(test.chartDrawn()))
            test.window.checkWait("the chart is drawn: " + (test.chartView() ? test.chartView().status + " " + test.chartView().message : "none"))
        if (test.window.at())
            check(Js.indexOf(test.chartView().description, "Fuga") >= 0 && Js.indexOf(test.chartView().description, "Suma") < 0,
                  "its text alternative names the rows, not the summary: " + test.chartView().description)
        if (test.window.at()) test.page.setTableCell(test.row, 2, 2, "100")
        if (test.window.at() && !test.window.waiting(test.chartShows("200")))
            test.window.checkWait("the chart follows the table: " + (test.chartView() ? test.chartView().description : ""))
        if (test.window.at()) test.undoes(2, "two undo steps take the cell and the chart back")
        if (test.window.at()) check(test.window.pageText() === test.before, "the page's text is as it was")
    }
    function csvCopied(): bool {
        return Js.indexOf(test.clipboard(), "Pozycja,Ilość,Cena,Razem" + test.crlf) === 0
               || Js.indexOf(test.clipboard(), "Pozycja,Ilość,Cena,Razem\n") === 0
    }
    function chartDrawn(): bool {
        const chart = test.chartView()
        return chart !== null && chart.status === "ok" && chart.height > 50
    }
    function chartShows(text: string): bool {
        const chart = test.chartView()
        return chart !== null && Js.indexOf(chart.description, text) >= 0
    }

    Component.onCompleted: test.start()
}
