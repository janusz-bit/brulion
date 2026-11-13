pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// SelectionToolbar — the tools of a block selection (docs/UI.md "Selecting and copying"):
// "%n block(s) selected", Copy, Cut, Delete, Select all, One by one (docked: taps pick single
// blocks, BlockEditor.pickMode), More (Copy as plain text, QR code… — docs/UI.md "QR code" —, the
// block menu's "Copy link" for a single block, Indent / Outdent, Move up / down — for blocks
// picked with gaps between them shown disabled, a tap says why —, the task status) and Done (docked, first:
// leaves the selection mode) or × (floating, last: clears the selection). Two places:
//   * docked: the "selection" tool set of MobileEditToolbar on touch UIs — in the editing
//     toolbar's slot, on the keyboard's place (BlockEditor.toolbarMode);
//   * floating: desktops, a compact bar at the top of the editor while two or more blocks are
//     selected (BlockEditor).
// Blocks selected across several days of the journal stream (BlockEditor.inSpan): the count is the
// stream's, Copy, Copy as plain text and QR code… take them all; the other tools are shown
// unavailable and a tap says that only copying works there; Select all selects the day's own
// blocks. In reading mode (BlockEditor.readOnly) only what changes nothing: Copy, Select all, One
// by one, Copy as plain text, QR code… and Copy link.
// Buttons never take the keyboard: it stays with the selection (the editor's list).
//
//   SelectionToolbar { editor: editor; docked: true }
Row {
    id: bar

    /// The editor as an Item, for the editor itself to pass (a file cannot give its own root to a
    /// property of its own type in compiled code); `editor` follows it unless set directly.
    property Item editorItem
    final property BlockEditor editor: editorItem as BlockEditor
    /// In the touch toolbar's slot (else the desktop's floating bar).
    property bool docked: false
    readonly property PageController controller: editor ? editor.controller : null
    /// Blocks of several days are selected (the journal stream's StreamSelection).
    readonly property bool span: editor !== null && editor.inSpan
    readonly property int count: span ? editor.stream.count : controller ? controller.selectionCount : 0
    /// Why a tool does nothing now ("" when it works): a selection over several days.
    readonly property string spanBlocked: span ? editor.spanReason : ""
    /// Reading mode: the tools that would change the note are not shown.
    readonly property bool readOnly: editor !== null && editor.readOnly
    /// One block selected (its two ends are the same block, nothing picked besides it): the
    /// block's own tools apply.
    readonly property bool singleBlock: !span && controller !== null && controller.selectedRow >= 0
                                        && controller.selectedRow === controller.selectionAnchorRow
                                        && !controller.selectionPicked
    /// The selection can be indented (PageController.canIndentSelection: in a document only list
    /// items), read when "More" opens.
    property bool indentable: true
    /// Why the selection is not indented or moved ("" when it can be: no gaps between its
    /// blocks), read when "More" opens.
    property string unmovable: ""
    /// Indenting and moving from the menu: a selection with gaps says why instead.
    /// `action`: "indent", "outdent", "up", "down".
    function moveOr(action: string): void {
        if (unmovable !== "") {
            editor.notice(unmovable, "info", null)
            return
        }
        switch (action) {
        case "indent": controller.indentSelection(); break
        case "outdent": controller.outdentSelection(); break
        case "up": controller.moveSelectionUp(); break
        case "down": controller.moveSelectionDown(); break
        }
    }
    /// The task status menu: not over several days.
    function setStatus(status: string): void {
        if (!editor.spanRefuses())
            controller.setSelectionTaskStatus(status)
    }

    spacing: 0
    Accessible.role: Accessible.ToolBar
    Accessible.name: qsTr("Selection tools")

    // `unavailable` (BIconButton): shown but not possible now, the reason as its tooltip; a tap
    // says why.
    component Tool: BIconButton {
        // (Centred in the row; a binding to `undefined` anchors is not compiled.)
        y: parent ? (parent.height - height) / 2 : 0
        focusPolicy: Qt.NoFocus
        size: "small"
        enabled: bar.count > 0
    }

    // Docked: "Done" first (Android's contextual bars), always on screen however narrow.
    Tool {
        objectName: "selectionDone"
        visible: bar.docked
        iconName: "check"
        label: qsTr("Done", "leave the selection mode")
        enabled: true
        onClicked: bar.editor.leaveSelectionMode()
    }
    Rectangle {
        visible: bar.docked
        anchors.verticalCenter: parent.verticalCenter
        width: 1
        height: Theme.controlHeightSm
        color: Theme.border
    }
    Text {
        objectName: "selectionCount"
        anchors.verticalCenter: parent.verticalCenter
        // A phone: on two lines when needed, so every tool fits the bar.
        width: bar.docked && Theme.compact ? Math.min(implicitWidth, 92) : implicitWidth
        leftPadding: Theme.space2
        rightPadding: Theme.space2
        text: qsTr("%n block(s) selected", "", bar.count)
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        font.family: Theme.fontUi
        font.pixelSize: bar.docked && Theme.compact ? Theme.textXs : Theme.textSm
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
        visible: !bar.readOnly
        iconName: "scissors"
        label: qsTr("Cut")
        shortcut: "Ctrl+X"
        unavailable: bar.spanBlocked
        onClicked: bar.editor.cutSelection()
    }
    Tool {
        objectName: "selectionDelete"
        visible: !bar.readOnly
        iconName: "trash-2"
        label: qsTr("Delete")
        shortcut: "Delete"
        unavailable: bar.spanBlocked
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
    // Docked: taps pick single blocks into or out of the selection instead of moving its end.
    Tool {
        objectName: "selectionPickMode"
        visible: bar.docked
        iconName: "list-checks"
        label: qsTr("One by one", "pick single blocks into the selection")
        // (Not `checkable`: a click must not break the binding to the editor's mode.)
        checked: bar.editor !== null && bar.editor.pickMode
        Accessible.checkable: true
        Accessible.checked: checked
        enabled: true
        unavailable: bar.spanBlocked
        onClicked: if (!bar.editor.spanRefuses()) bar.editor.pickMode = !bar.editor.pickMode
    }
    Tool {
        id: moreButton
        objectName: "selectionMore"
        iconName: "ellipsis"
        label: qsTr("More")
        onClicked: bar.openMenu()
    }
    Tool {
        objectName: "selectionClear"
        visible: !bar.docked
        iconName: "x"
        label: qsTr("Clear selection")
        shortcut: "Esc"
        enabled: true
        onClicked: bar.editor.leaveSelectionMode()
    }

    // ---- More -----------------------------------------------------------------------------------
    /// The menu (made when first opened): above the docked bar, below the floating one.
    property BMenu menu: null
    function openMenu(): void {
        if (!menu)
            menu = menuComponent.createObject(bar) as BMenu
        // A document's paragraphs and headings have no outline: no Indent / Outdent.
        indentable = controller !== null && controller.canIndentSelection()
        unmovable = span ? editor.spanReason : controller !== null && !controller.selectionContiguous ? editor.gapReason : ""
        // "Copy link" only for one block (its item in the menu, made for the submenu).
        for (let i = 0; i < menu.count; ++i) {
            const entry = menu.itemAt(i) as T.MenuItem
            const name = entry && entry.subMenu ? entry.subMenu.objectName : ""
            if (name === "selectionCopyLinkMenu" || name === "selectionTaskMenu") {
                const shown = name === "selectionCopyLinkMenu" ? singleBlock : !readOnly
                entry.visible = shown
                entry.height = shown ? entry.implicitHeight : 0
            }
        }
        menu.bottomMargin = editor ? editor.menuBottomMargin() : Theme.space2
        if (docked) {
            // Above the bar (it sits on the keyboard's place): the height it will have.
            let h = menu.topPadding + menu.bottomPadding
            for (let i = 0; i < menu.count; ++i) {
                const entry = menu.itemAt(i) as Item
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
            BMenuItem {
                objectName: "selectionQrCode"
                text: qsTr("QR code…")
                iconName: "qr-code"
                onTriggered: bar.editor.showSelectionQrCode()
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
            BMenuSeparator {
                visible: !bar.readOnly
                height: !bar.readOnly ? implicitHeight : 0
            }
            OptionalItem {
                objectName: "selectionIndent"
                shown: bar.indentable && !bar.readOnly
                text: qsTr("Indent")
                iconName: "list-indent-increase"
                shortcut: bar.docked ? "" : "Tab"
                unavailable: bar.unmovable
                onTriggered: bar.moveOr("indent")
            }
            OptionalItem {
                objectName: "selectionOutdent"
                shown: bar.indentable && !bar.readOnly
                text: qsTr("Outdent")
                iconName: "list-indent-decrease"
                shortcut: bar.docked ? "" : "Shift+Tab"
                unavailable: bar.unmovable
                onTriggered: bar.moveOr("outdent")
            }
            OptionalItem {
                objectName: "selectionMoveUp"
                shown: !bar.readOnly
                text: qsTr("Move up")
                iconName: "move-up"
                shortcut: bar.docked ? "" : "Alt+Up"
                unavailable: bar.unmovable
                onTriggered: bar.moveOr("up")
            }
            OptionalItem {
                objectName: "selectionMoveDown"
                shown: !bar.readOnly
                text: qsTr("Move down")
                iconName: "move-down"
                shortcut: bar.docked ? "" : "Alt+Down"
                unavailable: bar.unmovable
                onTriggered: bar.moveOr("down")
            }
            BMenu {
                objectName: "selectionTaskMenu"
                title: qsTr("Task status")
                iconName: "square-check-big"
                BMenuItem { text: qsTr("To do"); unavailable: bar.spanBlocked; onTriggered: bar.setStatus("todo") }
                BMenuItem { text: qsTr("In progress"); unavailable: bar.spanBlocked; onTriggered: bar.setStatus("doing") }
                BMenuItem { text: qsTr("Done", "task status"); unavailable: bar.spanBlocked; onTriggered: bar.setStatus("done") }
                BMenuItem { text: qsTr("Waiting"); unavailable: bar.spanBlocked; onTriggered: bar.setStatus("deferred") }
                BMenuItem { text: qsTr("Cancelled"); unavailable: bar.spanBlocked; onTriggered: bar.setStatus("cancelled") }
                BMenuSeparator {}
                BMenuItem { text: qsTr("No status"); unavailable: bar.spanBlocked; onTriggered: bar.setStatus("") }
            }
        }
    }
}
