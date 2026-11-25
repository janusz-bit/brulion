pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartNumber — an ordered list item's number (document mode), made by a BlockDelegate Loader through
// BlockParts.part("Number") (blockviews.h: one component for every block of every editor, not one in
// each block). It finds its block once, when it is made: BlockViews.ancestor() (the Loader, then
// the block).
Text {
    id: number
    final property BlockDelegate block: BlockViews.ancestor(number, 2) as BlockDelegate
    readonly property real gap: Math.round(number.block.editor.listIndent * 0.25)
    // Rich text like the item's own rendered text: the same line layout, the same baseline.
    text: TextHelper.escapeHtml(number.block.listMarker)
    textFormat: Text.RichText
    font.family: Theme.fontEditor
    font.pixelSize: number.block.editor.fontSize
    font.features: { "tnum": 1 }
    lineHeightMode: Text.FixedHeight
    lineHeight: number.block.editor.lineHeight
    color: number.block.closedTask ? Theme.fgFaint : Theme.fgMuted
    Accessible.ignored: true
    // The hit area, as the bullet's: a touch target (24 px with a mouse) around the number,
    // its right edge at the text column (with no room left of it, under the first
    // letters, which edit the item as well).
    Item {
        readonly property real size: Theme.touchUi ? Theme.touchTarget : 24
        readonly property real textEdge: number.width + number.gap
        width: Math.max(size, textEdge)
        height: Math.max(size, number.height)
        x: Math.max(-(number.parent ? number.parent.x : 0), textEdge - width)
        y: (number.height - height) / 2
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: number.block.editOnTap(0)
            onLongPressed: number.block.openMenu(number, 0, number.height)
        }
        BlockMenuTap { onTapped: number.block.openMenu(number, 0, number.height) }
    }
}
