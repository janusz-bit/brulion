pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartTable — a table block's view, made by BlockDelegate's `content` Loader through
// BlockParts.part("Table") (blockviews.h: one component for every block of every editor, not one in
// each block); its block is BlockViews.ancestor() (the Loader, then the block).
TableBlockView {
    id: part
    blockItem: BlockViews.ancestor(part, 2)
}
