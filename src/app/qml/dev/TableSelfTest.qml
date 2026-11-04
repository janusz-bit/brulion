pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// TableSelfTest — rows and columns of a table with real input, in the editor playground (`brulion
// --editor --editor-script tabletest`; ctest tst_editor_qml_table, desktop, and
// tst_editor_qml_table_touch, a phone with --touch): the block menu of a cell has Wiersz and Kolumna
// (a right-click on the cell, also the one being edited; a tap on the cell and the toolbar's
// "Więcej"), a block menu not opened from a cell has neither; a new row or column has its cell
// edited with the keyboard's focus after the menu is gone, typing reaches the document; with keys
// Alt+arrows move the cell's row or column with the cell still edited, Ctrl+Enter inserts a row; the
// header's items that make no sense are disabled; the alignment is ticked, also after choosing the
// one it already has; deleting a row and a column; the formulas' references follow (=B2*C2, =REF!);
// each step is undone and the page's text comes back byte for byte. Sorting from the header's menu
// (the summary row stays, the rows keep their lines, one undo step; rows already in order: nothing
// written), the filter row (typing "> 40" hides a row, nothing is written, "Wyczyść filtry"), "Kopiuj
// jako CSV" (formulas as their results) and a ```chart of the table following its cells.
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property var window
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property var events: null
    property int row: -1
    property int chartRow: -1
    property string before: ""
    /// Alt+Left / Alt+Right, as AppShell's Back and Forward take them, fired.
    property int appShortcuts: 0

    function check(ok, what) { window.check(ok, what) }
    function wait(ready, what) { return window.waitStep(ready, what) }
    function view() {
        const item = window.delegateAt(row)
        return item ? item.contentItem as TableBlockView : null
    }
    function menu() { return editor.blockMenu }
    function menuOpen() { return editor.blockMenu !== null && editor.blockMenu.opened }
    /// The item of the block menu that opens its submenu `name`.
    function subItem(name) {
        const m = menu()
        for (let i = 0; m && i < m.count; ++i) {
            const it = m.itemAt(i)
            if (it && it.subMenu && it.subMenu.objectName === name)
                return it
        }
        return null
    }
    function item(sub, name) {
        const s = subItem(sub)
        return s ? window.menuItemNamed(s.subMenu, name) : null
    }
    function raw(r, c) { return view().rawText(r, c) }
    /// The middle of cell (r, c) in the table's coordinates (r -1 = the header).
    function pointOf(r, c) {
        const t = view()
        return Qt.point(t.columnWidths.slice(0, c).reduce((a, b) => a + b, 0) + t.columnWidths[c] / 2,
                        t.rowY(r) + t.rowHeight / 2)
    }
    function tapCell(r, c) {
        const t = view()
        const p = pointOf(r, c)
        if (Theme.touchUi) {
            events.touchEvent(t).press(0, t, p.x, p.y).commit()
            events.touchEvent(t).release(0, t, p.x, p.y).commit()
        } else {
            events.mouseClick(t, p.x, p.y, Qt.LeftButton, Qt.NoModifier, -1)
        }
    }
    function editing(r, c) {
        const t = view()
        return t !== null && editor.editingTable === t && t.editRow === r && t.editColumn === c
                && t.cellInput !== null && t.cellInput.activeFocus && !menuOpen()
    }
    /// The block menu of cell (r, c): a right-click; on touch UIs a tap on the cell and the
    /// toolbar's "Więcej".
    function openOn(r, c) {
        const opened = wait(() => menuOpen() && menu().cellRow === r && menu().cellColumn === c,
                            () => "the menu of cell " + r + "," + c + " opens")
        if (!Theme.touchUi) {
            return [
                () => {
                    const p = pointOf(r, c)
                    events.mouseClick(view(), p.x, p.y, Qt.RightButton, Qt.NoModifier, -1)
                },
                opened
            ]
        }
        return [
            () => tapCell(r, c),
            wait(() => editor.toolbarMode === "cell" && window.findShown(window.findItem(editor, "editToolbarSlot"), "toolbarCellMore") !== null,
                 "a cell's toolbar has “More”"),
            () => {
                const flick = window.findItem(editor, "editToolbarFlick")
                flick.contentX = Math.max(0, flick.contentWidth - flick.width)
            },
            () => window.tapItem(window.findShown(window.findItem(editor, "editToolbarSlot"), "toolbarCellMore")),
            opened
        ]
    }
    /// Choose item `name` of the submenu `sub` and wait until the menu is gone.
    function choose(sub, name) {
        return [
            () => window.tapItem(subItem(sub)),
            wait(() => subItem(sub) && subItem(sub).subMenu.opened, "the submenu " + sub + " opens"),
            () => {
                check(item(sub, name) !== null && item(sub, name).enabled, name + " can be chosen")
                window.tapItem(item(sub, name))
            },
            wait(() => !menuOpen(), "choosing " + name + " closes the menu")
        ]
    }
    /// Item `name` of the block menu itself (not of a submenu): tapped, the menu gone.
    function chooseTop(name) {
        return [
            () => {
                const it = window.menuItemNamed(menu(), name)
                check(it !== null && it.visible && it.enabled, name + " is in the menu")
                window.tapItem(it)
            },
            wait(() => !menuOpen(), "choosing " + name + " closes the menu")
        ]
    }
    function filterField(c) { return window.findItem(view(), "tableFilter" + c) }
    function chartView() {
        const item = chartRow >= 0 ? window.delegateAt(chartRow) : null
        return item ? window.findItem(item, "chartView") : null
    }
    function undoes(n, what) {
        return () => {
            for (let i = 0; i < n; ++i)
                page.undo()
            check(window.pageText() === before, what)
        }
    }
    function closeCell() {
        return () => {
            const t = view()
            if (t.cellInput)
                t.closeCell()
        }
    }

    function steps() {
        events = Qt.createQmlObject('import QtQuick; import QtTest; TestEvent {}', test, "tableEvents")
        window.taskEvents = events // (window.tapItem)
        for (const sequence of ["Alt+Left", "Alt+Right"]) {
            const shortcut = Qt.createQmlObject('import QtQuick; Shortcut { sequence: "' + sequence + '" }', window, "appShortcut")
            shortcut.activated.connect(() => { test.appShortcuts += 1 })
        }
        const keys = !Theme.touchUi ? [
            // ---- Keys in a cell: rows and columns move, the cell stays edited ----------------------
            () => events.keyClick(Qt.Key_Down, Qt.AltModifier, -1),
            wait(() => editing(2, 1), "Alt+Down moves the row, the cell goes on being edited"),
            () => {
                check(raw(2, 1) === "ąć" && view().cellInput.text === "ąć", "with its text")
                check(raw(1, 3) === "=B2*C2", "the formula of the row it passed: " + raw(1, 3))
            },
            () => events.keyClick(Qt.Key_Up, Qt.AltModifier, -1),
            wait(() => editing(1, 1), "Alt+Up moves it back"),
            () => events.keyClick(Qt.Key_Right, Qt.AltModifier, -1),
            wait(() => editing(1, 2), "Alt+Right moves the column"),
            () => check(appShortcuts === 0, "the app's Forward does not take it"),
            () => {
                check(raw(1, 2) === "ąć" && view().header[1] === "Cena" && view().header[2] === "Ilość", "the column moved, its header too")
                check(raw(2, 3) === "=C3*B3", "formulas follow it: " + raw(2, 3))
            },
            () => events.keyClick(Qt.Key_Left, Qt.AltModifier, -1),
            wait(() => editing(1, 1), "Alt+Left moves it back"),
            () => events.keyClick(Qt.Key_Return, Qt.ControlModifier, -1),
            wait(() => editing(2, 1) && view().rows.length === 6, "Ctrl+Enter inserts a row below, its cell edited"),
            () => check(raw(1, 1) === "ąć" && raw(3, 3) === "=B4*C4", "the rows below it move down: " + raw(3, 3)),
            () => events.keyClick(Qt.Key_Escape, Qt.NoModifier, -1),
            wait(() => editor.editingTable === null && page.selectedRow === row, "Esc ends the editing, the table selected"),
            undoes(7, "seven undo steps take back the keys, the typing and the row"),
            () => events.keyClick(Qt.Key_Left, Qt.AltModifier, -1),
            wait(() => appShortcuts === 1, "outside a cell Alt+Left is the app's Back")
        ] : [
            closeCell(),
            undoes(2, "two undo steps take back the typing and the row")
        ]
        const editedCellMenu = !Theme.touchUi ? [
            // ---- A right-click on the cell being edited ---------------------------------------------
            () => tapCell(0, 1),
            wait(() => editing(0, 1), "a click edits the cell"),
            () => {
                const p = pointOf(0, 1)
                events.mouseClick(view(), p.x, p.y, Qt.RightButton, Qt.NoModifier, -1)
            },
            wait(() => menuOpen() && menu().inCell && menu().cellRow === 0 && menu().cellColumn === 1,
                 "a right-click on the edited cell opens the cell's menu"),
            () => {
                check(editor.editingTable === null, "its editing ended")
                menu().close()
            },
            wait(() => !menuOpen(), "closed")
        ] : []

        return [
            () => {
                row = window.viewRow("table")
                check(row >= 0, "the sample has a table")
                editor.listView.positionViewAtIndex(row, ListView.Center)
                before = window.pageText()
            },
            // ---- A block menu not from a cell ---------------------------------------------------------
            () => {
                const scene = view().mapToItem(null, 4, 4)
                editor.openBlockMenuAt(row, Qt.point(scene.x, scene.y))
            },
            wait(menuOpen, "the block menu without a cell opens"),
            () => {
                check(!menu().inCell && !subItem("tableRowMenu").visible && !subItem("tableColumnMenu").visible,
                      "no Row or Column without a cell")
                check(subItem("copyLinkMenu").visible, "Copy link stays")
                menu().close()
            },
            wait(() => !menuOpen(), "closed"),
            // ---- Insert a row below: its cell edited, typing reaches the document -------------------
            ...openOn(0, 1),
            () => check(subItem("tableRowMenu").visible && subItem("tableColumnMenu").visible, "Row and Column are shown"),
            ...choose("tableRowMenu", "tableRowBelow"),
            wait(() => editing(1, 1), "the new row's cell is edited, with the keys"),
            () => {
                check(view().rows.length === 5, "one row more")
                check(raw(2, 3) === "=B3*C3", "the formula below follows its cells: " + raw(2, 3))
                check(editing(1, 1), "the cell keeps the keys")
            },
            () => window.typeKeys("ąć"),
            () => check(raw(1, 1) === "ąć", "typing reaches the cell"),
            ...keys,
            // ---- The header's menu --------------------------------------------------------------------
            ...openOn(-1, 1),
            () => {
                check(!item("tableRowMenu", "tableRowAbove").enabled && item("tableRowMenu", "tableRowBelow").enabled,
                      "nothing goes above the header, a row below it can")
                check(!item("tableRowMenu", "tableRowUp").enabled && !item("tableRowMenu", "tableRowDown").enabled
                      && !item("tableRowMenu", "tableRowDelete").enabled, "the header does not move or go")
                check(item("tableColumnMenu", "tableColumnDelete").enabled && item("tableColumnMenu", "tableColumnMoveLeft").enabled,
                      "its column does")
                menu().close()
            },
            wait(() => !menuOpen(), "closed"),
            // ---- Insert a column: its header edited ---------------------------------------------------
            ...openOn(0, 1),
            ...choose("tableColumnMenu", "tableColumnRight"),
            wait(() => editing(-1, 2), "the new column's header is edited"),
            () => {
                check(view().cellInput.text === "Kolumna 5" && view().columns === 5, "called " + view().cellInput.text)
                check(raw(1, 4) === "=B2*D2", "formulas follow: " + raw(1, 4))
            },
            closeCell(),
            undoes(1, "one undo step takes the column back"),
            // ---- The alignment --------------------------------------------------------------------------
            ...openOn(0, 1),
            () => check(item("tableColumnMenu", "tableAlignRight").checked && !item("tableColumnMenu", "tableAlignCenter").checked,
                        "the column's alignment is ticked"),
            ...choose("tableColumnMenu", "tableAlignRight"), // the one it has: nothing changes
            ...openOn(0, 1),
            () => check(item("tableColumnMenu", "tableAlignRight").checked, "still ticked after choosing it again"),
            ...choose("tableColumnMenu", "tableAlignCenter"),
            () => check(view().aligns[1] === "center" && window.pageText().indexOf(":---:") > 0, "centred, in the delimiter row"),
            ...openOn(0, 1),
            () => check(item("tableColumnMenu", "tableAlignCenter").checked && !item("tableColumnMenu", "tableAlignRight").checked,
                        "the new one is ticked"),
            () => {
                menu().close()
            },
            wait(() => !menuOpen(), "closed"),
            closeCell(),
            undoes(1, "one undo step takes the alignment back"),
            // ---- Delete a row, a column ------------------------------------------------------------------
            ...openOn(0, 1),
            ...choose("tableRowMenu", "tableRowDelete"),
            wait(() => view().rows.length === 3, "the row is gone"),
            () => check(raw(0, 3) === "=B1*C1" && window.pageText().indexOf("Płytki") < 0, "formulas follow: " + raw(0, 3)),
            closeCell(),
            undoes(1, "one undo step brings the row back"),
            ...openOn(0, 1),
            ...choose("tableColumnMenu", "tableColumnDelete"),
            wait(() => view().columns === 3, "the column is gone"),
            () => {
                check(raw(1, 2) === "=REF!*B2", "a reference to it: " + raw(1, 2))
                check(view().rows[1][2].display === "#ERR" && view().rows[1][2].error !== "", "shown as an error")
            },
            closeCell(),
            undoes(1, "one undo step brings the column back"),
            ...editedCellMenu,
            // ---- Sorting from the header ----------------------------------------------------------------
            ...openOn(-1, 2),
            () => check(item("tableColumnMenu", "tableSortAscending").enabled, "the header's column sorts"),
            ...choose("tableColumnMenu", "tableSortAscending"),
            wait(() => raw(0, 0).indexOf("Fuga") === 0 && raw(2, 0).indexOf("**Płytki**") === 0,
                 () => "sorted by the price: " + raw(0, 0) + " / " + raw(2, 0)),
            () => {
                check(raw(3, 0) === "Suma", "the summary row stays at the end")
                check(raw(1, 3) === "=B2*C2", "a formula of a row that stayed: " + raw(1, 3))
                check(window.pageText().indexOf("| Fuga #zakupy | 2 | 38 | =[Ilość]*[Cena] |\n| Klej *elastyczny* | 4 | 45 | =B2*C2 |\n| **Płytki**") > 0,
                      "the rows keep their lines")
            },
            closeCell(),
            undoes(1, "one undo step takes the sort back"),
            ...openOn(-1, 2),
            ...choose("tableColumnMenu", "tableSortDescending"),
            () => check(window.pageText() === before, "rows in that order already: nothing written"),
            // ---- Filters -------------------------------------------------------------------------------
            ...openOn(0, 2),
            ...chooseTop("tableFilterRows"),
            wait(() => view().filtering && filterField(2) !== null && filterField(2).activeFocus,
                 "the filter row shows, the keys in the column's field"),
            () => {
                for (const ch of "> 40")
                    EditorInput.sendKey(filterField(2), 0, 0, ch)
            },
            wait(() => view().shownRows.length === 3, () => "a row under 40 is hidden: " + view().shownRows),
            () => {
                check(view().shownRows.indexOf(2) < 0 && view().shownRows.indexOf(3) >= 0, "Fuga hidden, the summary row shown")
                check(window.pageText() === before, "filtering writes nothing")
                check(window.findItem(view(), "tableFilterStatus").text.indexOf("3") >= 0, "the rows shown are counted")
            },
            () => {
                // The field gives the keys back (on touch UIs the keyboard and its toolbar go).
                filterField(2).focus = false
                editor.listView.forceActiveFocus()
                editor.listView.positionViewAtIndex(row, ListView.Beginning) // the buttons above the bottom bar
            },
            wait(() => !filterField(2).activeFocus && editor.toolbarMode !== "cell",
                 () => "the filter field is left: " + filterField(2).activeFocus + " " + editor.toolbarMode + " " + window.activeFocusItem),
            () => window.tapItem(window.findItem(view(), "tableClearFilters")),
            wait(() => !view().filtering && view().shownRows.length === 4,
                 () => "Clear filters shows every row: " + view().filtering + " " + view().shownRows
                       + " " + window.findItem(view(), "tableClearFilters")),
            // ---- CSV -----------------------------------------------------------------------------------
            ...openOn(0, 1),
            ...chooseTop("tableCopyCsv"),
            wait(() => window.clipboardText().indexOf("Pozycja,Ilość,Cena,Razem\r\n") === 0
                       || window.clipboardText().indexOf("Pozycja,Ilość,Cena,Razem\n") === 0,
                 () => "the table copied as CSV: " + window.clipboardText().slice(0, 40)),
            () => check(window.clipboardText().indexOf("=[") < 0 && window.clipboardText().indexOf("Fuga #zakupy,2,38,76") > 0,
                        "formulas as their results"),
            closeCell(),
            // ---- A chart of the table ----------------------------------------------------------------------
            () => {
                chartRow = page.insertBlockAfter(row, "```chart\ntype: bar\ny: Razem\n```")
                page.stopEditing()
                editor.listView.positionViewAtIndex(chartRow, ListView.Center)
            },
            wait(() => chartView() !== null && chartView().status === "ok" && chartView().height > 50,
                 () => "the chart is drawn: " + (chartView() ? chartView().status + " " + chartView().message : "none")),
            () => check(chartView().description.indexOf("Fuga") >= 0 && chartView().description.indexOf("Suma") < 0,
                        "its text alternative names the rows, not the summary: " + chartView().description),
            () => page.setTableCell(row, 2, 2, "100"),
            wait(() => chartView() !== null && chartView().description.indexOf("200") >= 0,
                 () => "the chart follows the table: " + (chartView() ? chartView().description : "")),
            undoes(2, "two undo steps take the cell and the chart back"),
            () => check(window.pageText() === before, "the page's text is as it was")
        ]
    }

    Component.onCompleted: window.runSteps(steps())
}
