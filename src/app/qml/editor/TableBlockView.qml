pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// TableBlockView — a GFM table as a grid (docs/SYNTAX.md §9 "Table formulas"). Cells show their
// Markdown rendered like block text (bold, links, tags, date chips — the core InlineRenderer, so
// note text is never interpreted as HTML); formula cells (`=sum(above)`, `=[Cena]*[Ilość]`) show
// the result (errors as #ERR with the message as tooltip, cycles as #CYCLE). Links in cells work
// like links in blocks (a click elsewhere edits the cell); a right-click or long press on a date,
// reminder, 🔁 or priority chip of a cell opens its menu (DateChipMenu: the cell's text changes, one
// undo step), on a link the link's menu. Clicking a cell edits its raw text (the formula);
// Enter / Tab / Shift+Tab move, Esc cancels the cell's edit. Typing reaches the document at once
// (PageController.setTableCell with `typing`: one undo step per cell), so a cell left open when
// the app is suspended or closed is saved like any text. Rows and columns are added with
// the buttons below the grid: always shown on touch screens; with a mouse while the table is
// selected or a cell is edited (Enter in the last row and Tab in the last cell add a row too).
// A right-click on a cell, also the one being edited, opens the block menu with the cell's Wiersz
// and Kolumna submenus (BlockMenu: insert, move, delete, align; on touch UIs through the toolbar's
// "Więcej"); in a cell Ctrl+Enter inserts a row below and Alt+arrows move its row or column, the
// cell moving with it. The formulas' references follow their cells (calc/table.h).
// While a cell is edited the editor knows (BlockEditor.editingTable): on touch UIs its toolbar,
// docked on the keyboard, offers undo, redo, the next cell, "Więcej" (the table's block menu,
// BlockEditor.openTableBlockMenu) and "hide keyboard" (nextCell(), undoInCell(), redoInCell(),
// closeCell(): the cell stays edited after undo and redo), and the cell is kept above it. The
// table is the current block meanwhile (Ctrl+Shift+C, the palette's link commands); a cell left
// for the palette or the side panel leaves the table selected (stayCurrent()).
// Sorting and filters (docs/SYNTAX.md §9): the Kolumna submenu sorts the rows by the cell's column
// (PageController.sortTable: the file's rows rewritten, one undo step) and "Filtruj wiersze" shows a
// row of filter fields under the header (one per column: "> 100", "!tekst", "zolw"); rows that do
// not match are hidden — nothing is written, the filters last while the page is open
// (BlockListModel.setTableFilter) — and the line under the grid says how many rows show, with
// "Wyczyść filtry". A row being edited stays shown until it is left. The block menu also copies the
// table as CSV or saves it to a file (EditorDialogs.saveTableCsv).
Item {
    id: root

    property BlockDelegate block
    readonly property var table: block.model.tableData || ({})
    readonly property var header: table.header || []
    readonly property var headerHtml: table.headerHtml || []
    readonly property var aligns: table.aligns || []
    readonly property var rows: table.rows || []
    readonly property int columns: header.length
    /// The filter row is shown (the block menu's "Filtruj wiersze"); its texts by column.
    readonly property bool filtering: table.filtering || false
    readonly property var filters: table.filters || []
    /// The rows shown, in order: those passing the filters, and the row being edited.
    readonly property var shownRows: {
        const flags = table.shown || []
        const out = []
        for (let r = 0; r < rows.length; ++r) {
            if (flags[r] !== false || r === editRow)
                out.push(r)
        }
        return out
    }
    readonly property real filterHeight: filtering ? (Theme.touchUi ? 48 : 36) : 0
    readonly property int cellPadding: Theme.space3
    readonly property int rowHeight: Theme.touchUi ? 40 : 32
    readonly property real firstLineCenter: rowHeight / 2
    property int editRow: -2   // -2 none, -1 header
    property int editColumn: -1
    /// The text field of the cell being edited (null when none): leave(), next(), undoTable().
    property var cellInput: null
    readonly property bool active: editRow > -2 || block.model.isSelected
    /// The row of buttons below the grid. Not on hover: a table that grew under the resting mouse
    /// pointer moved everything below it while the page scrolled past, and room kept for hidden
    /// buttons left a gap under every table (docs/UI.md "Scrolling performance"). Selecting the
    /// table or editing a cell — never scrolling — adds it.
    readonly property bool buttonsShown: active || Theme.touchUi || filtering

    /// Natural column widths (clamped), stretched to fill the available width.
    readonly property var columnWidths: {
        const out = []
        let total = 0
        for (let c = 0; c < columns; ++c) {
            let w = headerMetrics.advanceWidth(header[c] || "")
            for (let r = 0; r < rows.length; ++r) {
                const cell = rows[r][c]
                if (cell)
                    w = Math.max(w, cellMetrics.advanceWidth(cell.plain !== undefined ? cell.plain : (cell.display || "")))
            }
            w = Math.min(280, Math.max(56, Math.ceil(w) + 2 * cellPadding))
            out.push(w)
            total += w
        }
        if (total > 0 && total < width) {
            const k = width / total
            for (let c = 0; c < out.length; ++c)
                out[c] = Math.floor(out[c] * k)
        }
        return out
    }
    readonly property real gridWidth: columnWidths.reduce((a, b) => a + b, 0)

    // The buttons' own height (the Loader's, from its item's implicit size at creation), not a
    // Row's, which a positioner only sets in its next polish.
    implicitHeight: frame.height + (buttons.active ? buttons.implicitHeight + Theme.space1 : 0)
    height: implicitHeight

    Accessible.role: Accessible.Table
    Accessible.name: qsTr("Table")

    FontMetrics { id: headerMetrics; font.family: Theme.fontUi; font.pixelSize: Theme.textBase; font.weight: Theme.weightSemiBold }
    FontMetrics { id: cellMetrics; font.family: Theme.fontUi; font.pixelSize: Theme.textBase }

    /// What the block menu needs of cell (r, c): BlockMenu.openFor().
    function cellInfo(r, c) {
        return { row: r, column: c, rows: rows.length, columns: columns, align: aligns[c] || "" }
    }
    function focusFirstCell() {
        startCell(rows.length > 0 ? 0 : -1, 0)
    }
    /// The menu of the token or link at (x, y) of cell (r, c)'s text `text` (r -1 = the header): a
    /// date, reminder, 🔁 or priority chip's (the cell's text changes), a link's. False: none there.
    function cellMenu(r, c, text, x, y) {
        const editor = root.block.editor
        const link = text.linkAt(x, y)
        if (link === "")
            return false
        if (editor.openCellTokenMenu(root.block.index, r, c, link, text, x, y))
            return true
        const index = root.block.index
        return editor.openLinkMenu(link, "", (newPane) => {
            if (newPane)
                editor.controller.activateLink(index, link, true)
            else
                editor.activateLink(index, link)
        }, text, x, y)
    }
    function startCell(r, c) {
        if (c < 0 || c >= columns || r < -1 || r >= rows.length)
            return
        root.block.controller.selectedRow = -1
        root.block.controller.stopEditing()
        editRow = r
        editColumn = c
    }
    /// The y of row `r` in the grid (-1 the header); a hidden row: where it would be.
    function rowY(r) {
        if (r < 0)
            return 0
        const shown = shownRows.indexOf(r)
        return rowHeight + filterHeight + (shown >= 0 ? shown : shownRows.length) * rowHeight
    }
    /// The shown row after (`step` 1) or before (-1) row `r` (-1 the header): rows.length after the
    /// last, -1 before the first.
    function nextShownRow(r, step) {
        for (let n = r + step; n >= 0 && n < rows.length; n += step) {
            if ((table.shown || [])[n] !== false)
                return n
        }
        return step > 0 ? rows.length : -1
    }
    /// Show the filter row with the keys in column `c`'s field.
    function openFilter(c) {
        root.block.controller.setTableFilterOpen(root.block.index, true)
        Qt.callLater(() => {
            const field = filterRepeater.itemAt(Math.max(0, c))
            if (field)
                field.forceActiveFocus()
        })
    }
    function rawText(r, c) {
        if (r === -1)
            return header[c] || ""
        const cell = rows[r] ? rows[r][c] : null
        return cell ? cell.text : ""
    }
    function commit(r, c, value) {
        if (value !== rawText(r, c))
            root.block.controller.setTableCell(root.block.index, r, c, value)
    }
    function finish() {
        editRow = -2
        editColumn = -1
    }
    // The editor's toolbar follows the cell being edited (a table delegate destroyed while a cell
    // is edited hands it back too).
    onEditRowChanged: {
        const editor = root.block ? root.block.editor : null
        if (!editor)
            return
        if (editRow > -2)
            editor.editingTable = root
        else if (editor.editingTable === root)
            editor.editingTable = null
    }
    Component.onDestruction: {
        const editor = root.block ? root.block.editor : null
        if (editor && editor.editingTable === root)
            editor.editingTable = null
    }
    // ---- The touch toolbar's cell tools -----------------------------------------------------------
    // Each commits the word the keyboard is still composing first: Android keyboards compose
    // every word, and it is not in the field's text until committed (a cell left without that
    // lost the word).
    /// The next cell (Tab: the next column, else the first one of the next row — a new row after
    /// the last).
    function nextCell() {
        const input = root.cellInput
        if (!input)
            return
        EditorInput.commitPreedit(input)
        input.next()
    }
    /// Undo / redo: like Ctrl+Z in a cell (the cell's typing first), but the same cell goes on
    /// being edited, with the text the step left in it — the toolbar and the keyboard stay. (A
    /// step that takes the caret to a block leaves the editing there.)
    function undoInCell() {
        stepInCell(true)
    }
    function redoInCell() {
        stepInCell(false)
    }
    function stepInCell(back) {
        const input = root.cellInput
        if (!input)
            return
        EditorInput.commitPreedit(input)
        const r = editRow
        const c = editColumn
        const controller = root.block.controller
        const editor = root.block.editor
        const blockRow = root.block.index
        input.leave()
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
    /// "Kopiuj odwołanie do bloku" (BlockEditor.currentRow). Not when another block or cell took
    /// over meanwhile.
    function stayCurrent() {
        const delegate = root.block
        const editor = delegate ? delegate.editor : null
        if (!editor)
            return
        const blockRow = delegate.index
        editor.later(() => {
            const controller = editor.controller
            // Still this table (not a page loaded meanwhile), and nothing else current.
            if (controller && controller.editingRow < 0 && controller.selectedRow < 0 && editor.editingTable === null
                    && editor.listView.itemAtIndex(blockRow) === delegate)
                controller.selectedRow = blockRow
        })
    }
    /// End the cell's editing, keeping its text ("hide keyboard").
    function closeCell() {
        const input = root.cellInput
        if (!input)
            return
        EditorInput.commitPreedit(input)
        input.leave()
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
                        x: root.columnWidths.slice(0, index).reduce((a, b) => a + b, 0) + 2
                        y: Math.round((root.filterHeight - implicitHeight) / 2)
                        width: (root.columnWidths[index] || 0) - 4
                        implicitWidth: 40 // (its width is the column's)
                        size: "small"
                        clearable: true
                        placeholderText: qsTr("Filter")
                        accessibleName: qsTr("Filter of column %1").arg(root.header[index] || "")
                        text: root.filters[index] || ""
                        onTextEdited: root.block.controller.setTableFilter(root.block.index, index, text)
                        onCleared: root.block.controller.setTableFilter(root.block.index, index, "")
                        // Escape in an empty field: the filters go, the table is selected.
                        onEscapePressed: {
                            const controller = root.block.controller
                            const list = root.block.editor.listView
                            const blockRow = root.block.index
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
                    readonly property int r: index === 0 ? -1 : (root.shownRows[index - 1] ?? -1)  // -1 = header
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
                            readonly property var cellData: rowItem.r >= 0 && root.rows[rowItem.r] ? root.rows[rowItem.r][c] : null
                            readonly property bool headerCell: rowItem.r === -1
                            readonly property bool formula: cellData ? cellData.formula : false
                            readonly property string error: cellData ? cellData.error : ""
                            readonly property string shown: headerCell ? (root.header[c] || "") : cellData ? cellData.display : ""
                            /// Rendered cell (escaped rich text from the core renderer).
                            readonly property string shownHtml: headerCell ? (root.headerHtml[c] || "")
                                                                : cellData && cellData.html !== undefined ? cellData.html : ""
                            readonly property string align: root.aligns[c] || ""
                            readonly property bool editingCell: root.editRow === rowItem.r && root.editColumn === c
                            /// The link under the mouse or the pressing finger, underlined (BlockText.qml).
                            property string hoveredLink: ""
                            function trackLink(x: real, y: real) {
                                const link = cell.editingCell ? "" : cellText.linkAt(x - cellText.x, y - cellText.y)
                                const h = link !== "" && !link.startsWith("reminder:") && !root.block.editor.editsOnClick(link) ? link : ""
                                if (h !== cell.hoveredLink)
                                    cell.hoveredLink = h
                            }
                            onEditingCellChanged: cell.hoveredLink = ""
                            x: root.columnWidths.slice(0, c).reduce((a, b) => a + b, 0)
                            width: root.columnWidths[c] || 0
                            height: root.rowHeight
                            Accessible.role: headerCell ? Accessible.ColumnHeader : Accessible.Cell
                            Accessible.name: shown

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
                                visible: cell.headerCell && (root.filters[cell.c] || "").trim() !== ""
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
                                    if (href.startsWith("reminder:") || root.block.editor.editsOnClick(href))
                                        root.cellMenu(rowItem.r, cell.c, cellText, x, y)
                                    else
                                        root.block.editor.activateLink(root.block.index, href)
                                }
                            }
                            // Created when shown: a tooltip (a popup with its own items) in
                            // every cell made a table slow to create while scrolling.
                            Loader {
                                active: cellHover.hovered && (cell.error !== "" || cellText.truncated)
                                sourceComponent: BToolTip {
                                    parent: cell
                                    visible: true
                                    text: root.block.editor.autoTextSafe(cell.error !== "" ? cell.error : cell.shown)
                                }
                            }
                            HoverHandler {
                                id: cellHover
                                enabled: !Theme.touchUi
                                cursorShape: cellText.linkAt(point.position.x - cellText.x, point.position.y - cellText.y) !== ""
                                             ? Qt.PointingHandCursor : Qt.IBeamCursor
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
                                        root.block.editor.longPressText(root.block.index)
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
                                    const p = eventPoint.position
                                    if (!root.cellMenu(rowItem.r, cell.c, cellText, p.x - cellText.x, p.y - cellText.y))
                                        root.block.editor.openBlockMenuAt(root.block.index, eventPoint.scenePosition,
                                                                          root.cellInfo(rowItem.r, cell.c))
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
                                    if (root.block.editor.blockTapped(root.block.index))
                                        return // Shift+click, the touch selection mode
                                    const p = eventPoint.position
                                    // Reminder, date, 🔁 and priority chips name ranges of the cell's
                                    // text: a click edits the cell (their menu is the right button's).
                                    const link = cellText.linkAt(p.x - cellText.x, p.y - cellText.y)
                                    if (link !== "" && !link.startsWith("reminder:") && !root.block.editor.editsOnClick(link))
                                        root.block.editor.activateLink(root.block.index, link)
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
                                    function leave(nextRow, nextColumn) {
                                        if (done)
                                            return
                                        done = true
                                        const r = rowItem.r
                                        const c = cell.c
                                        root.commit(r, c, text)
                                        if (nextRow === undefined)
                                            root.finish()
                                        else if (nextRow >= root.rows.length) {
                                            root.block.controller.addTableRow(root.block.index)
                                            Qt.callLater(() => { if (root) root.startCell(nextRow, nextColumn) })
                                        } else
                                            Qt.callLater(() => { if (root) root.startCell(nextRow, nextColumn) })
                                    }
                                    Component.onCompleted: {
                                        text = root.rawText(rowItem.r, cell.c)
                                        cursorPosition = length
                                        ready = true
                                        root.cellInput = input
                                        forceActiveFocus()
                                        // Above the toolbar and the keyboard (also once they show).
                                        Qt.callLater(() => {
                                            if (root && root.cellInput === input)
                                                root.block.editor.ensureEditVisible()
                                        })
                                    }
                                    Component.onDestruction: if (root.cellInput === input) root.cellInput = null
                                    /// Commit the text, do `edit` to the table (its rows are made anew),
                                    /// then go on editing cell (r, c).
                                    function reshape(edit, r, c) {
                                        if (done)
                                            return
                                        EditorInput.commitPreedit(input)
                                        done = true
                                        const view = root
                                        view.commit(rowItem.r, cell.c, text)
                                        view.finish()
                                        edit(view.block.controller, view.block.index)
                                        Qt.callLater(() => { if (view) view.startCell(r, c) })
                                    }
                                    function next() {
                                        const r = rowItem.r
                                        const c = cell.c
                                        if (c + 1 < root.columns) leave(r, c + 1)
                                        else leave(root.nextShownRow(r, 1), 0)
                                    }
                                    function undoTable() {
                                        done = true
                                        root.finish()
                                        const controller = root.block.controller
                                        const list = root.block.editor.listView
                                        const blockRow = root.block.index
                                        controller.undo()
                                        if (controller.editingRow < 0) {
                                            controller.selectedRow = Math.min(blockRow, controller.blocks.count - 1)
                                            list.forceActiveFocus()
                                        }
                                    }
                                    onTextChanged: {
                                        if (!ready || done)
                                            return
                                        root.block.controller.setTableCell(root.block.index, rowItem.r, cell.c, text, true)
                                    }
                                    onActiveFocusChanged: {
                                        if (activeFocus || done)
                                            return
                                        leave()
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
                                                reshape((controller, block) => controller.moveTableRow(block, r, to), to, c)
                                            event.accepted = true
                                        } else if ((event.modifiers & Qt.AltModifier) && !(event.modifiers & Qt.ControlModifier)
                                                   && (event.key === Qt.Key_Left || event.key === Qt.Key_Right)) {
                                            const to = event.key === Qt.Key_Left ? c - 1 : c + 1
                                            if (to >= 0 && to < root.columns)
                                                reshape((controller, block) => controller.moveTableColumn(block, c, to), r, to)
                                            event.accepted = true
                                        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                                   && (event.modifiers & Qt.ControlModifier)) {
                                            reshape((controller, block) => controller.insertTableRow(block, r + 1), r + 1, c)
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_Escape) {
                                            done = true
                                            root.finish()
                                            // Cancel: the cell's typing is one undo step, undone
                                            // with the cell closed first (finish()): its field
                                            // would keep the text the undo takes back.
                                            const controller = root.block.controller
                                            const list = root.block.editor.listView
                                            const blockRow = root.block.index
                                            controller.cancelTableCell(blockRow, r, c)
                                            controller.selectedRow = blockRow
                                            list.forceActiveFocus()
                                            event.accepted = true
                                        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                                   && (event.modifiers & Qt.AltModifier) && !(event.modifiers & Qt.ControlModifier)) {
                                            // Alt+Enter: follow the link (path part) at the caret;
                                            // with Shift in the side panel.
                                            const blockRow = root.block.index
                                            const url = root.block.controller.linkInText(blockRow, input.text, input.cursorPosition)
                                            root.block.editor.followLink(blockRow, url,
                                                                         (event.modifiers & Qt.ShiftModifier) !== 0 && !Theme.narrow)
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                            leave(root.nextShownRow(r, 1), c)
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_Tab) {
                                            next()
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_Backtab) {
                                            if (c > 0) leave(r, c - 1)
                                            else if (r > -1) leave(root.nextShownRow(r, -1), root.columns - 1)
                                            event.accepted = true
                                        } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Z) {
                                            undoTable()
                                            event.accepted = true
                                        }
                                    }
                                }
                            }
                            // A right-click on the cell being edited: the field would take the
                            // press from a handler, an area on top of it takes only that button.
                            MouseArea {
                                anchors.fill: parent
                                enabled: cell.editingCell
                                acceptedButtons: Qt.RightButton
                                onClicked: (mouse) => {
                                    const view = root
                                    const info = view.cellInfo(rowItem.r, cell.c)
                                    const scene = mapToItem(null, mouse.x, mouse.y)
                                    view.closeCell()
                                    view.stayCurrent()
                                    view.block.editor.openBlockMenuAt(view.block.index, scene, info)
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
            readonly property real addedWidth: addColumn.x + addColumn.implicitWidth
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
                text: qsTr("%1 of %n row(s) shown", "", root.rows.length).arg((root.table.shownCount ?? root.rows.length))
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
                onClicked: root.block.controller.setTableFilterOpen(root.block.index, false)
            }
            BButton {
                id: addRow
                size: "small"
                variant: "ghost"
                iconName: "plus"
                text: qsTr("Row")
                focusPolicy: Qt.NoFocus
                onClicked: root.block.controller.addTableRow(root.block.index)
            }
            BButton {
                id: addColumn
                x: addRow.implicitWidth + Theme.space1
                size: "small"
                variant: "ghost"
                iconName: "plus"
                text: qsTr("Column")
                focusPolicy: Qt.NoFocus
                onClicked: root.block.controller.addTableColumn(root.block.index)
            }
        }
    }
}
