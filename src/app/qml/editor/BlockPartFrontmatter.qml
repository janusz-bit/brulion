pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartFrontmatter — the front matter's view, made by BlockDelegate's `content` Loader through
// BlockParts.part("Frontmatter") (blockviews.h: one component for every block of every editor, not one in
// each block); its block is BlockViews.ancestor() (the Loader, then the block).
FrontmatterView {
    id: part
    blockItem: BlockViews.ancestor(part, 2)
}
