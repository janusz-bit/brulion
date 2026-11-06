pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// DrawingStylePanel — the style of the selected elements (DrawingSelectionBar's "Style"; docs/UI.md
// "Drawing editor"): only what the selection has (DrawingCanvas.selectionStyle): the fill and its
// pattern, the line's thickness and dashes, corners, arrowheads, the text's size, aligning and
// distributing several parts (alignSelection, distributeSelection), opacity. Each
// choice is one undo step (DrawingCanvas.styleSelection) and the panel stays open for the next.
// Inside the view (`area`), beside the bar (`barHost`, its parent), scrolling when it does not fit.
T.Popup {
    id: panel

    required property DrawingCanvas canvas
    required property Item area
    required property Item barHost

    readonly property var current: canvas.selectionStyle
    /// selectionStyle's `key` as text ("" missing) and as a flag.
    function styleText(key: string): string {
        return AppController.routeText(panel.canvas.selectionStyle, key)
    }
    function styleFlag(key: string): bool {
        return AppController.routeFlag(panel.canvas.selectionStyle, key)
    }
    readonly property real gap: Theme.space2

    /// Excalidraw's light fills, which ink stays readable on.
    readonly property list<var> fillColors: [
        { color: "#ffc9c9", label: qsTr("Light red") },
        { color: "#ffec99", label: qsTr("Light yellow") },
        { color: "#b2f2bb", label: qsTr("Light green") },
        { color: "#a5d8ff", label: qsTr("Light blue") },
        { color: "#eebefa", label: qsTr("Light violet") },
        { color: "#dee2e6", label: qsTr("Light grey") }
    ]

    function displayed(color: string): string {
        return panel.canvas.dark ? panel.canvas.displayColorName(color, false) : color
    }

    x: Math.max(-barHost.x + gap, Math.min(area.width - barHost.x - width - gap, (barHost.width - width) / 2))
    y: {
        const above = -height - gap
        if (barHost.y + above >= gap)
            return above
        const below = barHost.height + gap
        return Math.max(-barHost.y + gap, Math.min(area.height - barHost.y - height - gap, below))
    }
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    padding: Theme.space3
    modal: true
    dim: false
    focus: true
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    objectName: "drawingStylePanel"

    background: Item {
        BShadow { target: panelFrame; level: 2 }
        Rectangle {
            id: panelFrame
            anchors.fill: parent
            color: Theme.bgElevated
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.borderSubtle
        }
    }

    /// One choice of a row: its name, marked when it is the current one.
    component Choice: T.AbstractButton {
        id: choice
        required property string label
        property bool selected: false
        implicitWidth: Math.max(choiceText.implicitWidth + 2 * Theme.space2, Theme.touchUi ? Theme.touchTarget : Theme.controlHeight)
        implicitHeight: Theme.touchUi ? Theme.touchTarget : Theme.controlHeight
        focusPolicy: Qt.NoFocus
        hoverEnabled: !Theme.touchUi
        Accessible.role: Accessible.RadioButton
        Accessible.name: label
        Accessible.checked: selected
        background: Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: Theme.controlHeight
            radius: Theme.radius
            color: choice.selected ? Theme.bgSelected : choice.hovered ? Theme.bgHover : "transparent"
        }
        contentItem: Text {
            id: choiceText
            text: choice.label
            color: Theme.fg
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    /// A row: its title and its choices ([{ value, label }]).
    component Options: Column {
        id: options
        required property string title
        required property string key
        property list<var> choices: []
        /// The selection's value of `key`, as text (the choices' values compared as text).
        readonly property string valueText: panel.styleText(options.key)
        spacing: 2
        width: parent ? parent.width : 0
        BText {
            text: options.title
            variant: "caption"
        }
        Flow {
            width: parent.width
            spacing: 2
            Repeater {
                model: options.choices
                delegate: Choice {
                    id: choiceItem
                    required property var modelData
                    required property var value
                    readonly property string valueText: AppController.routeText(modelData, "value")
                    objectName: "drawingStyle_" + options.key + "_" + valueText
                    selected: options.valueText === valueText
                    onClicked: panel.canvas.styleSelection(Js.withKey({}, options.key, choiceItem.value))
                }
            }
        }
    }

    contentItem: Flickable {
        implicitWidth: Math.min(320, panel.area.width - 2 * panel.gap - 2 * panel.padding)
        implicitHeight: Math.min(sections.implicitHeight, panel.area.height - 2 * panel.gap - 2 * panel.padding)
        contentWidth: width
        contentHeight: sections.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: sections
            width: parent.width
            spacing: Theme.space2

            Column {
                visible: panel.styleFlag("fillable")
                width: parent.width
                spacing: 2
                BText {
                    text: qsTr("Fill")
                    variant: "caption"
                }
                Flow {
                    width: parent.width
                    spacing: 2
                    Choice {
                        objectName: "drawingStyleNoFill"
                        label: qsTr("None", "fill")
                        selected: panel.styleText("fill") === "transparent"
                        onClicked: panel.canvas.styleSelection({ fill: "transparent" })
                    }
                    Repeater {
                        model: panel.fillColors
                        delegate: BColorSwatch {
                            id: fillSwatch
                            required property var modelData
                            readonly property string fill: AppController.routeText(modelData, "color")
                            objectName: "drawingStyleFill" + Js.sliceFrom(fill, 1)
                            color: panel.displayed(fill)
                            label: AppController.routeText(modelData, "label")
                            size: 22
                            focusPolicy: Qt.NoFocus
                            selected: panel.styleText("fill") === fill
                            onClicked: panel.canvas.styleSelection({ fill: fillSwatch.fill })
                        }
                    }
                }
            }
            Options {
                visible: panel.styleFlag("fillable") && panel.styleText("fill") !== "transparent"
                title: qsTr("Fill pattern")
                key: "fillStyle"
                choices: [
                    { value: "solid", label: qsTr("Solid", "fill") },
                    { value: "hachure", label: qsTr("Hatched") },
                    { value: "cross-hatch", label: qsTr("Cross-hatched") }
                ]
            }
            Options {
                visible: panel.styleFlag("thick")
                title: qsTr("Line thickness")
                key: "thickness"
                choices: [
                    { value: 0, label: qsTr("Fine") },
                    { value: 1, label: qsTr("Medium") },
                    { value: 2, label: qsTr("Thick") }
                ]
            }
            Options {
                visible: panel.styleFlag("dashable")
                title: qsTr("Line style")
                key: "strokeStyle"
                choices: [
                    { value: "solid", label: qsTr("Solid", "line") },
                    { value: "dashed", label: qsTr("Dashed") },
                    { value: "dotted", label: qsTr("Dotted") }
                ]
            }
            Options {
                visible: panel.styleFlag("roundable")
                title: qsTr("Corners")
                key: "rounded"
                choices: [
                    { value: false, label: qsTr("Sharp") },
                    { value: true, label: qsTr("Rounded") }
                ]
            }
            Options {
                visible: panel.styleFlag("heads")
                title: qsTr("Line start")
                key: "startHead"
                choices: panel.heads
            }
            Options {
                visible: panel.styleFlag("heads")
                title: qsTr("Line end")
                key: "endHead"
                choices: panel.heads
            }
            Options {
                visible: panel.styleFlag("text")
                title: qsTr("Text size")
                key: "fontSize"
                choices: [
                    { value: 16, label: qsTr("Small") },
                    { value: 20, label: qsTr("Medium", "text size") },
                    { value: 28, label: qsTr("Large") },
                    { value: 36, label: qsTr("Extra large") }
                ]
            }
            Column {
                visible: panel.styleFlag("canAlign")
                width: parent.width
                spacing: 2
                BText {
                    text: qsTr("Align")
                    variant: "caption"
                }
                Flow {
                    width: parent.width
                    spacing: 2
                    Repeater {
                        model: panel.alignments
                        delegate: BIconButton {
                            id: alignButton
                            required property var modelData
                            readonly property string value: AppController.routeText(modelData, "value")
                            objectName: "drawingStyleAlign_" + value
                            iconName: AppController.routeText(modelData, "icon")
                            label: AppController.routeText(modelData, "label")
                            shortcut: AppController.routeText(modelData, "keys")
                            focusPolicy: Qt.NoFocus
                            onClicked: panel.canvas.alignSelection(alignButton.value)
                        }
                    }
                    Item {
                        visible: panel.styleFlag("canDistribute")
                        width: Theme.space2
                        height: 1
                    }
                    BIconButton {
                        objectName: "drawingStyleDistribute_horizontal"
                        visible: panel.styleFlag("canDistribute")
                        iconName: "align-horizontal-distribute-center"
                        label: qsTr("Distribute horizontally")
                        shortcut: "Alt+H"
                        focusPolicy: Qt.NoFocus
                        onClicked: panel.canvas.distributeSelection("horizontal")
                    }
                    BIconButton {
                        objectName: "drawingStyleDistribute_vertical"
                        visible: panel.styleFlag("canDistribute")
                        iconName: "align-vertical-distribute-center"
                        label: qsTr("Distribute vertically")
                        shortcut: "Alt+V"
                        focusPolicy: Qt.NoFocus
                        onClicked: panel.canvas.distributeSelection("vertical")
                    }
                }
            }
            Options {
                title: qsTr("Opacity")
                key: "opacity"
                choices: [
                    { value: 100, label: "100 %" },
                    { value: 75, label: "75 %" },
                    { value: 50, label: "50 %" },
                    { value: 25, label: "25 %" }
                ]
            }
        }
    }

    /// The alignments of the "Align" row.
    readonly property list<var> alignments: [
        { value: "left", icon: "align-start-vertical", label: qsTr("Align left"), keys: "Ctrl+Shift+Left" },
        { value: "center", icon: "align-center-vertical", label: qsTr("Centre horizontally"), keys: "" },
        { value: "right", icon: "align-end-vertical", label: qsTr("Align right"), keys: "Ctrl+Shift+Right" },
        { value: "top", icon: "align-start-horizontal", label: qsTr("Align top"), keys: "Ctrl+Shift+Up" },
        { value: "middle", icon: "align-center-horizontal", label: qsTr("Centre vertically"), keys: "" },
        { value: "bottom", icon: "align-end-horizontal", label: qsTr("Align bottom"), keys: "Ctrl+Shift+Down" }
    ]
    readonly property list<var> heads: [
        { value: "", label: qsTr("None", "line end") },
        { value: "arrow", label: qsTr("Arrow") },
        { value: "triangle", label: qsTr("Triangle") },
        { value: "circle", label: qsTr("Circle") },
        { value: "bar", label: qsTr("Bar") },
        { value: "diamond", label: qsTr("Diamond") }
    ]
}
