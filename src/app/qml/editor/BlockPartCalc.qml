pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartCalc — a calc block's view, made by BlockDelegate's `content` Loader through
// BlockParts.part("Calc") (blockviews.h: one component for every block of every editor, not one in
// each block); its block is BlockViews.ancestor() (the Loader, then the block).
CalcBlockView {
    id: part
    blockItem: BlockViews.ancestor(part, 2)
}
