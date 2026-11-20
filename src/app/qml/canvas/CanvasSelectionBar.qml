pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CanvasSelectionBar — what can be done with the selected cards and edges (CanvasView; docs/UI.md
// "Canvas view"), a small bar floating above the selection (below it near the top edge): the
// colour (no colour, Obsidian's six presets, a hex colour of the file shown as it is), edit the
// text or the label, open the file or the web address, duplicate, group / ungroup, align and
// distribute, the arrowheads of edges (none, one way, both ways, the other way round), to the
// front / to the back, delete. Hidden while a gesture moves the selection.
Item {
    id: root

    required property CanvasEditor board
    /// The selection's bounds on screen (CanvasEditor.selectionScreenRect).
    property rect selection: board.selectionScreenRect
    readonly property string kind: board.selectionKind
    readonly property int nodeCount: board.selectedNodes.length
    readonly property int edgeCount: board.selectedEdges.length

    signal editRequested()
    signal openRequested(bool newPane)
    signal labelRequested()

    visible: board.selectionCount > 0 && !board.busy && board.editingNode === ""
    width: bubble.width
    height: bubble.height
    x: Math.max(Theme.space2, Math.min((parent ? parent.width : 400) - width - Theme.space2,
                                       selection.x + selection.width / 2 - width / 2))
    y: selection.y - height - Theme.space3 >= Theme.space2 ? selection.y - height - Theme.space3
                                                             : Math.min((parent ? parent.height : 400) - height - Theme.space2,
                                                                        selection.y + selection.height + Theme.space3)

    function openColors(): void { colorMenu.popup(colorButton, 0, colorButton.height) }

    component BarButton: BIconButton {
        y: parent ? (parent.height - height) / 2 : 0
        focusPolicy: Qt.NoFocus
        size: "small"
    }
    /// A colour of the menu: its swatch as the app shows it, checked while the selection has it.
    component ColorItem: BMenuItem {
        required property string value
        readonly property string swatchHex: value !== "" ? CanvasColors.hex(value, Theme.dark) : "transparent"
        swatch: swatchHex
        iconName: value === "" ? "circle" : ""
        checkable: true
        checked: root.board.selectionColor === value
        onTriggered: root.board.colorSelection(value)
    }

    BShadow { target: bubble; level: 2 }
    Rectangle {
        id: bubble
        width: buttons.width + 2 * Theme.space1
        height: buttons.height + 2 * Theme.space1
        radius: Theme.radiusLg
        color: Theme.bgElevated
        border.width: 1
        border.color: Theme.border

        Row {
            id: buttons
            x: Theme.space1
            y: Theme.space1
            spacing: 2

            Rectangle {
                id: colorButton
                objectName: "canvasColorButton"
                width: Theme.controlHeightSm
                height: Theme.controlHeightSm
                radius: Theme.radius
                color: colorHover.hovered ? Theme.bgHover : "transparent"
                Rectangle {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    radius: 8
                    readonly property string hex: CanvasColors.hex(root.board.selectionColor, Theme.dark)
                    color: hex !== "" ? hex : "transparent"
                    border.width: hex !== "" ? 0 : 1.5
                    border.color: Theme.fgMuted
                }
                HoverHandler { id: colorHover }
                TapHandler { onTapped: root.openColors() }
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Colour")
                BToolTip { visible: colorHover.hovered; text: qsTr("Colour") }
            }
            BarButton {
                objectName: "canvasEditButton"
                visible: root.nodeCount === 1 && root.edgeCount === 0 && (root.kind === "text" || root.kind === "group")
                label: root.kind === "group" ? qsTr("Rename the group") : qsTr("Edit text")
                iconName: "pencil"
                shortcut: "Enter"
                onClicked: root.editRequested()
            }
            BarButton {
                visible: root.edgeCount === 1 && root.nodeCount === 0
                label: qsTr("Label")
                iconName: "type"
                onClicked: root.labelRequested()
            }
            BarButton {
                objectName: "canvasOpenButton"
                visible: root.nodeCount === 1 && root.edgeCount === 0 && (root.kind === "file" || root.kind === "link")
                label: root.kind === "link" ? qsTr("Open the web page") : qsTr("Open")
                iconName: "external-link"
                onClicked: root.openRequested(false)
            }
            BarButton {
                id: arrowButton
                visible: root.edgeCount > 0
                label: qsTr("Arrowheads")
                iconName: "arrow-left-right"
                onClicked: arrowMenu.popup(arrowButton, 0, arrowButton.height)
            }
            BarButton {
                visible: root.nodeCount > 0
                label: qsTr("Duplicate")
                iconName: "copy-plus"
                shortcut: "Ctrl+D"
                onClicked: root.board.duplicateSelection()
            }
            BarButton {
                visible: root.nodeCount > 0 && root.kind !== "group"
                label: qsTr("Group")
                iconName: "group"
                shortcut: "Ctrl+G"
                onClicked: root.board.addGroup("")
            }
            BarButton {
                visible: root.kind === "group"
                label: qsTr("Ungroup")
                iconName: "ungroup"
                shortcut: "Ctrl+Shift+G"
                onClicked: root.board.ungroupSelection()
            }
            BarButton {
                id: alignButton
                visible: root.nodeCount > 1
                label: qsTr("Align")
                iconName: "align-start-vertical"
                onClicked: alignMenu.popup(alignButton, 0, alignButton.height)
            }
            BarButton {
                id: orderButton
                visible: root.nodeCount > 0
                label: qsTr("Arrange")
                iconName: "bring-to-front"
                onClicked: orderMenu.popup(orderButton, 0, orderButton.height)
            }
            BarButton {
                objectName: "canvasDeleteButton"
                label: qsTr("Delete")
                iconName: "trash-2"
                shortcut: "Del"
                onClicked: root.board.deleteSelection()
            }
        }
    }

    BMenu {
        id: colorMenu
        objectName: "canvasColorMenu"
        ColorItem { value: ""; text: qsTr("No colour") }
        ColorItem { value: "1"; text: qsTr("Red") }
        ColorItem { value: "2"; text: qsTr("Orange") }
        ColorItem { value: "3"; text: qsTr("Yellow") }
        ColorItem { value: "4"; text: qsTr("Green") }
        ColorItem { value: "5"; text: qsTr("Cyan") }
        ColorItem { value: "6"; text: qsTr("Purple") }
    }

    BMenu {
        id: arrowMenu
        BMenuItem {
            text: qsTr("No arrowheads")
            checkable: true
            checked: !root.board.selectionArrowStart && !root.board.selectionArrowEnd
            onTriggered: root.board.setArrows(false, false)
        }
        BMenuItem {
            text: qsTr("One way")
            checkable: true
            checked: !root.board.selectionArrowStart && root.board.selectionArrowEnd
            onTriggered: root.board.setArrows(false, true)
        }
        BMenuItem {
            text: qsTr("Both ways")
            checkable: true
            checked: root.board.selectionArrowStart && root.board.selectionArrowEnd
            onTriggered: root.board.setArrows(true, true)
        }
        BMenuSeparator {}
        BMenuItem { text: qsTr("Reverse"); iconName: "arrow-left-right"; onTriggered: root.board.reverseSelectedEdges() }
    }

    BMenu {
        id: alignMenu
        BMenuItem { text: qsTr("Align left"); iconName: "align-start-vertical"; onTriggered: root.board.alignSelection("left") }
        BMenuItem { text: qsTr("Align centres"); iconName: "align-center-vertical"; onTriggered: root.board.alignSelection("center") }
        BMenuItem { text: qsTr("Align right"); iconName: "align-end-vertical"; onTriggered: root.board.alignSelection("right") }
        BMenuItem { text: qsTr("Align top"); iconName: "align-start-horizontal"; onTriggered: root.board.alignSelection("top") }
        BMenuItem { text: qsTr("Align middles"); iconName: "align-center-horizontal"; onTriggered: root.board.alignSelection("middle") }
        BMenuItem { text: qsTr("Align bottom"); iconName: "align-end-horizontal"; onTriggered: root.board.alignSelection("bottom") }
        BMenuSeparator {}
        BMenuItem {
            text: qsTr("Space evenly across")
            iconName: "align-horizontal-distribute-center"
            enabled: root.nodeCount > 2
            onTriggered: root.board.distributeSelection("horizontal")
        }
        BMenuItem {
            text: qsTr("Space evenly down")
            iconName: "align-vertical-distribute-center"
            enabled: root.nodeCount > 2
            onTriggered: root.board.distributeSelection("vertical")
        }
    }

    BMenu {
        id: orderMenu
        BMenuItem { text: qsTr("Bring to front"); iconName: "bring-to-front"; shortcut: "Ctrl+Shift+]"; onTriggered: root.board.reorderSelection("front") }
        BMenuItem { text: qsTr("Bring forward"); shortcut: "Ctrl+]"; onTriggered: root.board.reorderSelection("forward") }
        BMenuItem { text: qsTr("Send backward"); shortcut: "Ctrl+["; onTriggered: root.board.reorderSelection("backward") }
        BMenuItem { text: qsTr("Send to back"); iconName: "send-to-back"; shortcut: "Ctrl+Shift+["; onTriggered: root.board.reorderSelection("back") }
    }
}
