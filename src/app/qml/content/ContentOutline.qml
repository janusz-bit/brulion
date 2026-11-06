pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// ContentOutline — the headings of the page in the editor (OutlineModel over the editor's
// blocks: live while typing), indented by level; the heading the caret is under is marked. A
// click (or Enter) scrolls the editor to the heading, selects it and flashes it. A right-click
// (mouse, touchpad, stylus), a long press or the Menu key opens "Copy link to heading" /
// "Copy embed" (BlockEditor.copyLink: [[Page#Heading]], ![[Page#Heading]]).
//
//   ContentOutline { controller: page; editor: editor }
//
// `embedded`: a section of the side panel's accordion (ContentSidePanel) — no scrolling of its
// own, as tall as its headings (implicitHeight), a one-line hint when there are none.
Item {
    id: root

    property PageController controller
    property BlockEditor editor
    property bool embedded: false
    /// The headings listed.
    readonly property int count: outline.count

    implicitHeight: embedded ? (outline.count > 0 ? list.contentHeight + list.anchors.topMargin : emptyHint.implicitHeight) : 0

    readonly property int currentRow: controller ? (controller.editingRow >= 0 ? controller.editingRow : controller.selectedRow) : -1

    OutlineModel {
        id: outline
        source: root.controller ? root.controller.blocks : null
    }

    ListView {
        id: list
        TouchFlick.enabled: true
        anchors.fill: parent
        anchors.topMargin: root.embedded ? 0 : Theme.space2
        interactive: !root.embedded
        clip: true
        model: outline
        boundsBehavior: Flickable.StopAtBounds
        currentIndex: outline.count >= 0 ? outline.entryForSourceRow(root.currentRow) : -1
        keyNavigationEnabled: true
        T.ScrollBar.vertical: BScrollBar { visible: !root.embedded }
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Outline")

        delegate: BListItem {
            id: item
            required property int index
            // OutlineModel's roles.
            required property int row
            required property int depth
            required text
            width: list.width
            dense: true
            leftPadding: (Theme.touchUi ? 14 : 10) + item.depth * (Theme.touchUi ? 16 : 14)
            iconName: item.depth === 0 ? "heading" : ""
            selected: list.currentIndex === item.index
            font.weight: item.depth === 0 ? Theme.weightMedium : Theme.weightNormal
            onClicked: if (root.editor) root.editor.revealRow(item.row)
            onPressAndHold: root.openMenu(item, item.row)
            Keys.onMenuPressed: root.openMenu(item, item.row)
            TapHandler {
                // Right-click only — never a finger (Qt hands touch points to every TapHandler):
                // touch has the long press above.
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                acceptedButtons: Qt.RightButton
                onTapped: root.openMenu(item, item.row)
            }
        }
    }

    /// The heading's menu (made when first opened). `row`: the heading's row in the editor, tracked
    /// while the menu is open (the page may change meanwhile: sync, undo).
    property BMenu headingMenu: null
    property int menuHandle: -1
    function openMenu(item: Item, row: int): void {
        if (!root.editor || !root.controller || row < 0)
            return
        if (!root.headingMenu)
            root.headingMenu = headingMenuComponent.createObject(root) as BMenu
        if (root.menuHandle >= 0)
            root.controller.untrack(root.menuHandle)
        root.menuHandle = root.controller.trackBlock(row)
        root.headingMenu.popup(item, Theme.space4, item.height)
    }
    function copy(kind: string): void {
        const row = root.controller && root.menuHandle >= 0 ? root.controller.trackedRow(root.menuHandle) : -1
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

    BText {
        id: emptyHint
        visible: root.embedded && outline.count === 0
        x: Theme.space2
        width: parent.width - 2 * Theme.space2
        text: qsTr("No headings yet: “# Plan”, “## Shopping” show up here.")
        variant: "muted"
        font.pixelSize: Theme.textSm
        wrapMode: Text.Wrap
    }
    BEmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.space4, implicitWidth)
        visible: !root.embedded && outline.count === 0
        compact: true
        iconName: "heading"
        title: qsTr("No headings yet")
        description: qsTr("Headings of the page (“# Plan”, “## Shopping”) show up here for quick jumps.")
    }
}
