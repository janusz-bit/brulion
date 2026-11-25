pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartChart — a chart block's view, made by BlockDelegate's `content` Loader through
// BlockParts.part("Chart") (blockviews.h: one component for every block of every editor, not one in
// each block); its block is BlockViews.ancestor() (the Loader, then the block).
ChartBlockView {
    id: part
    blockItem: BlockViews.ancestor(part, 2)
}
