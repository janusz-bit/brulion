pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// DrawingSelectionBar — what can be done with the selected elements of a drawing (DrawingView;
// docs/UI.md "Drawing editor"): a floating bar centred above the selection (below it when there is
// no room, always inside the view) with the pen's colours (recolour; on narrow windows one swatch
// opening them), "Styl" (DrawingStylePanel: fill, lines, opacity, corners, arrowheads, text size),
// "Duplikuj", "Na wierzch", "Na spód" (in "Więcej" on narrow windows), "Więcej" (copy, cut — as
// Excalidraw's clipboard —, turn by 90°, mirror, group, ungroup) and "Usuń". Shown while something is
// selected and nothing is being drawn, moved or written; touch-sized on touch screens. Its parent
// is the paper's area: the bar takes the presses on it, everything else goes to the paper.
// The buttons take no keyboard focus (the view's keys go on working).
Item {
    id: root

    required property DrawingCanvas canvas
    /// The pen's colours: [{ color, label }].
    property var colors: []

    readonly property bool shown: canvas.visible && canvas.selectionCount > 0 && !canvas.busy && !canvas.textEditing

    /// A stored colour as the paper shows it now (re-evaluated when the paper turns dark).
    function displayed(color) {
        return root.canvas.dark ? root.canvas.displayColor(color) : color
    }

    visible: shown

    /// The style panel, "Więcej" (route.open, screenshots).
    function openStyle() { stylePanel.open() }
    function openMore() { moreMenu.popup(moreButton, 0, moreButton.height) }

    component Separator: Rectangle {
        width: 1
        height: Theme.controlHeight - 8
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        color: Theme.borderSubtle
    }

    BShadow {
        target: frame
        level: 2
    }

    Rectangle {
        id: frame
        objectName: "drawingSelectionBar"
        // (Clear of the frame's handles.)
        readonly property rect selection: root.canvas.selectionOuterRect
        readonly property real margin: Theme.space2
        width: Math.min(row.implicitWidth + 2 * Theme.space1, root.width - 2 * margin)
        height: row.implicitHeight + 2 * Theme.space1
        x: Math.max(margin, Math.min(root.width - width - margin, selection.x + selection.width / 2 - width / 2))
        y: {
            const above = selection.y - height - margin
            if (above >= margin)
                return above
            return Math.max(margin, Math.min(root.height - height - margin, selection.y + selection.height + margin))
        }
        radius: Theme.radiusLg
        color: Theme.bgElevated
        border.width: 1
        border.color: Theme.borderSubtle
        clip: true

        // The presses between the buttons stay on the bar (no lasso under it).
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        Row {
            id: row
            x: Theme.space1
            y: Theme.space1
            spacing: 2

            Repeater {
                model: Theme.compact ? [] : root.colors
                delegate: BColorSwatch {
                    required property var modelData
                    objectName: "drawingSelectionColor" + modelData.color.slice(1)
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    color: root.displayed(modelData.color)
                    label: modelData.label
                    size: 22
                    focusPolicy: Qt.NoFocus
                    onClicked: root.canvas.recolorSelection(modelData.color)
                }
            }
            BColorSwatch {
                id: colorButton
                objectName: "drawingSelectionColour"
                visible: Theme.compact
                anchors.verticalCenter: parent.verticalCenter
                color: root.displayed(root.canvas.color)
                label: qsTr("Colour")
                size: 26
                focusPolicy: Qt.NoFocus
                onClicked: colorPopup.open()
            }
            Separator {}
            BIconButton {
                id: styleButton
                objectName: "drawingSelectionStyle"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "sliders-horizontal"
                label: qsTr("Style")
                focusPolicy: Qt.NoFocus
                onClicked: stylePanel.open()
            }
            BIconButton {
                objectName: "drawingSelectionDuplicate"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "copy"
                label: qsTr("Duplicate")
                shortcut: "Ctrl+D"
                focusPolicy: Qt.NoFocus
                onClicked: root.canvas.duplicateSelection()
            }
            BIconButton {
                objectName: "drawingSelectionFront"
                visible: !Theme.compact
                anchors.verticalCenter: parent.verticalCenter
                iconName: "bring-to-front"
                label: qsTr("Bring to front")
                shortcut: "Ctrl+Shift+]"
                focusPolicy: Qt.NoFocus
                onClicked: root.canvas.reorderSelection("front")
            }
            BIconButton {
                objectName: "drawingSelectionBack"
                visible: !Theme.compact
                anchors.verticalCenter: parent.verticalCenter
                iconName: "send-to-back"
                label: qsTr("Send to back")
                shortcut: "Ctrl+Shift+["
                focusPolicy: Qt.NoFocus
                onClicked: root.canvas.reorderSelection("back")
            }
            BIconButton {
                id: moreButton
                objectName: "drawingSelectionMore"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "ellipsis"
                label: qsTr("More")
                focusPolicy: Qt.NoFocus
                onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
            }
            BIconButton {
                objectName: "drawingSelectionDelete"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "trash-2"
                label: qsTr("Delete")
                shortcut: "Delete"
                focusPolicy: Qt.NoFocus
                onClicked: root.canvas.deleteSelection()
            }
        }
    }

    DrawingStylePanel {
        id: stylePanel
        parent: frame
        canvas: root.canvas
        area: root
        barHost: frame
    }

    BMenu {
        id: moreMenu
        objectName: "drawingSelectionMenu"
        BMenuItem {
            objectName: "drawingSelectionCopy"
            text: qsTr("Copy")
            iconName: "copy"
            shortcut: "Ctrl+C"
            onTriggered: root.canvas.copySelection()
        }
        BMenuItem {
            objectName: "drawingSelectionCut"
            text: qsTr("Cut")
            iconName: "scissors"
            shortcut: "Ctrl+X"
            onTriggered: root.canvas.cutSelection()
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "drawingSelectionRotate"
            text: qsTr("Rotate by 90°")
            iconName: "rotate-cw"
            onTriggered: root.canvas.rotateSelection(90)
        }
        BMenuItem {
            objectName: "drawingSelectionFlipH"
            text: qsTr("Flip horizontally")
            iconName: "flip-horizontal-2"
            shortcut: "Shift+H"
            onTriggered: root.canvas.flipSelection("horizontal")
        }
        BMenuItem {
            objectName: "drawingSelectionFlipV"
            text: qsTr("Flip vertically")
            iconName: "flip-vertical-2"
            shortcut: "Shift+V"
            onTriggered: root.canvas.flipSelection("vertical")
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "drawingSelectionGroup"
            text: qsTr("Group")
            iconName: "group"
            shortcut: "Ctrl+G"
            enabled: root.canvas.selectionStyle.canGroup === true
            onTriggered: root.canvas.groupSelection()
        }
        BMenuItem {
            objectName: "drawingSelectionUngroup"
            text: qsTr("Ungroup")
            iconName: "ungroup"
            shortcut: "Ctrl+Shift+G"
            enabled: root.canvas.selectionStyle.canUngroup === true
            onTriggered: root.canvas.ungroupSelection()
        }
        BMenuSeparator { visible: Theme.compact; height: visible ? implicitHeight : 0 }
        BMenuItem {
            visible: Theme.compact
            height: visible ? implicitHeight : 0
            text: qsTr("Bring to front")
            iconName: "bring-to-front"
            onTriggered: root.canvas.reorderSelection("front")
        }
        BMenuItem {
            visible: Theme.compact
            height: visible ? implicitHeight : 0
            text: qsTr("Send to back")
            iconName: "send-to-back"
            onTriggered: root.canvas.reorderSelection("back")
        }
    }

    // Narrow windows: the colours behind one swatch.
    T.Popup {
        id: colorPopup
        objectName: "drawingSelectionColorPopup"
        parent: frame
        x: Math.max(-frame.x + Theme.space2, Math.min(root.width - frame.x - width - Theme.space2,
                                                      colorButton.x - width / 2))
        y: frame.y > height + Theme.space2 ? -height - Theme.space2 : frame.height + Theme.space2
        implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                contentWidth + leftPadding + rightPadding)
        implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                                 contentHeight + topPadding + bottomPadding)
        padding: Theme.space3
        modal: true
        dim: false
        focus: true
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
        background: Item {
            BShadow { target: popupFrame; level: 2 }
            Rectangle {
                id: popupFrame
                anchors.fill: parent
                color: Theme.bgElevated
                radius: Theme.radiusLg
                border.width: 1
                border.color: Theme.borderSubtle
            }
        }
        contentItem: Grid {
            columns: 6
            spacing: 2
            Repeater {
                model: root.colors
                delegate: BColorSwatch {
                    required property var modelData
                    color: root.displayed(modelData.color)
                    label: modelData.label
                    focusPolicy: Qt.NoFocus
                    onClicked: {
                        root.canvas.recolorSelection(modelData.color)
                        colorPopup.close()
                    }
                }
            }
        }
    }
}
