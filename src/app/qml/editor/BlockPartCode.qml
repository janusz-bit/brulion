pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartCode — a code block's view, made by BlockDelegate's `content` Loader through
// BlockParts.part("Code") (blockviews.h: one component for every block of every editor, not one in
// each block); its block is BlockViews.ancestor() (the Loader, then the block).
CodeBlockView {
    id: part
    blockItem: BlockViews.ancestor(part, 2)
}
