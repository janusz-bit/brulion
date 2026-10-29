pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// DrawingToolbar — the drawing editor's tools (DrawingView; docs/UI.md "Drawing editor"): undo,
// redo, the pen, the highlighter, the eraser, the selection (lasso), the shapes (one button showing
// the last of rectangle, ellipse, diamond, line and arrow; a second click opens their menu) and
// text, the current tool's colours and widths (text: its sizes S, M, L, XL), the paper (plain,
// lines, grid), its light or dark background ("Tło rysunku": as the app, light, dark), "Rysuj
// palcem" on touch screens, "Pokaż cały rysunek" and Gotowe.
// Wide windows: everything in one row above the paper. `compact` (phones; at the bottom, in
// reach of the thumb): the colours and widths behind one swatch (a popup above the bar), the
// paper, its background, finger drawing and fitting in "Więcej"; when one row cannot hold it all
// (`twoRows`: a phone held upright) the tools and the swatch get a line of their own above undo,
// redo, "Więcej" and Gotowe — nothing hides off the edge. A row that still does not fit scrolls
// sideways. The swatches show the colours as the paper shows them (on dark paper the
// pen's colours mapped as the strokes are, DrawingCanvas.displayColor); what is chosen and saved
// is the colour as drawn on light paper.
// The buttons take no keyboard focus: Space is the view's (held: move the paper).
Rectangle {
    id: bar

    required property DrawingCanvas canvas
    property bool compact: false
    /// The current tool's colours: [{ color, label }], and its widths: [{ value, label }].
    property var colors: []
    property var widths: []
    /// The text's sizes: [{ value, label }].
    property var fontSizes: []
    /// The shape tool the shape button stands for ("rectangle", "ellipse", "diamond", "line", "arrow").
    property string shapeTool: "rectangle"
    /// One finger draws (touch screens).
    property bool fingerDraws: false
    /// The bar lies at the bottom of the view (compact): its hairline on top.
    property bool atBottom: compact

    /// A button's width (touch-sized on touch screens).
    readonly property int slot: Math.max(Theme.controlHeight, Theme.touchUi ? Theme.touchTarget : 0)
    /// What one compact row needs: ten buttons (undo, redo, the pen, the highlighter, the eraser,
    /// the selection, the shapes, text, the swatch — counted while hidden too, so the bar keeps its
    /// shape when the tool changes — and "Więcej"), two separators, Gotowe and the margins.
    readonly property real oneRowWidth: 10 * slot + 2 + doneButton.implicitWidth + 3 * Theme.space2
    /// Compact and too narrow for one row: two lines (see the header).
    readonly property bool twoRows: compact && width > 0 && width < oneRowWidth
    readonly property int lineHeight: twoRows ? slot + 6 : Theme.touchUi ? 56 : 46
    readonly property int groupSpacing: compact ? 0 : 2

    signal colorChosen(string color)
    signal widthChosen(real width)
    signal fontSizeChosen(real size)
    /// A shape tool chosen in the shapes' menu (the button then stands for it).
    signal shapeChosen(string tool)
    signal fingerToggled()
    signal fitRequested()
    signal done()

    /// A tool that has a colour (everything but the eraser and the selection).
    readonly property bool drawingTool: canvas.tool !== "eraser" && canvas.tool !== "select"
    readonly property bool textTool: canvas.tool === "text"
    readonly property bool shapeActive: ["rectangle", "ellipse", "diamond", "line", "arrow"].indexOf(canvas.tool) >= 0

    /// Open the colours and widths (compact), the "Więcej" menu (compact), the paper's menu or
    /// the background's (the "Więcej" menu on phones) — route.open, screenshots.
    function openStyle() { stylePopup.open() }
    function openShapes() { shapeMenu.popup(shapeButton, 0, bar.atBottom ? -shapeMenu.implicitHeight : shapeButton.height) }
    function openMore() { moreMenu.popup(moreButton, 0, -moreMenu.implicitHeight) }
    function openBackground() { backgroundMenu.popup(backgroundButton, 0, backgroundButton.height) }
    function openTheme() {
        if (bar.compact)
            bar.openMore()
        else
            themeMenu.popup(themeButton, 0, themeButton.height)
    }

    function shapeIcon(tool) {
        return tool === "ellipse" ? "circle" : tool === "diamond" ? "diamond" : tool === "line" ? "slash"
             : tool === "arrow" ? "move-up-right" : "square"
    }
    function shapeLabel(tool) {
        return tool === "ellipse" ? qsTr("Ellipse") : tool === "diamond" ? qsTr("Diamond") : tool === "line" ? qsTr("Line")
             : tool === "arrow" ? qsTr("Arrow") : qsTr("Rectangle")
    }
    function shapeKey(tool) {
        return tool === "ellipse" ? "O" : tool === "diamond" ? "D" : tool === "line" ? "L" : tool === "arrow" ? "A" : "R"
    }
    function chooseShape(tool) {
        bar.canvas.tool = tool
        bar.shapeChosen(tool)
    }

    function backgroundIcon(name) {
        return name === "lines" ? "rows-3" : name === "grid" ? "grid-3x3" : "square"
    }
    function themeIcon(name) {
        return name === "light" ? "sun" : name === "dark" ? "moon" : "sun-moon"
    }
    /// A colour of the current tool as the paper shows it now (re-evaluated when it turns dark).
    function shown(color) {
        return bar.canvas.dark ? bar.canvas.displayColor(color, bar.canvas.tool === "highlighter") : color
    }

    implicitHeight: twoRows ? 2 * lineHeight : lineHeight
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
                // (Ink on a dark bar would vanish: the theme's text colour shows the thickness.)
                color: Theme.fg
            }
        }
        BToolTip {
            visible: widthButton.hovered
            text: widthButton.label
        }
    }

    /// A text size: "A" as large as the text would look.
    component TextSizeButton: T.AbstractButton {
        id: sizeButton
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
            color: sizeButton.selected ? Theme.bgSelected : sizeButton.hovered ? Theme.bgHover : "transparent"
        }
        contentItem: Item {
            Text {
                anchors.centerIn: parent
                text: "A"
                font.family: Theme.fontUi
                // 16 → 11 px … 36 → 22 px: the sizes told apart within the button.
                font.pixelSize: Math.round(6 + sizeButton.value * 0.45)
                color: Theme.fg
            }
        }
        BToolTip {
            visible: sizeButton.hovered
            text: sizeButton.label
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
        anchors.right: bar.twoRows ? parent.right : doneButton.left
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space2
        height: bar.lineHeight
        contentWidth: Math.max(width, content.width)
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        clip: true

        // One row: undo and redo, the tools, the rest. Two lines: the tools alone (centred), the
        // others on the lower line.
        Item {
            id: content
            x: bar.twoRows ? (flick.contentWidth - width) / 2 : 0
            height: flick.height
            width: bar.twoRows ? toolRow.width
                               : leadRow.width + toolRow.width + tailRow.width + 2 * bar.groupSpacing

            Row {
                id: leadRow
                parent: bar.twoRows ? lowerLine : content
                x: bar.twoRows ? Theme.space2 : 0
                height: parent ? parent.height : 0
                spacing: bar.groupSpacing

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
                Separator { visible: !bar.twoRows }
            }

            Row {
                id: toolRow
                x: bar.twoRows ? 0 : leadRow.width + bar.groupSpacing
                height: content.height
                spacing: bar.groupSpacing

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
                BIconButton {
                    objectName: "drawingSelect"
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "lasso"
                    label: qsTr("Select")
                    shortcut: "V"
                    focusPolicy: Qt.NoFocus
                    checkable: true
                    checked: bar.canvas.tool === "select"
                    onClicked: bar.canvas.tool = "select"
                }
                // The last shape; clicked while a shape is the tool: the shapes' menu.
                BIconButton {
                    id: shapeButton
                    objectName: "drawingShape"
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: bar.shapeIcon(bar.shapeActive ? bar.canvas.tool : bar.shapeTool)
                    label: qsTr("Shapes") + " — " + bar.shapeLabel(bar.shapeActive ? bar.canvas.tool : bar.shapeTool)
                    shortcut: bar.shapeKey(bar.shapeActive ? bar.canvas.tool : bar.shapeTool)
                    focusPolicy: Qt.NoFocus
                    // (Not toggled by a click: a click on the active shape opens the menu.)
                    checked: bar.shapeActive
                    onClicked: {
                        if (bar.shapeActive)
                            bar.openShapes()
                        else
                            bar.chooseShape(bar.shapeTool)
                    }
                }
                BIconButton {
                    objectName: "drawingText"
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "type"
                    label: qsTr("Text")
                    shortcut: "T"
                    focusPolicy: Qt.NoFocus
                    checkable: true
                    checked: bar.textTool
                    onClicked: bar.canvas.tool = "text"
                }
                Separator { visible: bar.drawingTool }

                // Wide: the colours and widths in the row.
                Repeater {
                    model: !bar.compact && bar.drawingTool ? bar.colors : []
                    delegate: BColorSwatch {
                        required property var modelData
                        // (parent is null while the tool's change takes the delegate away)
                        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                        color: bar.shown(modelData.color)
                        label: modelData.label
                        size: 22
                        focusPolicy: Qt.NoFocus
                        selected: Qt.colorEqual(bar.canvas.color, modelData.color)
                        onClicked: bar.colorChosen(modelData.color)
                    }
                }
                Separator { visible: !bar.compact && bar.drawingTool }
                Repeater {
                    model: !bar.compact && bar.drawingTool && !bar.textTool ? bar.widths : []
                    delegate: WidthButton {
                        required property var modelData
                        // (parent is null while the tool's change takes the delegate away)
                        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                        value: modelData.value
                        label: modelData.label
                        selected: Math.abs(bar.canvas.penWidth - modelData.value) < 0.01
                        onClicked: bar.widthChosen(modelData.value)
                    }
                }
                Repeater {
                    model: !bar.compact && bar.textTool ? bar.fontSizes : []
                    delegate: TextSizeButton {
                        required property var modelData
                        objectName: "drawingTextSize" + modelData.value
                        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                        value: modelData.value
                        label: modelData.label
                        selected: Math.abs(bar.canvas.fontSize - modelData.value) < 0.01
                        onClicked: bar.fontSizeChosen(modelData.value)
                    }
                }
                // Compact: one swatch for both.
                BColorSwatch {
                    id: styleButton
                    objectName: "drawingStyle"
                    visible: bar.compact && bar.drawingTool
                    anchors.verticalCenter: parent.verticalCenter
                    color: bar.shown(bar.canvas.color)
                    label: bar.textTool ? qsTr("Colour and size") : qsTr("Colour and thickness")
                    size: 26
                    focusPolicy: Qt.NoFocus
                    onClicked: stylePopup.open()
                }
            }

            Row {
                id: tailRow
                parent: bar.twoRows ? lowerLine : content
                x: bar.twoRows ? doneButton.x - Theme.space2 - width : toolRow.x + toolRow.width + bar.groupSpacing
                height: parent ? parent.height : 0
                spacing: bar.groupSpacing

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
                // Light or dark paper: as the app, light, dark.
                BIconButton {
                    id: themeButton
                    objectName: "drawingTheme"
                    visible: !bar.compact
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: bar.themeIcon(bar.canvas.theme)
                    label: qsTr("Drawing background")
                    focusPolicy: Qt.NoFocus
                    enabled: bar.canvas.editable
                    onClicked: themeMenu.popup(themeButton, 0, themeButton.height)
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
    }

    /// Two lines: the lower one (undo, redo, "Więcej", Gotowe).
    Item {
        id: lowerLine
        visible: bar.twoRows
        y: bar.lineHeight
        width: bar.width
        height: bar.lineHeight
    }

    BButton {
        id: doneButton
        objectName: "drawingDone"
        anchors.right: parent.right
        anchors.rightMargin: Theme.space2
        anchors.verticalCenter: bar.twoRows ? lowerLine.verticalCenter : parent.verticalCenter
        size: "small"
        variant: "primary"
        iconName: "check"
        text: qsTr("Done")
        focusPolicy: Qt.NoFocus
        onClicked: bar.done()
    }

    BMenu {
        id: shapeMenu
        objectName: "drawingShapeMenu"
        BMenuItem {
            objectName: "drawingShapeRectangle"
            text: qsTr("Rectangle")
            iconName: "square"
            shortcut: "R"
            onTriggered: bar.chooseShape("rectangle")
        }
        BMenuItem {
            objectName: "drawingShapeEllipse"
            text: qsTr("Ellipse")
            iconName: "circle"
            shortcut: "O"
            onTriggered: bar.chooseShape("ellipse")
        }
        BMenuItem {
            objectName: "drawingShapeDiamond"
            text: qsTr("Diamond")
            iconName: "diamond"
            shortcut: "D"
            onTriggered: bar.chooseShape("diamond")
        }
        BMenuItem {
            objectName: "drawingShapeLine"
            text: qsTr("Line")
            iconName: "slash"
            shortcut: "L"
            onTriggered: bar.chooseShape("line")
        }
        BMenuItem {
            objectName: "drawingShapeArrow"
            text: qsTr("Arrow")
            iconName: "move-up-right"
            shortcut: "A"
            onTriggered: bar.chooseShape("arrow")
        }
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
        id: themeMenu
        objectName: "drawingThemeMenu"
        BMenuItem {
            objectName: "drawingThemeAuto"
            text: qsTr("Like the app")
            iconName: "sun-moon"
            checkable: true
            checked: bar.canvas.theme === "auto"
            onTriggered: bar.canvas.theme = "auto"
        }
        BMenuItem {
            objectName: "drawingThemeLight"
            text: qsTr("Light")
            iconName: "sun"
            checkable: true
            checked: bar.canvas.theme === "light"
            onTriggered: bar.canvas.theme = "light"
        }
        BMenuItem {
            objectName: "drawingThemeDark"
            text: qsTr("Dark")
            iconName: "moon"
            checkable: true
            checked: bar.canvas.theme === "dark"
            onTriggered: bar.canvas.theme = "dark"
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
            objectName: "drawingThemeAutoItem"
            text: qsTr("Background like the app")
            iconName: "sun-moon"
            checkable: true
            checked: bar.canvas.theme === "auto"
            onTriggered: bar.canvas.theme = "auto"
        }
        BMenuItem {
            text: qsTr("Light background")
            iconName: "sun"
            checkable: true
            checked: bar.canvas.theme === "light"
            onTriggered: bar.canvas.theme = "light"
        }
        BMenuItem {
            objectName: "drawingThemeDarkItem"
            text: qsTr("Dark background")
            iconName: "moon"
            checkable: true
            checked: bar.canvas.theme === "dark"
            onTriggered: bar.canvas.theme = "dark"
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
        contentItem: Column {
            spacing: Theme.space2
            Grid {
                columns: 6
                spacing: 2
                Repeater {
                    model: bar.colors
                    delegate: BColorSwatch {
                        required property var modelData
                        color: bar.shown(modelData.color)
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
                    model: bar.textTool ? [] : bar.widths
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
                Repeater {
                    model: bar.textTool ? bar.fontSizes : []
                    delegate: TextSizeButton {
                        required property var modelData
                        value: modelData.value
                        label: modelData.label
                        selected: Math.abs(bar.canvas.fontSize - modelData.value) < 0.01
                        onClicked: {
                            bar.fontSizeChosen(modelData.value)
                            stylePopup.close()
                        }
                    }
                }
            }
        }
    }
}
