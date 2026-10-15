pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// FrontmatterView — page properties (YAML frontmatter or Logseq `key:: value` lines at the top)
// as a collapsed "Properties" panel: a header with the number of properties; expanded, a
// key/value list. "Edit" (or Enter when selected) edits the raw YAML in place.
Rectangle {
    id: root

    property BlockDelegate block
    property bool expanded: false
    readonly property var properties: block.model.properties || []
    readonly property real firstLineCenter: header.height / 2

    implicitHeight: column.implicitHeight
    height: implicitHeight
    radius: Theme.radius
    color: root.expanded || root.block.editing ? Theme.bgSunken : "transparent"
    border.width: root.expanded || root.block.editing ? 1 : 0
    border.color: root.block.editing ? Theme.alpha(Theme.focusRing, 0.5) : Theme.borderSubtle

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 0

        Item {
            id: header
            Layout.fillWidth: true
            implicitHeight: Theme.touchUi ? 40 : 32
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space2
                anchors.rightMargin: Theme.space1
                spacing: Theme.space2
                BIcon {
                    name: "chevron-right"
                    size: Theme.iconSizeSm
                    color: Theme.fgMuted
                    rotation: root.expanded || root.block.editing ? 90 : 0
                    Behavior on rotation { NumberAnimation { duration: Theme.durationFast } }
                }
                Text {
                    text: qsTr("Properties")
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    font.weight: Theme.weightMedium
                    color: Theme.fgMuted
                }
                BBadge {
                    count: root.properties.length
                }
                Item { Layout.fillWidth: true }
                BIconButton {
                    visible: !root.block.editing && (root.expanded || Theme.touchUi)
                    size: "small"
                    iconName: "pencil"
                    label: qsTr("Edit properties")
                    focusPolicy: Qt.NoFocus
                    onClicked: root.block.editor.startEditing(root.block.index, -1)
                }
            }
            TapHandler {
                onTapped: {
                    if (root.block.editing)
                        root.block.editor.stopEditing(true)
                    else
                        root.expanded = !root.expanded
                }
            }
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Properties")
        }

        // Key / value list.
        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space8
            Layout.rightMargin: Theme.space3
            Layout.bottomMargin: Theme.space3
            visible: root.expanded && !root.block.editing
            columns: 2
            columnSpacing: Theme.space4
            rowSpacing: Theme.space1
            Repeater {
                // Two cells per property: the key (muted) and its value.
                model: root.expanded ? root.properties.length * 2 : 0
                delegate: Text {
                    required property int index
                    readonly property var property: root.properties[Math.floor(index / 2)]
                    readonly property bool isKey: index % 2 === 0
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: !isKey
                    text: property ? (isKey ? property.key : property.value) : ""
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: isKey ? Theme.fgMuted : Theme.fg
                    wrapMode: isKey ? Text.NoWrap : Text.Wrap
                }
            }
        }
        Loader {
            id: editorLoader
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space8
            Layout.rightMargin: Theme.space3
            Layout.bottomMargin: Theme.space3
            active: root.block.editing
            visible: active
            sourceComponent: InlineEditor {
                width: editorLoader.width
                block: root.block
                multiline: true
                highlightMode: "code"
                font.family: Theme.fontMono
                font.pixelSize: Theme.textSm
                lineHeightPx: Theme.lineHeightPx(Theme.textSm, 1.5)
            }
        }
    }
}
