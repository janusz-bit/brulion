pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartDateButton — "Add date" of a block that has one, made by a BlockDelegate Loader through
// BlockParts.part("DateButton") (blockviews.h: one component for every block of every editor, not one in
// each block). It finds its block once, when it is made: BlockViews.ancestor() (the Loader, then
// the block). Its texts keep BlockDelegate's translation context.
BIconButton {
    id: dateButton
    final property BlockDelegate block: BlockViews.ancestor(dateButton, 2) as BlockDelegate
    objectName: "taskDateButton"
    label: dateButton.block.addable ? qsTranslate("BlockDelegate", "Add a reminder or an alarm") : qsTranslate("BlockDelegate", "Add date")
    iconName: "calendar-plus"
    size: "small"
    // The edited text keeps the keyboard (the menu takes it while it is open).
    focusPolicy: Qt.NoFocus
    onClicked: {
        if (dateButton.block.addable)
            dateButton.block.editor.addDateTo(dateButton.block.index)
        else
            dateButton.block.editor.openQuickDateMenu(dateButton.block.index, dateButton)
    }
}
