pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// MobileEditToolbar — the editing toolbar of touch UIs, docked on the virtual keyboard: BlockEditor
// places it on the bottom edge of its dock host (the editor on a page, the journal stream's frame
// for its days), which the shell keeps on the keyboard's top (KeyboardInsets, docs/UI.md
// "On-screen keyboard"); it stays there when the system hides the keyboard while editing goes on.
// The tools follow `mode` (BlockEditor.toolbarMode):
//   "block"   outdent, indent, move up/down, task (a tap cycles the status, a long press opens the
//             status menu), [[, #, /, date, reminder, picture, undo, redo, hide keyboard. In the
//             document mode the writing tools come first — a list button (paragraph ↔ list
//             item), task, [[, #, / — then outdent / indent (list items only) and move up/down, so
//             a phone's first screen of buttons holds no outline tools that are disabled in a
//             paragraph;
//   "cell"    a table cell: undo, redo, next cell, hide keyboard;
//   "source"  source mode: [[, #, /, undo, redo (the source's own), hide keyboard.
// "Hide keyboard" ends the editing (the toolbar goes with it). Buttons never take focus, so the
// caret stays where it is; a tap beside them stays on the bar. Scrolls sideways when the window is
// narrower than the buttons. Each tool set is made the first time its mode is used and kept (the
// bar is hidden, not rebuilt, between two edits: the scroll position stays).
// Extending it: a new button goes into its mode's row; another tool set (a selection's) is a new
// mode of BlockEditor.toolbarMode with its own row here, docked in the same slot.
Rectangle {
    id: bar

    property BlockEditor editor
    /// "block" | "cell" | "source" (see above).
    readonly property string mode: editor ? editor.toolbarMode : ""
    /// The tools shown: `mode`, kept while the bar is hidden between two edits.
    property string shownMode: ""
    readonly property PageController controller: editor ? editor.controller : null
    readonly property int row: controller ? controller.editingRow : -1
    readonly property InlineEditor edit: editor ? editor.activeEditor : null
    /// Source mode: its text editor (insertions, its own undo).
    readonly property TextEdit sourceEdit: editor && mode === "source" ? editor.sourceTextEdit : null
    /// The table whose cell is being edited.
    readonly property TableBlockView table: editor && mode === "cell" ? editor.editingTable : null
    readonly property bool documentMode: editor !== null && editor.documentMode
    /// The edited block is a list item (the document mode indents only those).
    readonly property bool listItem: edit !== null && edit.block !== null && edit.block.listItem

    color: Theme.bgElevated

    Accessible.role: Accessible.ToolBar
    Accessible.name: qsTr("Editing tools")

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.border
    }

    onModeChanged: showMode()
    Component.onCompleted: showMode()
    // A tool set is made when its mode first comes (latched here: a Loader loads synchronously
    // inside its `active` binding) and then kept.
    function showMode() {
        if (mode === "")
            return
        shownMode = mode
        if (mode === "block")
            blockLoader.active = true
        else if (mode === "cell")
            cellLoader.active = true
        else if (mode === "source")
            sourceLoader.active = true
    }

    /// Commit the word the keyboard is composing before a button acts on the block: otherwise
    /// it is dropped when the controller resets the block's text.
    function commitComposition() {
        if (edit)
            EditorInput.commitPreedit(edit)
    }

    /// Insert `s` at the caret of the block (or of the source in source mode).
    function insertText(s) {
        const target = bar.mode === "source" ? bar.sourceEdit : bar.edit
        if (!target || target.readOnly)
            return
        if (target === bar.edit)
            commitComposition()
        else
            EditorInput.commitPreedit(target)
        if (target.selectedText !== "")
            target.remove(target.selectionStart, target.selectionEnd)
        const before = target.cursorPosition > 0 ? target.getText(target.cursorPosition - 1, target.cursorPosition) : ""
        const lead = (s === "#" || s === "/") && before !== "" && before !== " " && before !== "\n" ? " " : ""
        target.insert(target.cursorPosition, lead + s)
    }
    /// Close the keyboard and end the editing (the toolbar goes with it).
    function hideKeyboard() {
        if (bar.mode === "block") {
            bar.commitComposition()
            Qt.inputMethod.hide()
            bar.editor.stopEditing(false)
        } else if (bar.mode === "cell") {
            // The composed word first: hiding the keyboard may drop it.
            if (bar.table && bar.table.cellInput)
                EditorInput.commitPreedit(bar.table.cellInput)
            Qt.inputMethod.hide()
            if (bar.table)
                bar.table.closeCell()
        } else if (bar.mode === "source") {
            if (bar.sourceEdit)
                EditorInput.commitPreedit(bar.sourceEdit)
            Qt.inputMethod.hide()
            bar.editor.releaseSourceKeyboard()
        }
    }

    component ToolButton: BIconButton {
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        focusPolicy: Qt.NoFocus
        size: "small"
        enabled: bar.row >= 0
    }
    component Separator: Rectangle {
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        width: 1
        height: bar.height * 0.5
        color: Theme.border
    }
    /// Indent, outdent, move: the outline's first tools; in a document after the writing tools.
    component StructureTools: Row {
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        ToolButton {
            iconName: "list-indent-decrease"
            label: qsTr("Outdent")
            enabled: bar.row >= 0 && (!bar.documentMode || bar.listItem)
            onClicked: { bar.commitComposition(); bar.controller.outdent(bar.row) }
        }
        ToolButton {
            iconName: "list-indent-increase"
            label: qsTr("Indent")
            enabled: bar.row >= 0 && (!bar.documentMode || bar.listItem)
            onClicked: { bar.commitComposition(); bar.controller.indent(bar.row) }
        }
        ToolButton {
            iconName: "move-up"
            label: qsTr("Move up")
            onClicked: { bar.commitComposition(); bar.controller.moveUp(bar.row) }
        }
        ToolButton {
            iconName: "move-down"
            label: qsTr("Move down")
            onClicked: { bar.commitComposition(); bar.controller.moveDown(bar.row) }
        }
    }
    /// Task, [[, #, /.
    component WritingTools: Row {
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        ToolButton {
            id: taskButton
            objectName: "toolbarTask"
            iconName: "square-check-big"
            label: qsTr("Task")
            // A tap: the next status (Ctrl+Enter); held down: the status menu (instead of the
            // label's tooltip).
            showToolTip: false
            onClicked: { bar.commitComposition(); bar.controller.cycleTask(bar.row) }
            onPressAndHold: { bar.commitComposition(); bar.editor.openTaskStatusMenuAbove(bar.row, taskButton) }
        }
        ToolButton {
            iconName: "link"
            label: qsTr("Link to page")
            onClicked: bar.insertText("[[")
        }
        ToolButton {
            iconName: "hash"
            label: qsTr("Tag")
            onClicked: bar.insertText("#")
        }
        ToolButton {
            iconName: "slash"
            label: qsTr("Commands")
            onClicked: bar.insertText("/")
        }
    }

    // Taps between and beside the buttons stay on the bar (the Flickable takes them only while
    // the buttons are wider than the bar): they went through to what the bar covers — a block
    // started being edited, the source's caret moved under the bar.
    MouseArea {
        objectName: "editToolbarBackground"
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
    }

    Flickable {
        id: flick
        objectName: "editToolbarFlick"
        anchors.fill: parent
        anchors.topMargin: 1
        contentWidth: buttons.width
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        clip: true

        Row {
            id: buttons
            height: parent.height
            leftPadding: Theme.space1
            rightPadding: Theme.space1
            spacing: 0

            // One tool set at a time (the Loaders take their Row's width; hidden ones take no
            // room). Made when first needed (showMode()), then kept.
            Loader {
                id: blockLoader
                height: parent.height
                active: false
                visible: bar.shownMode === "block"
                sourceComponent: blockTools
            }
            Loader {
                id: cellLoader
                height: parent.height
                active: false
                visible: bar.shownMode === "cell"
                sourceComponent: cellTools
            }
            Loader {
                id: sourceLoader
                height: parent.height
                active: false
                visible: bar.shownMode === "source"
                sourceComponent: sourceTools
            }
        }
    }

    // ---- A block being edited ------------------------------------------------------------------
    Component {
        id: blockTools
        Row {
            objectName: "toolbarBlockTools"

            // Document: list, task, [[, #, / | outdent, indent, up, down | …
            // Outline:  outdent, indent, up, down | task, [[, #, / …
            ToolButton {
                objectName: "toolbarList"
                visible: bar.documentMode
                iconName: "list"
                label: qsTr("Bulleted list")
                checked: bar.listItem // not checkable: the block's kind says it
                onClicked: { bar.commitComposition(); bar.controller.toggleList(bar.row) }
            }
            WritingTools { objectName: "toolbarWritingDocument"; visible: bar.documentMode }
            Separator { visible: bar.documentMode }
            StructureTools { objectName: "toolbarStructure" }
            Separator {}
            WritingTools { objectName: "toolbarWriting"; visible: !bar.documentMode }
            ToolButton {
                iconName: "calendar"
                label: qsTr("Due date")
                onClicked: { bar.commitComposition(); bar.editor.dialogs.pickDate(bar.row, "due") }
            }
            ToolButton {
                iconName: "bell"
                label: qsTr("Reminder")
                onClicked: { bar.commitComposition(); bar.editor.dialogs.createReminder(bar.row, false) }
            }
            ToolButton {
                iconName: "image"
                label: qsTr("Insert picture")
                onClicked: {
                    bar.commitComposition()
                    bar.editor.dialogs.pickImage(bar.row, bar.edit ? bar.edit.cursorPosition : -1)
                }
            }
            Separator {}
            ToolButton {
                iconName: "undo-2"
                label: qsTr("Undo")
                enabled: bar.controller !== null && bar.controller.canUndo
                onClicked: { bar.commitComposition(); bar.controller.undo() }
            }
            ToolButton {
                iconName: "redo-2"
                label: qsTr("Redo")
                enabled: bar.controller !== null && bar.controller.canRedo
                onClicked: { bar.commitComposition(); bar.controller.redo() }
            }
            ToolButton {
                objectName: "toolbarHideKeyboard"
                iconName: "keyboard-off"
                label: qsTr("Hide keyboard")
                onClicked: bar.hideKeyboard()
            }
        }
    }

    // ---- A table cell being edited -------------------------------------------------------------
    Component {
        id: cellTools
        Row {
            objectName: "toolbarCellTools"
            ToolButton {
                objectName: "toolbarCellUndo"
                iconName: "undo-2"
                label: qsTr("Undo")
                enabled: bar.table !== null && bar.controller !== null && bar.controller.canUndo
                onClicked: bar.table.undoInCell()
            }
            ToolButton {
                objectName: "toolbarCellRedo"
                iconName: "redo-2"
                label: qsTr("Redo")
                enabled: bar.table !== null && bar.controller !== null && bar.controller.canRedo
                onClicked: bar.table.redoInCell()
            }
            Separator {}
            ToolButton {
                objectName: "toolbarNextCell"
                iconName: "arrow-right"
                label: qsTr("Next cell")
                enabled: bar.table !== null
                onClicked: bar.table.nextCell()
            }
            Separator {}
            ToolButton {
                objectName: "toolbarHideKeyboard"
                iconName: "keyboard-off"
                label: qsTr("Hide keyboard")
                enabled: bar.table !== null
                onClicked: bar.hideKeyboard()
            }
        }
    }

    // ---- Source mode ---------------------------------------------------------------------------
    Component {
        id: sourceTools
        Row {
            objectName: "toolbarSourceTools"
            ToolButton {
                objectName: "toolbarSourceLink"
                iconName: "link"
                label: qsTr("Link to page")
                enabled: bar.sourceEdit !== null
                onClicked: bar.insertText("[[")
            }
            ToolButton {
                objectName: "toolbarSourceTag"
                iconName: "hash"
                label: qsTr("Tag")
                enabled: bar.sourceEdit !== null
                onClicked: bar.insertText("#")
            }
            ToolButton {
                objectName: "toolbarSourceCommands"
                iconName: "slash"
                label: qsTr("Commands")
                enabled: bar.sourceEdit !== null
                onClicked: bar.insertText("/")
            }
            Separator {}
            ToolButton {
                objectName: "toolbarSourceUndo"
                iconName: "undo-2"
                label: qsTr("Undo")
                enabled: bar.sourceEdit !== null && bar.sourceEdit.canUndo
                onClicked: bar.sourceEdit.undo()
            }
            ToolButton {
                objectName: "toolbarSourceRedo"
                iconName: "redo-2"
                label: qsTr("Redo")
                enabled: bar.sourceEdit !== null && bar.sourceEdit.canRedo
                onClicked: bar.sourceEdit.redo()
            }
            Separator {}
            ToolButton {
                objectName: "toolbarHideKeyboard"
                iconName: "keyboard-off"
                label: qsTr("Hide keyboard")
                enabled: bar.sourceEdit !== null
                onClicked: bar.hideKeyboard()
            }
        }
    }

    // More buttons beyond the edge (a phone shows about nine of the fourteen): a chevron over a
    // fade says so, and tapping it scrolls there — the row otherwise ended at a random button with
    // no hint that reminder, undo or "hide keyboard" exist.
    component EdgeMore: Rectangle {
        id: more
        property bool atEnd: true
        signal activated()
        width: Theme.touchUi ? Theme.touchTarget : 32
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.alpha(Theme.bgElevated, more.atEnd ? 0 : 1) }
            GradientStop { position: 0.45; color: Theme.bgElevated }
            GradientStop { position: 1; color: Theme.alpha(Theme.bgElevated, more.atEnd ? 1 : 0) }
        }
        BIcon {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: more.atEnd ? parent.right : undefined
            anchors.left: more.atEnd ? undefined : parent.left
            anchors.margins: 4
            name: more.atEnd ? "chevron-right" : "chevron-left"
            size: Theme.iconSizeSm
            color: Theme.fgMuted
        }
        TapHandler { onTapped: more.activated() }
        Accessible.role: Accessible.Button
        Accessible.name: more.atEnd ? qsTr("More editing tools") : qsTr("First editing tools")
    }
    EdgeMore {
        anchors.top: flick.top
        anchors.bottom: flick.bottom
        anchors.right: flick.right
        visible: flick.interactive && flick.contentX + flick.width < flick.contentWidth - 1
        onActivated: flick.contentX = flick.contentWidth - flick.width
    }
    EdgeMore {
        anchors.top: flick.top
        anchors.bottom: flick.bottom
        anchors.left: flick.left
        atEnd: false
        visible: flick.interactive && flick.contentX > 1
        onActivated: flick.contentX = 0
    }
}
