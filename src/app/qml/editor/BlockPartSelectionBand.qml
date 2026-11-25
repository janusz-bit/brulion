pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartSelectionBand — the selection band of a block that has one, made by a BlockDelegate Loader through
// BlockParts.part("SelectionBand") (blockviews.h: one component for every block of every editor, not one in
// each block). It finds its block once, when it is made: BlockViews.ancestor() (the Loader, then
// the block).
Rectangle {
    id: bandPart
    final property BlockDelegate block: BlockViews.ancestor(bandPart, 2) as BlockDelegate
    objectName: "selectionBand"
    topLeftRadius: bandPart.block.bandStart ? Theme.radiusSm : 0
    topRightRadius: bandPart.block.bandStart ? Theme.radiusSm : 0
    bottomLeftRadius: bandPart.block.bandEnd ? Theme.radiusSm : 0
    bottomRightRadius: bandPart.block.bandEnd ? Theme.radiusSm : 0
    color: Theme.bgSelected
    border.width: bandPart.block.bandEdge === "single" ? 1 : 0
    border.color: Theme.focusRing.alpha(0.35)
    // A range: a line down its left edge marks the band as one.
    Rectangle {
        visible: bandPart.block.bandEdge !== "single"
        width: 2
        height: parent.height
        color: Theme.focusRing.alpha(0.6)
    }
}
