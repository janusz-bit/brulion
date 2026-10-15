pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// MobileEditToolbar — the editing toolbar of touch UIs, sitting right above the virtual
// keyboard (Qt.inputMethod.keyboardRectangle; at the bottom when the window is resized for the
// keyboard): outdent, indent, move up/down, task, [[, #, /, date, reminder, picture, undo, redo,
// hide keyboard. Buttons never take focus, so the caret stays in the block being edited. Scrolls
// sideways when the window is narrower than the buttons.
Rectangle {
    id: bar

    property BlockEditor editor
    readonly property PageController controller: editor ? editor.controller : null
    readonly property int row: controller ? controller.editingRow : -1
    readonly property InlineEditor edit: editor ? editor.activeEditor : null
    /// Top of the virtual keyboard in the editor's coordinates (its height when hidden).
    readonly property real keyboardTop: {
        const kb = Qt.inputMethod.keyboardRectangle
        if (!Qt.inputMethod.visible || kb.height <= 0 || !editor)
            return editor ? editor.height : 0
        const p = editor.mapFromItem(null, 0, kb.y / Screen.devicePixelRatio)
        return Math.min(editor.height, p.y)
    }

    height: Theme.touchTarget + Theme.space2 + 1
    y: keyboardTop - height
    z: 10
    color: Theme.bgElevated

    Accessible.role: Accessible.ToolBar
    Accessible.name: qsTr("Editing tools")

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.border
    }

    /// Commit the word the keyboard is composing before a button acts on the block: otherwise
    /// it is dropped when the controller resets the block's text.
    function commitComposition() {
        if (edit)
            EditorInput.commitPreedit(edit)
    }

    function insertText(s) {
        if (!edit || edit.readOnly)
            return
        commitComposition()
        if (edit.selectedText !== "")
            edit.remove(edit.selectionStart, edit.selectionEnd)
        const before = edit.cursorPosition > 0 ? edit.text.charAt(edit.cursorPosition - 1) : ""
        const lead = (s === "#" || s === "/") && before !== "" && before !== " " && before !== "\n" ? " " : ""
        edit.insert(edit.cursorPosition, lead + s)
    }

    component ToolButton: BIconButton {
        focusPolicy: Qt.NoFocus
        size: "small"
        enabled: bar.row >= 0
    }

    Flickable {
        id: flick
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

            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "list-indent-decrease"
                label: qsTr("Outdent")
                onClicked: { bar.commitComposition(); bar.controller.outdent(bar.row) }
            }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "list-indent-increase"
                label: qsTr("Indent")
                onClicked: { bar.commitComposition(); bar.controller.indent(bar.row) }
            }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "move-up"
                label: qsTr("Move up")
                onClicked: { bar.commitComposition(); bar.controller.moveUp(bar.row) }
            }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "move-down"
                label: qsTr("Move down")
                onClicked: { bar.commitComposition(); bar.controller.moveDown(bar.row) }
            }
            Rectangle { width: 1; height: parent.height * 0.5; anchors.verticalCenter: parent.verticalCenter; color: Theme.border }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "square-check-big"
                label: qsTr("Task")
                onClicked: { bar.commitComposition(); bar.controller.cycleTask(bar.row) }
            }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "link"
                label: qsTr("Link to page")
                onClicked: bar.insertText("[[")
            }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "hash"
                label: qsTr("Tag")
                onClicked: bar.insertText("#")
            }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "slash"
                label: qsTr("Commands")
                onClicked: bar.insertText("/")
            }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "calendar"
                label: qsTr("Due date")
                onClicked: { bar.commitComposition(); bar.editor.dialogs.pickDate(bar.row, "due") }
            }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "bell"
                label: qsTr("Reminder")
                onClicked: { bar.commitComposition(); bar.editor.dialogs.createReminder(bar.row, false) }
            }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "image"
                label: qsTr("Insert picture")
                onClicked: {
                    bar.commitComposition()
                    bar.editor.dialogs.pickImage(bar.row, bar.edit ? bar.edit.cursorPosition : -1)
                }
            }
            Rectangle { width: 1; height: parent.height * 0.5; anchors.verticalCenter: parent.verticalCenter; color: Theme.border }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "undo-2"
                label: qsTr("Undo")
                enabled: bar.controller !== null && bar.controller.canUndo
                onClicked: { bar.commitComposition(); bar.controller.undo() }
            }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "redo-2"
                label: qsTr("Redo")
                enabled: bar.controller !== null && bar.controller.canRedo
                onClicked: { bar.commitComposition(); bar.controller.redo() }
            }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "keyboard-off"
                label: qsTr("Hide keyboard")
                onClicked: {
                    bar.commitComposition()
                    Qt.inputMethod.hide()
                    bar.editor.stopEditing(false)
                }
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
