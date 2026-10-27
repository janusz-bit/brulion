pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// DrawingToolbar — the drawing editor's tools (DrawingView; docs/UI.md "Drawing editor"): undo,
// redo, the pen, the highlighter and the eraser, the current tool's colours and widths, the
// paper (plain, lines, grid), "Rysuj palcem" on touch screens, "Pokaż cały rysunek" and Gotowe.
// Wide windows: everything in one row above the paper. `compact` (phones; at the bottom, in
// reach of the thumb): the colours and widths behind one swatch (a popup above the bar), the
// paper, finger drawing and fitting in "Więcej"; the row scrolls sideways when it still does not fit.
// The buttons take no keyboard focus: Space is the view's (held: move the paper).
Rectangle {
    id: bar

    required property DrawingCanvas canvas
    property bool compact: false
    /// The current tool's colours: [{ color, label }], and its widths: [{ value, label }].
    property var colors: []
    property var widths: []
    /// One finger draws (touch screens).
    property bool fingerDraws: false
    /// The bar lies at the bottom of the view (compact): its hairline on top.
    property bool atBottom: compact

    signal colorChosen(string color)
    signal widthChosen(real width)
    signal fingerToggled()
    signal fitRequested()
    signal done()

    readonly property bool drawingTool: canvas.tool !== "eraser"

    function backgroundIcon(name) {
        return name === "lines" ? "rows-3" : name === "grid" ? "grid-3x3" : "square"
    }

    implicitHeight: Theme.touchUi ? 56 : 46
    color: Theme.bgElevated

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        y: bar.atBottom ? 0 : parent.height - 1
        height: 1
        color: Theme.borderSubtle
    }

    /// A width: a dot as wide as the stroke would look.
    component WidthButton: T.AbstractButton {
        id: widthButton
        required property real value
        required property string label
        property bool selected: false
        implicitWidth: Math.max(Theme.controlHeight, Theme.touchUi ? Theme.touchTarget : 0)
        implicitHeight: implicitWidth
        focusPolicy: Qt.NoFocus
        hoverEnabled: !Theme.touchUi
        Accessible.role: Accessible.RadioButton
        Accessible.name: label
        Accessible.checked: selected
        background: Rectangle {
            anchors.centerIn: parent
            width: Theme.controlHeight
            height: width
            radius: Theme.radius
            color: widthButton.selected ? Theme.bgSelected : widthButton.hovered ? Theme.bgHover : "transparent"
        }
        contentItem: Item {
            Rectangle {
                anchors.centerIn: parent
                // The look of the line at zoom 1, capped to the button.
                width: Math.max(3, Math.min(Theme.controlHeight - 8, widthButton.value * 4.25 * 1.2))
                height: width
                radius: width / 2
                color: bar.canvas.color
                opacity: bar.canvas.tool === "highlighter" ? 0.6 : 1
            }
        }
        BToolTip {
            visible: widthButton.hovered
            text: widthButton.label
        }
    }

    component Separator: Rectangle {
        width: 1
        height: Theme.controlHeight - 8
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        color: Theme.borderSubtle
    }

    Flickable {
        id: flick
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: doneButton.left
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space2
        contentWidth: Math.max(width, row.implicitWidth)
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        clip: true

        Row {
            id: row
            height: flick.height
            spacing: bar.compact ? 0 : 2

            BIconButton {
                objectName: "drawingUndo"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "undo-2"
                label: qsTr("Undo")
                shortcut: "Ctrl+Z"
                focusPolicy: Qt.NoFocus
                enabled: bar.canvas.canUndo
                onClicked: bar.canvas.undo()
            }
            BIconButton {
                objectName: "drawingRedo"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "redo-2"
                label: qsTr("Redo")
                shortcut: "Ctrl+Shift+Z"
                focusPolicy: Qt.NoFocus
                enabled: bar.canvas.canRedo
                onClicked: bar.canvas.redo()
            }
            Separator {}
            BIconButton {
                objectName: "drawingPen"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "pencil"
                label: qsTr("Pen")
                shortcut: "P"
                focusPolicy: Qt.NoFocus
                checkable: true
                checked: bar.canvas.tool === "pen"
                onClicked: bar.canvas.tool = "pen"
            }
            BIconButton {
                objectName: "drawingHighlighter"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "highlighter"
                label: qsTr("Highlighter")
                shortcut: "H"
                focusPolicy: Qt.NoFocus
                checkable: true
                checked: bar.canvas.tool === "highlighter"
                onClicked: bar.canvas.tool = "highlighter"
            }
            BIconButton {
                objectName: "drawingEraser"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "eraser"
                label: qsTr("Eraser")
                shortcut: "E"
                focusPolicy: Qt.NoFocus
                checkable: true
                checked: bar.canvas.tool === "eraser"
                onClicked: bar.canvas.tool = "eraser"
            }
            Separator { visible: bar.drawingTool }

            // Wide: the colours and widths in the row.
            Repeater {
                model: !bar.compact && bar.drawingTool ? bar.colors : []
                delegate: BColorSwatch {
                    required property var modelData
                    anchors.verticalCenter: parent.verticalCenter
                    color: modelData.color
                    label: modelData.label
                    size: 22
                    focusPolicy: Qt.NoFocus
                    selected: Qt.colorEqual(bar.canvas.color, modelData.color)
                    onClicked: bar.colorChosen(modelData.color)
                }
            }
            Separator { visible: !bar.compact && bar.drawingTool }
            Repeater {
                model: !bar.compact && bar.drawingTool ? bar.widths : []
                delegate: WidthButton {
                    required property var modelData
                    anchors.verticalCenter: parent.verticalCenter
                    value: modelData.value
                    label: modelData.label
                    selected: Math.abs(bar.canvas.penWidth - modelData.value) < 0.01
                    onClicked: bar.widthChosen(modelData.value)
                }
            }
            // Compact: one swatch for both.
            BColorSwatch {
                id: styleButton
                objectName: "drawingStyle"
                visible: bar.compact && bar.drawingTool
                anchors.verticalCenter: parent.verticalCenter
                color: bar.canvas.color
                label: qsTr("Colour and thickness")
                size: 26
                focusPolicy: Qt.NoFocus
                onClicked: stylePopup.open()
            }
            Separator { visible: !bar.compact }

            // The paper.
            BIconButton {
                id: backgroundButton
                objectName: "drawingBackground"
                visible: !bar.compact
                anchors.verticalCenter: parent.verticalCenter
                iconName: bar.backgroundIcon(bar.canvas.background)
                label: qsTr("Paper")
                focusPolicy: Qt.NoFocus
                onClicked: backgroundMenu.popup(backgroundButton, 0, backgroundButton.height)
            }
            BIconButton {
                objectName: "drawingFinger"
                visible: !bar.compact && Theme.touchUi
                anchors.verticalCenter: parent.verticalCenter
                iconName: "pointer"
                label: qsTr("Draw with finger")
                focusPolicy: Qt.NoFocus
                checkable: true
                checked: bar.fingerDraws
                onClicked: bar.fingerToggled()
            }
            BIconButton {
                objectName: "drawingFit"
                visible: !bar.compact
                anchors.verticalCenter: parent.verticalCenter
                iconName: "maximize"
                label: qsTr("Show the whole drawing")
                shortcut: "Ctrl+0"
                focusPolicy: Qt.NoFocus
                onClicked: bar.fitRequested()
            }
            BIconButton {
                id: moreButton
                objectName: "drawingMore"
                visible: bar.compact
                anchors.verticalCenter: parent.verticalCenter
                iconName: "ellipsis"
                label: qsTr("More")
                focusPolicy: Qt.NoFocus
                onClicked: moreMenu.popup(moreButton, 0, -moreMenu.implicitHeight)
            }
        }
    }

    BButton {
        id: doneButton
        objectName: "drawingDone"
        anchors.right: parent.right
        anchors.rightMargin: Theme.space2
        anchors.verticalCenter: parent.verticalCenter
        size: "small"
        variant: "primary"
        iconName: "check"
        text: qsTr("Done")
        focusPolicy: Qt.NoFocus
        onClicked: bar.done()
    }

    BMenu {
        id: backgroundMenu
        BMenuItem {
            text: qsTr("Plain")
            iconName: "square"
            checkable: true
            checked: bar.canvas.background === "plain"
            onTriggered: bar.canvas.background = "plain"
        }
        BMenuItem {
            text: qsTr("Lines")
            iconName: "rows-3"
            checkable: true
            checked: bar.canvas.background === "lines"
            onTriggered: bar.canvas.background = "lines"
        }
        BMenuItem {
            text: qsTr("Grid")
            iconName: "grid-3x3"
            checkable: true
            checked: bar.canvas.background === "grid"
            onTriggered: bar.canvas.background = "grid"
        }
    }

    BMenu {
        id: moreMenu
        BMenuItem {
            text: qsTr("Plain paper")
            iconName: "square"
            checkable: true
            checked: bar.canvas.background === "plain"
            onTriggered: bar.canvas.background = "plain"
        }
        BMenuItem {
            text: qsTr("Lined paper")
            iconName: "rows-3"
            checkable: true
            checked: bar.canvas.background === "lines"
            onTriggered: bar.canvas.background = "lines"
        }
        BMenuItem {
            text: qsTr("Squared paper")
            iconName: "grid-3x3"
            checkable: true
            checked: bar.canvas.background === "grid"
            onTriggered: bar.canvas.background = "grid"
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "drawingFingerItem"
            text: qsTr("Draw with finger")
            iconName: "pointer"
            checkable: true
            checked: bar.fingerDraws
            onTriggered: bar.fingerToggled()
        }
        BMenuItem {
            text: qsTr("Show the whole drawing")
            iconName: "maximize"
            onTriggered: bar.fitRequested()
        }
    }

    // Compact: the colours and widths of the current tool, above the bar.
    T.Popup {
        id: stylePopup
        objectName: "drawingStylePopup"
        parent: bar
        x: Math.max(0, Math.min(bar.width - width, styleButton.mapToItem(bar, 0, 0).x - width / 2))
        y: bar.atBottom ? -height - Theme.space2 : bar.height + Theme.space2
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
        contentItem: Column {
            spacing: Theme.space2
            Grid {
                columns: 6
                spacing: 2
                Repeater {
                    model: bar.colors
                    delegate: BColorSwatch {
                        required property var modelData
                        color: modelData.color
                        label: modelData.label
                        focusPolicy: Qt.NoFocus
                        selected: Qt.colorEqual(bar.canvas.color, modelData.color)
                        onClicked: bar.colorChosen(modelData.color)
                    }
                }
            }
            Row {
                spacing: 2
                Repeater {
                    model: bar.widths
                    delegate: WidthButton {
                        required property var modelData
                        value: modelData.value
                        label: modelData.label
                        selected: Math.abs(bar.canvas.penWidth - modelData.value) < 0.01
                        onClicked: {
                            bar.widthChosen(modelData.value)
                            stylePopup.close()
                        }
                    }
                }
            }
        }
    }
}
