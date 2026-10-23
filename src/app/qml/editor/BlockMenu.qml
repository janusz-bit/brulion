pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockMenu — the menu of one block of the editor (docs/UI.md "The block editor"): Kopiuj, Wytnij,
// Kopiuj jako tekst, Zaznacz (docs/UI.md "Selecting and copying" — a block that is part of the
// block selection: the whole selection; else the block with its children), Przybliż,
// Zwiń / Rozwiń (outline), Kopiuj odnośnik ▸ (Odwołanie do bloku ((…)), Link do bloku [[…#^…]],
// on headings Link do nagłówka [[…#…]], Osadzenie — what `BlockEditor.copyLink` puts on the
// clipboard, docs/SYNTAX.md §2 "Copied links"), Wstaw obraz…, Zwiększ / Zmniejsz wcięcie,
// Przenieś w górę / w dół, Usuń blok. One menu per editor, made when first opened
// (BlockEditor.openBlockMenu): from a block's bullet or number (a right-click, a long press), a
// right-click anywhere on a block (mouse, touchpad, stylus — never a finger: a long press on text
// is left for selecting it), the touch toolbar's "Więcej". The block is tracked while the menu is
// open (sync or undo may move it); an action goes to wherever it is then, nowhere when it is gone.
// On touch UIs the "Kopiuj odnośnik" submenu opens in place of the menu (BMenu: no cascade).
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
    /// Why that selection is not indented or moved (blocks picked with gaps between them; "" when
    /// it can be): the items are shown disabled, a tap says why.
    property string unmovable: ""
    readonly property bool documentMode: editor !== null && editor.documentMode

    /// Follow the block at `row` (until the next openFor) and take its shape.
    function openFor(row: int) {
        if (!controller)
            return
        if (handle >= 0)
            controller.untrack(handle)
        handle = controller.trackBlock(row)
        const info = controller.blocks.get(row)
        listItem = info.kind === "listItem"
        heading = controller.isHeading(row)
        hasChildren = info.hasChildren
        collapsed = info.collapsed
        inSelection = controller.multiSelection && controller.selectedRows().indexOf(row) >= 0
        selectionCount = inSelection ? controller.selectionCount : 1
        unmovable = inSelection && !controller.selectionContiguous ? editor.gapReason : ""
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

    component OptionalItem: BMenuItem {
        property bool shown: true
        visible: shown
        height: shown ? implicitHeight : 0
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
        onTriggered: menu.act((r) => menu.inSelection ? menu.editor.deleteSelection() : menu.controller.deleteBlock(r))
    }
}
