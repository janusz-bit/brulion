pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// SelectionToolbar — the tools of a block selection (docs/UI.md "Selecting and copying"):
// "Zaznaczono %n", Kopiuj, Wytnij, Usuń, Zaznacz wszystko, Więcej (Kopiuj jako tekst, the block
// menu's "Kopiuj odnośnik" for a single block, Zwiększ / Zmniejsz wcięcie, Przenieś w górę / w dół,
// the task status, Zaznacz tekst) and, docked, Gotowe (leaves the selection mode). Two places:
//   * docked: the "selection" tool set of MobileEditToolbar on touch UIs — in the editing
//     toolbar's slot, on the keyboard's place (BlockEditor.toolbarMode);
//   * floating: desktops, a compact bar at the top of the editor while two or more blocks are
//     selected (BlockEditor).
// Buttons never take the keyboard: it stays with the selection (the editor's list).
//
//   SelectionToolbar { editor: editor; docked: true }
Row {
    id: bar

    property BlockEditor editor
    /// In the touch toolbar's slot (else the desktop's floating bar).
    property bool docked: false
    readonly property PageController controller: editor ? editor.controller : null
    readonly property int count: controller ? controller.selectionCount : 0
    /// One block selected (its two ends are the same block): the block's own tools apply.
    readonly property bool singleBlock: controller !== null && controller.selectedRow >= 0
                                        && controller.selectedRow === controller.selectionAnchorRow

    spacing: 0
    Accessible.role: Accessible.ToolBar
    Accessible.name: qsTr("Selection tools")

    component Tool: BIconButton {
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        focusPolicy: Qt.NoFocus
        size: "small"
        enabled: bar.count > 0
    }

    Text {
        objectName: "selectionCount"
        anchors.verticalCenter: parent.verticalCenter
        leftPadding: Theme.space2
        rightPadding: Theme.space2
        text: qsTr("%n block(s) selected", "", bar.count)
        textFormat: Text.PlainText
        font.family: Theme.fontUi
        font.pixelSize: Theme.textSm
        font.weight: Theme.weightMedium
        color: Theme.fg
    }
    Tool {
        objectName: "selectionCopy"
        iconName: "copy"
        label: qsTr("Copy")
        shortcut: "Ctrl+C"
        onClicked: bar.editor.copySelection(false)
    }
    Tool {
        objectName: "selectionCut"
        iconName: "scissors"
        label: qsTr("Cut")
        shortcut: "Ctrl+X"
        onClicked: bar.editor.cutSelection()
    }
    Tool {
        objectName: "selectionDelete"
        iconName: "trash-2"
        label: qsTr("Delete")
        shortcut: "Delete"
        onClicked: bar.editor.deleteSelection()
    }
    Tool {
        objectName: "selectionSelectAll"
        iconName: "text-select"
        label: qsTr("Select all")
        shortcut: "Ctrl+A"
        enabled: bar.controller !== null
        onClicked: bar.editor.selectAllBlocks()
    }
    Tool {
        id: moreButton
        objectName: "selectionMore"
        iconName: "ellipsis"
        label: qsTr("More")
        onClicked: bar.openMenu()
    }
    Rectangle {
        visible: bar.docked
        anchors.verticalCenter: parent.verticalCenter
        width: 1
        height: Theme.controlHeightSm
        color: Theme.border
    }
    Tool {
        objectName: bar.docked ? "selectionDone" : "selectionClear"
        iconName: bar.docked ? "check" : "x"
        label: bar.docked ? qsTr("Done") : qsTr("Clear selection")
        shortcut: bar.docked ? "" : "Esc"
        enabled: true
        onClicked: bar.editor.leaveSelectionMode()
    }

    // ---- Więcej ---------------------------------------------------------------------------------
    /// The menu (made when first opened): above the docked bar, below the floating one.
    property BMenu menu: null
    function openMenu() {
        if (!menu)
            menu = menuComponent.createObject(bar) as BMenu
        // "Kopiuj odnośnik" only for one block (its item in the menu, made for the submenu).
        for (let i = 0; i < menu.count; ++i) {
            const entry = menu.itemAt(i) as T.MenuItem
            if (entry && entry.subMenu && entry.subMenu.objectName === "selectionCopyLinkMenu") {
                entry.visible = singleBlock
                entry.height = singleBlock ? entry.implicitHeight : 0
            }
        }
        menu.bottomMargin = editor ? editor.menuBottomMargin() : Theme.space2
        if (docked) {
            // Above the bar (it sits on the keyboard's place): the height it will have.
            let h = menu.topPadding + menu.bottomPadding
            for (let i = 0; i < menu.count; ++i) {
                const entry = menu.itemAt(i)
                if (entry && entry.height !== 0) // (items are not visible while the menu is closed)
                    h += entry.implicitHeight
            }
            menu.popup(moreButton, 0, -h - Theme.space1)
        } else
            menu.popup(moreButton, 0, moreButton.height)
    }
    Component {
        id: menuComponent
        BMenu {
            id: more
            objectName: "selectionMenu"

            component OptionalItem: BMenuItem {
                property bool shown: true
                visible: shown
                height: shown ? implicitHeight : 0
            }

            BMenuItem {
                objectName: "selectionCopyText"
                text: qsTr("Copy as plain text")
                iconName: "copy"
                onTriggered: bar.editor.copySelection(true)
            }
            BMenu {
                objectName: "selectionCopyLinkMenu"
                title: qsTr("Copy link")
                iconName: "link"
                BMenuItem {
                    objectName: "selectionCopyBlockRef"
                    text: qsTr("Block reference ((…))")
                    iconName: "link"
                    onTriggered: bar.editor.copyLink(bar.controller.selectedRow, "blockRef")
                }
                BMenuItem {
                    text: qsTr("Link to block [[…#^…]]")
                    iconName: "bookmark"
                    onTriggered: bar.editor.copyLink(bar.controller.selectedRow, "blockLink")
                }
                OptionalItem {
                    shown: bar.singleBlock && bar.controller.isHeading(bar.controller.selectedRow)
                    text: qsTr("Link to heading [[…#…]]")
                    iconName: "heading"
                    onTriggered: bar.editor.copyLink(bar.controller.selectedRow, "heading")
                }
                BMenuItem {
                    text: qsTr("Embed")
                    iconName: "rows-2"
                    onTriggered: bar.editor.copyLink(bar.controller.selectedRow, "embed")
                }
            }
            BMenuSeparator {}
            BMenuItem {
                text: qsTr("Indent")
                iconName: "list-indent-increase"
                shortcut: bar.docked ? "" : "Tab"
                onTriggered: bar.controller.indentSelection()
            }
            BMenuItem {
                text: qsTr("Outdent")
                iconName: "list-indent-decrease"
                shortcut: bar.docked ? "" : "Shift+Tab"
                onTriggered: bar.controller.outdentSelection()
            }
            BMenuItem {
                text: qsTr("Move up")
                iconName: "move-up"
                shortcut: bar.docked ? "" : "Alt+Up"
                onTriggered: bar.controller.moveSelectionUp()
            }
            BMenuItem {
                text: qsTr("Move down")
                iconName: "move-down"
                shortcut: bar.docked ? "" : "Alt+Down"
                onTriggered: bar.controller.moveSelectionDown()
            }
            BMenu {
                objectName: "selectionTaskMenu"
                title: qsTr("Task status")
                iconName: "square-check-big"
                BMenuItem { text: qsTr("To do"); onTriggered: bar.controller.setSelectionTaskStatus("todo") }
                BMenuItem { text: qsTr("In progress"); onTriggered: bar.controller.setSelectionTaskStatus("doing") }
                BMenuItem { text: qsTr("Done"); onTriggered: bar.controller.setSelectionTaskStatus("done") }
                BMenuItem { text: qsTr("Waiting"); onTriggered: bar.controller.setSelectionTaskStatus("deferred") }
                BMenuItem { text: qsTr("Cancelled"); onTriggered: bar.controller.setSelectionTaskStatus("cancelled") }
                BMenuSeparator {}
                BMenuItem { text: qsTr("No status"); onTriggered: bar.controller.setSelectionTaskStatus("") }
            }
            BMenuSeparator {}
            BMenuItem {
                objectName: "selectionSelectText"
                text: qsTr("Select text…")
                iconName: "text-select"
                onTriggered: bar.editor.openSelectText()
            }
        }
    }
}
