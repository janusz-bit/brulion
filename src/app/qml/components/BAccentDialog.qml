pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// BAccentDialog — a custom accent colour (Ustawienia → Wygląd → Kolor akcentu → „Własny…"):
// a grid of colours (12 hues × 3 shades) and a field for any "#rrggbb", with a live preview of
// what the app makes of it on the current background — the accent as a mark, as text and as a
// filled badge. ThemePalette derives readable shades by itself (WCAG 2: text 4.5:1, marks 3:1);
// a note under the preview says so. Our own picker: Qt's ColorDialog is not themed and missing in
// the browser.
//
//   BAccentDialog { id: accentDialog; onChosen: (color) => { AppSettings.customAccent = color; … } }
//   accentDialog.openWith(AppSettings.customAccent)
BDialog {
    id: dialog

    /// The colour shown: "#rrggbb".
    property string value: "#e2574c"

    /// Accepted: the colour ("#rrggbb", lower case).
    signal chosen(string color)

    function openWith(color) {
        const c = ThemePalette.normalizeColor(color || "")
        value = c !== "" ? c : ThemePalette.accentSwatch(Theme.accentName, Theme.customAccent, Theme.dark,
                                                         Theme.dark ? Theme.darkSurface : Theme.lightSurface)
        field.text = value
        field.errorText = ""
        open()
    }

    /// What Theme would use with this colour.
    readonly property var preview: ThemePalette.colors(Theme.dark, Theme.lightSurface, Theme.darkSurface, "custom", value)
    /// 12 hues × 3 shades.
    readonly property var grid: {
        const out = []
        for (const l of [0.36, 0.5, 0.64]) {
            for (let h = 0; h < 12; ++h)
                out.push(String(Qt.hsla(h / 12, 0.68, l, 1)))
        }
        return out
    }

    objectName: "accentDialog"
    title: qsTr("Custom accent colour")
    iconName: "palette"
    acceptText: qsTr("Use this colour")
    acceptEnabled: ThemePalette.normalizeColor(field.text) !== ""
    preferredWidth: Theme.dialogWidthSm
    onAccepted: dialog.chosen(dialog.value)

    // One row per shade when 12 fit, else two (the hues stay in columns).
    Grid {
        id: swatchGrid
        objectName: "accentDialogGrid"
        Layout.fillWidth: true
        readonly property real cell: Theme.touchUi ? Theme.touchTarget : 28
        columns: width >= 12 * cell ? 12 : 6
        spacing: 0
        Repeater {
            model: dialog.grid
            delegate: BColorSwatch {
                required property string modelData
                size: Theme.touchUi ? 28 : 20
                color: modelData
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
            objectName: "accentDialogField"
            Layout.preferredWidth: 140
            accessibleName: qsTr("Colour")
            placeholderText: "#3b7dd8"
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhPreferLatin
            maximumLength: 7
            onTextEdited: {
                const c = ThemePalette.normalizeColor(text)
                if (c !== "") {
                    dialog.value = c
                    errorText = ""
                }
            }
            onEditingFinished: errorText = ThemePalette.normalizeColor(text) !== "" || text === ""
                                          ? "" : qsTr("Enter a colour like #3b7dd8.")
            onAccepted: dialog.tryAccept()
        }
    }

    // Preview on the current background.
    Rectangle {
        objectName: "accentDialogPreview"
        Layout.fillWidth: true
        implicitHeight: previewColumn.implicitHeight + 2 * Theme.space4
        radius: Theme.radius
        color: Theme.bg
        border.width: 1
        border.color: Theme.border

        ColumnLayout {
            id: previewColumn
            anchors.fill: parent
            anchors.margins: Theme.space4
            spacing: Theme.space3

            RowLayout {
                spacing: Theme.space3
                Rectangle {
                    implicitWidth: 28
                    implicitHeight: 28
                    radius: 14
                    color: dialog.preview.accentSolid
                    BText {
                        anchors.centerIn: parent
                        text: "14"
                        color: "#ffffff"
                        font.weight: Theme.weightSemiBold
                        font.pixelSize: Theme.textSm
                    }
                }
                BText {
                    text: qsTr("Today")
                    variant: "label"
                    color: dialog.preview.accentText
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 3
                    radius: 2
                    color: dialog.preview.accent
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: badgeText.implicitHeight + Theme.space2
                radius: Theme.radiusSm
                color: Qt.rgba(Qt.color(dialog.preview.accent).r, Qt.color(dialog.preview.accent).g,
                               Qt.color(dialog.preview.accent).b, Theme.dark ? 0.18 : 0.12)
                BText {
                    id: badgeText
                    anchors.verticalCenter: parent.verticalCenter
                    x: Theme.space2
                    width: parent.width - 2 * Theme.space2
                    text: qsTr("Zażółć gęślą jaźń — text in the accent colour")
                    color: dialog.preview.accentText
                    elide: Text.ElideRight
                }
            }
        }
    }

    BText {
        objectName: "accentDialogNote"
        Layout.fillWidth: true
        text: qsTr("Brulion uses lighter or darker shades of this colour where it would be hard to read.")
        variant: "muted"
        font.pixelSize: Theme.textSm
        wrapMode: Text.Wrap
    }
}
