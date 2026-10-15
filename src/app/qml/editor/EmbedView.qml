pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// EmbedView — attachments embedded in a block (`![[photo.png]]`, `![alt](assets/a.png)`):
// images at their natural size up to the column width (rounded, with a hairline border),
// other files as chips. URLs come from EditorHost::assetUrl (file:// on desktop/Android,
// blob/data on WebAssembly). A click on a picture emits clicked(target).
Column {
    id: root

    property var embeds: []
    signal clicked(string target)

    /// See BlockEditor.autoTextSafe (BChip shows its text with Text.AutoText).
    function autoTextSafe(s) {
        return s && s.indexOf("<") >= 0 ? TextHelper.escapeHtml(s) : (s || "")
    }

    spacing: Theme.space2

    Repeater {
        model: root.embeds
        delegate: Item {
            id: embed
            required property var modelData
            readonly property bool image: modelData.image && modelData.url.toString() !== ""
            width: root.width
            height: image ? picture.height : chip.height

            Rectangle {
                id: picture
                visible: embed.image
                readonly property real naturalWidth: img.status === Image.Ready ? img.implicitWidth : 320
                readonly property real naturalHeight: img.status === Image.Ready ? img.implicitHeight : 180
                // Natural size, at most the column width and 420 px high.
                width: Math.min(root.width, naturalWidth, 420 * naturalWidth / Math.max(1, naturalHeight))
                height: Math.round(width * naturalHeight / Math.max(1, naturalWidth))
                radius: Theme.radius
                color: Theme.bgSunken
                border.width: 1
                border.color: Theme.borderSubtle
                clip: true

                // Decoded at most 2048 px wide/high: a phone photo (12 MP) would otherwise take
                // 48 MB (+ mipmaps) per embed. No PreserveAspectFit: with it Qt also scales small
                // images *up* to sourceSize; the frame has the picture's aspect ratio anyway.
                Image {
                    id: img
                    anchors.fill: parent
                    anchors.margins: 1
                    source: embed.image ? embed.modelData.url : ""
                    sourceSize: Qt.size(2048, 2048)
                    asynchronous: true
                    smooth: true
                    mipmap: true
                    Accessible.role: Accessible.Graphic
                    Accessible.name: embed.modelData.alt !== "" ? embed.modelData.alt : embed.modelData.name
                }
                BSpinner {
                    anchors.centerIn: parent
                    size: 20
                    running: img.status === Image.Loading
                    visible: running
                }
                Text {
                    visible: img.status === Image.Error
                    anchors.centerIn: parent
                    text: qsTr("Cannot load %1").arg(embed.modelData.name)
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgMuted
                }
                TapHandler { onTapped: root.clicked(embed.modelData.target) }
                HoverHandler { cursorShape: Qt.PointingHandCursor; enabled: !Theme.touchUi }
            }

            BChip {
                id: chip
                visible: !embed.image
                interactive: true
                iconName: embed.modelData.image ? "image" : "file"
                text: root.autoTextSafe(embed.modelData.name)
                onClicked: root.clicked(embed.modelData.target)
            }
        }
    }
}
