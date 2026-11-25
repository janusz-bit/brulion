pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockSelectionGrip — a round grip at a block selection's start (top left) or end (bottom
// right), 44 px to hold (touch selection mode): the finger moves that end of the selection (the
// editor follows it, also when the block scrolls away). BlockPartGripStart / BlockPartGripEnd, made
// by BlockDelegate's Loaders through BlockParts.part(); it finds its block once, when it is made
// (BlockViews.ancestor(): the Loader, then the block). Its texts keep BlockDelegate's translation
// context.
Item {
    id: grip
    final property BlockDelegate block: BlockViews.ancestor(grip, 2) as BlockDelegate
    property bool atStart: true
    objectName: atStart ? "selectionGripStart" : "selectionGripEnd"
    width: Theme.touchTarget
    height: Theme.touchTarget
    Rectangle {
        anchors.centerIn: parent
        width: 18
        height: 18
        radius: 9
        color: Theme.link
        border.width: 2
        border.color: Theme.bg
    }
    MouseArea {
        anchors.fill: parent
        preventStealing: true
        onPressed: grip.block.editor.beginGripDrag(grip.atStart)
    }
    Accessible.role: Accessible.Button
    Accessible.name: atStart ? qsTranslate("BlockDelegate", "Start of the selection") : qsTranslate("BlockDelegate", "End of the selection")
}
