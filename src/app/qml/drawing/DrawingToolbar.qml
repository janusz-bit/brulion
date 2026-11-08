pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// DrawingToolbar — the drawing editor's tools (DrawingView; docs/UI.md "Drawing editor"): undo,
// redo, the pen, the highlighter, the eraser, the selection (a lasso or a rectangle; a second click
// opens the choice), the shapes (one button showing the last of them: rectangle, ellipse, diamond,
// line, arrow and the polygons; a second click opens their menu) and text, the current tool's colours and widths (text: its sizes S, M, L, XL), the paper (plain,
// lines, grid) and its backdrop ("Backdrop from a picture or PDF…", "Another PDF page…", "Previous /
// Next PDF page", "Remove backdrop" and "Paste"
// in the paper's menu), its light or dark background ("Drawing background": as the app, light, dark,
// "Background colour…": DrawingPaperDialog),
// "Draw with finger" on touch screens, "Show the whole drawing" and Done.
// Wide windows: everything in one row above the paper. `compact` (phones; at the bottom, in
// reach of the thumb): the colours and widths behind one swatch (a popup above the bar), the
// paper, its background, finger drawing and fitting in "More"; when one row cannot hold it all
// (`twoRows`: a phone held upright) the tools and the swatch get a line of their own above undo,
// redo, "More" and Done — nothing hides off the edge. A row that still does not fit scrolls
// sideways. The swatches show the colours as the paper shows them (on dark paper the
// pen's colours mapped as the strokes are, DrawingCanvas.displayColor); what is chosen and saved
// is the colour as drawn on light paper.
// The buttons take no keyboard focus: Space is the view's (held: move the paper).
Rectangle {
    id: bar

    required property DrawingCanvas canvas
    property bool compact: false
    /// The current tool's colours: [{ color, label }], and its widths: [{ value, label }].
    property list<var> colors: []
    property list<var> widths: []
    /// The text's sizes: [{ value, label }].
    property list<var> fontSizes: []
    /// No rows (a Repeater's model while there is nothing to show).
    readonly property list<var> noRows: []
    /// The shape tool the shape button stands for (isShape()).
    property string shapeTool: "rectangle"
    /// One finger draws (touch screens).
    property bool fingerDraws: false
    /// The bar lies at the bottom of the view (compact): its hairline on top.
    property bool atBottom: compact

    /// A button's width (touch-sized on touch screens).
    readonly property int slot: Math.max(Theme.controlHeight, Theme.touchUi ? Theme.touchTarget : 0)
    /// What one compact row needs: ten buttons (undo, redo, the pen, the highlighter, the eraser,
    /// the selection, the shapes, text, the swatch — counted while hidden too, so the bar keeps its
    /// shape when the tool changes — and "More"), two separators, Done and the margins.
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
    /// How the select tool takes elements, chosen in its menu: "lasso", "box".
    signal selectModeChosen(string mode)
    signal fingerToggled()
    signal fitRequested()
    signal done()
    /// The backdrop's entries of the paper's menu ("More" on phones): a picture or a PDF page
    /// chosen, another page of its PDF, removed.
    signal backdropRequested()
    signal backdropPageRequested()
    /// The drawing of the previous (−1) or the next (1) page of the backdrop's PDF.
    signal pageStepRequested(int step)
    /// "Paste" (the clipboard's elements or text).
    signal pasteRequested()
    signal backdropRemoved()
    /// "Background colour…" chosen in the background's menu ("More" on phones).
    signal paperColorRequested()

    /// "Another PDF page…": the backdrop is a PDF's page and PDFs can be drawn here.
    readonly property bool backdropPdf: canvas.hasBackdrop && canvas.backdropPage > 0 && canvas.backdropPath !== ""
                                        && DrawingBackdrops.pdfAvailable

    /// A tool that has a colour (everything but the eraser and the selection).
    readonly property bool drawingTool: canvas.tool !== "eraser" && canvas.tool !== "select"
    readonly property bool textTool: canvas.tool === "text"
    /// The shape tools (the tools of the shapes' menu, below): their icons, names and keys — a
    /// tool that is no shape as the rectangle.
    function isShape(tool: string): bool {
        switch (tool) {
        case "rectangle":
        case "ellipse":
        case "diamond":
        case "line":
        case "arrow":
        case "triangle":
        case "rightTriangle":
        case "parallelogram":
        case "trapezoid":
        case "pentagon":
        case "hexagon":
        case "star":
        case "blockArrow":
        case "callout":
        case "heart":
        case "cloud":
            return true
        default:
            return false
        }
    }
    /// A shape tool's icon.
    function shapeIcon(tool: string): string {
        switch (tool) {
        case "ellipse": return "circle"
        case "diamond": return "diamond"
        case "line": return "slash"
        case "arrow": return "move-up-right"
        case "triangle": return "triangle"
        case "rightTriangle": return "triangle-right"
        case "parallelogram": return "parallelogram"
        case "trapezoid": return "trapezoid"
        case "pentagon": return "pentagon"
        case "hexagon": return "hexagon"
        case "star": return "star"
        case "blockArrow": return "arrow-big-right"
        case "callout": return "message-square"
        case "heart": return "heart"
        case "cloud": return "cloud"
        default: return "square"
        }
    }
    /// A shape tool's name.
    function shapeLabel(tool: string): string {
        switch (tool) {
        case "ellipse": return qsTr("Ellipse")
        case "diamond": return qsTr("Diamond")
        case "line": return qsTr("Line")
        case "arrow": return qsTr("Arrow")
        case "triangle": return qsTr("Triangle")
        case "rightTriangle": return qsTr("Right triangle")
        case "parallelogram": return qsTr("Parallelogram")
        case "trapezoid": return qsTr("Trapezoid")
        case "pentagon": return qsTr("Pentagon")
        case "hexagon": return qsTr("Hexagon")
        case "star": return qsTr("Star")
        case "blockArrow": return qsTr("Block arrow")
        case "callout": return qsTr("Speech bubble")
        case "heart": return qsTr("Heart")
        case "cloud": return qsTr("Cloud")
        default: return qsTr("Rectangle")
        }
    }
    /// A shape tool's key ("" none).
    function shapeKey(tool: string): string {
        switch (tool) {
        case "ellipse": return "O"
        case "diamond": return "D"
        case "line": return "L"
        case "arrow": return "A"
        case "triangle": return ""
        case "rightTriangle": return ""
        case "parallelogram": return ""
        case "trapezoid": return ""
        case "pentagon": return ""
        case "hexagon": return ""
        case "star": return ""
        case "blockArrow": return ""
        case "callout": return ""
        case "heart": return ""
        case "cloud": return ""
        default: return "R"
        }
    }
    readonly property bool shapeActive: bar.isShape(bar.canvas.tool)

    /// Open the colours and widths (compact), the "More" menu (compact), the paper's menu or
    /// the background's (the "More" menu on phones) — route.open, screenshots.
    function openStyle(): void { stylePopup.open() }
    function openShapes(): void { shapeMenu.popup(shapeButton, 0, bar.atBottom ? -shapeMenu.implicitHeight : shapeButton.height) }
    function openMore(): void { moreMenu.popup(moreButton, 0, -moreMenu.implicitHeight) }
    function openBackground(): void { backgroundMenu.popup(backgroundButton, 0, backgroundButton.height) }
    function openTheme(): void {
        if (bar.compact)
            bar.openMore()
        else
            themeMenu.popup(themeButton, 0, themeButton.height)
    }

    function chooseShape(tool: string): void {
        bar.canvas.tool = tool
        bar.shapeChosen(tool)
    }

    /// The paper's icon: plain paper a blank sheet ("square" is the rectangle tool's).
    function backgroundIcon(name: string): string {
        return name === "lines" ? "rows-3" : name === "grid" ? "grid-3x3" : "file"
    }
    function themeIcon(name: string): string {
        return name === "light" ? "sun" : name === "dark" ? "moon" : "sun-moon"
    }
    /// A colour of the current tool as the paper shows it now (re-evaluated when it turns dark).
    function shown(color: string): string {
        return bar.canvas.dark ? bar.canvas.displayColorName(color, bar.canvas.tool === "highlighter") : color
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
        y: parent ? (parent.height - height) / 2 : 0
        color: Theme.borderSubtle
    }

    /// The corner caret of a tool with a menu behind its second click (Select, Shapes): a small
    /// chevron in the bottom-right corner of the button's square — nothing else said that the
    /// rectangle selection and the eleven polygons are there. (Lucide's chevron fills the middle
    /// of its box: at 14 px it draws 7 × 3.5 px, set 3 px in from the corner.)
    component MenuCaret: BIcon {
        required property BIconButton button
        size: 14
        x: button.width - button.rightInset - 13
        y: button.height - button.bottomInset - 12
        name: "chevron-down"
        color: Theme.fgMuted
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
                    id: selectButton
                    iconName: bar.canvas.selectMode === "box" ? "square-dashed" : "lasso"
                    label: qsTr("Select")
                    shortcut: "V"
                    focusPolicy: Qt.NoFocus
                    // (Not toggled by a click: a click on the active tool opens the choice.)
                    checked: bar.canvas.tool === "select"
                    onClicked: {
                        if (bar.canvas.tool === "select")
                            selectMenu.popup(selectButton, 0, bar.atBottom ? -selectMenu.implicitHeight : selectButton.height)
                        else
                            bar.canvas.tool = "select"
                    }
                    MenuCaret { button: selectButton }
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
                    MenuCaret { button: shapeButton }
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
                    model: !bar.compact && bar.drawingTool ? bar.colors : bar.noRows
                    delegate: BColorSwatch {
                        id: rowSwatch
                        required property var modelData
                        readonly property string stored: AppController.routeText(modelData, "color")
                        // (parent is null while the tool's change takes the delegate away)
                        y: parent ? (parent.height - height) / 2 : 0
                        color: bar.shown(stored)
                        label: AppController.routeText(modelData, "label")
                        size: 22
                        focusPolicy: Qt.NoFocus
                        selected: Qt.colorEqual(bar.canvas.color, stored)
                        onClicked: bar.colorChosen(rowSwatch.stored)
                    }
                }
                Separator { visible: !bar.compact && bar.drawingTool }
                Repeater {
                    model: !bar.compact && bar.drawingTool && !bar.textTool ? bar.widths : bar.noRows
                    delegate: WidthButton {
                        id: rowWidth
                        // (parent is null while the tool's change takes the delegate away)
                        y: parent ? (parent.height - height) / 2 : 0
                        selected: Math.abs(bar.canvas.penWidth - value) < 0.01
                        onClicked: bar.widthChosen(rowWidth.value)
                    }
                }
                Repeater {
                    model: !bar.compact && bar.textTool ? bar.fontSizes : bar.noRows
                    delegate: TextSizeButton {
                        id: rowSize
                        objectName: "drawingTextSize" + value
                        y: parent ? (parent.height - height) / 2 : 0
                        selected: Math.abs(bar.canvas.fontSize - value) < 0.01
                        onClicked: bar.fontSizeChosen(rowSize.value)
                    }
                }
                // Compact: one swatch for both.
                BColorSwatch {
                    id: styleButton
                    objectName: "drawingStyle"
                    visible: bar.compact && bar.drawingTool
                    anchors.verticalCenter: parent.verticalCenter
                    color: bar.shown(bar.canvas.color.toString())
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

    /// Two lines: the lower one (undo, redo, "More", Done).
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

    component ShapeItem: BMenuItem {
        required property string tool
        objectName: "drawingShape" + Js.toUpperCase(Js.charAt(tool, 0)) + Js.sliceFrom(tool, 1)
        text: bar.shapeLabel(tool)
        iconName: bar.shapeIcon(tool)
        shortcut: bar.shapeKey(tool)
        onTriggered: bar.chooseShape(tool)
    }

    BMenu {
        id: selectMenu
        objectName: "drawingSelectMenu"
        BMenuItem {
            objectName: "drawingSelectLasso"
            text: qsTr("Lasso")
            iconName: "lasso"
            checkable: true
            checked: bar.canvas.selectMode === "lasso"
            onTriggered: bar.selectModeChosen("lasso")
        }
        BMenuItem {
            objectName: "drawingSelectBox"
            text: qsTr("Rectangle selection")
            iconName: "square-dashed"
            checkable: true
            checked: bar.canvas.selectMode === "box"
            onTriggered: bar.selectModeChosen("box")
        }
    }

    BMenu {
        id: shapeMenu
        objectName: "drawingShapeMenu"
        ShapeItem { tool: "rectangle" }
        ShapeItem { tool: "ellipse" }
        ShapeItem { tool: "diamond" }
        ShapeItem { tool: "line" }
        ShapeItem { tool: "arrow" }
        BMenuSeparator {}
        ShapeItem { tool: "triangle" }
        ShapeItem { tool: "rightTriangle" }
        ShapeItem { tool: "parallelogram" }
        ShapeItem { tool: "trapezoid" }
        ShapeItem { tool: "pentagon" }
        ShapeItem { tool: "hexagon" }
        ShapeItem { tool: "star" }
        ShapeItem { tool: "blockArrow" }
        ShapeItem { tool: "callout" }
        ShapeItem { tool: "heart" }
        ShapeItem { tool: "cloud" }
    }

    BMenu {
        id: backgroundMenu
        objectName: "drawingBackgroundMenu"
        BMenuItem {
            text: qsTr("Plain")
            iconName: "file"
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
        BMenuSeparator {}
        BMenuItem {
            objectName: "drawingBackdrop"
            text: qsTr("Backdrop from a picture or PDF…")
            iconName: "image"
            enabled: bar.canvas.editable
            onTriggered: bar.backdropRequested()
        }
        BMenuItem {
            objectName: "drawingBackdropPage"
            visible: bar.backdropPdf
            height: visible ? implicitHeight : 0
            text: qsTr("Another PDF page…")
            iconName: "file-text"
            onTriggered: bar.backdropPageRequested()
        }
        BMenuItem {
            objectName: "drawingPreviousPage"
            visible: bar.backdropPdf
            height: visible ? implicitHeight : 0
            enabled: bar.canvas.backdropPage > 1
            text: qsTr("Previous PDF page")
            iconName: "chevron-left"
            shortcut: "PgUp"
            onTriggered: bar.pageStepRequested(-1)
        }
        BMenuItem {
            objectName: "drawingNextPage"
            visible: bar.backdropPdf
            height: visible ? implicitHeight : 0
            text: qsTr("Next PDF page")
            iconName: "chevron-right"
            shortcut: "PgDown"
            onTriggered: bar.pageStepRequested(1)
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "drawingPaste"
            text: qsTr("Paste")
            iconName: "clipboard-paste"
            shortcut: "Ctrl+V"
            enabled: bar.canvas.editable
            onTriggered: bar.pasteRequested()
        }
        // Deleting: last, set apart (the rule of BMenuItem's header).
        BMenuSeparator {
            visible: bar.canvas.hasBackdrop
            height: visible ? implicitHeight : 0
        }
        BMenuItem {
            objectName: "drawingBackdropRemove"
            visible: bar.canvas.hasBackdrop
            height: visible ? implicitHeight : 0
            text: qsTr("Remove backdrop")
            iconName: "trash-2"
            danger: true
            onTriggered: bar.backdropRemoved()
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
        BMenuSeparator {}
        BMenuItem {
            objectName: "drawingPaperColor"
            text: qsTr("Background colour…")
            iconName: "palette"
            onTriggered: bar.paperColorRequested()
        }
    }

    BMenu {
        id: moreMenu
        BMenuItem {
            text: qsTr("Plain paper")
            iconName: "file"
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
        BMenuItem {
            objectName: "drawingPaperColorItem"
            text: qsTr("Background colour…")
            iconName: "palette"
            enabled: bar.canvas.editable
            onTriggered: bar.paperColorRequested()
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "drawingBackdropItem"
            text: qsTr("Backdrop from a picture or PDF…")
            iconName: "image"
            enabled: bar.canvas.editable
            onTriggered: bar.backdropRequested()
        }
        BMenuItem {
            visible: bar.backdropPdf
            height: visible ? implicitHeight : 0
            text: qsTr("Another PDF page…")
            iconName: "file-text"
            onTriggered: bar.backdropPageRequested()
        }
        BMenuItem {
            objectName: "drawingPreviousPageItem"
            visible: bar.backdropPdf
            height: visible ? implicitHeight : 0
            enabled: bar.canvas.backdropPage > 1
            text: qsTr("Previous PDF page")
            iconName: "chevron-left"
            shortcut: "PgUp"
            onTriggered: bar.pageStepRequested(-1)
        }
        BMenuItem {
            objectName: "drawingNextPageItem"
            visible: bar.backdropPdf
            height: visible ? implicitHeight : 0
            text: qsTr("Next PDF page")
            iconName: "chevron-right"
            shortcut: "PgDown"
            onTriggered: bar.pageStepRequested(1)
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "drawingPasteItem"
            text: qsTr("Paste")
            iconName: "clipboard-paste"
            shortcut: "Ctrl+V"
            enabled: bar.canvas.editable
            onTriggered: bar.pasteRequested()
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
        // Deleting: last, set apart (the rule of BMenuItem's header).
        BMenuSeparator {
            visible: bar.canvas.hasBackdrop
            height: visible ? implicitHeight : 0
        }
        BMenuItem {
            objectName: "drawingBackdropRemoveItem"
            visible: bar.canvas.hasBackdrop
            height: visible ? implicitHeight : 0
            text: qsTr("Remove backdrop")
            iconName: "trash-2"
            danger: true
            onTriggered: bar.backdropRemoved()
        }
    }

    // Compact: the colours and widths of the current tool, above the bar.
    T.Popup {
        id: stylePopup
        objectName: "drawingStylePopup"
        parent: bar
        // (Where the swatch is, read when it opens.)
        property point at: Qt.point(0, 0)
        onAboutToShow: stylePopup.at = styleButton.mapToItem(bar, 0, 0)
        x: Math.max(0, Math.min(bar.width - width, at.x - width / 2))
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
                        id: popupSwatch
                        required property var modelData
                        readonly property string stored: AppController.routeText(modelData, "color")
                        color: bar.shown(stored)
                        label: AppController.routeText(modelData, "label")
                        focusPolicy: Qt.NoFocus
                        selected: Qt.colorEqual(bar.canvas.color, stored)
                        onClicked: bar.colorChosen(popupSwatch.stored)
                    }
                }
            }
            Row {
                spacing: 2
                Repeater {
                    model: bar.textTool ? bar.noRows : bar.widths
                    delegate: WidthButton {
                        id: popupWidth
                        selected: Math.abs(bar.canvas.penWidth - value) < 0.01
                        onClicked: {
                            bar.widthChosen(popupWidth.value)
                            stylePopup.close()
                        }
                    }
                }
                Repeater {
                    model: bar.textTool ? bar.fontSizes : bar.noRows
                    delegate: TextSizeButton {
                        id: popupSize
                        selected: Math.abs(bar.canvas.fontSize - value) < 0.01
                        onClicked: {
                            bar.fontSizeChosen(popupSize.value)
                            stylePopup.close()
                        }
                    }
                }
            }
        }
    }
}
