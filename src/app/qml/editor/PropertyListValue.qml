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
// "Kopiuj", "Usuń" ("Cofnij" in the toast). Delete on a focused chip removes it everywhere.
// The items are placed here, as a Flow places them, but the "+" never starts a line alone: the
// last chip goes down with it. Heights and places by bindings, as TasksRow's chips (docs/UI.md
// "Scrolling performance"): a row created expanded has its final height at once.
Item {
    id: root
    objectName: "propertyListValue"

    /// The property (a `properties` role entry).
    required property var entry
    required property PropertyRow row
    readonly property PageController controller: row.controller
    readonly property BlockEditor editor: row.editor
    /// The items as a JS array of strings (the role's string list is no Array: concat() and
    /// splice() would not see its items).
    readonly property var items: {
        const out = []
        const src = entry.items || []
        for (let i = 0; i < src.length; ++i)
            out.push(String(src[i]))
        return out
    }
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

    function safe(s) {
        return editor ? editor.autoTextSafe(s) : s
    }
    /// The target of a `[[Page]]` item ("" for any other item).
    function linkTarget(item) {
        const m = /^\[\[([^\[\]|]+)(\|[^\[\]]*)?\]\]$/.exec(item.trim())
        return m ? m[1].trim() : ""
    }
    /// What is typed, split on commas outside [[…]] and quotes.
    function splitItems(text) {
        const out = []
        let depth = 0
        let quoted = false
        let start = 0
        for (let i = 0; i < text.length; ++i) {
            const c = text.charAt(i)
            if (quoted) {
                if (c === "\"")
                    quoted = false
                continue
            }
            if (c === "\"")
                quoted = true
            else if (c === "[" && text.charAt(i + 1) === "[") { ++depth; ++i }
            else if (c === "]" && text.charAt(i + 1) === "]" && depth > 0) { --depth; ++i }
            else if (c === "," && depth === 0) {
                out.push(text.slice(start, i))
                start = i + 1
            }
        }
        out.push(text.slice(start))
        return out.map(s => s.trim()).filter(s => s !== "")
    }
    /// Add what is typed (several items when it has commas); true when it was written.
    function add(text) {
        const parts = splitItems(text)
        if (parts.length === 0)
            return true
        return row.setValue(root.items.concat(parts))
    }
    /// Add what the field holds and close it (the field lost the keyboard, the page changes): what
    /// cannot be added is said in a toast.
    function addAndClose(text) {
        if (text.trim() !== "" && !add(text))
            row.noticeError()
        row.error = ""
        adding = false
    }
    function removeAt(i) {
        const next = root.items.slice()
        next.splice(i, 1)
        return row.setValue(next)
    }
    /// The text a chip shows for `item` (a tag with "#", a [[Page]] its page's name).
    function display(item) {
        const target = root.type === "list" ? linkTarget(item) : ""
        return root.type === "tags" ? "#" + item : target !== "" ? target : item
    }
    /// Open what the chip of `item` opens (a tag's page, a [[Page]]); false when it opens nothing.
    function openItem(item) {
        if (root.type === "tags") {
            root.editor.activateLink(root.row.blockRow, "tag:" + encodeURIComponent(item))
            return true
        }
        const target = root.type === "list" ? linkTarget(item) : ""
        if (target === "")
            return false
        root.editor.activateLink(root.row.blockRow, "page:" + encodeURIComponent(target))
        return true
    }
    /// Remove item `i` from its menu: out of sight of an ×, so a toast says it, with "Cofnij".
    function removeFromMenu(i) {
        const item = root.items[i]
        if (item === undefined || readOnly)
            return
        const ed = root.editor
        const before = root.controller.undoToken()
        if (!removeAt(i))
            return
        const text = root.type === "tags" ? qsTr("Removed tag #%1 from this page").arg(item)
                   : root.type === "aliases" ? qsTr("Removed alias “%1”").arg(item)
                   : qsTr("Removed “%1”").arg(root.display(item))
        ed.notice(text, "info", ed.undoAction(before))
    }

    // ---- a chip's menu (created when first needed: docs/UI.md "Scrolling performance") ----------
    property var chipMenu: null
    function openChipMenu(i, chip) {
        if (i < 0 || i >= root.items.length)
            return
        if (!chipMenu)
            chipMenu = chipMenuComponent.createObject(root)
        chipMenu.index = i
        chipMenu.item = root.items[i]
        chipMenu.bottomMargin = root.editor.menuBottomMargin()
        chipMenu.popup(chip, 0, chip.height)
    }
    /// Where the items would go in `width` (chips.placement; the self-test checks the "+" rule).
    function placementAt(width) {
        return chips.layoutFor(width)
    }
    /// The chip at (`x`, `y`) of this item, or null (PropertyRow: a long press on a chip is the
    /// chip's, not the row's menu).
    function chipAt(x, y) {
        for (let i = 0; i < chipRepeater.count; ++i) {
            const c = chipRepeater.itemAt(i)
            if (c && c.visible && c.contains(c.mapFromItem(root, x, y)))
                return c
        }
        return null
    }
    Component {
        id: chipMenuComponent
        BMenu {
            id: chipMenuItem
            objectName: "propertyChipMenu"
            property int index: -1
            property string item
            readonly property bool opens: root.type === "tags" || (root.type === "list" && root.linkTarget(item) !== "")
            BMenuItem {
                objectName: "propertyChipOpen"
                visible: chipMenuItem.opens
                height: visible ? implicitHeight : 0
                text: root.type === "tags" ? qsTr("Open tag page") : qsTr("Open")
                iconName: root.type === "tags" ? "hash" : "file-text"
                onTriggered: root.openItem(chipMenuItem.item)
            }
            BMenuItem {
                objectName: "propertyChipCopy"
                visible: root.type !== "tags"
                height: visible ? implicitHeight : 0
                text: qsTr("Copy")
                iconName: "copy"
                onTriggered: root.editor.copyText(chipMenuItem.item)
            }
            BMenuItem {
                objectName: "propertyChipRemove"
                visible: !root.readOnly
                height: visible ? implicitHeight : 0
                text: root.type === "tags" ? qsTr("Remove from this page")
                    : root.type === "aliases" ? qsTr("Remove alias")
                    : qsTr("Remove “%1”").arg(root.safe(root.display(chipMenuItem.item)))
                iconName: "x"
                danger: true
                onTriggered: root.removeFromMenu(chipMenuItem.index)
            }
        }
    }
    function openAdd() {
        if (readOnly)
            return
        adding = true
        Qt.callLater(() => { if (root && root.field) (root.field as Item).forceActiveFocus() })
    }

    Item {
        id: chips
        width: root.width
        height: placement.height
        readonly property real spacing: Theme.space1
        /// The tallest item of a line.
        readonly property real lineHeight: placement.lineHeight
        /// Where the items go, as a Flow would place them — left to right, a new line when one does
        /// not fit — but the "+" never alone at the start of a line: the chip before it comes down
        /// with it (when both fit there). The open field is placed as any item. Computed from the
        /// items' sizes (a Flow's places arrive with its next polish: docs/UI.md "Scrolling
        /// performance"): { items: [{ item, x, y }], height, lineHeight }.
        readonly property var placement: layoutFor(chips.width)
        function layoutFor(w) {
            const shown = []
            for (let i = 0; i < chips.children.length; ++i) {
                const c = chips.children[i]
                if (c.visible && c.width > 0)
                    shown.push(c)
            }
            const s = chips.spacing
            // The line each item starts: a break before index `forced` too.
            function lines(forced) {
                const starts = []
                let x = 0
                for (let i = 0; i < shown.length; ++i) {
                    if (i === forced || (x > 0 && x + shown[i].width > w)) {
                        starts.push(i)
                        x = 0
                    } else if (i === 0) {
                        starts.push(0)
                    }
                    x += shown[i].width + s
                }
                return starts
            }
            let starts = lines(-1)
            const add = shown.indexOf(addButton)
            if (add > 1 && starts.indexOf(add) > 0 && starts.indexOf(add - 1) < 0
                    && shown[add - 1].width + s + addButton.width <= w)
                starts = lines(add - 1)
            const out = []
            let y = 0
            let tallest = 0
            for (let l = 0; l < starts.length; ++l) {
                const first = starts[l]
                const last = l + 1 < starts.length ? starts[l + 1] : shown.length
                let x = 0
                let line = 0
                for (let i = first; i < last; ++i) {
                    out.push({ item: shown[i], x: x, y: y })
                    x += shown[i].width + s
                    line = Math.max(line, shown[i].height)
                }
                tallest = Math.max(tallest, line)
                y += line + (l + 1 < starts.length ? s : 0)
            }
            return { items: out, height: y, lineHeight: tallest }
        }
        /// The place of `item` ({ x, y }; hidden items at 0, 0).
        function placeOf(item) {
            const items = placement.items
            for (let i = 0; i < items.length; ++i) {
                if (items[i].item === item)
                    return items[i]
            }
            return { x: 0, y: 0 }
        }

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
                readonly property var place: chips.placeOf(chip)
                x: place.x
                y: place.y
                objectName: "propertyChip_" + index
                variant: root.type === "tags" ? "tag" : "neutral"
                iconName: root.type === "aliases" ? "bookmark" : target !== "" ? "file-text" : ""
                showIcon: iconName !== ""
                text: root.safe(root.display(modelData))
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
            readonly property var place: chips.placeOf(addButton)
            x: place.x
            y: place.y
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
            readonly property var place: chips.placeOf(addLoader)
            x: place.x
            y: place.y
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
                provider: root.type === "aliases" ? null : (typed) => {
                    const held = root.items.map(s => s.toLowerCase())
                    return root.controller.suggestPropertyValues(root.row.key, typed, 8)
                        .filter(v => held.indexOf(String(v.value).toLowerCase()) < 0)
                        .map(v => ({ title: v.kind === "tag" ? "#" + v.value : String(v.value),
                                     subtitle: v.kind === "page" ? qsTr("page") : v.count > 0 ? String(v.count) : "",
                                     icon: v.kind === "page" ? "file-text" : v.kind === "tag" ? "hash" : "",
                                     value: v.value }))
                }
                onActiveFocusChanged: {
                    if (activeFocus) {
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
                    if (root.add(String(value)))
                        addField.text = ""
                }
                onCommitted: (text) => {
                    if (text.trim() === "") {
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
                    if (t.endsWith(",") && root.splitItems(t.slice(0, -1)).length > 0 && t.indexOf("[[") < 0) {
                        if (root.add(t.slice(0, -1)))
                            addField.text = ""
                    }
                }
                keyFilter: (event) => {
                    if (addField.text !== "" || addField.preeditText !== "")
                        return false
                    if (event.key === Qt.Key_Backspace && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                        if (root.items.length > 0)
                            root.removeAt(root.items.length - 1)
                        return true
                    }
                    if (event.key === Qt.Key_Z && (event.modifiers & Qt.ControlModifier)) {
                        root.controller.undo()
                        return true
                    }
                    return false
                }
            }
        }
    }
}
