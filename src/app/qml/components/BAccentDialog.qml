pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// BAccentDialog — a custom accent colour (Settings → Appearance → Accent colour → "Custom…"):
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

    function openWith(color: string): void {
        const c = ThemePalette.normalizeColor(color)
        showColor(c !== "" ? c : ThemePalette.accentSwatch(Theme.accentName, Theme.customAccent, Theme.dark,
                                                           Theme.dark ? Theme.darkSurface : Theme.lightSurface))
        open()
    }
    /// `color` ("#rrggbb") in the field and the preview.
    function showColor(color: string): void {
        value = color
        field.text = color
        field.errorText = ""
    }

    // What Theme would use with this colour.
    ThemeColors {
        id: previewColors
        dark: Theme.dark
        lightSurface: Theme.lightSurface
        darkSurface: Theme.darkSurface
        accentName: "custom"
        customAccent: dialog.value
    }
    // (A colour: ThemeColors' tokens are "#rrggbb" text.)
    readonly property color previewAccent: previewColors.accent
    /// 12 hues × 3 shades: Qt.hsla(h / 12, 0.68, l, 1) for l = 0.36, 0.5, 0.64 and h = 0 … 11.
    readonly property list<string> grid: [
        "#9a1d1d", "#9a5c1d", "#9a9a1d", "#5c9a1d", "#1d9a1d", "#1d9a5c", "#1d9a9a", "#1d5c9a", "#1d1d9a", "#5c1d9a", "#9a1d9a", "#9a1d5c",
        "#d62929", "#d68029", "#d6d629", "#80d629", "#29d629", "#29d680", "#29d6d6", "#2980d6", "#2929d6", "#8029d6", "#d629d6", "#d62980",
        "#e26565", "#e2a365", "#e2e265", "#a3e265", "#65e265", "#65e2a3", "#65e2e2", "#65a3e2", "#6565e2", "#a365e2", "#e265e2", "#e265a3"
    ]

    objectName: "accentDialog"
    title: qsTr("Custom accent colour")
    iconName: "palette"
    acceptText: qsTr("Use this colour")
    acceptEnabled: ThemePalette.normalizeColor(field.text) !== ""
    preferredWidth: Theme.dialogWidthSm
    onAccepted: dialog.chosen(dialog.value)

    // One row per shade when 12 fit, else two (the hues stay in columns).
    Grid {
        objectName: "accentDialogGrid"
        Layout.fillWidth: true
        readonly property real cell: Theme.touchUi ? Theme.touchTarget : 28
        columns: width >= 12 * cell ? 12 : 6
        Repeater {
            model: dialog.grid
            delegate: BColorSwatch {
                required property string modelData
                size: Theme.touchUi ? 28 : 20
                color: modelData
                label: modelData
                selected: dialog.value === modelData
                onClicked: dialog.showColor(modelData)
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
                const c = ThemePalette.normalizeColor(field.text)
                if (c !== "") {
                    dialog.value = c
                    field.errorText = ""
                }
            }
            onEditingFinished: field.errorText = ThemePalette.normalizeColor(field.text) !== "" || field.text === ""
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
                    color: previewColors.accentSolid
                    BText {
                        anchors.centerIn: parent
                        text: "14"
                        color: previewColors.accentOn
                        font.weight: Theme.weightSemiBold
                        font.pixelSize: Theme.textSm
                    }
                }
                BText {
                    text: qsTr("Today")
                    variant: "label"
                    color: previewColors.accentText
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 3
                    radius: 2
                    color: dialog.previewAccent
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: badgeText.implicitHeight + Theme.space2
                radius: Theme.radiusSm
                color: dialog.previewAccent.alpha(Theme.dark ? 0.18 : 0.12)
                BText {
                    id: badgeText
                    anchors.verticalCenter: parent.verticalCenter
                    x: Theme.space2
                    width: parent.width - 2 * Theme.space2
                    text: qsTr("Zażółć gęślą jaźń — text in the accent colour")
                    color: previewColors.accentText
                    elide: Text.ElideRight
                }
            }
        }
    }

    BText {
        objectName: "accentDialogNote"
        Layout.fillWidth: true
        text: qsTr("Brulion uses lighter or darker shades of this colour where it would be hard to read.")
        variant: "caption"
        wrapMode: Text.Wrap
    }
}
