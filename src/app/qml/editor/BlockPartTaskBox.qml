pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartTaskBox — the task checkbox of a block that has one, made by a BlockDelegate Loader through
// BlockParts.part("TaskBox") (blockviews.h: one component for every block of every editor, not one in
// each block). It finds its block once, when it is made: BlockViews.ancestor() (the Loader, then
// the block).
TaskBox {
    id: box
    final property BlockDelegate block: BlockViews.ancestor(box, 2) as BlockDelegate
    objectName: "taskBox"
    status: box.block.taskStatus
    onToggled: box.block.controller.toggleTask(box.block.index)
    // The editor's status menu (it follows the block while it is open).
    ownMenu: false
    onMenuRequested: box.block.editor.openTaskStatusMenu(box.block.index, box, 0, box.height)
}
