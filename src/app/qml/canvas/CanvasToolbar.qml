pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CanvasToolbar — the canvas view's tools (CanvasView; docs/UI.md "Canvas view"): undo, redo; add a
// text card, a note (the page picker), a file (the system's file picker), a web address, a group;
// "Select" on touch screens (taps add to the selection, a drag draws a rubber band); zoom out, the
// zoom (a click: 100 %), zoom in, fit; "More": paste, select all, export as PNG / SVG, zoom to the
// selection. Above the board on wide windows, at the bottom on phones (in reach of the thumb);
// a row too long for the window scrolls sideways. The buttons take no keyboard focus.
Rectangle {
    id: bar

    required property CanvasEditor board
    property bool compact: false
    /// The bar lies at the bottom of the view: its hairline on top.
    property bool atBottom: compact

    signal addText()
    signal addNote()
    signal addFile()
    signal addLink()
    signal addGroup()
    signal pasteRequested()
    signal exportRequested(string format)

    readonly property int slot: Math.max(Theme.controlHeight, Theme.touchUi ? Theme.touchTarget : 0)
    implicitHeight: Theme.touchUi ? 56 : 46
    color: Theme.bgElevated

    function openMore(): void { moreMenu.popup(moreButton, 0, bar.atBottom ? -moreMenu.implicitHeight : moreButton.height) }
    function openAdd(): void { addMenu.popup(addButton, 0, bar.atBottom ? -addMenu.implicitHeight : addButton.height) }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        y: bar.atBottom ? 0 : parent.height - 1
        height: 1
        color: Theme.borderSubtle
    }

    component Separator: Rectangle {
        width: 1
        height: Theme.controlHeight - 8
        y: parent ? (parent.height - height) / 2 : 0
        color: Theme.borderSubtle
    }
    component BarButton: BIconButton {
        anchors.verticalCenter: parent.verticalCenter
        focusPolicy: Qt.NoFocus
        toolTipBelow: !bar.atBottom
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space2
        contentWidth: Math.max(width, row.width)
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        clip: true

        Row {
            id: row
            height: flick.height
            spacing: 2
            x: bar.compact ? Math.max(0, (flick.width - width) / 2) : 0

            BarButton {
                objectName: "canvasUndo"
                label: qsTr("Undo")
                iconName: "undo-2"
                shortcut: "Ctrl+Z"
                enabled: bar.board.canUndo
                onClicked: bar.board.undo()
            }
            BarButton {
                objectName: "canvasRedo"
                label: qsTr("Redo")
                iconName: "redo-2"
                shortcut: "Ctrl+Shift+Z"
                enabled: bar.board.canRedo
                onClicked: bar.board.redo()
            }
            Separator {}
            BarButton {
                objectName: "canvasAddText"
                label: qsTr("Add a card")
                iconName: "sticky-note"
                onClicked: bar.addText()
            }
            BarButton {
                objectName: "canvasAddNote"
                visible: !bar.compact
                label: qsTr("Add a note…")
                iconName: "file-text"
                onClicked: bar.addNote()
            }
            BarButton {
                objectName: "canvasAddFile"
                visible: !bar.compact
                label: qsTr("Add a file…")
                iconName: "paperclip"
                onClicked: bar.addFile()
            }
            BarButton {
                objectName: "canvasAddLink"
                visible: !bar.compact
                label: qsTr("Add a web address…")
                iconName: "globe"
                onClicked: bar.addLink()
            }
            BarButton {
                objectName: "canvasAddGroup"
                visible: !bar.compact
                label: bar.board.selectionCount > 0 ? qsTr("Group the selection") : qsTr("Add a group")
                iconName: "group"
                shortcut: "Ctrl+G"
                onClicked: bar.addGroup()
            }
            BarButton {
                id: addButton
                objectName: "canvasAddMenu"
                visible: bar.compact
                label: qsTr("Add")
                iconName: "plus"
                onClicked: bar.openAdd()
            }
            BarButton {
                objectName: "canvasSelectMode"
                visible: Theme.touchUi
                label: qsTr("Select several")
                iconName: "lasso"
                checkable: true
                checked: bar.board.selectMode
                onClicked: bar.board.selectMode = !bar.board.selectMode
            }
            Separator {}
            BarButton {
                objectName: "canvasZoomOut"
                visible: !bar.compact
                label: qsTr("Zoom out")
                iconName: "zoom-out"
                shortcut: "Ctrl+-"
                onClicked: bar.board.zoomBy(0.8, -1, -1)
            }
            BButton {
                objectName: "canvasZoom"
                anchors.verticalCenter: parent.verticalCenter
                focusPolicy: Qt.NoFocus
                variant: "ghost"
                size: "small"
                text: Math.round(bar.board.zoom * 100) + " %"
                onClicked: bar.board.setZoom(1)
            }
            BarButton {
                objectName: "canvasZoomIn"
                visible: !bar.compact
                label: qsTr("Zoom in")
                iconName: "zoom-in"
                shortcut: "Ctrl++"
                onClicked: bar.board.zoomBy(1.25, -1, -1)
            }
            BarButton {
                objectName: "canvasFit"
                label: qsTr("Show the whole canvas")
                iconName: "maximize"
                shortcut: "Shift+1"
                onClicked: bar.board.zoomToFit()
            }
            Separator {}
            BarButton {
                id: moreButton
                objectName: "canvasMore"
                label: qsTr("More")
                iconName: "ellipsis"
                onClicked: bar.openMore()
            }
        }
    }

    BMenu {
        id: addMenu
        BMenuItem { text: qsTr("Card"); iconName: "sticky-note"; onTriggered: bar.addText() }
        BMenuItem { text: qsTr("Note…"); iconName: "file-text"; onTriggered: bar.addNote() }
        BMenuItem { text: qsTr("File…"); iconName: "paperclip"; onTriggered: bar.addFile() }
        BMenuItem { text: qsTr("Web address…"); iconName: "globe"; onTriggered: bar.addLink() }
        BMenuItem {
            text: bar.board.selectionCount > 0 ? qsTr("Group the selection") : qsTr("Group")
            iconName: "group"
            onTriggered: bar.addGroup()
        }
    }

    BMenu {
        id: moreMenu
        objectName: "canvasMoreMenu"
        BMenuItem { text: qsTr("Paste"); iconName: "clipboard-paste"; shortcut: "Ctrl+V"; onTriggered: bar.pasteRequested() }
        BMenuItem { text: qsTr("Select all"); iconName: "text-select"; shortcut: "Ctrl+A"; onTriggered: bar.board.selectAll() }
        BMenuItem {
            text: qsTr("Zoom to the selection")
            iconName: "zoom-in"
            shortcut: "Shift+2"
            enabled: bar.board.selectionCount > 0
            onTriggered: bar.board.zoomToSelection()
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "canvasExportPng"
            text: qsTr("Export as PNG picture")
            iconName: "image"
            enabled: bar.board.nodeCount > 0
            onTriggered: bar.exportRequested("png")
        }
        BMenuItem {
            text: qsTr("Export as SVG picture")
            iconName: "image"
            enabled: bar.board.nodeCount > 0
            onTriggered: bar.exportRequested("svg")
        }
    }
}
