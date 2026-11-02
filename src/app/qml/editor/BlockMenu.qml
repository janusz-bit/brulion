pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockMenu — the menu of one block of the editor (docs/UI.md "The block editor"): Kopiuj, Wytnij,
// Kopiuj jako tekst, Zaznacz (docs/UI.md "Selecting and copying" — a block that is part of the
// block selection: the whole selection; else the block with its children), Przybliż,
// Zwiń / Rozwiń (outline), Kopiuj odnośnik ▸ (Odwołanie do bloku ((…)), Link do bloku [[…#^…]],
// on headings Link do nagłówka [[…#…]], Osadzenie — what `BlockEditor.copyLink` puts on the
// clipboard, docs/SYNTAX.md §2 "Copied links"), Wstaw obraz…, Zwiększ / Zmniejsz wcięcie,
// Przenieś w górę / w dół, Usuń blok. A block that is part of a selection over several days of
// the journal stream (BlockEditor.inSpan): Kopiuj and Kopiuj jako tekst copy the whole span (its
// count), Wytnij, indenting, moving and Usuń are shown unavailable — a tap says that only copying
// works there (docs/UI.md "Across journal days"). One menu per editor, made when first opened
// (BlockEditor.openBlockMenu): from a block's bullet or number (a right-click, a long press), a
// right-click anywhere on a block (mouse, touchpad, stylus — never a finger: a long press on text
// is left for selecting it), the touch toolbar's "Więcej". The block is tracked while the menu is
// open (sync or undo may move it); an action goes to wherever it is then, nowhere when it is gone.
// On touch UIs the "Kopiuj odnośnik" submenu opens in place of the menu (BMenu: no cascade).
// From a table's cell (a right-click on it, the cell toolbar's "Więcej": BlockEditor.openBlockMenu's
// `cell`) also Wiersz ▸ and Kolumna ▸: insert, move and delete that cell's row or column, the
// column's alignment; a new row or column has its cell opened for typing.
//
//   BlockMenu { editor: editor }    menu.openFor(row); menu.popup(item, x, y)
BMenu {
    id: menu

    property BlockEditor editor
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

    // Submenu items are OptionalItems too, shown as their OptionalMenu says. Set before the first
    // submenu below: a menu makes the item of a submenu as soon as it gets it.
    delegate: OptionalItem {
        shown: {
            const optional = subMenu as OptionalMenu
            return !optional || optional.shown
        }
    }

    /// Follow the block at `row` (until the next openFor) and take its shape. `cell`: the table
    /// cell it was opened from.
    function openFor(row: int, cell) {
        if (!controller)
            return
        if (handle >= 0)
            controller.untrack(handle)
        handle = controller.trackBlock(row)
        cellRow = cell ? cell.row : -2
        cellColumn = cell ? cell.column : -1
        tableRows = cell ? cell.rows : 0
        tableColumns = cell ? cell.columns : 0
        cellAlign = cell ? cell.align : ""
        const info = controller.blocks.get(row)
        listItem = info.kind === "listItem"
        heading = controller.isHeading(row)
        hasChildren = info.hasChildren
        collapsed = info.collapsed
        // Over several days even one block of this day is part of the span.
        const span = editor.inSpan
        inSelection = (controller.multiSelection || span) && controller.selectedRows().indexOf(row) >= 0
        selectionCount = !inSelection ? 1 : span ? editor.stream.count : controller.selectionCount
        unmovable = !inSelection ? "" : span ? editor.spanReason : !controller.selectionContiguous ? editor.gapReason : ""
        uncuttable = inSelection && span ? editor.spanReason : ""
    }
    /// Indent or move: the selection the block is part of (`selection`), else the block (`one`) —
    /// a selection with gaps says why not instead.
    function moveOr(one, selection) {
        if (!inSelection)
            act(one)
        else if (unmovable !== "")
            editor.notice(unmovable, "info")
        else
            act(selection)
    }
    /// Copying and cutting act on the selection the block is part of, else on the block (with its
    /// children), which becomes the selection.
    function selectFor(r: int) {
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
    /// Run `fn(row)` for the block when it is still there.
    function act(fn) {
        const r = row()
        if (r >= 0)
            fn(r)
    }

    /// A new row or column: `insert(row of the table's block)`, then cell (r, c) opened for typing
    /// once the menu has given the keys back.
    function insertAndEdit(insert, r: int, c: int) {
        act((block) => {
            insert(block)
            menu.editor.editTableCellOnceClosed(menu, block, r, c)
        })
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
        required property string align
        checkable: true
        checked: menu.cellAlign === align
        onTriggered: {
            checked = Qt.binding(() => menu.cellAlign === align) // the click toggled it
            menu.act((r) => menu.controller.setTableColumnAlign(r, menu.cellColumn, align))
        }
    }

    BMenuItem {
        objectName: "blockMenuCopy"
        text: menu.inSelection ? qsTr("Copy %n block(s)", "", menu.selectionCount) : qsTr("Copy")
        iconName: "copy"
        shortcut: "Ctrl+C"
        onTriggered: menu.act((r) => { menu.selectFor(r); menu.editor.copySelection(false) })
    }
    BMenuItem {
        objectName: "blockMenuCut"
        text: menu.inSelection ? qsTr("Cut %n block(s)", "", menu.selectionCount) : qsTr("Cut")
        iconName: "scissors"
        shortcut: "Ctrl+X"
        unavailable: menu.uncuttable
        onTriggered: menu.act((r) => { menu.selectFor(r); menu.editor.cutSelection() })
    }
    BMenuItem {
        objectName: "blockMenuCopyText"
        text: qsTr("Copy as plain text")
        iconName: "copy"
        onTriggered: menu.act((r) => { menu.selectFor(r); menu.editor.copySelection(true) })
    }
    OptionalItem {
        objectName: "blockMenuSelect"
        shown: !menu.inSelection
        text: qsTr("Select")
        iconName: "square-dashed"
        onTriggered: menu.act((r) => menu.editor.selectBlock(r))
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
            onTriggered: menu.insertAndEdit((r) => menu.controller.insertTableRow(r, menu.cellRow),
                                            menu.cellRow, menu.cellColumn)
        }
        BMenuItem {
            objectName: "tableRowBelow"
            text: qsTr("Insert row below")
            iconName: "arrow-down"
            shortcut: "Ctrl+Enter"
            onTriggered: menu.insertAndEdit((r) => menu.controller.insertTableRow(r, menu.cellRow + 1),
                                            menu.cellRow + 1, menu.cellColumn)
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "tableRowUp"
            text: qsTr("Move row up")
            iconName: "move-up"
            shortcut: "Alt+Up"
            enabled: menu.cellRow > 0
            onTriggered: menu.act((r) => menu.controller.moveTableRow(r, menu.cellRow, menu.cellRow - 1))
        }
        BMenuItem {
            objectName: "tableRowDown"
            text: qsTr("Move row down")
            iconName: "move-down"
            shortcut: "Alt+Down"
            enabled: menu.cellRow >= 0 && menu.cellRow + 1 < menu.tableRows
            onTriggered: menu.act((r) => menu.controller.moveTableRow(r, menu.cellRow, menu.cellRow + 1))
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "tableRowDelete"
            text: qsTr("Delete row")
            iconName: "trash-2"
            danger: true
            enabled: menu.cellRow >= 0
            onTriggered: menu.act((r) => menu.controller.removeTableRow(r, menu.cellRow))
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
            onTriggered: menu.insertAndEdit((r) => menu.controller.insertTableColumn(r, menu.cellColumn),
                                            -1, menu.cellColumn)
        }
        BMenuItem {
            objectName: "tableColumnRight"
            text: qsTr("Insert column on the right")
            iconName: "arrow-right"
            onTriggered: menu.insertAndEdit((r) => menu.controller.insertTableColumn(r, menu.cellColumn + 1),
                                            -1, menu.cellColumn + 1)
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "tableColumnMoveLeft"
            text: qsTr("Move column left")
            iconName: "arrow-left"
            shortcut: "Alt+Left"
            enabled: menu.cellColumn > 0
            onTriggered: menu.act((r) => menu.controller.moveTableColumn(r, menu.cellColumn, menu.cellColumn - 1))
        }
        BMenuItem {
            objectName: "tableColumnMoveRight"
            text: qsTr("Move column right")
            iconName: "arrow-right"
            shortcut: "Alt+Right"
            enabled: menu.cellColumn + 1 < menu.tableColumns
            onTriggered: menu.act((r) => menu.controller.moveTableColumn(r, menu.cellColumn, menu.cellColumn + 1))
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
            onTriggered: menu.act((r) => menu.controller.removeTableColumn(r, menu.cellColumn))
        }
    }
    OptionalItem {
        objectName: "blockMenuZoom"
        shown: !menu.documentMode
        text: qsTr("Zoom in")
        iconName: "search"
        onTriggered: menu.act((r) => menu.controller.zoomInto(r))
    }
    OptionalItem {
        shown: menu.hasChildren && !menu.documentMode
        text: menu.collapsed ? qsTr("Expand") : qsTr("Collapse")
        iconName: menu.collapsed ? "chevrons-up-down" : "chevrons-down-up"
        shortcut: "Ctrl+."
        onTriggered: menu.act((r) => menu.controller.toggleFold(r))
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
            shortcut: "Ctrl+Shift+C"
            onTriggered: menu.act((r) => menu.editor.copyLink(r, "blockRef"))
        }
        BMenuItem {
            objectName: "copyBlockLink"
            text: qsTr("Link to block [[…#^…]]")
            iconName: "bookmark"
            onTriggered: menu.act((r) => menu.editor.copyLink(r, "blockLink"))
        }
        OptionalItem {
            objectName: "copyHeadingLink"
            shown: menu.heading
            text: qsTr("Link to heading [[…#…]]")
            iconName: "heading"
            onTriggered: menu.act((r) => menu.editor.copyLink(r, "heading"))
        }
        BMenuItem {
            objectName: "copyEmbed"
            text: qsTr("Embed")
            iconName: "rows-2"
            onTriggered: menu.act((r) => menu.editor.copyLink(r, "embed"))
        }
    }
    BMenuItem {
        text: qsTr("Insert picture…")
        iconName: "image"
        onTriggered: menu.act((r) => menu.editor.dialogs.pickImage(r, -1))
    }
    BMenuItem {
        objectName: "blockMenuDrawing"
        text: qsTr("Insert drawing")
        iconName: "pen-tool"
        onTriggered: menu.act((r) => menu.editor.insertDrawing(r, -1))
    }
    BMenuItem {
        objectName: "blockMenuDrawingOn"
        text: qsTr("Insert a drawing on a picture or PDF…")
        iconName: "pen-tool"
        onTriggered: menu.act((r) => menu.editor.dialogs.pickDrawingBackdrop(r, -1))
    }
    BMenuSeparator {}
    OptionalItem {
        shown: !menu.documentMode || menu.listItem
        text: qsTr("Indent")
        iconName: "list-indent-increase"
        shortcut: "Tab"
        unavailable: menu.unmovable
        onTriggered: menu.moveOr((r) => menu.controller.indent(r), () => menu.controller.indentSelection())
    }
    OptionalItem {
        shown: !menu.documentMode || menu.listItem
        text: qsTr("Outdent")
        iconName: "list-indent-decrease"
        shortcut: "Shift+Tab"
        unavailable: menu.unmovable
        onTriggered: menu.moveOr((r) => menu.controller.outdent(r), () => menu.controller.outdentSelection())
    }
    BMenuItem {
        objectName: "blockMenuMoveUp"
        text: qsTr("Move up")
        iconName: "move-up"
        shortcut: "Alt+Up"
        unavailable: menu.unmovable
        onTriggered: menu.moveOr((r) => menu.controller.moveUp(r), () => menu.controller.moveSelectionUp())
    }
    BMenuItem {
        text: qsTr("Move down")
        iconName: "move-down"
        shortcut: "Alt+Down"
        unavailable: menu.unmovable
        onTriggered: menu.moveOr((r) => menu.controller.moveDown(r), () => menu.controller.moveSelectionDown())
    }
    BMenuSeparator {}
    BMenuItem {
        objectName: "blockMenuDelete"
        text: menu.inSelection ? qsTr("Delete %n block(s)", "", menu.selectionCount) : qsTr("Delete block")
        iconName: "trash-2"
        danger: true
        unavailable: menu.uncuttable
        onTriggered: menu.act((r) => menu.inSelection ? menu.editor.deleteSelection() : menu.controller.deleteBlock(r))
    }
}
