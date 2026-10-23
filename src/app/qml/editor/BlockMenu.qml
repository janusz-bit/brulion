pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockMenu — the menu of one block of the editor (docs/UI.md "The block editor"): Przybliż,
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
            if (entry && entry.shown !== false)
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
        onTriggered: menu.act((r) => menu.controller.indent(r))
    }
    OptionalItem {
        shown: !menu.documentMode || menu.listItem
        text: qsTr("Outdent")
        iconName: "list-indent-decrease"
        shortcut: "Shift+Tab"
        onTriggered: menu.act((r) => menu.controller.outdent(r))
    }
    BMenuItem {
        text: qsTr("Move up")
        iconName: "move-up"
        shortcut: "Alt+Up"
        onTriggered: menu.act((r) => menu.controller.moveUp(r))
    }
    BMenuItem {
        text: qsTr("Move down")
        iconName: "move-down"
        shortcut: "Alt+Down"
        onTriggered: menu.act((r) => menu.controller.moveDown(r))
    }
    BMenuSeparator {}
    BMenuItem {
        text: qsTr("Delete block")
        iconName: "trash-2"
        danger: true
        onTriggered: menu.act((r) => menu.controller.deleteBlock(r))
    }
}
