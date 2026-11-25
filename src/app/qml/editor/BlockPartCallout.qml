pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartCallout — the frame of a callout or quote of a block that has one, made by a BlockDelegate Loader through
// BlockParts.part("Callout") (blockviews.h: one component for every block of every editor, not one in
// each block). It finds its block once, when it is made: BlockViews.ancestor() (the Loader, then
// the block).
Rectangle {
    id: frame
    final property BlockDelegate block: BlockViews.ancestor(frame, 2) as BlockDelegate
    readonly property bool callout: frame.block.view === "callout"
    readonly property string type: frame.block.rowData.calloutType
    readonly property color tint: {
        switch (type) {
        case "tip": case "success": case "check": case "done": return Theme.success
        case "warning": case "caution": case "attention": return Theme.warning
        case "danger": case "error": case "bug": case "failure": return Theme.marginRed // whatever the accent
        case "todo": case "info": return Theme.info
        case "quote": case "cite": return Theme.fgFaint
        default: return Theme.info
        }
    }
    implicitHeight: inner.implicitHeight + (callout ? 2 * Theme.space3 : 0)
    height: implicitHeight
    radius: callout ? Theme.radius : 0
    color: callout ? tint.alpha(Theme.dark ? 0.12 : 0.10) : "transparent"
    Rectangle {
        width: 3
        height: parent.height
        radius: 1.5
        color: frame.callout ? frame.tint : Theme.borderStrong
    }
    Item {
        id: inner
        x: Theme.space4
        y: frame.callout ? Theme.space3 : 0
        width: parent.width - x - (frame.callout ? Theme.space3 : 0)
        implicitHeight: text.implicitHeight
        BlockText {
            id: text
            width: parent.width
            blockItem: frame.block
            quoted: !frame.callout
        }
    }
}
