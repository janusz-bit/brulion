pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// ContentOutline — the headings of the page in the editor (OutlineModel over the editor's
// blocks: live while typing), indented by level; the heading the caret is under is marked. A
// click (or Enter) scrolls the editor to the heading, selects it and flashes it. A right-click
// (mouse, touchpad, stylus), a long press or the Menu key opens "Kopiuj link do nagłówka" /
// "Kopiuj osadzenie" (BlockEditor.copyLink: [[Strona#Nagłówek]], ![[Strona#Nagłówek]]).
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
            onPressAndHold: root.openMenu(item, item.model.row)
            Keys.onMenuPressed: root.openMenu(item, item.model.row)
            TapHandler {
                // Right-click only — never a finger (Qt hands touch points to every TapHandler):
                // touch has the long press above.
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                acceptedButtons: Qt.RightButton
                onTapped: root.openMenu(item, item.model.row)
            }
        }
    }

    /// The heading's menu (made when first opened). `row`: the heading's row in the editor, tracked
    /// while the menu is open (the page may change meanwhile: sync, undo).
    property BMenu headingMenu: null
    property int menuHandle: -1
    function openMenu(item: Item, row: int) {
        if (!root.editor || !root.controller || row < 0)
            return
        if (!headingMenu)
            headingMenu = headingMenuComponent.createObject(root) as BMenu
        if (menuHandle >= 0)
            root.controller.untrack(menuHandle)
        menuHandle = root.controller.trackBlock(row)
        headingMenu.popup(item, Theme.space4, item.height)
    }
    function copy(kind: string) {
        const row = root.controller && menuHandle >= 0 ? root.controller.trackedRow(menuHandle) : -1
        if (row >= 0 && root.editor)
            root.editor.copyLink(row, kind)
    }
    Component {
        id: headingMenuComponent
        BMenu {
            objectName: "outlineHeadingMenu"
            BMenuItem {
                objectName: "outlineCopyHeadingLink"
                text: qsTr("Copy link to heading")
                iconName: "heading"
                onTriggered: root.copy("heading")
            }
            BMenuItem {
                objectName: "outlineCopyEmbed"
                text: qsTr("Copy embed")
                iconName: "rows-2"
                onTriggered: root.copy("embed")
            }
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
