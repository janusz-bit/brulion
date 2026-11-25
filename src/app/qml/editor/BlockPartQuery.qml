pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartQuery — a query block's view, made by BlockDelegate's `content` Loader through
// BlockParts.part("Query") (blockviews.h: one component for every block of every editor, not one in
// each block); its block is BlockViews.ancestor() (the Loader, then the block).
QueryBlockView {
    id: part
    blockItem: BlockViews.ancestor(part, 2)
}
