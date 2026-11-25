pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartFoldArrow — the fold arrow of a block that has one, made by a BlockDelegate Loader through
// BlockParts.part("FoldArrow") (blockviews.h: one component for every block of every editor, not one in
// each block). It finds its block once, when it is made: BlockViews.ancestor() (the Loader, then
// the block). Its texts keep BlockDelegate's translation context.
Item {
    id: foldArrow
    final property BlockDelegate block: BlockViews.ancestor(foldArrow, 2) as BlockDelegate
    visible: foldArrow.block.collapsed || foldArrow.block.hovered || Theme.touchUi || arrowTap.pressed
    opacity: foldArrow.block.collapsed ? 0.9 : 0.6
    Accessible.role: Accessible.Button
    Accessible.name: foldArrow.block.collapsed ? qsTranslate("BlockDelegate", "Expand") : qsTranslate("BlockDelegate", "Collapse")

    BIcon {
        anchors.centerIn: parent
        name: "chevron-right"
        size: Theme.iconSizeSm
        color: arrowHover.hovered ? Theme.fg : Theme.fgMuted
        rotation: foldArrow.block.collapsed ? 0 : 90
        Behavior on rotation { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
    }
    // Touch: a 44 px hit area around the small arrow (the bullet, stacked above, keeps the
    // part that overlaps it).
    Item {
        anchors.centerIn: parent
        width: Theme.touchUi ? Theme.touchTarget : parent.width
        height: Theme.touchUi ? Theme.touchTarget : parent.height
        HoverHandler { id: arrowHover; cursorShape: Qt.PointingHandCursor }
        TapHandler {
            id: arrowTap
            onTapped: foldArrow.block.controller.toggleFold(foldArrow.block.index)
        }
    }
}
