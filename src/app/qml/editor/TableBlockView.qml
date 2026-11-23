pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// TableBlockView — a GFM table as a grid (docs/SYNTAX.md §9 "Table formulas"). Cells show their
// Markdown rendered like block text (bold, links, tags, date chips — the core InlineRenderer, so
// note text is never interpreted as HTML); formula cells (`=sum(above)`, `=[Price]*[Quantity]`) show
// the result (errors as #ERR with the message as tooltip, cycles as #CYCLE). Links in cells work
// like links in blocks (a click elsewhere edits the cell); a right-click or long press on a date,
// reminder, 🔁 or priority chip of a cell opens its menu (DateChipMenu: the cell's text changes, one
// undo step), on a link the link's menu. Clicking a cell edits its raw text (the formula);
// Enter / Tab / Shift+Tab move, Esc cancels the cell's edit. Typing reaches the document at once
// (PageController.setTableCell with `typing`: one undo step per cell), so a cell left open when
// the app is suspended or closed is saved like any text. Rows and columns are added with
// the buttons below the grid: always shown on touch screens; with a mouse while the table is
// selected or a cell is edited (Enter in the last row and Tab in the last cell add a row too).
// A right-click on a cell, also the one being edited, opens the block menu with the cell's Row
// and Column submenus (BlockMenu: insert, move, delete, align; on touch UIs through the toolbar's
// "More"); in a cell Ctrl+Enter inserts a row below and Alt+arrows move its row or column, the
// cell moving with it. The formulas' references follow their cells (calc/table.h).
// While a cell is edited the editor knows (BlockEditor.editingTable): on touch UIs its toolbar,
// docked on the keyboard, offers undo, redo, the next cell, "More" (the table's block menu,
// BlockEditor.openTableBlockMenu) and "hide keyboard" (nextCell(), undoInCell(), redoInCell(),
// closeCell(): the cell stays edited after undo and redo), and the cell is kept above it. The
// table is the current block meanwhile (Ctrl+Shift+C, the palette's link commands); a cell left
// for the palette or the side panel leaves the table selected (stayCurrent()).
// A rectangle of cells is selected with Shift+click or Shift+arrows (a cell's at the text's edge:
// the cell is left, the keys go to `rangeKeys`): arrows move it, Enter / F2 or a typed character edit
// the cell, Delete / Backspace empty the cells, Ctrl+C / Ctrl+X copy and cut them as TSV (the block
// menu copies them as Markdown too), Ctrl+V pastes TSV, CSV, a Markdown table or lines there, Esc
// leaves it (PageController.copyTableCells / clearTableCells / pasteTableCells: one undo step each).
// Sorting and filters (docs/SYNTAX.md §9): the Column submenu sorts the rows by the cell's column
// (PageController.sortTable: the file's rows rewritten, one undo step) and "Filter rows" shows a
// row of filter fields under the header (one per column: "> 100", "!text", "zolw"); rows that do
// not match are hidden — nothing is written, the filters last while the page is open
// (BlockListModel.setTableFilter) — and the line under the grid says how many rows show, with
// "Clear filters". A row being edited stays shown until it is left. The block menu also copies the
// table as CSV or saves it to a file (EditorDialogs.saveTableCsv).
Item {
    id: root

    /// The delegate (an Item: BlockDelegate passes itself, and a file cannot store itself into
    /// a property of its own type — compiled, a module-qualified cast crashes).
    property Item blockItem
    final property BlockDelegate block: blockItem as BlockDelegate
    /// The block's row (read once here: the delegate's `index` is no final property).
    readonly property int blockIndex: block ? block.index : -1
    readonly property list<string> header: root.block.rowData.tableData.header
    readonly property list<string> aligns: root.block.rowData.tableData.aligns
    readonly property list<var> rows: root.block.rowData.tableData.rows
    readonly property int columns: header.length
    /// The filter row is shown (the block menu's "Filter rows"); its texts by column.
    readonly property bool filtering: root.block.rowData.tableData.filtering === true
    readonly property list<string> filters: root.block.rowData.tableData.filters
    /// How many rows pass the filters.
    readonly property int shownCount: root.block.rowData.tableData.shownCount
    /// The rows shown, in order: those passing the filters, and the row being edited.
    readonly property list<int> shownRows: BlockViews.tableShownRows(root.block.rowData.tableData, root.editRow)
    readonly property real filterHeight: filtering ? (Theme.touchUi ? 48 : 36) : 0
    readonly property int cellPadding: Theme.space3
    readonly property int rowHeight: Theme.touchUi ? 40 : 32
    /// The header's lines (`<br>` in a header cell: up to three) and its row's height.
    readonly property int headerLines: Math.max(1, Math.min(3, root.block.rowData.tableData.headerLines))
    readonly property int headerHeight: rowHeight + (headerLines - 1) * Theme.lineHeightPx(Theme.textBase, 1.4)
    readonly property real firstLineCenter: rowHeight / 2
    property int editRow: -2   // -2 none, -1 header
    property int editColumn: -1
    /// The text field of the cell being edited (null when none): leave(), next(), undoTable().
    final property T.TextField cellInput: null
    readonly property bool active: editRow > -2 || root.block.rowData.isSelected
    /// The row of buttons below the grid. Not on hover: a table that grew under the resting mouse
    /// pointer moved everything below it while the page scrolled past, and room kept for hidden
    /// buttons left a gap under every table (docs/UI.md "Scrolling performance"). Selecting the
    /// table or editing a cell — never scrolling — adds it.
    /// Reading mode (BlockEditor.readOnly): no cell opens for typing, no "+ Row" / "+ Column";
    /// filtering (it writes nothing) stays.
    readonly property bool readOnly: root.block.editor !== null && root.block.editor.readOnly
    readonly property bool buttonsShown: ((active || Theme.touchUi) && !readOnly) || filtering

    /// Natural column widths (clamped), stretched to fill the available width.
    readonly property list<real> columnWidths: BlockViews.tableColumnWidths(root.block.rowData.tableData, headerMetrics.font,
                                                                            cellMetrics.font, root.width, root.cellPadding)
    readonly property real gridWidth: root.columnX(root.columnWidths.length)

    /// leave()'s "no next cell": the cell's editing ends.
    readonly property int noCell: -99
    /// A command for the cell being edited (its field acts on it): "next", "leave".
    signal cellCommand(string command)
    /// Shift+click on cell (toRow, toColumn) while a cell is edited: its field selects up to it.
    signal cellExtend(int toRow, int toColumn)

    // The buttons' own height (the Loader's, from its item's implicit size at creation), not a
    // Row's, which a positioner only sets in its next polish.
    implicitHeight: frame.height + (buttons.active ? buttons.implicitHeight + Theme.space1 : 0)
    height: implicitHeight

    Accessible.role: Accessible.Table
    Accessible.name: qsTr("Table")

    FontMetrics { id: headerMetrics; font.family: Theme.fontUi; font.pixelSize: Theme.textBase; font.weight: Theme.weightSemiBold }
    FontMetrics { id: cellMetrics; font.family: Theme.fontUi; font.pixelSize: Theme.textBase }
    /// rawText()'s cell.
    MapHolder { id: scratch }

    /// The x of column `c` (the widths of the columns before it).
    function columnX(c: int): real {
        let x = 0
        for (let i = 0; i < c && i < root.columnWidths.length; ++i)
            x += root.columnWidths[i]
        return x
    }
    function columnWidth(c: int): real {
        return c >= 0 && c < root.columnWidths.length ? root.columnWidths[c] : 0
    }
    function headerText(c: int): string {
        return c >= 0 && c < root.header.length ? root.header[c] : ""
    }
    function alignOf(c: int): string {
        return c >= 0 && c < root.aligns.length ? root.aligns[c] : ""
    }
    /// The table's row count (BlockEditor: the block menu of the cell being edited).
    function cellRowCount(): int {
        return root.rows.length
    }
    /// Column `c`'s alignment ("" the default).
    function cellAlignOf(c: int): string {
        return root.alignOf(c)
    }
    function filterOf(c: int): string {
        return c >= 0 && c < root.filters.length ? root.filters[c] : ""
    }
    /// Cell (r, c) of the table: { text, display, html, plain, error, formula } (r -1 the header).
    function cellData(r: int, c: int): var {
        return BlockViews.tableCell(root.block.rowData.tableData, r, c)
    }
    function focusFirstCell(): void {
        startCell(rows.length > 0 ? 0 : -1, 0)
    }
    /// The menu of the token or link at (x, y) of cell (r, c)'s text `text` (r -1 = the header): a
    /// date, reminder, 🔁 or priority chip's (the cell's text changes), a link's. False: none there.
    function cellMenu(r: int, c: int, text: BRichText, x: real, y: real): bool {
        const editor = root.block.editor
        const link = root.linkIn(text, x, y)
        if (link === "")
            return false
        if (editor.openCellTokenMenu(root.blockIndex, r, c, link, text, x, y))
            return true
        // (No requester: the editor opens the link on this block's row itself.)
        return editor.openLinkMenu(link, "", root.blockIndex, null, text, x, y)
    }
    /// The link (path part) at `cursor` of a cell's `text`.
    function linkInText(text: string, cursor: int): string {
        return root.block.controller.linkInText(root.blockIndex, text, cursor)
    }
    /// The link at (x, y) of `text` ("" none).
    function linkIn(text: BRichText, x: real, y: real): string {
        return text.linkAt(x, y)
    }
    function startCell(r: int, c: int): void {
        if (c < 0 || c >= columns || r < -1 || r >= rows.length || root.block.editor.readOnlyRefuses())
            return
        root.block.controller.selectedRow = -1
        root.block.controller.stopEditing()
        // (The column first: row and column change together, no cell in between is edited.)
        editColumn = -1
        editRow = r
        editColumn = c
        root.caretAtStart = -1
        root.typedText = ""
    }
    /// startCell(r, c) once the events being handled are done (as Qt.callLater: repeated calls
    /// merge into the last one).
    property int laterRow: -2
    property int laterColumn: -1
    function startCellLater(r: int, c: int): void {
        root.laterRow = r
        root.laterColumn = c
        startLaterTimer.restart()
    }
    Later {
        id: startLaterTimer
        onTriggered: root.startCell(root.laterRow, root.laterColumn)
    }
    /// The y of row `r` in the grid (-1 the header); a hidden row: where it would be.
    function rowY(r: int): real {
        if (r < 0)
            return 0
        const shown = shownRows.indexOf(r)
        return headerHeight + filterHeight + (shown >= 0 ? shown : shownRows.length) * rowHeight
    }
    /// The shown row after (`step` 1) or before (-1) row `r` (-1 the header): rows.length after the
    /// last, -1 before the first.
    function nextShownRow(r: int, step: int): int {
        return BlockViews.tableNextShownRow(root.block.rowData.tableData, r, step)
    }
    /// Show the filter row with the keys in column `c`'s field.
    property int filterFocusColumn: 0
    function openFilter(c: int): void {
        root.block.controller.setTableFilterOpen(root.blockIndex, true)
        root.filterFocusColumn = Math.max(0, c)
        filterFocusTimer.restart()
    }
    Later {
        id: filterFocusTimer
        onTriggered: {
            const field = filterRepeater.itemAt(root.filterFocusColumn)
            if (field)
                field.forceActiveFocus()
        }
    }
    function rawText(r: int, c: int): string {
        scratch.map = root.cellData(r, c)
        return scratch.map.text
    }
    function commit(r: int, c: int, value: string): void {
        if (value !== rawText(r, c))
            root.block.controller.setTableCell(root.blockIndex, r, c, value)
    }
    function finish(): void {
        editRow = -2
        editColumn = -1
    }

    // ---- The cell selection -----------------------------------------------------------------------
    /// The selected rectangle: from the anchor to the cursor cell (-1 the header, -2: none). Not
    /// while a cell is edited: leaving it by Shift+arrows or Shift+click selects.
    property int anchorRow: -2
    property int anchorColumn: -1
    property int cursorRow: -2
    property int cursorColumn: -1
    readonly property bool hasSelection: anchorRow > -2
    readonly property bool multiCell: anchorRow > -2 && (anchorRow !== cursorRow || anchorColumn !== cursorColumn)
    /// The caret of the cell that opens next (-1: at its end): a cell reached by Left / Right / Up /
    /// Down (startCell() hands it to the field and forgets it).
    property int caretAtStart: -1
    /// The keys of the selection (rangeKeys) hold the keyboard.
    readonly property bool selecting: hasSelection && rangeKeys.activeFocus
    function selectionTop(): int { return Math.min(anchorRow, cursorRow) }
    function selectionBottom(): int { return Math.max(anchorRow, cursorRow) }
    function selectionLeft(): int { return Math.min(anchorColumn, cursorColumn) }
    function selectionRight(): int { return Math.max(anchorColumn, cursorColumn) }
    function inSelection(r: int, c: int): bool {
        return anchorRow > -2 && r >= selectionTop() && r <= selectionBottom() && c >= selectionLeft() && c <= selectionRight()
    }
    /// The rows of the selection as shown (-1 the header), and from its top row down.
    function selectedRows(): list<int> {
        return BlockViews.tableRowsBetween(root.block.rowData.tableData, anchorRow, cursorRow)
    }
    function selectedRowCount(): int {
        return selectedRows().length
    }
    function selectedColumnCount(): int {
        return selectionRight() - selectionLeft() + 1
    }
    function selectedCellCount(): int {
        return selectedRowCount() * selectedColumnCount()
    }
    /// Select (anchor) .. (cursor) and give the selection the keys.
    function select(ar: int, ac: int, cr: int, cc: int): void {
        if (ac < 0 || ac >= columns || cc < 0 || cc >= columns || ar < -1 || cr < -1 || ar >= rows.length || cr >= rows.length)
            return
        root.anchorRow = ar
        root.anchorColumn = ac
        root.cursorRow = cr
        root.cursorColumn = cc
        root.block.controller.selectedRow = -1
        rangeKeys.forceActiveFocus()
        root.revealCell(cr, cc)
    }
    function clearSelection(): void {
        root.anchorRow = -2
        root.anchorColumn = -1
        root.cursorRow = -2
        root.cursorColumn = -1
    }
    /// select() once the events being handled are done (a paste or a new row first changes the table).
    property int laterAnchorRow: -2
    property int laterAnchorColumn: -1
    property int laterCursorRow: -2
    property int laterCursorColumn: -1
    /// How far down from the top row the selection later reaches (-1: to the given cursor row).
    property int laterRowSpan: -1
    function selectLater(ar: int, ac: int, cr: int, cc: int, rowSpan: int): void {
        root.laterAnchorRow = ar
        root.laterAnchorColumn = ac
        root.laterCursorRow = cr
        root.laterCursorColumn = cc
        root.laterRowSpan = rowSpan
        selectLaterTimer.restart()
    }
    Later {
        id: selectLaterTimer
        onTriggered: {
            if (root.block === null || root.block.editor === null || !root.block.editor.alive)
                return
            const toRow = root.laterRowSpan > 0
                          ? BlockViews.tableRowAfter(root.block.rowData.tableData, root.laterAnchorRow, root.laterRowSpan - 1)
                          : root.laterCursorRow
            root.select(root.laterAnchorRow, root.laterAnchorColumn, toRow, root.laterCursorColumn)
        }
    }
    /// The cursor cell's place is in view: the table's sideways scroll, and the page's.
    function revealCell(r: int, c: int): void {
        const left = root.columnX(c)
        const right = left + root.columnWidth(c)
        if (left < frame.contentX)
            frame.contentX = left
        else if (right > frame.contentX + frame.width)
            frame.contentX = Math.max(0, Math.min(right - frame.width, frame.contentWidth - frame.width))
        root.block.editor.ensureVisible(root, Qt.rect(0, root.rowY(r), root.width, root.rowHeight))
    }
    /// The cell reached from (r, c) by an arrow key: Up / Down the shown row above / below (the
    /// header and the last row stop), Left / Right the neighbouring column; the cell itself at an edge.
    function neighbour(r: int, c: int, key: int): point {
        let toR = r
        let toC = c
        if (key === Qt.Key_Up && r > -1) {
            toR = root.nextShownRow(r, -1)
        } else if (key === Qt.Key_Down) {
            const next = root.nextShownRow(r, 1)
            if (next < root.rows.length)
                toR = next
        } else if (key === Qt.Key_Left && c > 0) {
            toC = c - 1
        } else if (key === Qt.Key_Right && c + 1 < root.columns) {
            toC = c + 1
        }
        return Qt.point(toC, toR)
    }
    /// A key of the selection (rangeKeys).
    function rangeKey(event: KeyEvent): void {
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        const alt = (event.modifiers & (Qt.AltModifier | Qt.MetaModifier)) !== 0
        event.accepted = true
        if (alt) {
            event.accepted = false
            return
        }
        switch (event.key) {
        case Qt.Key_Up:
        case Qt.Key_Down:
        case Qt.Key_Left:
        case Qt.Key_Right: {
            if (ctrl) {
                event.accepted = false
                return
            }
            const to = root.neighbour(root.cursorRow, root.cursorColumn, event.key)
            if (shift)
                root.select(root.anchorRow, root.anchorColumn, to.y, to.x)
            else
                root.select(to.y, to.x, to.y, to.x)
            return
        }
        case Qt.Key_Home:
        case Qt.Key_End: {
            const toC = event.key === Qt.Key_Home ? 0 : root.columns - 1
            const toR = ctrl ? (event.key === Qt.Key_Home ? -1 : root.nextShownRow(root.rows.length, -1)) : root.cursorRow
            if (shift)
                root.select(root.anchorRow, root.anchorColumn, toR, toC)
            else
                root.select(toR, toC, toR, toC)
            return
        }
        case Qt.Key_Tab:
        case Qt.Key_Backtab: {
            const back = event.key === Qt.Key_Backtab || shift
            const r = root.cursorRow
            const c = root.cursorColumn
            if (!back && c + 1 < root.columns) {
                root.select(r, c + 1, r, c + 1)
            } else if (back && c > 0) {
                root.select(r, c - 1, r, c - 1)
            } else if (!back) {
                const next = root.nextShownRow(r, 1)
                if (next >= root.rows.length && !root.readOnly) {
                    // The last cell: a new row (as in a cell being edited).
                    root.block.controller.addTableRow(root.blockIndex)
                    root.selectLater(next, 0, next, 0, -1)
                } else if (next < root.rows.length) {
                    root.select(next, 0, next, 0)
                }
            } else if (r > -1) {
                const prev = root.nextShownRow(r, -1)
                root.select(prev, root.columns - 1, prev, root.columns - 1)
            }
            return
        }
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_F2:
            if (ctrl) {
                event.accepted = false
                return
            }
            root.startCell(root.cursorRow, root.cursorColumn)
            return
        case Qt.Key_Escape: {
            const blockRow = root.blockIndex
            const list = root.block.editor.listView
            root.clearSelection()
            root.block.controller.selectedRow = blockRow
            list.forceActiveFocus()
            return
        }
        case Qt.Key_Delete:
        case Qt.Key_Backspace:
            if (!shift && !ctrl)
                root.clearCells()
            else
                event.accepted = false
            return
        default:
            break
        }
        if (ctrl && !shift) {
            switch (event.key) {
            case Qt.Key_C: root.copyCells("tsv", false); return
            case Qt.Key_X: root.copyCells("tsv", true); return
            case Qt.Key_V: root.pasteCells(); return
            case Qt.Key_A: root.select(-1, 0, root.nextShownRow(root.rows.length, -1), root.columns - 1); return
            case Qt.Key_Z: root.stepSelection(false); return
            case Qt.Key_Y: root.stepSelection(true); return
            }
        } else if (ctrl && shift && event.key === Qt.Key_Z) {
            root.stepSelection(true)
            return
        }
        // A character typed over the cell: the cell is edited, with that text in place of its own.
        if (!ctrl && event.text !== "" && Js.charCodeAt(event.text, 0) >= 32 && Js.charCodeAt(event.text, 0) !== 127) {
            root.typedText = event.text
            root.startCell(root.cursorRow, root.cursorColumn)
            return
        }
        event.accepted = false
    }
    /// Undo / redo with the selection kept (the editor's own step would hand the keys to the list).
    function stepSelection(redo: bool): void {
        if (root.block.editor.readOnlyRefuses())
            return
        if (redo)
            root.block.controller.redo()
        else
            root.block.controller.undo()
    }
    /// Shift+click on cell (r, c) while a cell is edited: that cell is left, the cells up to (r, c) selected.
    function extendFromEdit(r: int, c: int): void {
        root.cellExtend(r, c)
    }
    /// The text typed over a selected cell, which the field that opens takes in place of the cell's.
    property string typedText: ""
    /// The controller's answers (a map read through a holder: compiled code needs the type).
    MapHolder { id: answer }
    /// Copy the selected cells ("tsv" or "markdown"); `cut` empties them.
    function copyCells(format: string, cut: bool): void {
        const controller = root.block.controller
        const count = root.selectedCellCount()
        const block = root.blockIndex
        const left = root.selectionLeft()
        const right = root.selectionRight()
        const picked = root.selectedRows()
        answer.map = controller.copyTableCells(block, picked, left, right, format, cut)
        if (answer.map.ok === true)
            root.block.editor.notice(cut ? qsTr("Cut %n cell(s)", "", count) : qsTr("Copied %n cell(s)", "", count), "success", null)
        else if (answer.map.reason === "noClipboard")
            root.block.editor.notice(qsTr("Could not copy: the clipboard is not available"), "error", null)
    }
    /// Empty the selected cells.
    function clearCells(): void {
        const controller = root.block.controller
        const block = root.blockIndex
        const left = root.selectionLeft()
        const right = root.selectionRight()
        const picked = root.selectedRows()
        controller.clearTableCells(block, picked, left, right)
    }
    /// Paste the clipboard's text with its top-left cell at the selection's, over the selection.
    function pasteCells(): void {
        const top = root.selectionTop()
        const left = root.selectionLeft()
        const rowCount = root.selectedRowCount()
        const columnCount = root.selectedColumnCount()
        root.pasteAt(top, left, rowCount, columnCount)
    }
    /// Paste the clipboard's text from cell (r, c) down and right over `rowCount` x `columnCount`
    /// cells; the cells it filled are then selected.
    function pasteAt(r: int, c: int, rowCount: int, columnCount: int): void {
        const editor = root.block.editor
        const controller = root.block.controller
        const block = root.blockIndex
        const down = BlockViews.tableRowsFrom(root.block.rowData.tableData, r)
        answer.map = controller.pasteClipboardCells(block, down, c, rowCount, columnCount)
        if (answer.map.readOnly === true)
            return
        if (answer.map.ok !== true) {
            editor.notice(qsTr("Nothing to paste"), "info", null)
            return
        }
        // (Typed copies: passed on after other calls.)
        const pastedRows = answer.map.rows + 0
        const lastColumn = c + answer.map.columns - 1
        const pastedCells = answer.map.cells + 0
        const cut = answer.map.columnsCut + 0
        editor.notice(qsTr("Pasted %n cell(s)", "", pastedCells), "success", null)
        if (cut > 0)
            editor.notice(qsTr("%n column(s) of the pasted text did not fit the table and were left out", "", cut), "info", null)
        root.selectLater(r, c, -1, lastColumn, pastedRows)
    }

    /// The keys of the cell selection: an item with no size that holds the keyboard while cells are
    /// selected (the editor's list would take Up / Down for blocks). Losing the keyboard ends the selection.
    Item {
        id: rangeKeys
        objectName: "tableRangeKeys"
        width: 0
        height: 0
        activeFocusOnTab: false
        Keys.onPressed: (event) => root.rangeKey(event)
        // The list's own keys (selecting blocks) and the app's shortcuts do not take these.
        Keys.onShortcutOverride: (event) => {
            if (!root.hasSelection)
                return
            const plain = (event.modifiers & (Qt.AltModifier | Qt.MetaModifier)) === 0
            event.accepted = plain && (event.modifiers & Qt.ControlModifier) !== 0
                             && (event.key === Qt.Key_C || event.key === Qt.Key_X || event.key === Qt.Key_V || event.key === Qt.Key_A
                                 || event.key === Qt.Key_Z || event.key === Qt.Key_Y)
        }
        onActiveFocusChanged: {
            if (!activeFocus)
                root.clearSelection()
        }
    }
    // The editor's toolbar follows the cell being edited (a table delegate destroyed while a cell
    // is edited hands it back too).
    onEditRowChanged: {
        const editor = root.block ? root.block.editor : null
        if (!editor)
            return
        // (Through the editor's setter: this file cannot store itself into a TableBlockView.)
        if (editRow > -2)
            editor.setEditingTable(root)
        else if (editor.editingTable === root)
            editor.setEditingTable(null)
    }
    Component.onDestruction: {
        const editor = root.block ? root.block.editor : null
        if (editor && editor.editingTable === root)
            editor.setEditingTable(null)
    }
    // ---- The touch toolbar's cell tools -----------------------------------------------------------
    // Each commits the word the keyboard is still composing first: Android keyboards compose
    // every word, and it is not in the field's text until committed (a cell left without that
    // lost the word).
    /// The next cell (Tab: the next column, else the first one of the next row — a new row after
    /// the last).
    function nextCell(): void {
        const input = root.cellInput
        if (!input)
            return
        EditorInput.commitPreedit(input)
        root.cellCommand("next")
    }
    /// Undo / redo: like Ctrl+Z in a cell (the cell's typing first), but the same cell goes on
    /// being edited, with the text the step left in it — the toolbar and the keyboard stay. (A
    /// step that takes the caret to a block leaves the editing there.)
    function undoInCell(): void {
        stepInCell(true)
    }
    function redoInCell(): void {
        stepInCell(false)
    }
    function stepInCell(back: bool): void {
        const input = root.cellInput
        if (!input)
            return
        EditorInput.commitPreedit(input)
        const r = editRow
        const c = editColumn
        const controller = root.block.controller
        const editor = root.block.editor
        const blockRow = root.blockIndex
        root.cellCommand("leave")
        if (back)
            controller.undo()
        else
            controller.redo()
        // The cell was closed before the step (its field would keep the text the step replaced);
        // the table keeps its delegate (undo changes the block in place): the cell opens again
        // with the text the step left in it.
        if (controller.editingRow < 0)
            editor.editTableCell(blockRow, r, c)
    }
    /// A cell left because the keyboard focus went elsewhere (the command palette, the side
    /// panel): the table stays the current block — selected, as Esc leaves it — for the palette's
    /// "Copy block reference" (BlockEditor.currentRow). Not when another block or cell took
    /// over meanwhile.
    property int stayRow: -1
    function stayCurrent(): void {
        const delegate = root.block
        const editor = delegate ? delegate.editor : null
        if (!editor)
            return
        root.stayRow = delegate.index
        stayTimer.restart()
    }
    Later {
        id: stayTimer
        onTriggered: {
            const delegate = root.block
            const editor = delegate ? delegate.editor : null
            if (!editor || !editor.alive)
                return
            const controller = editor.controller
            const blockRow = root.stayRow
            // Still this table (not a page loaded meanwhile), and nothing else current.
            if (controller && controller.editingRow < 0 && controller.selectedRow < 0 && editor.editingTable === null
                    && editor.listView.itemAtIndex(blockRow) === delegate)
                controller.selectedRow = blockRow
        }
    }
    /// End the cell's editing, keeping its text ("hide keyboard").
    function closeCell(): void {
        const input = root.cellInput
        if (!input)
            return
        EditorInput.commitPreedit(input)
        root.cellCommand("leave")
    }
    /// The length of the text of the cell being edited (-1: none is).
    function cellLength(): int {
        const input = root.cellInput
        if (input === null)
            return -1
        return input.length
    }
    /// The cell field asks to be kept above the toolbar and the keyboard once they show.
    property T.TextField ensureFor: null
    function ensureVisibleLater(input: T.TextField): void {
        root.ensureFor = input
        ensureTimer.restart()
    }
    Later {
        id: ensureTimer
        onTriggered: {
            if (root.ensureFor !== null && root.cellInput === root.ensureFor)
                root.block.editor.ensureEditVisible()
        }
    }

    Flickable {
        id: frame
        width: parent.width
        height: grid.height
        contentWidth: root.gridWidth
        contentHeight: grid.height
        clip: root.gridWidth > width
        interactive: root.gridWidth > width
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        // A table wider than the page: a scroll bar under it to drag with the mouse (the wheel
        // with Shift, a touchpad sideways or a finger move it too).
        T.ScrollBar.horizontal: BScrollBar {
            objectName: "tableScrollBar"
            visible: root.gridWidth > frame.width
            // Always drawn with a mouse: it says that the table goes on to the right.
            policy: Theme.touchUi ? T.ScrollBar.AsNeeded : T.ScrollBar.AlwaysOn
            // (Just under the grid's last row, inside its frame.)
            y: frame.height - height
        }

        Rectangle {
            id: grid
            width: root.gridWidth
            height: root.headerHeight + root.shownRows.length * root.rowHeight + root.filterHeight
            radius: Theme.radius
            color: Theme.bgElevated
            border.width: 1
            border.color: root.editRow > -2 ? Theme.alpha(Theme.focusRing, 0.5) : Theme.border

            // Header background.
            Rectangle {
                x: 1
                y: 1
                width: parent.width - 2
                height: root.headerHeight - 1
                topLeftRadius: Theme.radius - 1
                topRightRadius: Theme.radius - 1
                color: Theme.bgSunken
            }

            // The filter fields under the header (only while filtering).
            Item {
                visible: root.filtering
                y: root.headerHeight
                width: parent.width
                height: root.filterHeight
                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.borderSubtle
                }
                Repeater {
                    id: filterRepeater
                    model: root.filtering ? root.columns : 0
                    delegate: BTextField {
                        id: filterField
                        required property int index
                        objectName: "tableFilter" + index
                        x: root.columnX(filterField.index) + 2
                        y: Math.round((root.filterHeight - implicitHeight) / 2)
                        width: root.columnWidth(filterField.index) - 4
                        implicitWidth: 40 // (its width is the column's)
                        size: "small"
                        clearable: true
                        placeholderText: qsTr("Filter")
                        accessibleName: qsTr("Filter of column %1").arg(root.headerText(filterField.index))
                        text: root.filterOf(filterField.index)
                        onTextEdited: root.block.controller.setTableFilter(root.blockIndex, filterField.index, filterField.text)
                        onCleared: root.block.controller.setTableFilter(root.blockIndex, filterField.index, "")
                        // Escape in an empty field: the filters go, the table is selected.
                        onEscapePressed: {
                            const controller = root.block.controller
                            const list = root.block.editor.listView
                            const blockRow = root.blockIndex
                            controller.setTableFilterOpen(blockRow, false)
                            controller.selectedRow = blockRow
                            list.forceActiveFocus()
                        }
                        onAccepted: filterField.focus = false
                    }
                }
            }

            Repeater {
                model: root.shownRows.length + 1
                delegate: Item {
                    id: rowItem
                    required property int index
                    readonly property int r: index === 0 || index - 1 >= root.shownRows.length ? -1 : root.shownRows[index - 1]  // -1 = header
                    y: index === 0 ? 0 : root.headerHeight + (index - 1) * root.rowHeight + root.filterHeight
                    width: grid.width
                    height: index === 0 ? root.headerHeight : root.rowHeight

                    Rectangle {
                        visible: rowItem.index > 0
                        width: parent.width
                        height: 1
                        color: rowItem.index === 1 ? Theme.borderStrong : Theme.borderSubtle
                    }

                    Repeater {
                        model: root.columns
                        delegate: Item {
                            id: cell
                            required property int index
                            readonly property int c: index
                            readonly property bool headerCell: rowItem.r === -1
                            readonly property bool formula: cellRecord.map.formula === true
                            readonly property string error: cellRecord.map.error
                            readonly property string shown: cellRecord.map.display
                            /// Rendered cell (escaped rich text from the core renderer).
                            readonly property string shownHtml: cellRecord.map.html
                            readonly property string align: root.alignOf(c)
                            /// The cell's { text, display, html, plain, error, formula }.
                            MapHolder {
                                id: cellRecord
                                map: root.cellData(rowItem.r, cell.c)
                            }
                            readonly property bool editingCell: root.editRow === rowItem.r && root.editColumn === c
                            /// In the selected rectangle of cells; the cursor cell of it.
                            readonly property bool picked: root.inSelection(rowItem.r, c)
                            readonly property bool cursorCell: root.hasSelection && rowItem.r === root.cursorRow && c === root.cursorColumn
                            /// The link under the mouse or the pressing finger, underlined (BlockText.qml).
                            property string hoveredLink: ""
                            /// Where a right-click was, in the scene (the block menu's place).
                            property point scene
                            function trackLink(x: real, y: real): void {
                                const link = cell.editingCell ? "" : cellText.linkAt(x - cellText.x, y - cellText.y)
                                cell.hoveredLink = link !== "" && !Js.startsWith(link, "reminder:") && !root.block.editor.editsOnClick(link) ? link : ""
                            }
                            onEditingCellChanged: cell.hoveredLink = ""
                            x: root.columnX(cell.c)
                            width: root.columnWidth(cell.c)
                            height: rowItem.height
                            Accessible.role: cell.headerCell ? Accessible.ColumnHeader : Accessible.Cell
                            Accessible.name: cell.shown
                            Accessible.selected: cell.picked

                            Rectangle {
                                visible: cell.c > 0
                                width: 1
                                height: parent.height
                                color: Theme.borderSubtle
                            }
                            Rectangle {
                                visible: cell.formula && !cell.editingCell
                                anchors.fill: parent
                                anchors.margins: 1
                                color: Theme.alpha(Theme.info, Theme.dark ? 0.07 : 0.06)
                            }
                            // The selected cells (a rectangle), the cursor cell outlined.
                            Rectangle {
                                visible: cell.picked
                                anchors.fill: parent
                                anchors.margins: 1
                                color: Theme.alpha(Theme.accent, Theme.dark ? 0.26 : 0.16)
                                border.width: cell.cursorCell ? 2 : 0
                                border.color: Theme.focusRing
                            }
                            BRichText {
                                id: cellText
                                visible: !cell.editingCell
                                x: root.cellPadding
                                y: Math.round((parent.height - height) / 2)
                                width: parent.width - 2 * root.cellPadding
                                horizontalAlignment: cell.align === "right" || (cell.formula && cell.align === "") ? Text.AlignRight
                                                   : cell.align === "center" ? Text.AlignHCenter : Text.AlignLeft
                                // One line (a header: its `<br>` lines): a longer text ends with "…"
                                // (its tooltip shows it all).
                                html: cell.shownHtml
                                hoveredLink: cell.hoveredLink
                                maximumLineCount: cell.headerCell ? root.headerLines : 1
                                lineHeight: Theme.lineHeightPx(Theme.textBase, 1.4)
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textBase
                                font.weight: cell.headerCell ? Theme.weightSemiBold : cell.formula ? Theme.weightMedium : Theme.weightNormal
                                color: cell.error !== "" ? Theme.dangerText : Theme.fg
                            }
                            // A column with a filter: its funnel in the header.
                            BIcon {
                                visible: cell.headerCell && Js.trim(root.filterOf(cell.c)) !== ""
                                x: parent.width - width - 4
                                y: Math.round((parent.height - height) / 2)
                                name: visible ? "filter" : ""
                                size: 12
                                color: Theme.accent
                            }
                            // Screen readers: the cell's links as link items (the cell stays the
                            // accessible Cell; its text item is ignored).
                            BLinkTargets {
                                x: cellText.x
                                y: cellText.y
                                textItem: cellText.textItem
                                html: cell.shownHtml
                                shown: !cell.editingCell
                                maxHeight: cellText.height
                                onActivated: (href, x, y) => {
                                    if (root.block.editor.openCellTokenDialog(root.blockIndex, rowItem.r, cell.c, href))
                                        return // a date chip: the date dialog, as a click
                                    if (Js.startsWith(href, "reminder:") || root.block.editor.editsOnClick(href))
                                        root.cellMenu(rowItem.r, cell.c, cellText, x, y)
                                    else
                                        root.block.editor.activateLink(root.blockIndex, href)
                                }
                            }
                            // Created when shown: a tooltip (a popup with its own items) in
                            // every cell made a table slow to create while scrolling.
                            Loader {
                                active: cellHover.hovered && (cell.error !== "" || cellText.truncated)
                                sourceComponent: BToolTip {
                                    parent: cell
                                    visible: true
                                    text: cell.error !== "" ? cell.error : cell.shown
                                }
                            }
                            HoverHandler {
                                id: cellHover
                                enabled: !Theme.touchUi
                                cursorShape: cellText.linkAt(point.position.x - cellText.x, point.position.y - cellText.y) !== ""
                                             ? Qt.PointingHandCursor : root.readOnly ? Qt.ArrowCursor : Qt.IBeamCursor
                                onPointChanged: {
                                    cell.trackLink(point.position.x, point.position.y)
                                    // The link under the resting pointer: its preview (LinkPreviews).
                                    LinkPreviews.hover(cellText, cell.hoveredLink, root.block.controller ? root.block.controller.path : "",
                                                       root.block.controller ? root.block.controller.host : null,
                                                       point.position.x - cellText.x, point.position.y - cellText.y)
                                }
                                onHoveredChanged: {
                                    if (!hovered) {
                                        LinkPreviews.leave(cellText)
                                        if (!cellTap.pressed)
                                            cell.hoveredLink = ""
                                    }
                                }
                            }
                            // A finger held on a cell selects the table (touch UIs) — on a token or a
                            // link of its text, outside the selection mode, opens its menu.
                            TapHandler {
                                id: cellHold
                                enabled: !cell.editingCell && Theme.touchUi
                                acceptedDevices: PointerDevice.TouchScreen
                                onLongPressed: {
                                    const p = cellHold.point.position
                                    if (root.block.editor.selectionMode || !root.cellMenu(rowItem.r, cell.c, cellText, p.x - cellText.x, p.y - cellText.y))
                                        root.block.editor.longPressText(root.blockIndex)
                                }
                            }
                            // A right-click on a token or a link of the cell: its menu; elsewhere the
                            // block menu (as the list's handler opens it).
                            TapHandler {
                                enabled: !cell.editingCell
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                                acceptedButtons: Qt.RightButton
                                gesturePolicy: TapHandler.WithinBounds
                                onTapped: (eventPoint) => {
                                    const x = eventPoint.position.x
                                    const y = eventPoint.position.y
                                    cell.scene = eventPoint.scenePosition
                                    if (!root.cellMenu(rowItem.r, cell.c, cellText, x - cellText.x, y - cellText.y))
                                        root.block.editor.openCellBlockMenuAt(root.blockIndex, cell.scene,
                                                                              rowItem.r, cell.c, root.rows.length, root.columns, cell.align)
                                }
                            }
                            TapHandler {
                                id: cellTap
                                enabled: !cell.editingCell
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onPressedChanged: {
                                    if (pressed)
                                        cell.trackLink(cellTap.point.position.x, cellTap.point.position.y)
                                    else if (!cellHover.hovered)
                                        cell.hoveredLink = ""
                                }
                                onTapped: (eventPoint) => {
                                    const x = eventPoint.position.x
                                    const y = eventPoint.position.y
                                    // Shift+click with a cell edited or selected: the cells up to this one.
                                    if ((EditorInput.keyboardModifiers() & Qt.ShiftModifier) !== 0 && !root.block.editor.selectionMode
                                            && (root.editRow > -2 || root.hasSelection)) {
                                        if (root.editRow > -2)
                                            root.extendFromEdit(rowItem.r, cell.c)
                                        else
                                            root.select(root.anchorRow, root.anchorColumn, rowItem.r, cell.c)
                                        return
                                    }
                                    if (root.block.editor.blockTapped(root.blockIndex))
                                        return // Shift+click, the touch selection mode
                                    // Reminder, 🔁 and priority chips name ranges of the cell's text:
                                    // a click edits the cell (their menu is the right button's); a
                                    // date chip's opens the date dialog on it.
                                    const link = root.linkIn(cellText, x - cellText.x, y - cellText.y)
                                    if (root.block.editor.openCellTokenDialog(root.blockIndex, rowItem.r, cell.c, link))
                                        return
                                    if (link !== "" && !Js.startsWith(link, "reminder:") && !root.block.editor.editsOnClick(link))
                                        root.block.editor.activateLink(root.blockIndex, link)
                                    else
                                        root.startCell(rowItem.r, cell.c)
                                }
                            }

                            Loader {
                                anchors.fill: parent
                                active: cell.editingCell
                                sourceComponent: T.TextField {
                                    id: input
                                    leftPadding: root.cellPadding
                                    rightPadding: root.cellPadding
                                    verticalAlignment: TextInput.AlignVCenter
                                    font.family: cell.formula ? Theme.fontMono : Theme.fontUi
                                    font.pixelSize: Theme.textBase
                                    font.weight: cell.headerCell ? Theme.weightSemiBold : Theme.weightNormal
                                    color: Theme.fg
                                    selectionColor: Theme.selection
                                    selectedTextColor: Theme.selectionFg
                                    selectByMouse: true
                                    Accessible.name: qsTr("Cell %1").arg(cell.shown)
                                    background: Rectangle {
                                        color: Theme.bgElevated2
                                        border.width: 2
                                        border.color: Theme.focusRing
                                        radius: 2
                                    }
                                    property bool done: false
                                    property bool ready: false
                                    /// Commit the text and end the cell's editing; then cell (nextRow,
                                    /// nextColumn) is edited (root.noCell: none; past the last row: a new one).
                                    function leave(nextRow: int, nextColumn: int): void {
                                        if (input.done)
                                            return
                                        input.done = true
                                        const r = rowItem.r
                                        const c = cell.c
                                        root.commit(r, c, input.text)
                                        if (nextRow === root.noCell) {
                                            root.finish()
                                        } else {
                                            if (nextRow >= root.rows.length)
                                                root.block.controller.addTableRow(root.blockIndex)
                                            root.startCellLater(nextRow, nextColumn)
                                        }
                                    }
                                    Component.onCompleted: {
                                        // A character typed over a selected cell replaces its text.
                                        text = root.typedText !== "" ? root.typedText : root.rawText(rowItem.r, cell.c)
                                        const caret = root.caretAtStart
                                        cursorPosition = caret < 0 ? length : Math.min(caret, length)
                                        ready = true
                                        if (root.typedText !== "")
                                            root.block.controller.setTableCell(root.blockIndex, rowItem.r, cell.c, text, true)
                                        root.cellInput = input
                                        forceActiveFocus()
                                        // Above the toolbar and the keyboard (also once they show).
                                        root.ensureVisibleLater(input)
                                    }
                                    Component.onDestruction: if (root.cellInput === input) root.cellInput = null
                                    Connections {
                                        target: root
                                        function onCellExtend(toRow: int, toColumn: int) {
                                            if (root.cellInput === input)
                                                input.extendTo(toRow, toColumn)
                                        }
                                        function onCellCommand(command: string) {
                                            if (root.cellInput !== input)
                                                return
                                            if (command === "next")
                                                input.next()
                                            else if (command === "leave")
                                                input.leave(root.noCell, 0)

                                        }
                                    }
                                    /// Commit the text, do `edit` to the table (its rows are made anew:
                                    /// "moveRow" a to b, "moveColumn" a to b, "insertRow" at a), then go on
                                    /// editing cell (r, c).
                                    function reshape(edit: string, a: int, b: int, r: int, c: int): void {
                                        if (input.done)
                                            return
                                        EditorInput.commitPreedit(input)
                                        input.done = true
                                        root.commit(rowItem.r, cell.c, input.text)
                                        root.finish()
                                        const controller = root.block.controller
                                        const block = root.blockIndex
                                        if (edit === "moveRow")
                                            controller.moveTableRow(block, a, b)
                                        else if (edit === "moveColumn")
                                            controller.moveTableColumn(block, a, b)
                                        else if (edit === "insertRow")
                                            controller.insertTableRow(block, a)
                                        root.startCellLater(r, c)
                                    }
                                    /// Go to cell (toR, toC), the caret at `caret` (-1: its end).
                                    function go(toR: int, toC: int, caret: int): void {
                                        if (input.done)
                                            return
                                        EditorInput.commitPreedit(input)
                                        root.caretAtStart = caret
                                        input.leave(toR, toC)
                                    }
                                    /// Leave the cell, selecting the rectangle from it to (toR, toC) (Shift+arrows,
                                    /// Shift+click): the keys go to the selection.
                                    function extendTo(toR: int, toC: int): void {
                                        if (input.done)
                                            return
                                        EditorInput.commitPreedit(input)
                                        input.done = true
                                        root.commit(rowItem.r, cell.c, input.text)
                                        const r = rowItem.r
                                        const c = cell.c
                                        root.finish()
                                        root.select(r, c, toR, toC)
                                    }
                                    /// The arrow keys: Up / Down always go to the cell above / below (the header
                                    /// and the last row stop them), Left / Right when the caret is at the text's
                                    /// edge — with Shift the cell selection grows that way instead. False: the
                                    /// field's own key (moving the caret, selecting text).
                                    function arrow(key: int, shift: bool): bool {
                                        if (input.preeditText !== "")
                                            return false
                                        const horizontal = key === Qt.Key_Left || key === Qt.Key_Right
                                        if (horizontal) {
                                            const atEdge = key === Qt.Key_Left ? input.cursorPosition === 0 : input.cursorPosition === input.length
                                            if (!atEdge || (!shift && input.selectedText !== ""))
                                                return false
                                        }
                                        const to = root.neighbour(rowItem.r, cell.c, key)
                                        if (shift) {
                                            input.extendTo(to.y, to.x)
                                        } else if (to.y !== rowItem.r || to.x !== cell.c) {
                                            input.go(to.y, to.x, key === Qt.Key_Left ? -1 : key === Qt.Key_Right ? 0 : input.cursorPosition)
                                        }
                                        return true
                                    }
                                    /// Ctrl+V of text that is several cells: pasted from this cell on. False: the
                                    /// clipboard holds one text, which the field pastes itself.
                                    function pasteCells(): bool {
                                        if (root.block.controller.clipboardCellCount() < 2)
                                            return false
                                        EditorInput.commitPreedit(input)
                                        input.done = true
                                        root.commit(rowItem.r, cell.c, input.text)
                                        const r = rowItem.r
                                        const c = cell.c
                                        root.finish()
                                        root.pasteAt(r, c, 1, 1)
                                        return true
                                    }
                                    function next(): void {
                                        const r = rowItem.r
                                        const c = cell.c
                                        if (c + 1 < root.columns) input.leave(r, c + 1)
                                        else input.leave(root.nextShownRow(r, 1), 0)
                                    }
                                    function undoTable(): void {
                                        input.done = true
                                        root.finish()
                                        const controller = root.block.controller
                                        const list = root.block.editor.listView
                                        const blockRow = root.blockIndex
                                        controller.undo()
                                        if (controller.editingRow < 0) {
                                            controller.selectedRow = Math.min(blockRow, controller.blocks.count - 1)
                                            list.forceActiveFocus()
                                        }
                                    }
                                    onTextChanged: {
                                        if (!input.ready || input.done)
                                            return
                                        root.block.controller.setTableCell(root.blockIndex, rowItem.r, cell.c, input.text, true)
                                    }
                                    onActiveFocusChanged: {
                                        if (input.activeFocus || input.done)
                                            return
                                        input.leave(root.noCell, 0)
                                        root.stayCurrent()
                                    }
                                    // Alt+←/→ are the app's Back and Forward elsewhere.
                                    Keys.onShortcutOverride: (event) => {
                                        event.accepted = (event.modifiers & Qt.AltModifier) !== 0
                                                         && (event.key === Qt.Key_Left || event.key === Qt.Key_Right
                                                             || event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                                    }
                                    Keys.onPressed: (event) => {
                                        const r = rowItem.r
                                        const c = cell.c
                                        if ((event.modifiers & Qt.AltModifier) && !(event.modifiers & Qt.ControlModifier)
                                                && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                                            const to = event.key === Qt.Key_Up ? r - 1 : r + 1
                                            if (r >= 0 && to >= 0 && to < root.rows.length)
                                                input.reshape("moveRow", r, to, to, c)
                                            event.accepted = true
                                        } else if ((event.modifiers & Qt.AltModifier) && !(event.modifiers & Qt.ControlModifier)
                                                   && (event.key === Qt.Key_Left || event.key === Qt.Key_Right)) {
                                            const to = event.key === Qt.Key_Left ? c - 1 : c + 1
                                            if (to >= 0 && to < root.columns)
                                                input.reshape("moveColumn", c, to, r, to)
                                            event.accepted = true
                                        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                                   && (event.modifiers & Qt.ControlModifier)) {
                                            input.reshape("insertRow", r + 1, 0, r + 1, c)
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_Escape) {
                                            input.done = true
                                            root.finish()
                                            // Cancel: the cell's typing is one undo step, undone
                                            // with the cell closed first (finish()): its field
                                            // would keep the text the undo takes back.
                                            const controller = root.block.controller
                                            const list = root.block.editor.listView
                                            const blockRow = root.blockIndex
                                            controller.cancelTableCell(blockRow, r, c)
                                            controller.selectedRow = blockRow
                                            list.forceActiveFocus()
                                            event.accepted = true
                                        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                                   && (event.modifiers & Qt.AltModifier) && !(event.modifiers & Qt.ControlModifier)) {
                                            // Alt+Enter: follow the link (path part) at the caret;
                                            // with Shift in the side panel.
                                            const blockRow = root.blockIndex
                                            const url = root.linkInText(input.text, input.cursorPosition)
                                            root.block.editor.followLink(blockRow, url,
                                                                         (event.modifiers & Qt.ShiftModifier) !== 0 && !Theme.narrow)
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                            input.leave(root.nextShownRow(r, 1), c)
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_Tab) {
                                            input.next()
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_Backtab) {
                                            if (c > 0) input.leave(r, c - 1)
                                            else if (r > -1) input.leave(root.nextShownRow(r, -1), root.columns - 1)
                                            event.accepted = true
                                        } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Z) {
                                            input.undoTable()
                                            event.accepted = true
                                        } else if (((event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier)) === Qt.ControlModifier
                                                    && event.key === Qt.Key_V)
                                                   || ((event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier)) === Qt.ShiftModifier
                                                       && event.key === Qt.Key_Insert)) {
                                            event.accepted = input.pasteCells()
                                        } else if ((event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) === 0
                                                   && (event.key === Qt.Key_Up || event.key === Qt.Key_Down
                                                       || event.key === Qt.Key_Left || event.key === Qt.Key_Right)) {
                                            event.accepted = input.arrow(event.key, (event.modifiers & Qt.ShiftModifier) !== 0)
                                        }
                                    }
                                }
                            }
                            // A right-click on the cell being edited: the field would take the
                            // press from a handler, an area on top of it takes only that button.
                            MouseArea {
                                id: cellArea
                                anchors.fill: parent
                                enabled: cell.editingCell
                                acceptedButtons: Qt.RightButton
                                onClicked: (mouse) => {
                                    cell.scene = cellArea.mapToItem(null, mouse.x, mouse.y)
                                    root.closeCell()
                                    root.stayCurrent()
                                    root.block.editor.openCellBlockMenuAt(root.blockIndex, cell.scene, rowItem.r, cell.c,
                                                                          root.rows.length, root.columns, cell.align)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Loader {
        id: buttons
        active: root.buttonsShown
        y: frame.height + Theme.space1
        // Placed by bindings (a Row would take its size only in its next polish).
        sourceComponent: Item {
            id: buttonRow
            readonly property real lineHeight: Math.max(addRow.implicitHeight, addColumn.implicitHeight)
            readonly property real addedWidth: root.readOnly ? 0 : addColumn.x + addColumn.implicitWidth
            /// The filters' line under the add buttons when both do not fit side by side (a phone).
            readonly property bool wrapped: root.filtering
                                            && addedWidth + Theme.space3 + filterStatus.implicitWidth + Theme.space1
                                               + clearFilters.implicitWidth > root.width
            implicitWidth: !root.filtering ? addedWidth : clearFilters.x + clearFilters.implicitWidth
            implicitHeight: wrapped ? 2 * lineHeight : lineHeight
            // While filtering: how many rows show, and the way back to all of them.
            Text {
                id: filterStatus
                objectName: "tableFilterStatus"
                visible: root.filtering
                x: buttonRow.wrapped ? Theme.space2 : buttonRow.addedWidth + Theme.space3
                y: buttonRow.wrapped ? buttonRow.lineHeight : 0
                height: buttonRow.lineHeight
                verticalAlignment: Text.AlignVCenter
                text: qsTr("%1 of %n row(s) shown", "", root.rows.length).arg(root.shownCount)
                textFormat: Text.PlainText
                font.family: Theme.fontUi
                font.pixelSize: Theme.textXs
                color: Theme.fgMuted
            }
            BButton {
                id: clearFilters
                objectName: "tableClearFilters"
                visible: root.filtering
                x: filterStatus.x + filterStatus.implicitWidth + Theme.space1
                y: filterStatus.y
                size: "small"
                variant: "ghost"
                iconName: "x"
                text: qsTr("Clear filters")
                focusPolicy: Qt.NoFocus
                onClicked: root.block.controller.setTableFilterOpen(root.blockIndex, false)
            }
            BButton {
                id: addRow
                visible: !root.readOnly
                size: "small"
                variant: "ghost"
                iconName: "plus"
                text: qsTr("Row")
                focusPolicy: Qt.NoFocus
                onClicked: root.block.controller.addTableRow(root.blockIndex)
            }
            BButton {
                id: addColumn
                visible: !root.readOnly
                x: addRow.implicitWidth + Theme.space1
                size: "small"
                variant: "ghost"
                iconName: "plus"
                text: qsTr("Column")
                focusPolicy: Qt.NoFocus
                onClicked: root.block.controller.addTableColumn(root.blockIndex)
            }
        }
    }
}
