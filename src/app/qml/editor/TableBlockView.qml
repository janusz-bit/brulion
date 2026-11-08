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
        editRow = r
        editColumn = c
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
        return rowHeight + filterHeight + (shown >= 0 ? shown : shownRows.length) * rowHeight
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

        Rectangle {
            id: grid
            width: root.gridWidth
            height: (root.shownRows.length + 1) * root.rowHeight + root.filterHeight
            radius: Theme.radius
            color: Theme.bgElevated
            border.width: 1
            border.color: root.editRow > -2 ? Theme.alpha(Theme.focusRing, 0.5) : Theme.border

            // Header background.
            Rectangle {
                x: 1
                y: 1
                width: parent.width - 2
                height: root.rowHeight - 1
                topLeftRadius: Theme.radius - 1
                topRightRadius: Theme.radius - 1
                color: Theme.bgSunken
            }

            // The filter fields under the header (only while filtering).
            Item {
                visible: root.filtering
                y: root.rowHeight
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
                    y: index === 0 ? 0 : index * root.rowHeight + root.filterHeight
                    width: grid.width
                    height: root.rowHeight

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
                            /// The link under the mouse or the pressing finger, underlined (BlockText.qml).
                            property string hoveredLink: ""
                            /// Where a right-click was, in the scene (the block menu's place).
                            property point scene
                            function trackLink(x: real, y: real): void {
                                const link = cell.editingCell ? "" : cellText.linkAt(x - cellText.x, y - cellText.y)
                                const h = link !== "" && !Js.startsWith(link, "reminder:") && !root.block.editor.editsOnClick(link) ? link : ""
                                if (h !== cell.hoveredLink)
                                    cell.hoveredLink = h
                            }
                            onEditingCellChanged: cell.hoveredLink = ""
                            x: root.columnX(cell.c)
                            width: root.columnWidth(cell.c)
                            height: root.rowHeight
                            Accessible.role: cell.headerCell ? Accessible.ColumnHeader : Accessible.Cell
                            Accessible.name: cell.shown

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
                            BRichText {
                                id: cellText
                                visible: !cell.editingCell
                                x: root.cellPadding
                                y: Math.round((parent.height - height) / 2)
                                width: parent.width - 2 * root.cellPadding
                                horizontalAlignment: cell.align === "right" || (cell.formula && cell.align === "") ? Text.AlignRight
                                                   : cell.align === "center" ? Text.AlignHCenter : Text.AlignLeft
                                // One line: a longer text ends with "…" (its tooltip shows it all).
                                html: cell.shownHtml
                                hoveredLink: cell.hoveredLink
                                maximumLineCount: 1
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
                                    if (root.block.editor.blockTapped(root.blockIndex))
                                        return // Shift+click, the touch selection mode
                                    // Reminder, date, 🔁 and priority chips name ranges of the cell's
                                    // text: a click edits the cell (their menu is the right button's).
                                    const link = root.linkIn(cellText, x - cellText.x, y - cellText.y)
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
                                        } else if (nextRow >= root.rows.length) {
                                            root.block.controller.addTableRow(root.blockIndex)
                                            root.startCellLater(nextRow, nextColumn)
                                        } else {
                                            root.startCellLater(nextRow, nextColumn)
                                        }
                                    }
                                    Component.onCompleted: {
                                        text = root.rawText(rowItem.r, cell.c)
                                        cursorPosition = length
                                        ready = true
                                        root.cellInput = input
                                        forceActiveFocus()
                                        // Above the toolbar and the keyboard (also once they show).
                                        root.ensureVisibleLater(input)
                                    }
                                    Component.onDestruction: if (root.cellInput === input) root.cellInput = null
                                    Connections {
                                        target: root
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
                                        const view = root
                                        view.commit(rowItem.r, cell.c, input.text)
                                        view.finish()
                                        const controller = view.block.controller
                                        const block = view.blockIndex
                                        if (edit === "moveRow")
                                            controller.moveTableRow(block, a, b)
                                        else if (edit === "moveColumn")
                                            controller.moveTableColumn(block, a, b)
                                        else if (edit === "insertRow")
                                            controller.insertTableRow(block, a)
                                        view.startCellLater(r, c)
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
                                    const view = root
                                    cell.scene = cellArea.mapToItem(null, mouse.x, mouse.y)
                                    view.closeCell()
                                    view.stayCurrent()
                                    view.block.editor.openCellBlockMenuAt(view.blockIndex, cell.scene, rowItem.r, cell.c,
                                                                          view.rows.length, view.columns, cell.align)
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
