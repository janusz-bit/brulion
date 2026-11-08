pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// PropertyListValue — the value of a list property in the properties panel (PropertyRow): its
// items as chips — a list's [[links]] open their page, tags (#chips) their tag page, aliases
// carry a bookmark — then a "+" that turns into a BSuggestField: Enter or "," adds what is typed
// (a pasted "a, b, c" adds three) and the field stays for the next one; Backspace in the empty
// field removes the last chip; Ctrl+Z there undoes; Escape closes it. What cannot be added (an
// alias with `|`, a tag that is none) is said under the field; leaving the field then says it in a
// toast (the field closes). Suggestions: tags for tags, the notebook's values of the key and,
// after "[[", pages; none for aliases.
// Removing an item (one undo step): with a mouse the chip's ×; on touch UIs the chip's menu (no ×:
// its target made a chip 44 px wider), as the page header's chips — a press and hold on any chip,
// a tap on one that opens nothing (a plain item, an alias): a tag's page or a [[Page]] opened,
// "Copy", "Remove" ("Undo" in the toast). Delete on a focused chip removes it everywhere.
// The items are placed here, as a Flow places them, but the "+" never starts a line alone: the
// last chip goes down with it. Heights and places by bindings, as TasksRow's chips (docs/UI.md
// "Scrolling performance"): a row created expanded has its final height at once.
Item {
    id: root
    objectName: "propertyListValue"

    /// The property (a `properties` role entry).
    required property var entry
    /// The row (an Item: PropertyRow cannot pass itself typed — the file's own type).
    required property Item rowItem
    readonly property PropertyRow row: rowItem as PropertyRow
    /// The row of the properties' block.
    readonly property int blockRow: row ? row.blockRow : -1
    readonly property PageController controller: row.controller
    readonly property BlockEditor editor: row.editor
    /// `entry` with its keys readable.
    readonly property MapHolder rec: MapHolder { map: root.entry }
    /// The items, as strings.
    readonly property list<string> items: root.entryItems()
    readonly property string type: row.type
    readonly property bool readOnly: !row.editable
    /// The add field is open.
    property bool adding: false
    readonly property alias field: addLoader.item

    implicitHeight: chips.height
    height: implicitHeight
    /// The tallest item of a chip line (a chip — its touch target on touch UIs —, the "+", the add
    /// field): PropertyRow centres the first line in the row's height, as the other values' lines.
    readonly property real lineHeight: chips.lineHeight

    function entryItems(): list<string> {
        if (root.rec.map.items === undefined)
            return []
        return root.rec.map.items
    }
    /// The items and `more` after them.
    function withItems(more: list<string>): list<string> {
        return Js.concatStrings(root.items, more)
    }
    /// `t` without its last character.
    function withoutLast(t: string): string {
        return Js.slice(t, 0, t.length - 1)
    }
    /// The target of a `[[Page]]` item ("" for any other item).
    function linkTarget(item: string): string {
        return BlockViews.listLinkTarget(item)
    }
    /// What is typed, split on commas outside [[…]] and quotes.
    function splitItems(text: string): list<string> {
        return BlockViews.splitListItems(text)
    }
    /// Add what is typed (several items when it has commas); true when it was written.
    function add(text: string): bool {
        if (root.splitItems(text).length === 0)
            return true
        return row.setValue(root.withItems(root.splitItems(text)))
    }
    /// Add what the field holds and close it (the field lost the keyboard, the page changes): what
    /// cannot be added is said in a toast.
    function addAndClose(text: string): void {
        if (Js.trim(text) !== "" && !add(text))
            row.noticeError()
        row.error = ""
        adding = false
    }
    function removeAt(i: int): bool {
        return row.setValue(Js.removedStringAt(root.items, i))
    }
    /// The text a chip shows for `item` (a tag with "#", a [[Page]] its page's name).
    function display(item: string): string {
        const target = root.type === "list" ? linkTarget(item) : ""
        return root.type === "tags" ? "#" + item : target !== "" ? target : item
    }
    /// Open what the chip of `item` opens (a tag's page, a [[Page]]); false when it opens nothing.
    function openItem(item: string): bool {
        if (root.type === "tags") {
            root.editor.activateLink(root.blockRow, root.encoded("tag:", item))
            return true
        }
        const target = root.type === "list" ? linkTarget(item) : ""
        if (target === "")
            return false
        root.editor.activateLink(root.blockRow, root.encoded("page:", target))
        return true
    }
    /// `prefix` and `text` URI-encoded (a tag: or page: link).
    function encoded(prefix: string, text: string): string {
        return prefix + Js.encodeURIComponent(text)
    }
    /// Remove item `i` from its menu: out of sight of an ×, so a toast says it, with "Undo".
    function removeFromMenu(i: int): void {
        if (i < 0 || i >= root.items.length || readOnly)
            return
        const item = root.items[i]
        const ed = root.editor
        const before = root.undoToken()
        if (!removeAt(i))
            return
        ed.noticeUndoable(root.removedText(item), "info", before)
    }
    /// The page's undo token now (PageController.undoToken).
    function undoToken(): string {
        return root.controller.undoToken()
    }
    /// The toast after `item` was removed.
    function removedText(item: string): string {
        return ensureChipMenu().removedText(root.type, item, root.display(item))
    }

    // ---- a chip's menu (created when first needed: docs/UI.md "Scrolling performance"; the
    // page header's chips have the same one, PropertyChipMenu) ----------------------------------
    property PropertyChipMenu chipMenu: null
    /// The item the chip menu is open for.
    property int chipMenuIndex: -1
    function ensureChipMenu(): PropertyChipMenu {
        if (!chipMenu)
            chipMenu = chipMenuComponent.createObject(root) as PropertyChipMenu
        return chipMenu
    }
    function openChipMenu(i: int, chip: Item): void {
        if (i < 0 || i >= root.items.length)
            return
        const menu = ensureChipMenu()
        const item = root.items[i]
        root.chipMenuIndex = i
        menu.kind = root.type === "tags" || root.type === "aliases" ? root.type : "list"
        menu.item = item
        menu.display = root.display(item)
        menu.opensItem = root.type === "list" && root.linkTarget(item) !== ""
        menu.readOnly = root.readOnly
        menu.bottomMargin = root.editor.menuBottomMargin()
        menu.popup(chip, 0, chip.height)
    }
    /// Where the items would go in `width` (ChipFlow.layoutFor; the self-test checks the "+" rule).
    function placementAt(width: real): var {
        return chips.layoutFor(width)
    }
    /// chipAt()'s point in a chip.
    property point probe
    /// The chip at (`x`, `y`) of this item, or null (PropertyRow: a long press on a chip is the
    /// chip's, not the row's menu).
    function chipAt(x: real, y: real): Item {
        for (let i = 0; i < chipRepeater.count; ++i) {
            const c = chipRepeater.itemAt(i)
            if (!c || !c.visible)
                continue
            root.probe = c.mapFromItem(root, x, y)
            if (c.contains(root.probe))
                return c
        }
        return null
    }
    Component {
        id: chipMenuComponent
        PropertyChipMenu {
            id: chipMenuItem
            objectName: "propertyChipMenu"
            onOpenRequested: root.openItem(chipMenuItem.item)
            onCopyRequested: root.editor.copyText(chipMenuItem.item)
            onRemoveRequested: root.removeFromMenu(root.chipMenuIndex)
        }
    }
    function openAdd(): void {
        if (readOnly)
            return
        adding = true
        addFocusTimer.restart()
    }
    Later {
        id: addFocusTimer
        onTriggered: if (root.field) (root.field as Item).forceActiveFocus()
    }

    // Where the items go, as a Flow would place them — left to right, a new line when one does not
    // fit — but the "+" never alone at the start of a line: the chip before it comes down with it
    // (when both fit there). The open field is placed as any item. Placed at once from the items'
    // sizes (a Flow's places arrive with its next polish: docs/UI.md "Scrolling performance").
    ChipFlow {
        id: chips
        width: root.width
        height: implicitHeight
        spacing: Theme.space1
        keepWith: addButton

        Repeater {
            id: chipRepeater
            model: root.items
            delegate: BChip {
                id: chip
                required property string modelData
                required property int index
                readonly property string target: root.type === "list" ? root.linkTarget(modelData) : ""
                /// A tap opens something (a tag's page, a [[Page]]); on touch UIs a tap on any other
                /// chip opens its menu.
                readonly property bool opens: root.type === "tags" || target !== ""
                objectName: "propertyChip_" + index
                variant: root.type === "tags" ? "tag" : "neutral"
                iconName: root.type === "aliases" ? "bookmark" : target !== "" ? "file-text" : ""
                showIcon: iconName !== ""
                text: root.display(modelData)
                interactive: opens || (Theme.touchUi && !root.readOnly)
                // An item of the list, not a filter: filled like the other items (the page icon
                // marks a [[Page]]; a link colour would be too faint on the chip in light).
                outlined: false
                // Touch UIs: no × (the chip's menu removes it); Delete on a focused chip still does.
                removable: !root.readOnly && !Theme.touchUi
                removeKey: !root.readOnly
                Accessible.name: root.type === "tags" ? qsTr("Tag %1").arg(modelData) : modelData
                onClicked: {
                    if (!root.openItem(chip.modelData) && Theme.touchUi)
                        root.openChipMenu(chip.index, chip)
                }
                onPressAndHold: {
                    if (!root.readOnly || chip.opens)
                        root.openChipMenu(chip.index, chip)
                }
                // The × removes at once (it is in sight); a key on touch UIs says it in a toast.
                onRemoveClicked: {
                    if (Theme.touchUi)
                        root.removeFromMenu(chip.index)
                    else
                        root.removeAt(chip.index)
                }
            }
        }
        BIconButton {
            id: addButton
            objectName: "propertyListAdd"
            visible: !root.readOnly && !root.adding
            size: "small"
            iconName: "plus"
            label: root.type === "tags" ? qsTr("Add tag") : root.type === "aliases" ? qsTr("Add alias") : qsTr("Add item")
            focusPolicy: Qt.NoFocus
            onClicked: root.openAdd()
        }
        Loader {
            id: addLoader
            active: root.adding
            visible: active
            width: active ? Math.min(Math.max(160, chips.width / 2), chips.width) : 0
            height: active && item ? (item as Item).implicitHeight : 0
            sourceComponent: BSuggestField {
                id: addField
                objectName: "propertyListField"
                size: "small"
                placeholderText: root.type === "tags" ? qsTr("Tag") : root.type === "aliases" ? qsTr("Alias") : qsTr("Item")
                errorText: root.row.error
                inputMethodHints: Qt.ImhNoAutoUppercase
                // Tags for tags, the notebook's values of the key and, after "[[", pages; none for
                // aliases. Not the items the list holds already.
                suggesting: root.type !== "aliases"
                onSuggestionsRequested: (typed) => {
                    addField.suggestions = BlockViews.propertyValueRows(
                                root.controller.suggestPropertyValues(root.row.key, typed, 8), true, root.items, "PropertyListValue")
                }
                onActiveFocusChanged: {
                    if (addField.activeFocus) {
                        root.row.fieldFocused(addField)
                        return
                    }
                    // Leaving the field adds what is typed (as Enter), then closes it.
                    if (root.adding)
                        root.addAndClose(addField.text)
                }
                Connections {
                    target: root.row
                    function onLeaving() {
                        if (addField.preeditText !== "")
                            EditorInput.commitPreedit(addField)
                        root.addAndClose(addField.text)
                    }
                }
                onPicked: (value) => {
                    if (root.add(value))
                        addField.text = ""
                }
                onCommitted: (text) => {
                    if (Js.trim(text) === "") {
                        root.adding = false
                        root.row.focusRow()
                        return
                    }
                    if (root.add(text))
                        addField.text = ""
                }
                onCancelled: {
                    addField.text = ""
                    root.row.error = ""
                    root.adding = false
                    root.row.focusRow()
                }
                // "," adds the item before it (Obsidian, Logseq); Backspace in the empty field
                // takes the last chip; Ctrl+Z in it undoes the last change of the page.
                onTextEdited: {
                    root.row.error = ""
                    const t = addField.text
                    if (Js.endsWith(t, ",") && root.splitItems(root.withoutLast(t)).length > 0 && Js.indexOf(t, "[[") < 0) {
                        if (root.add(root.withoutLast(t)))
                            addField.text = ""
                    }
                }
                onKeyPressedFirst: (event) => {
                    if (addField.text !== "" || addField.preeditText !== "")
                        return
                    if (event.key === Qt.Key_Backspace && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                        if (root.items.length > 0)
                            root.removeAt(root.items.length - 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Z && (event.modifiers & Qt.ControlModifier)) {
                        root.controller.undo()
                        event.accepted = true
                    }
                }
            }
        }
    }
}
