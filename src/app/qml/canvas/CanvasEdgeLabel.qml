pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CanvasEdgeLabel — an edge's label on a canvas (CanvasView; docs/UI.md "Canvas view"): in the
// middle of its curve, on a patch of the canvas's colour, above the cards (an edge between two close
// cards still shows its whole label), larger as the view zooms out so it stays readable.
Rectangle {
    id: label

    required property int index
    required property string edgeLabel
    required property string edgeColor
    required property real labelX
    required property real labelY
    required property bool selected
    property real zoom: 1
    readonly property real px: 1 / Math.max(0.01, zoom)
    readonly property string tint: CanvasColors.hex(edgeColor, Theme.dark)

    visible: edgeLabel !== ""
    z: 3
    x: labelX - width / 2
    y: labelY - height / 2
    width: labelText.implicitWidth + 2 * Theme.space2
    height: labelText.implicitHeight + Theme.space1 * 2
    radius: Theme.radius
    color: Theme.bg
    border.width: (selected ? 1.5 : 1) * px
    border.color: selected ? Theme.accent : Theme.borderSubtle
    Text {
        id: labelText
        anchors.centerIn: parent
        text: label.edgeLabel
        textFormat: Text.PlainText
        font.family: Theme.fontUi
        font.pixelSize: Math.min(48, Math.max(Theme.textSm, 11 * label.px))
        color: label.tint !== "" ? label.tint : Theme.fgMuted
    }
}
