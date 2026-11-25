pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartRule — the rule of a block that has one, made by a BlockDelegate Loader through
// BlockParts.part("Rule") (blockviews.h: one component for every block of every editor, not one in
// each block). It finds its block once, when it is made: BlockViews.ancestor() (the Loader, then
// the block).
Item {
    id: rulePart
    final property BlockDelegate block: BlockViews.ancestor(rulePart, 2) as BlockDelegate
    implicitHeight: rulePart.block.editing ? ruleText.implicitHeight : rulePart.block.editor.lineHeight
    height: implicitHeight
    Rectangle {
        visible: !rulePart.block.editing
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 1
        color: Theme.border
    }
    TapHandler {
        enabled: !rulePart.block.editing
        onTapped: rulePart.block.editOnTap(-1)
    }
    BlockText {
        id: ruleText
        visible: rulePart.block.editing
        width: parent.width
        blockItem: rulePart.block
    }
}
