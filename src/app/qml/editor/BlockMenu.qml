pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockMenu — the menu of one block of the editor (docs/UI.md "The block editor"): Copy, Cut,
// Copy as plain text, Select (docs/UI.md "Selecting and copying" — a block that is part of the
// block selection: the whole selection; else the block with its children), Zoom in,
// Collapse / Expand (outline), Copy link ▸ (Block reference ((…)), Link to block [[…#^…]],
// on headings Link to heading [[…#…]], Embed — what `BlockEditor.copyLink` puts on the
// clipboard, docs/SYNTAX.md §2 "Copied links"), Insert picture…, Indent / Outdent,
// Move up / down, Delete block. A block that is part of a selection over several days of
// the journal stream (BlockEditor.inSpan): Copy and Copy as plain text copy the whole span (its
// count), Cut, indenting, moving and Delete are shown unavailable — a tap says that only copying
// works there (docs/UI.md "Across journal days"). One menu per editor, made when first opened
// (BlockEditor.openBlockMenu): from a block's bullet or number (a right-click, a long press), a
// right-click anywhere on a block (mouse, touchpad, stylus — never a finger: a long press on text
// is left for selecting it), the touch toolbar's "More". The block is tracked while the menu is
// open (sync or undo may move it); an action goes to wherever it is then, nowhere when it is gone.
// On touch UIs the "Copy link" submenu opens in place of the menu (BMenu: no cascade).
// From a table's cell (a right-click on it, the cell toolbar's "More": BlockEditor.openBlockMenu's
// `cell`) also Row ▸ and Column ▸: insert, move and delete that cell's row or column, the
// column's alignment, sorting the rows by the column and filtering them (docs/SYNTAX.md §9); a new
// row or column has its cell opened for typing. Any table's menu also has Filter rows,
// Copy as CSV and Save as CSV….
//
//   BlockMenu { editor: editor }    menu.openFor(row, -2, 0, 0, 0, ""); menu.popup(item, x, y)
BMenu {
    id: menu

    /// The editor (an Item: BlockEditor passes itself, see blockItem in the block views).
    property Item editorItem
    final property BlockEditor editor: editorItem as BlockEditor
    readonly property PageController controller: editor ? editor.controller : null
    /// controller.trackBlock() handle of the block (-1: none).
    property int handle: -1
    // What the block was when the menu opened (it does not change shape while the menu is open).
    property bool listItem: false
    property bool heading: false
    property bool hasChildren: false
    property bool collapsed: false
    /// The block is part of a selection of several blocks: copying, cutting, indenting, moving and
    /// deleting act on all of them.
    property bool inSelection: false
    property int selectionCount: 0
    /// Why that selection is not indented or moved (blocks picked with gaps between them, a
    /// selection over several days; "" when it can be): the items are shown disabled, a tap says why.
    property string unmovable: ""
    /// Why that selection is not cut or deleted (a selection over several days; "" when it can be).
    property string uncuttable: ""
    readonly property bool documentMode: editor !== null && editor.documentMode
    /// The table cell the menu was opened from: its row (-1 the header, -2 no cell), its column, the
    /// table's size and the column's alignment (TableBlockView.cellInfo()).
    property int cellRow: -2
    property int cellColumn: -1
    property int tableRows: 0
    property int tableColumns: 0
    property string cellAlign: ""
    readonly property bool inCell: cellRow > -2
    /// The block is a table (its CSV and filter items).
    property bool table: false
    /// Bumped when a click toggled an alignment's check mark: its binding takes over again.
    property int checkRevision: 0

    // Submenu items are OptionalItems too, shown as their OptionalMenu says. Set before the first
    // submenu below: a menu makes the item of a submenu as soon as it gets it.
    delegate: OptionalItem {
        shown: {
            const optional = subMenu as OptionalMenu
            return !optional || optional.shown
        }
    }

    /// Follow the block at `row` (until the next openFor) and take its shape. The table cell it
    /// was opened from: its row (`cellRow`, -1 the header, -2 no cell), column, the table's size and
    /// the column's alignment.
    function openFor(row: int, cellRow: int, cellColumn: int, cellRows: int, cellColumns: int, cellAlign: string): void {
        if (!controller)
            return
        if (handle >= 0)
            controller.untrack(handle)
        handle = controller.trackBlock(row)
        menu.cellRow = cellRow
        menu.cellColumn = cellRow > -2 ? cellColumn : -1
        menu.tableRows = cellRow > -2 ? cellRows : 0
        menu.tableColumns = cellRow > -2 ? cellColumns : 0
        menu.cellAlign = cellRow > -2 ? cellAlign : ""
        info.map = controller.blocks.get(row)
        table = info.map.view === "table"
        listItem = info.map.kind === "listItem"
        heading = controller.isHeading(row)
        hasChildren = info.map.hasChildren === true
        collapsed = info.map.collapsed === true
        // Over several days even one block of this day is part of the span.
        const span = editor.inSpan
        inSelection = (controller.multiSelection || span) && menu.selectedRows().indexOf(row) >= 0
        selectionCount = !inSelection ? 1 : span ? editor.stream.count : controller.selectionCount
        unmovable = !inSelection ? "" : span ? editor.spanReason : !controller.selectionContiguous ? editor.gapReason : ""
        uncuttable = inSelection && span ? editor.spanReason : ""
    }
    /// The block's shape when the menu opened (BlockListModel.get()).
    MapHolder { id: info }
    /// The rows of the block selection.
    function selectedRows(): list<var> {
        return controller.selectedRows()
    }
    /// Indent or move (`how`: "indent", "outdent", "up", "down"): the selection the block is part
    /// of, else the block — a selection with gaps says why not instead.
    function moveOr(how: string): void {
        const r = row()
        if (r < 0)
            return
        if (!inSelection) {
            if (how === "indent")
                controller.indent(r)
            else if (how === "outdent")
                controller.outdent(r)
            else if (how === "up")
                controller.moveUp(r)
            else
                controller.moveDown(r)
        } else if (unmovable !== "") {
            editor.notice(unmovable, "info", null)
        } else if (how === "indent") {
            controller.indentSelection()
        } else if (how === "outdent") {
            controller.outdentSelection()
        } else if (how === "up") {
            controller.moveSelectionUp()
        } else {
            controller.moveSelectionDown()
        }
    }
    /// Copying and cutting act on the selection the block is part of, else on the block (with its
    /// children), which becomes the selection.
    function selectFor(r: int): void {
        if (menu.inSelection)
            return
        if (Theme.touchUi)
            menu.editor.selectBlock(r) // the selection mode: what was copied stays shown
        else
            menu.controller.selectRange(r, r)
    }
    /// The block's row now; -1 when it is gone.
    function row(): int {
        return controller && handle >= 0 ? controller.trackedRow(handle) : -1
    }
    /// Its height once open, known before it is shown (to place it above the touch toolbar). The
    /// items that only some blocks have say so with `shown` (their effective visibility is false
    /// while the menu is closed).
    function openHeight(): real {
        let h = topPadding + bottomPadding
        for (let i = 0; i < count; ++i) {
            const entry = itemAt(i)
            const optional = entry as OptionalItem
            if (entry && (!optional || optional.shown))
                h += entry.implicitHeight
        }
        return h
    }
    /// A new row (`what` "row": at table row `at`) or column ("column": at column `at`), then cell
    /// (r, c) opened for typing once the menu has given the keys back.
    function insertAndEdit(what: string, at: int, r: int, c: int): void {
        const block = row()
        if (block < 0)
            return
        if (what === "row")
            controller.insertTableRow(block, at)
        else
            controller.insertTableColumn(block, at)
        menu.editor.editTableCellOnceClosed(menu, block, r, c)
    }

    component OptionalItem: BMenuItem {
        property bool shown: true
        visible: shown
        height: shown ? implicitHeight : 0
    }
    component OptionalMenu: BMenu {
        property bool shown: true
    }
    /// One of the column's alignments, ticked when it is the column's.
    component AlignItem: BMenuItem {
        id: alignItem
        required property string align
        checkable: true
        checked: menu.checkRevision >= 0 && menu.cellAlign === align
        onTriggered: {
            ++menu.checkRevision // the click toggled it: the binding again (an assignment would end it)
            const r = menu.row()
            if (r >= 0)
                menu.controller.setTableColumnAlign(r, menu.cellColumn, alignItem.align)
        }
    }

    BMenuItem {
        objectName: "blockMenuCopy"
        text: menu.inSelection ? qsTr("Copy %n block(s)", "", menu.selectionCount) : qsTr("Copy")
        iconName: "copy"
        shortcut: "Ctrl+C"
        onTriggered: {
            const r = menu.row()
            if (r < 0)
                return
            menu.selectFor(r)
            menu.editor.copySelection(false)
        }
    }
    BMenuItem {
        objectName: "blockMenuCut"
        text: menu.inSelection ? qsTr("Cut %n block(s)", "", menu.selectionCount) : qsTr("Cut")
        iconName: "scissors"
        shortcut: "Ctrl+X"
        unavailable: menu.uncuttable
        onTriggered: {
            const r = menu.row()
            if (r < 0)
                return
            menu.selectFor(r)
            menu.editor.cutSelection()
        }
    }
    BMenuItem {
        objectName: "blockMenuCopyText"
        text: qsTr("Copy as plain text")
        iconName: "copy"
        onTriggered: {
            const r = menu.row()
            if (r < 0)
                return
            menu.selectFor(r)
            menu.editor.copySelection(true)
        }
    }
    OptionalItem {
        objectName: "tableCopyCsv"
        shown: menu.table && !menu.inSelection
        text: qsTr("Copy as CSV")
        iconName: "copy"
        onTriggered: {
            const r = menu.row()
            if (r >= 0 && menu.controller.copyTableCsv(r))
                menu.editor.notice(qsTr("The table was copied as CSV."), "success", null)
        }
    }
    OptionalItem {
        objectName: "tableSaveCsv"
        shown: menu.table && !menu.inSelection
        text: qsTr("Save as CSV…")
        iconName: "download"
        onTriggered: {
            const r = menu.row()
            if (r >= 0)
                menu.editor.dialogs.saveTableCsv(r)
        }
    }
    OptionalItem {
        objectName: "tableFilterRows"
        shown: menu.table && !menu.inSelection
        text: menu.editor && menu.editor.tableFiltering(menu.row()) ? qsTr("Hide filters") : qsTr("Filter rows")
        iconName: "filter"
        onTriggered: {
            const r = menu.row()
            if (r < 0)
                return
            if (menu.editor.tableFiltering(r))
                menu.controller.setTableFilterOpen(r, false)
            else
                menu.editor.openTableFilter(r, menu.inCell ? menu.cellColumn : 0, menu)
        }
    }
    OptionalItem {
        objectName: "blockMenuSelect"
        shown: !menu.inSelection
        text: qsTr("Select")
        iconName: "square-dashed"
        onTriggered: {
            const r = menu.row()
            if (r >= 0)
                menu.editor.selectBlock(r)
        }
    }
    BMenuSeparator {}
    OptionalMenu {
        objectName: "tableRowMenu"
        shown: menu.inCell
        title: qsTr("Row")
        iconName: "rows-3"

        BMenuItem {
            objectName: "tableRowAbove"
            text: qsTr("Insert row above")
            iconName: "arrow-up"
            enabled: menu.cellRow >= 0
            onTriggered: menu.insertAndEdit("row", menu.cellRow, menu.cellRow, menu.cellColumn)
        }
        BMenuItem {
            objectName: "tableRowBelow"
            text: qsTr("Insert row below")
            iconName: "arrow-down"
            shortcut: "Ctrl+Enter"
            onTriggered: menu.insertAndEdit("row", menu.cellRow + 1, menu.cellRow + 1, menu.cellColumn)
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "tableRowUp"
            text: qsTr("Move row up")
            iconName: "move-up"
            shortcut: "Alt+Up"
            enabled: menu.cellRow > 0
            onTriggered: {
                const r = menu.row()
                if (r >= 0)
                    menu.controller.moveTableRow(r, menu.cellRow, menu.cellRow - 1)
            }
        }
        BMenuItem {
            objectName: "tableRowDown"
            text: qsTr("Move row down")
            iconName: "move-down"
            shortcut: "Alt+Down"
            enabled: menu.cellRow >= 0 && menu.cellRow + 1 < menu.tableRows
            onTriggered: {
                const r = menu.row()
                if (r >= 0)
                    menu.controller.moveTableRow(r, menu.cellRow, menu.cellRow + 1)
            }
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "tableRowDelete"
            text: qsTr("Delete row")
            iconName: "trash-2"
            danger: true
            enabled: menu.cellRow >= 0
            onTriggered: {
                const r = menu.row()
                if (r >= 0)
                    menu.controller.removeTableRow(r, menu.cellRow)
            }
        }
    }
    OptionalMenu {
        objectName: "tableColumnMenu"
        shown: menu.inCell
        title: qsTr("Column")
        iconName: "columns-2"

        BMenuItem {
            objectName: "tableColumnLeft"
            text: qsTr("Insert column on the left")
            iconName: "arrow-left"
            onTriggered: menu.insertAndEdit("column", menu.cellColumn, -1, menu.cellColumn)
        }
        BMenuItem {
            objectName: "tableColumnRight"
            text: qsTr("Insert column on the right")
            iconName: "arrow-right"
            onTriggered: menu.insertAndEdit("column", menu.cellColumn + 1, -1, menu.cellColumn + 1)
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "tableSortAscending"
            text: qsTr("Sort ascending")
            iconName: "arrow-up-narrow-wide"
            enabled: menu.tableRows > 1
            onTriggered: {
                const r = menu.row()
                if (r >= 0 && !menu.controller.sortTable(r, menu.cellColumn, false))
                    menu.editor.notice(qsTr("The rows are in that order already."), "info", null)
            }
        }
        BMenuItem {
            objectName: "tableSortDescending"
            text: qsTr("Sort descending")
            iconName: "arrow-down-wide-narrow"
            enabled: menu.tableRows > 1
            onTriggered: {
                const r = menu.row()
                if (r >= 0 && !menu.controller.sortTable(r, menu.cellColumn, true))
                    menu.editor.notice(qsTr("The rows are in that order already."), "info", null)
            }
        }
        BMenuItem {
            objectName: "tableFilterColumn"
            text: qsTr("Filter by this column…")
            iconName: "filter"
            onTriggered: {
                const r = menu.row()
                if (r >= 0)
                    menu.editor.openTableFilter(r, menu.cellColumn, menu)
            }
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "tableColumnMoveLeft"
            text: qsTr("Move column left")
            iconName: "arrow-left"
            shortcut: "Alt+Left"
            enabled: menu.cellColumn > 0
            onTriggered: {
                const r = menu.row()
                if (r >= 0)
                    menu.controller.moveTableColumn(r, menu.cellColumn, menu.cellColumn - 1)
            }
        }
        BMenuItem {
            objectName: "tableColumnMoveRight"
            text: qsTr("Move column right")
            iconName: "arrow-right"
            shortcut: "Alt+Right"
            enabled: menu.cellColumn + 1 < menu.tableColumns
            onTriggered: {
                const r = menu.row()
                if (r >= 0)
                    menu.controller.moveTableColumn(r, menu.cellColumn, menu.cellColumn + 1)
            }
        }
        BMenuSeparator {}
        AlignItem { objectName: "tableAlignLeft"; align: "left"; text: qsTr("Align left") }
        AlignItem { objectName: "tableAlignCenter"; align: "center"; text: qsTr("Align center") }
        AlignItem { objectName: "tableAlignRight"; align: "right"; text: qsTr("Align right") }
        AlignItem { objectName: "tableAlignNone"; align: ""; text: qsTr("Default alignment") }
        BMenuSeparator {}
        BMenuItem {
            objectName: "tableColumnDelete"
            text: qsTr("Delete column")
            iconName: "trash-2"
            danger: true
            enabled: menu.tableColumns > 1
            onTriggered: {
                const r = menu.row()
                if (r >= 0)
                    menu.controller.removeTableColumn(r, menu.cellColumn)
            }
        }
    }
    OptionalItem {
        objectName: "blockMenuZoom"
        shown: !menu.documentMode
        text: qsTr("Zoom in")
        iconName: "search"
        onTriggered: {
            const r = menu.row()
            if (r >= 0)
                menu.controller.zoomInto(r)
        }
    }
    OptionalItem {
        shown: menu.hasChildren && !menu.documentMode
        text: menu.collapsed ? qsTr("Expand") : qsTr("Collapse")
        iconName: menu.collapsed ? "chevrons-up-down" : "chevrons-down-up"
        shortcut: "Ctrl+."
        onTriggered: {
            const r = menu.row()
            if (r >= 0)
                menu.controller.toggleFold(r)
        }
    }
    BMenu {
        id: copyMenu
        objectName: "copyLinkMenu"
        title: qsTr("Copy link")
        iconName: "link"

        BMenuItem {
            objectName: "copyBlockRef"
            text: qsTr("Block reference ((…))")
            iconName: "link"
            shortcut: menu.editor ? menu.editor.copyRefShortcut : ""
            onTriggered: {
                const r = menu.row()
                if (r >= 0)
                    menu.editor.copyLink(r, "blockRef")
            }
        }
        BMenuItem {
            objectName: "copyBlockLink"
            text: qsTr("Link to block [[…#^…]]")
            iconName: "bookmark"
            onTriggered: {
                const r = menu.row()
                if (r >= 0)
                    menu.editor.copyLink(r, "blockLink")
            }
        }
        OptionalItem {
            objectName: "copyHeadingLink"
            shown: menu.heading
            text: qsTr("Link to heading [[…#…]]")
            iconName: "heading"
            onTriggered: {
                const r = menu.row()
                if (r >= 0)
                    menu.editor.copyLink(r, "heading")
            }
        }
        BMenuItem {
            objectName: "copyEmbed"
            text: qsTr("Embed")
            iconName: "rows-2"
            onTriggered: {
                const r = menu.row()
                if (r >= 0)
                    menu.editor.copyLink(r, "embed")
            }
        }
    }
    BMenuItem {
        text: qsTr("Insert picture…")
        iconName: "image"
        onTriggered: {
            const r = menu.row()
            if (r >= 0)
                menu.editor.dialogs.pickImage(r, -1)
        }
    }
    BMenuItem {
        objectName: "blockMenuDrawing"
        text: qsTr("Insert drawing")
        iconName: "pen-tool"
        onTriggered: {
            const r = menu.row()
            if (r >= 0)
                menu.editor.insertDrawing(r, -1)
        }
    }
    BMenuItem {
        objectName: "blockMenuDrawingOn"
        text: qsTr("Insert a drawing on a picture or PDF…")
        iconName: "pen-tool"
        onTriggered: {
            const r = menu.row()
            if (r >= 0)
                menu.editor.dialogs.pickDrawingBackdrop(r, -1)
        }
    }
    BMenuSeparator {}
    OptionalItem {
        shown: !menu.documentMode || menu.listItem
        text: qsTr("Indent")
        iconName: "list-indent-increase"
        shortcut: "Tab"
        unavailable: menu.unmovable
        onTriggered: menu.moveOr("indent")
    }
    OptionalItem {
        shown: !menu.documentMode || menu.listItem
        text: qsTr("Outdent")
        iconName: "list-indent-decrease"
        shortcut: "Shift+Tab"
        unavailable: menu.unmovable
        onTriggered: menu.moveOr("outdent")
    }
    BMenuItem {
        objectName: "blockMenuMoveUp"
        text: qsTr("Move up")
        iconName: "move-up"
        shortcut: "Alt+Up"
        unavailable: menu.unmovable
        onTriggered: menu.moveOr("up")
    }
    BMenuItem {
        text: qsTr("Move down")
        iconName: "move-down"
        shortcut: "Alt+Down"
        unavailable: menu.unmovable
        onTriggered: menu.moveOr("down")
    }
    BMenuSeparator {}
    BMenuItem {
        objectName: "blockMenuDelete"
        text: menu.inSelection ? qsTr("Delete %n block(s)", "", menu.selectionCount) : qsTr("Delete block")
        iconName: "trash-2"
        danger: true
        unavailable: menu.uncuttable
        onTriggered: {
            const r = menu.row()
            if (r < 0)
                return
            if (menu.inSelection)
                menu.editor.deleteSelection()
            else
                menu.controller.deleteBlock(r)
        }
    }
}
