pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartTransclusions — the embedded blocks of a block that has one, made by a BlockDelegate Loader through
// BlockParts.part("Transclusions") (blockviews.h: one component for every block of every editor, not one in
// each block). It finds its block once, when it is made: BlockViews.ancestor() (the Loader, then
// the block).
TransclusionView {
    id: transclusionPart
    // (Its own `block` is its blockItem's.)
    blockItem: BlockViews.ancestor(transclusionPart, 2)
    transclusions: transclusionPart.block.hasTransclusions ? transclusionPart.block.transclusions : transclusionPart.block.noItems
}
