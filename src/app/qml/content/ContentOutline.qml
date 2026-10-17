pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// ContentOutline — the headings of the page in the editor (OutlineModel over the editor's
// blocks: live while typing), indented by level; the heading the caret is under is marked. A
// click (or Enter) scrolls the editor to the heading and selects it.
//
//   ContentOutline { controller: page; editor: editor }
Item {
    id: root

    property PageController controller
    property BlockEditor editor

    readonly property int currentRow: controller ? (controller.editingRow >= 0 ? controller.editingRow : controller.selectedRow) : -1

    OutlineModel {
        id: outline
        source: root.controller ? root.controller.blocks : null
    }

    ListView {
        id: list
        TouchFlick.enabled: true
        anchors.fill: parent
        anchors.topMargin: Theme.space2
        clip: true
        model: outline
        boundsBehavior: Flickable.StopAtBounds
        currentIndex: outline.count >= 0 ? outline.entryForSourceRow(root.currentRow) : -1
        keyNavigationEnabled: true
        T.ScrollBar.vertical: BScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Outline")

        delegate: BListItem {
            id: item
            required property int index
            required property var model
            width: list.width
            dense: true
            leftPadding: (Theme.touchUi ? 14 : 10) + item.model.depth * (Theme.touchUi ? 16 : 14)
            text: item.model.text
            iconName: item.model.depth === 0 ? "heading" : ""
            selected: list.currentIndex === item.index
            font.weight: item.model.depth === 0 ? Theme.weightMedium : Theme.weightNormal
            onClicked: if (root.editor) root.editor.revealRow(item.model.row)
        }
    }

    BEmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.space4, implicitWidth)
        visible: outline.count === 0
        compact: true
        iconName: "heading"
        title: qsTr("No headings yet")
        description: qsTr("Headings of the page (“# Plan”, “## Shopping”) show up here for quick jumps.")
    }
}
