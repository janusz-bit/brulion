pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartFlash — a block's flash (a link to it was followed), made by a BlockDelegate Loader through
// BlockParts.part("Flash") (blockviews.h: one component for every block of every editor, not one in
// each block). It finds its block once, when it is made: BlockViews.ancestor() (the Loader, then
// the block).
Rectangle {
    id: flashFrame
    final property BlockDelegate block: BlockViews.ancestor(flashFrame, 2) as BlockDelegate
    objectName: "blockFlash"
    radius: Theme.radiusSm
    color: Theme.mark
    SequentialAnimation on opacity {
        NumberAnimation { from: 0; to: 1; duration: Theme.reducedMotion ? 0 : 150; easing.type: Theme.easing }
        PauseAnimation { duration: 700 }
        NumberAnimation { to: 0; duration: Theme.reducedMotion ? 0 : 350; easing.type: Theme.easing }
        // (Not a ScriptAction: its script is not compiled, and qmlcachegen skipped the
        // rest of the file after it.)
        onFinished: flashFrame.block.flashing = false
    }
}
