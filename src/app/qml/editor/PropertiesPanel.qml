pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// PropertiesPanel — the rows of one page-property block (the YAML frontmatter or the leading
// `key:: value` lines) as typed fields (PropertyRow), then "+ Dodaj właściwość": a field with the
// notebook's keys as suggestions (key, its type's icon, how many pages use it); choosing or
// entering a key writes it at once with an empty value of the key's type in the notebook (`key:`,
// `key: []` for a list, `key: false` for a checkbox) — one undo step — and the value's editor
// gets the focus; Escape drops the draft (nothing written). A block Brulion cannot edit safely
// shows its values read-only with "Edytuj jako tekst". Used by FrontmatterView (expanded) and,
// with `draft`, by the editor's list header for the first property of a page that has none
// (BlockEditor.addProperty()). Rows stay through edits (a ListModel by key: a row is made only for
// a new key), the panel's height is a sum of its rows' (docs/UI.md "Scrolling performance").
Item {
    id: panel

    property BlockEditor editor
    property PageController controller
    /// The frontmatter row (-1: the draft above the blocks).
    property int blockRow: -1
    /// The `properties` role of the row.
    property var properties: []
    /// The `propertiesInfo` role: { source, editable, problem, canAdd }.
    property var info: ({})
    /// Only the add row (the page's first property).
    property bool draft: false

    readonly property string source: info.source || (controller ? controller.pageProperties.addTarget || "yaml" : "yaml")
    readonly property bool editable: draft || info.editable !== false
    readonly property bool canAdd: draft ? (controller !== null && controller.pageProperties.canAdd !== false)
                                         : (editable && info.canAdd !== false)
    readonly property bool stacked: Theme.compact || width < 480
    readonly property real rowHeight: Theme.touchUi ? 44 : (Theme.dense ? 28 : 32)
    readonly property real iconWidth: Theme.touchUi ? Theme.touchTarget : 24
    /// The key column: as wide as the widest key, 96 px to 30 % of the panel.
    readonly property real keyWidth: {
        let widest = 0
        for (const p of properties)
            widest = Math.max(widest, keyMetrics.advanceWidth(p.key))
        return Math.round(Math.max(96, Math.min(widest + 12, width * 0.3)))
    }
    /// The add field is open.
    property bool adding: draft
    property string addError: ""

    implicitHeight: rowsHeight + (canAdd ? addRow.height : 0) + (note.visible ? note.height + Theme.space2 : 0)
    height: implicitHeight

    FontMetrics {
        id: keyMetrics
        font.family: Theme.fontUi
        font.pixelSize: Theme.textSm
    }

    function propertyOf(key) {
        for (const p of properties) {
            if (p.key === key)
                return p
        }
        return null
    }
    /// The key of the row after `key` (else before it), "" when it is the only one.
    function neighbourOf(key) {
        for (let i = 0; i < keys.count; ++i) {
            if (keys.get(i).key !== key)
                continue
            if (i + 1 < keys.count)
                return keys.get(i + 1).key
            return i > 0 ? keys.get(i - 1).key : ""
        }
        return ""
    }
    function rowItem(i) {
        return i >= 0 && i < rows.count ? rows.itemAt(i) as PropertyRow : null
    }
    /// The keyboard to the row `step` after the one of `key` (past the last: the add row; before
    /// the first: the editor).
    function focusNext(key, step) {
        let i = -1
        for (let k = 0; k < keys.count; ++k) {
            if (keys.get(k).key === key)
                i = k
        }
        const next = i + step
        if (next >= keys.count) {
            if (canAdd)
                openAdd()
            else
                leaveToBlocks()
            return
        }
        if (next < 0) {
            editor.editingProperty = null
            editor.listView.forceActiveFocus()
            return
        }
        const item = rowItem(next)
        if (item)
            item.focusRow()
    }
    /// Alt+↓: to the first block after the properties.
    function leaveToBlocks() {
        editor.editingProperty = null
        const first = controller ? controller.nextVisibleRow(Math.max(0, blockRow)) : -1
        if (first >= 0)
            editor.startEditing(first, 0)
        else
            editor.listView.forceActiveFocus()
    }
    function openAdd() {
        if (!canAdd)
            return
        addError = ""
        adding = true
        Qt.callLater(() => {
            if (panel && addLoader.item)
                (addLoader.item as Item).forceActiveFocus()
        })
    }
    function closeAdd() {
        adding = false
        addError = ""
        if (draft)
            editor.propertyDraft = false
    }
    /// Write the new key (an empty value of its notebook type) and give its value the focus.
    function addKey(key) {
        const k = String(key).trim()
        if (k === "") {
            closeAdd()
            return
        }
        // (The draft goes once the property is there: only locals after the write.)
        const ed = editor
        const ctl = controller
        const isDraft = draft
        const notebook = ctl.notebookPropertyType(k)
        const type = notebook !== "" && notebook !== "unsupported" ? notebook : "text"
        const empty = type === "list" || type === "tags" || type === "aliases" ? [] : type === "checkbox" ? false : null
        const r = ctl.setPageProperty(isDraft ? "" : source, k, empty, type)
        if (!r.ok) {
            addError = r.reason
            return
        }
        addError = ""
        adding = false
        ed.propertyFocus = { key: k, part: type === "checkbox" ? "row" : "value" }
        ed.setPropertiesExpanded(true)
        if (isDraft)
            ed.propertyDraft = false
    }
    /// The row's menu (created when first needed: docs/UI.md "Scrolling performance").
    property PropertyMenu menu: null
    function openMenu(row, item, x, y) {
        if (!menu)
            menu = menuComponent.createObject(panel) as PropertyMenu
        menu.row = row
        menu.bottomMargin = editor.menuBottomMargin()
        menu.popup(item, x, y)
    }
    function typeIcon(t) {
        switch (t) {
        case "number": return "sigma"
        case "checkbox": return "square-check-big"
        case "date": return "calendar"
        case "datetime": return "calendar-clock"
        case "list": return "list"
        case "tags": return "tags"
        case "aliases": return "bookmark"
        default: return "type"
        }
    }

    // ---- rows: one per key, kept while the key stays ------------------------------------------------
    ListModel { id: keys }
    function syncKeys() {
        const want = draft ? [] : properties.map(p => p.key)
        for (let i = keys.count - 1; i >= 0; --i) {
            if (want.indexOf(keys.get(i).key) < 0)
                keys.remove(i)
        }
        for (let i = 0; i < want.length; ++i) {
            if (i < keys.count && keys.get(i).key === want[i])
                continue
            let at = -1
            for (let k = i + 1; k < keys.count; ++k) {
                if (keys.get(k).key === want[i]) {
                    at = k
                    break
                }
            }
            if (at >= 0)
                keys.move(at, i, 1)
            else
                keys.insert(i, { key: want[i] })
        }
    }
    onPropertiesChanged: syncKeys()
    Component.onCompleted: {
        syncKeys()
        takeFocusRequest()
    }
    /// BlockEditor.propertyFocus { part: "add" } (Ctrl+; and "Dodaj właściwość"): the add row.
    function takeFocusRequest() {
        const f = editor ? editor.propertyFocus : null
        if (!f || f.part !== "add" || (f.source && f.source !== source && !draft))
            return
        editor.propertyFocus = null
        openAdd()
    }
    Connections {
        target: panel.editor
        function onPropertyFocusChanged() { panel.takeFocusRequest() }
    }

    /// The sum of the rows' heights (bindings: the panel has its final height at once).
    property int revision: 0
    readonly property real rowsHeight: {
        let h = revision >= 0 ? 0 : 0 // (read: the Repeater's items came or went)
        for (let i = 0; i < rows.count; ++i) {
            const item = rows.itemAt(i)
            if (item)
                h += item.height
        }
        return h
    }
    Column {
        id: rowColumn
        width: panel.width
        Repeater {
            id: rows
            model: keys
            onItemAdded: panel.revision += 1
            onItemRemoved: panel.revision += 1
            delegate: PropertyRow {
                width: rowColumn.width
                panel: panel
            }
        }
    }

    // ---- the add row ------------------------------------------------------------------------------
    Item {
        id: addRow
        y: panel.rowsHeight
        width: panel.width
        visible: panel.canAdd
        height: panel.adding ? Math.max(panel.rowHeight, addLoader.item ? (addLoader.item as Item).implicitHeight + Theme.space1 : 0)
                             : panel.rowHeight

        T.AbstractButton {
            id: addButton
            objectName: "propertiesAdd"
            visible: !panel.adding
            height: panel.rowHeight
            width: Math.min(panel.width, addLabel.implicitWidth + panel.iconWidth + Theme.space3)
            focusPolicy: Qt.NoFocus
            hoverEnabled: !Theme.touchUi
            Accessible.role: Accessible.Button
            Accessible.name: addLabel.text
            onClicked: panel.openAdd()
            background: Rectangle {
                radius: Theme.radiusSm
                color: addButton.hovered || addButton.down ? Theme.bgHover : "transparent"
            }
            contentItem: Item {
                BIcon {
                    x: (panel.iconWidth - width) / 2
                    anchors.verticalCenter: parent.verticalCenter
                    name: "plus"
                    size: Theme.iconSizeSm
                    color: Theme.fgMuted
                }
                Text {
                    id: addLabel
                    x: panel.iconWidth + Theme.space1
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Add property")
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgMuted
                }
            }
        }
        Loader {
            id: addLoader
            active: panel.adding
            visible: active
            x: panel.iconWidth
            width: Math.min(panel.width - x, 360)
            sourceComponent: BSuggestField {
                id: keyField
                objectName: "propertiesKeyField"
                size: "small"
                placeholderText: qsTr("Property name")
                errorText: panel.addError
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                provider: (typed) => panel.controller.suggestPropertyKeys(typed, 8).map(k => ({
                    title: k.key,
                    subtitle: qsTr("%n page(s)", "", k.count),
                    icon: panel.typeIcon(k.type),
                    value: k.key
                }))
                property bool done: false
                Component.onCompleted: {
                    forceActiveFocus()
                    if (panel.controller && panel.controller.editingRow >= 0)
                        panel.controller.stopEditing()
                    panel.editor.editingProperty = keyField
                    panel.editor.later(() => panel.editor.ensureEditVisible())
                }
                onTypedTextChanged: panel.addError = ""
                onPicked: (value) => panel.addKey(value)
                onCommitted: (text) => panel.addKey(text)
                onCancelled: {
                    panel.closeAdd()
                    panel.editor.listView.forceActiveFocus()
                }
                // Leaving an empty field drops the draft; a typed name is not written by a tap
                // elsewhere (Enter or a suggestion writes it).
                onActiveFocusChanged: if (!activeFocus && text.trim() === "" && panel.adding) panel.closeAdd()
            }
        }
    }

    // ---- a block that cannot be edited in fields ----------------------------------------------------
    Item {
        id: note
        visible: !panel.editable && !panel.draft
        y: panel.rowsHeight + Theme.space2
        width: panel.width
        height: !visible ? 0 : panel.stacked ? noteText.implicitHeight + Theme.space1 + noteButton.implicitHeight
                                             : Math.max(noteText.implicitHeight, noteButton.implicitHeight)
        Text {
            id: noteText
            width: panel.stacked ? parent.width : parent.width - noteButton.width - Theme.space3
            text: qsTr("These properties cannot be changed safely in fields.")
            wrapMode: Text.Wrap
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            color: Theme.fgMuted
        }
        BButton {
            id: noteButton
            objectName: "propertiesEditAsText"
            x: panel.stacked ? 0 : parent.width - width
            y: panel.stacked ? noteText.implicitHeight + Theme.space1 : 0
            size: "small"
            variant: "secondary"
            text: qsTr("Edit as text")
            onClicked: panel.editor.startEditing(panel.blockRow, -1)
        }
    }

    Component {
        id: menuComponent
        PropertyMenu {}
    }
}
