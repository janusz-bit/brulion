pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// DrawingPaperDialog — the drawing's own background colour ("Drawing background" → "Background colour…";
// docs/UI.md "Drawing editor"): a grid of papers (neutrals, two rows of pastels, a dark row), a
// field for any "#rrggbb" and a preview with the default ink on it. Everything shows as the paper
// will (on dark paper the colours mapped, DrawingCanvas.displayColor); what is chosen and saved is
// the colour as drawn. A note says so while the drawing is shown dark, another one when the ink
// would be hard to see. Our own picker, as BAccentDialog.
//
//   DrawingPaperDialog { canvas: canvas; onChosen: (color) => { canvas.paper = color } }
//   dialog.openWith(canvas.paper)
BDialog {
    id: dialog

    required property DrawingCanvas canvas
    /// The colour chosen, as drawn: "#rrggbb".
    property string value: "#ffffff"

    /// Accepted: the colour ("#rrggbb", lower case).
    signal chosen(string color)

    function openWith(color: string): void {
        const c = ThemePalette.normalizeColor(color)
        dialog.value = c !== "" ? c : "#ffffff"
        field.text = dialog.value
        field.errorText = ""
        dialog.open()
    }
    /// A colour as the paper shows it now (re-evaluated when it turns dark).
    function shown(color: string): string {
        return dialog.canvas.dark ? dialog.canvas.displayColorName(color, false) : color
    }

    /// 12 neutrals, then 12 hues × 3 shades.
    readonly property list<string> grid: dialog.canvas.paperColors()

    objectName: "drawingPaperDialog"
    title: qsTr("Background colour")
    iconName: "palette"
    acceptText: qsTr("Use this colour")
    acceptEnabled: ThemePalette.normalizeColor(field.text) !== ""
    preferredWidth: Theme.dialogWidthSm
    onAccepted: dialog.chosen(dialog.value)

    // One row per shade when 12 fit, else two (the hues stay in columns).
    Grid {
        objectName: "drawingPaperGrid"
        Layout.fillWidth: true
        readonly property real cell: Theme.touchUi ? Theme.touchTarget : 28
        columns: width >= 12 * cell ? 12 : 6
        spacing: 0
        Repeater {
            model: dialog.grid
            delegate: BColorSwatch {
                required property string modelData
                size: Theme.touchUi ? 28 : 20
                color: dialog.shown(modelData)
                label: modelData
                selected: dialog.value === modelData
                onClicked: {
                    dialog.value = modelData
                    field.text = modelData
                    field.errorText = ""
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space3
        BText {
            Layout.fillWidth: true
            text: qsTr("Colour (# and six hexadecimal digits)")
            variant: "label"
            wrapMode: Text.Wrap
        }
        BTextField {
            id: field
            objectName: "drawingPaperField"
            Layout.preferredWidth: 140
            accessibleName: qsTr("Colour")
            placeholderText: "#fff9db"
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhPreferLatin
            maximumLength: 7
            onTextEdited: {
                const c = ThemePalette.normalizeColor(field.text)
                if (c !== "") {
                    dialog.value = c
                    field.errorText = ""
                }
            }
            onEditingFinished: field.errorText = ThemePalette.normalizeColor(field.text) !== "" || field.text === ""
                                                ? "" : qsTr("Enter a colour like #fff9db.")
            onAccepted: dialog.tryAccept()
        }
    }

    // The paper as it will show, with the default ink on it.
    Rectangle {
        objectName: "drawingPaperPreview"
        Layout.fillWidth: true
        implicitHeight: previewText.implicitHeight + 2 * Theme.space4
        radius: Theme.radius
        color: dialog.shown(dialog.value)
        border.width: 1
        border.color: Theme.border
        BText {
            id: previewText
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.space4
            width: parent.width - 2 * Theme.space4
            text: qsTr("Zażółć gęślą jaźń — ink on this paper")
            color: dialog.shown("#1f2430")
            elide: Text.ElideRight
        }
    }

    BText {
        objectName: "drawingPaperInkNote"
        visible: dialog.canvas.inkContrast(dialog.value) < 3
        Layout.fillWidth: true
        text: qsTr("The default ink is hard to see on this colour. Choose a lighter or darker background, or another pen colour.")
        font.pixelSize: Theme.textSm
        wrapMode: Text.Wrap
    }
    BText {
        objectName: "drawingPaperDarkNote"
        visible: dialog.canvas.dark
        Layout.fillWidth: true
        text: qsTr("The drawing is shown dark, so its colours are shown inverted. To see exactly this colour, set the background to Light.")
        variant: "muted"
        font.pixelSize: Theme.textSm
        wrapMode: Text.Wrap
    }
}
