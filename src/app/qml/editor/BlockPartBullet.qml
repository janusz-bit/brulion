pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartBullet — the bullet of a block that has one, made by a BlockDelegate Loader through
// BlockParts.part("Bullet") (blockviews.h: one component for every block of every editor, not one in
// each block). It finds its block once, when it is made: BlockViews.ancestor() (the Loader, then
// the block). Its texts keep BlockDelegate's translation context.
Item {
    id: bullet
    final property BlockDelegate block: BlockViews.ancestor(bullet, 2) as BlockDelegate
    /// The bullet's centre in the hit area.
    readonly property real dotX: bullet.block.bulletX - (bullet.parent ? bullet.parent.x : 0)
    Accessible.role: bullet.block.doc ? Accessible.Graphic : Accessible.Button
    Accessible.name: bullet.block.doc ? qsTranslate("BlockDelegate", "List item") : qsTranslate("BlockDelegate", "Zoom into block")

    // Folded blocks show a halo around the bullet (Logseq style); hover shows it too.
    Rectangle {
        x: bullet.dotX - width / 2
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        radius: 8
        color: bullet.block.collapsed ? Theme.bgPressed : Theme.bgHover
        visible: !bullet.block.doc && (bullet.block.collapsed || bulletHover.hovered)
    }
    Rectangle {
        // The same round bullet at every depth (a document too: nesting shows in the indent).
        x: bullet.dotX - width / 2
        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: width
        radius: width / 2
        color: bullet.block.closedTask ? Theme.fgFaint : bulletHover.hovered && !bullet.block.doc ? Theme.fg : Theme.fgMuted
    }
    HoverHandler {
        id: bulletHover
        cursorShape: bullet.block.doc ? Qt.ArrowCursor : Qt.PointingHandCursor
        enabled: !Theme.touchUi
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        // A document has no zoom: the bullet is part of the text, a click edits the item.
        // (The touch selection mode: a tap moves the selection's end here.)
        onTapped: {
            if (bullet.block.editor.blockTapped(bullet.block.index))
                return
            if (bullet.block.doc)
                bullet.block.editor.startEditing(bullet.block.index, 0)
            else
                bullet.block.controller.zoomInto(bullet.block.index)
        }
        onLongPressed: bullet.block.openMenu(bullet, bullet.dotX, bullet.height)
    }
    BlockMenuTap { onTapped: bullet.block.openMenu(bullet, bullet.dotX, bullet.height) }
}
