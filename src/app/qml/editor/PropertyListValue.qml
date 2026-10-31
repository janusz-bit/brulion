pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// PropertyListValue — the value of a list property in the properties panel (PropertyRow): its
// items as chips — a list's [[links]] open their page, tags (#chips) their tag page, aliases
// carry a bookmark — each with × (removes it, one undo step), then a "+" that turns into a
// BSuggestField (where the "+" would start a line of its own, or the list is empty, it is a
// labelled "+ Dodaj tag" / "Dodaj alias" / "Dodaj element" text button: no lone "+"): Enter or "," adds what is typed (a pasted "a, b, c" adds three) and the field
// stays for the next one; Backspace in the empty field removes the last chip; Ctrl+Z there undoes;
// Escape closes it. What cannot be added (an alias with `|`, a tag that is none) is said under
// the field; leaving the field then says it in a toast (the field closes). Suggestions: tags for tags, the notebook's values of the key and, after "[[",
// pages; none for aliases. Heights by bindings, as TasksRow's chips (docs/UI.md "Scrolling
// performance"): a row created expanded has its final height at once.
Item {
    id: root

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
    readonly property string addLabel: type === "tags" ? qsTr("Add tag") : type === "aliases" ? qsTr("Add alias") : qsTr("Add item")
    /// The icon "+" would start a line alone (or stand alone: no items): the labelled button
    /// shows instead. The chips' lines as the Flow places them (its height below).
    readonly property bool addAlone: {
        let x = 0
        for (let i = 0; i < chips.children.length; ++i) {
            const c = chips.children[i]
            if (c === iconAdd || c === labelAdd || c === addLoader || !c.visible || c.width <= 0)
                continue
            if (x > 0 && x + c.width > chips.width)
                x = 0
            x += c.width + chips.spacing
        }
        return x === 0 || x + iconAdd.width > chips.width
    }

    implicitHeight: chips.height
    height: implicitHeight

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
        row.setValue(next)
    }
    function openAdd() {
        if (readOnly)
            return
        adding = true
        Qt.callLater(() => { if (root && root.field) (root.field as Item).forceActiveFocus() })
    }

    Flow {
        id: chips
        width: root.width
        spacing: Theme.space1
        // The Flow's own height comes with its next polish: the same lines here (TasksRow).
        height: {
            let x = 0
            let line = 0
            let total = 0
            for (let i = 0; i < chips.children.length; ++i) {
                const c = chips.children[i]
                if (!c.visible || c.width <= 0)
                    continue
                if (x > 0 && x + c.width > chips.width) {
                    total += line + chips.spacing
                    x = 0
                    line = 0
                }
                x += c.width + chips.spacing
                line = Math.max(line, c.height)
            }
            return total + line
        }

        Repeater {
            model: root.items
            delegate: BChip {
                id: chip
                required property string modelData
                required property int index
                readonly property string target: root.type === "list" ? root.linkTarget(modelData) : ""
                objectName: "propertyChip_" + index
                variant: root.type === "tags" ? "tag" : "neutral"
                iconName: root.type === "aliases" ? "bookmark" : target !== "" ? "file-text" : ""
                showIcon: iconName !== ""
                text: root.safe(root.type === "tags" ? "#" + modelData : target !== "" ? target : modelData)
                interactive: root.type === "tags" || target !== ""
                // An item of the list, not a filter: filled like the other items (the page icon
                // marks a [[Page]]; a link colour would be too faint on the chip in light).
                outlined: false
                removable: !root.readOnly
                Accessible.name: root.type === "tags" ? qsTr("Tag %1").arg(modelData) : modelData
                onClicked: {
                    if (root.type === "tags")
                        root.editor.activateLink(root.row.blockRow, "tag:" + encodeURIComponent(chip.modelData))
                    else if (chip.target !== "")
                        root.editor.activateLink(root.row.blockRow, "page:" + encodeURIComponent(chip.target))
                }
                onRemoveClicked: root.removeAt(chip.index)
            }
        }
        BIconButton {
            id: iconAdd
            objectName: "propertyListAdd"
            visible: !root.readOnly && !root.adding && !root.addAlone
            size: "small"
            iconName: "plus"
            label: root.addLabel
            focusPolicy: Qt.NoFocus
            onClicked: root.openAdd()
        }
        // On a line of its own: labelled, like the panel's "+ Dodaj właściwość".
        BButton {
            id: labelAdd
            objectName: "propertyListAddLabelled"
            visible: !root.readOnly && !root.adding && root.addAlone
            variant: "ghost"
            size: "small"
            iconName: "plus"
            text: root.addLabel
            textColor: Theme.fgMuted
            font.weight: Theme.weightNormal
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
