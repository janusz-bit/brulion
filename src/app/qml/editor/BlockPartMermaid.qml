pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartMermaid — a Mermaid diagram's view, made by BlockDelegate's `content` Loader through
// BlockParts.part("Mermaid") (blockviews.h: one component for every block of every editor, not one in
// each block); its block is BlockViews.ancestor() (the Loader, then the block).
MermaidBlockView {
    id: part
    blockItem: BlockViews.ancestor(part, 2)
}
