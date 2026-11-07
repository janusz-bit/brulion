pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// PropertiesPanel — the rows of one page-property block (the YAML frontmatter or the leading
// `key:: value` lines) as typed fields (PropertyRow), then "+ Add property": a field with the
// notebook's keys as suggestions (key, its type's icon, how many pages use it); choosing or
// entering a key writes it at once with an empty value of the key's type in the notebook (`key:`,
// `key: []` for a list, `key: false` for a checkbox) — one undo step — and the value's editor
// gets the focus; Escape drops the draft (nothing written). A block Brulion cannot edit safely
// shows its values read-only with "Edit as text". Used by FrontmatterView (expanded) and,
// with `draft`, by the editor's list header for the first property of a page that has none
// (BlockEditor.addProperty()). Rows stay through edits (a ListModel by key: a row is made only for
// a new key), the panel's height is a sum of its rows' (docs/UI.md "Scrolling performance").
Item {
    id: panel

    /// The editor (an Item: BlockEditor passes itself, see blockItem in the block views).
    property Item editorItem
    final property BlockEditor editor: editorItem as BlockEditor
    property PageController controller
    /// The frontmatter row (-1: the draft above the blocks).
    property int blockRow: -1
    /// The `properties` role of the row.
    property list<var> properties
    /// The `propertiesInfo` role: { source, editable, problem, canAdd }.
    property var info: ({})
    /// Only the add row (the page's first property).
    property bool draft: false
    /// `info` with its keys readable.
    readonly property MapHolder infoRec: MapHolder { map: panel.info }

    readonly property string source: panel.infoRec.map.source
                                     || (panel.controller ? panel.controller.pageProperties.addTarget || "yaml" : "yaml")
    /// Reading mode (BlockEditor.readOnly): the values are shown, never edited, no add row.
    readonly property bool readOnly: editor !== null && editor.readOnly
    readonly property bool editable: !readOnly && (draft || panel.infoRec.map.editable !== false)
    readonly property bool canAdd: readOnly ? false
                                   : draft ? (panel.controller !== null && panel.controller.pageProperties.canAdd !== false)
                                   : (editable && panel.infoRec.map.canAdd !== false)
    /// The key above the value when the two columns (icon + key | value) would cut a key or
    /// squeeze the value: the widest key does not fit 40 % of the panel, or the value column
    /// beside it would be narrower than ~168 px (a narrow side panel). Short keys (tags, status,
    /// due) keep the two columns on phones too; a long one ("last updated") stacks
    /// every row, so no key is ever cut (a touch screen has no tooltip to read it).
    readonly property bool stacked: widestKey + 12 > width * 0.4 || width - iconWidth - keyWidth - Theme.space2 < 168
    readonly property real rowHeight: Theme.touchUi ? 44 : (Theme.dense ? 28 : 32)
    readonly property real iconWidth: Theme.touchUi ? Theme.touchTarget : 24
    /// The widest key's text (font.pixelSize read for real: it follows the text size).
    readonly property real widestKey: {
        if (keyMetrics.font.pixelSize <= 0)
            return 0
        return panel.widestOf(panel.propertyKeys())
    }
    /// The key column beside the values: as wide as the widest key, at least 96 px (never cut:
    /// a key wider than 40 % of the panel stacks the rows instead).
    readonly property real keyWidth: Math.round(Math.max(96, widestKey + 12))
    /// The add field is open.
    property bool adding: draft
    property string addError: ""

    implicitHeight: rowsHeight + (canAdd ? addRow.height : 0) + (note.visible ? note.height + Theme.space2 : 0)
    height: implicitHeight

    FontMetrics {
        id: keyMetrics
        font.family: Theme.fontUi
        font.pixelSize: Theme.textBase
    }

    /// The widest of `texts` in the keys' font.
    function widestOf(texts: list<string>): real {
        return BlockViews.widestText(keyMetrics.font, texts)
    }
    /// The keys of the properties, in order.
    function propertyKeys(): list<string> {
        return BlockViews.fieldTexts(panel.properties, "key")
    }
    /// The property of `key` ({ key, value, type, source, raw, items, empty, editable, multiline,
    /// linkDate, html }); an empty map when there is none.
    function propertyOf(key: string): var {
        return BlockViews.propertyOf(panel.properties, key)
    }
    /// The key of the row after `key` (else before it), "" when it is the only one.
    function neighbourOf(key: string): string {
        const all = keys.keys
        for (let i = 0; i < all.length; ++i) {
            if (all[i] !== key)
                continue
            if (i + 1 < all.length)
                return all[i + 1]
            return i > 0 ? all[i - 1] : ""
        }
        return ""
    }
    function rowItem(i: int): PropertyRow {
        return i >= 0 && i < rows.count ? rows.itemAt(i) as PropertyRow : null
    }
    /// The keyboard to the row `step` after the one of `key` (past the last: the add row; before
    /// the first: the editor).
    function focusNext(key: string, step: int): void {
        const all = keys.keys
        let i = -1
        for (let k = 0; k < all.length; ++k) {
            if (all[k] === key)
                i = k
        }
        const next = i + step
        if (next >= all.length) {
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
    function leaveToBlocks(): void {
        editor.editingProperty = null
        const first = controller ? controller.nextVisibleRow(Math.max(0, blockRow)) : -1
        if (first >= 0)
            editor.startEditing(first, 0)
        else
            editor.listView.forceActiveFocus()
    }
    function openAdd(): void {
        if (!canAdd)
            return
        addError = ""
        adding = true
        addFocusTimer.restart()
    }
    Later {
        id: addFocusTimer
        onTriggered: {
            if (addLoader.item)
                (addLoader.item as Item).forceActiveFocus()
        }
    }
    function closeAdd(): void {
        adding = false
        addError = ""
        if (draft)
            editor.propertyDraft = false
    }
    /// Write the new key (an empty value of its notebook type) and give its value the focus. A key
    /// the page has already (any spelling) is refused in the field: adding never overwrites.
    function addKey(key: string): void {
        const k = panel.trimmed(key)
        if (k === "") {
            closeAdd()
            return
        }
        // (The draft goes once the property is there: only locals after the write.)
        const ed = editor
        const ctl = controller
        const isDraft = draft
        const notebook = panel.notebookType(k)
        const type = notebook !== "" && notebook !== "unsupported" ? notebook : "text"
        const target = isDraft ? "" : source
        // An empty value of the type: [] for a list, false for a checkbox, null else.
        if (type === "list" || type === "tags" || type === "aliases")
            result.map = ctl.addPageProperty(target, k, panel.noItems, type)
        else if (type === "checkbox")
            result.map = ctl.addPageProperty(target, k, false, type)
        else
            result.map = ctl.addPageProperty(target, k, null, type)
        if (!result.map.ok) {
            panel.addError = result.map.reason
            return
        }
        panel.addError = ""
        panel.adding = false
        ed.propertyFocus = { key: k, part: type === "checkbox" ? "row" : "value" }
        ed.setPropertiesExpanded(true)
        if (isDraft)
            ed.propertyDraft = false
    }
    /// The row's menu (created when first needed: docs/UI.md "Scrolling performance").
    property PropertyMenu menu: null
    /// What the last edit answered ({ ok, status, reason, … }).
    MapHolder { id: result }
    /// BlockEditor.propertyFocus read.
    MapHolder { id: focusRequest }
    /// An empty list (a list property's first value).
    readonly property list<string> noItems: []
    function trimmed(s: string): string {
        return Js.trim(s)
    }
    function notebookType(key: string): string {
        return panel.controller ? panel.controller.notebookPropertyType(key) : ""
    }
    function openMenu(rowItem: Item, item: Item, x: real, y: real): void {
        const row = rowItem as PropertyRow
        if (!menu)
            menu = menuComponent.createObject(panel) as PropertyMenu
        menu.row = row
        menu.bottomMargin = editor.menuBottomMargin()
        menu.popup(item, x, y)
    }
    function typeIcon(t: string): string {
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
    KeyListModel {
        id: keys
        keys: panel.wantedKeys()
    }
    function wantedKeys(): list<string> {
        if (panel.draft)
            return []
        return panel.propertyKeys()
    }
    Component.onCompleted: takeFocusRequest()
    /// BlockEditor.propertyFocus { part: "add" } (Ctrl+; and "Add property"): the add row.
    function takeFocusRequest(): void {
        if (!panel.editor || panel.editor.propertyFocus === null || panel.editor.propertyFocus === undefined)
            return
        focusRequest.map = panel.editor.propertyFocus
        const source = focusRequest.map.source || ""
        if (focusRequest.map.part !== "add" || (source !== "" && source !== panel.source && !panel.draft))
            return
        panel.editor.propertyFocus = null
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
                panelItem: panel
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
                    font.pixelSize: Theme.textBase
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
                suggesting: true
                // The notebook's keys: how many pages use each, its type's icon.
                onSuggestionsRequested: (typed) => {
                    keyField.suggestions = BlockViews.propertyKeyRows(panel.controller.suggestPropertyKeys(typed, 8), true, false)
                }
                property bool done: false
                Component.onCompleted: {
                    forceActiveFocus()
                    if (panel.controller && panel.controller.editingRow >= 0)
                        panel.controller.stopEditing()
                    panel.editor.editingProperty = keyField
                    panel.editor.laterEnsureEditVisible()
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
                onActiveFocusChanged: if (!keyField.activeFocus && Js.trim(keyField.text) === "" && panel.adding) panel.closeAdd()
            }
        }
    }

    // ---- a block that cannot be edited in fields ----------------------------------------------------
    Item {
        id: note
        visible: !panel.editable && !panel.draft && !panel.readOnly
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
