pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// PropertyRow — one page property in the properties panel (PropertiesPanel, docs/UI.md "Page
// properties"): the type icon (a click opens the row's menu: type, rename, copy, remove), the
// key, and the value edited by its type — text (rendered: [[links]] and #tags work; a click
// beside them edits it in a field with the notebook's values as suggestions, "[[" pages; a text of
// several lines in a text area), number ("3,5" and "1 200" accepted), checkbox (each toggle one
// undo step), date / date and time (the date dialog: a calendar, natural language "jutro"),
// list / tags / aliases (PropertyListValue: chips). Every change goes through PageController
// (one undo step, only that property's lines change). The key is in a column beside the value
// (phones too); a panel too narrow for that (a value column under ~168 px) stacks the key above the
// value. Every control and tap area — the key's cell, the value's — is at least Theme.touchTarget
// tall on touch UIs.
//
// The row takes the keyboard focus (Tab, ↑/↓ in the panel): → or Enter edits the value, ← or F2
// renames, Ctrl+Backspace / Delete removes, the Menu key / Shift+F10 opens the menu, Alt+↓ goes
// to the first block, Ctrl+Z / Ctrl+Shift+Z undo and redo the page's last step, Escape back to the
// editor. An open field commits on Enter and when it
// loses the focus (also when the row goes away, and before the editor shows another page:
// PageController.pathAboutToChange), Escape leaves it unchanged. A field holds what is typed: it
// starts with the value of the moment it opened (no binding — the notebook changing re-renders the
// rows) and writes only what differs from it, so a value left as it was keeps its form (`|`
// text, `1.50`).
FocusScope {
    id: row
    objectName: "propertyRow_" + key

    required property string key
    /// The panel (an Item: PropertiesPanel cannot pass itself typed — the file's own type).
    required property Item panelItem
    readonly property PropertiesPanel panel: panelItem as PropertiesPanel

    /// The property ({ key, value, type, source, raw, items, empty, editable, multiline, linkDate,
    /// html }: PropertiesPanel.propertyOf); an empty map while the key is not there.
    readonly property MapHolder entry: MapHolder { map: row.panel.propertyOf(row.key) }
    readonly property bool present: entry.map.key !== undefined
    final property BlockEditor editor: panel.editor
    final property PageController controller: panel.controller
    readonly property int blockRow: panel.blockRow
    readonly property string source: present ? BlockViews.textAt(entry.map, "source") : panel.source
    readonly property bool editable: present && entry.map.editable === true && panel.editable
    readonly property bool empty: present && entry.map.empty === true
    /// The value as one line (`value` of the entry).
    readonly property string valueText: BlockViews.textAt(entry.map, "value")
    /// The value as written (String(raw): a date's "YYYY-MM-DD", a text with its line breaks).
    readonly property string rawText: present ? BlockViews.textAt(entry.map, "raw") : ""
    readonly property string lowerKey: Js.toLowerCase(key)
    readonly property bool fixedType: lowerKey === "tags" || lowerKey === "tag" || lowerKey === "aliases" || lowerKey === "alias"
    /// The type the value is edited as: the value's own; an empty one takes the type chosen in
    /// the menu this session, else the key's type in the notebook.
    readonly property string type: {
        const own = present ? BlockViews.textAt(entry.map, "type") : "text"
        if (!empty || fixedType)
            return own
        const chosen = editor ? BlockViews.textAt(editor.propertyTypeOverride, key) : ""
        if (chosen !== "")
            return chosen
        const notebook = controller ? controller.notebookPropertyType(key) : ""
        return notebook !== "" && notebook !== "unsupported" ? notebook : own
    }
    readonly property bool stacked: panel.stacked
    readonly property real rowHeight: panel.rowHeight
    /// The key's line: the row's height beside the value; above it (stacked) a label's line, a
    /// touch target on touch UIs (the key's cell is its tap area).
    readonly property real keyLineHeight: stacked ? (Theme.touchUi ? Theme.touchTarget : 28) : rowHeight
    readonly property real iconWidth: Theme.touchUi ? Theme.touchTarget : 24
    readonly property real valueX: stacked ? 0 : iconWidth + panel.keyWidth + Theme.space2
    readonly property real valueY: stacked ? keyLineHeight : 0
    readonly property real valueWidth: Math.max(60, width - valueX - Theme.space1)
    /// The value's cell in the value's coordinates (valueLoader): beside the key the row's height
    /// (the padding above and below the value included), stacked the line under the key — a
    /// value's tap area covers it, so it is a touch target tall on touch UIs.
    readonly property real cellTop: stacked ? 0 : -valuePadding
    readonly property real cellHeight: height - valueY
    /// Beside the key, the space above (and below) the value: its line centred in the row's height —
    /// a text, number, date or checkbox line is rowHeight − 2 × space1; a list's chip line its
    /// tallest item (touch: the chips' 44 px, so none; desktop: the 28-px "+", so 2). Every
    /// one-line row is rowHeight high, a list row of two lines two chip lines and their spacing.
    readonly property real valuePadding: {
        const list = valueLoader.item as PropertyListValue
        return list ? Math.max(0, (rowHeight - list.lineHeight) / 2) : Theme.space1
    }

    /// The value is being edited in a field (text, number) / the key is being renamed.
    property bool editingValue: false
    property bool renaming: false
    /// A refused edit's reason (shown under the field) and status (a status other than
    /// "invalidValue" was said in a toast already).
    property string error: ""
    property string errorStatus: ""
    /// A field closes with a refused value: its reason in a toast (unless one said it already).
    function noticeError(): void {
        if (error !== "" && errorStatus === "invalidValue")
            editor.notice(error, "error", null)
    }
    /// The editor is about to show another page: an open field writes what it holds now.
    signal leaving()

    implicitHeight: stacked ? valueY + Math.max(valueLoader.implicitHeight, rowHeight)
                            : Math.max(rowHeight, valueLoader.implicitHeight + 2 * valuePadding)
    height: implicitHeight
    activeFocusOnTab: true

    Accessible.role: Accessible.ListItem
    Accessible.name: qsTr("Property %1: %2").arg(key).arg(row.valueText)

    /// What the last edit answered ({ ok, status, reason, lost }).
    MapHolder { id: result }
    /// BlockEditor.propertyFocus read.
    MapHolder { id: focusRequest }

    function typeIcon(t: string): string {
        switch (t) {
        case "number": return "sigma"
        case "checkbox": return "square-check-big"
        case "date": return "calendar"
        case "datetime": return "calendar-clock"
        case "list": return "list"
        case "tags": return "tags"
        case "aliases": return "bookmark"
        case "unsupported": return "code"
        default: return "type"
        }
    }
    function typeName(t: string): string {
        switch (t) {
        case "number": return qsTr("Number")
        case "checkbox": return qsTr("Checkbox")
        case "date": return qsTr("Date")
        case "datetime": return qsTr("Date and time")
        case "list": return qsTr("List")
        case "tags": return qsTr("Tags")
        case "aliases": return qsTr("Aliases")
        case "unsupported": return qsTr("Other")
        default: return qsTr("Text")
        }
    }

    // ---- edits ------------------------------------------------------------------------------------
    /// Write `value` as the row's type; false (and `error` set) when it was refused.
    function setValue(value: var): bool {
        if (!controller || !present)
            return false
        result.map = controller.setPageProperty(source, key, value, type)
        const ok = result.map.ok === true
        error = ok ? "" : BlockViews.textAt(result.map, "reason")
        errorStatus = ok ? "" : BlockViews.textAt(result.map, "status")
        if (!ok && result.map.status !== "invalidValue")
            editor.notice(result.map.reason, "error", null)
        return ok
    }
    function remove(): void {
        if (!controller || !editable)
            return
        // The row goes with its property: only locals after the edit.
        const ed = editor
        const ctl = controller
        const before = row.undoToken()
        const name = key
        const next = row.neighbour()
        result.map = ctl.removePageProperty(source, key)
        if (!result.map.ok) {
            ed.notice(result.map.reason, "error", null)
            return
        }
        ed.noticeUndoable(qsTr("Removed property “%1”").arg(name), "info", before)
        if (next !== "")
            ed.propertyFocus = { key: next, part: "row" }
        else
            ed.listView.forceActiveFocus()
    }
    /// The page's undo token now (PageController.undoToken).
    function undoToken(): string {
        return row.controller.undoToken()
    }
    /// The key of the row after this one (else before it).
    function neighbour(): string {
        return row.panel.neighbourOf(row.key)
    }
    function setType(t: string): void {
        if (!controller || !editable || t === type)
            return
        if (empty) {
            // Nothing to convert: the type is remembered for this session (a list is written `[]`).
            editor.setPropertyTypeOverride(key, t)
            if (t !== "list")
                return
        }
        const before = row.undoToken()
        result.map = controller.setPagePropertyType(source, key, t)
        if (!result.map.ok)
            editor.notice(result.map.reason, "error", null)
        else if (result.map.lost)
            editor.noticeUndoable(result.map.reason, "info", before)
    }
    function rename(newKey: string): bool {
        const k = Js.trim(newKey)
        if (k === "" || k === key) {
            renaming = false
            focusRow()
            return true
        }
        const ed = editor
        result.map = controller.renamePageProperty(source, key, k)
        if (!result.map.ok) {
            error = result.map.reason
            return false
        }
        // (This row goes: the row of the new name takes the keyboard.)
        ed.propertyFocus = { key: k, part: "row" }
        return true
    }
    /// The list's items (a list, tags, aliases; none for other values).
    function items(): list<string> {
        if (!present || entry.map.items === undefined)
            return []
        return entry.map.items
    }
    /// No values held (the suggestions of a text value leave none out).
    readonly property list<string> noValues: []
    function copyValue(): void {
        if (!present)
            return
        const list = row.items()
        const text = list.length > 0 ? list.join(", ") : row.valueText
        result.map = controller.copyText(text)
        if (result.map.ok)
            editor.notice(qsTr("Copied %1").arg(text), "success", null)
    }

    // ---- focus ------------------------------------------------------------------------------------
    function focusRow(): void {
        editingValue = false
        renaming = false
        row.forceActiveFocus()
        row.fieldFocused(row)
    }
    /// A field of this row (or the row) got the keyboard: block editing ends, the editor keeps it
    /// above the on-screen keyboard (BlockEditor.editingProperty).
    function fieldFocused(item: Item): void {
        if (controller && controller.editingRow >= 0)
            controller.stopEditing()
        editor.editingProperty = item
        editor.laterEnsureEditVisible()
    }
    function startValueEdit(): void {
        if (!editable)
            return
        error = ""
        switch (type) {
        case "checkbox":
            setValue(!(present && entry.map.raw === true))
            break
        case "date":
        case "datetime":
            pickDate()
            break
        case "list":
        case "tags":
        case "aliases":
            if (valueLoader.item as PropertyListValue)
                (valueLoader.item as PropertyListValue).openAdd()
            break
        case "unsupported":
            break
        default:
            editingValue = true
        }
    }
    function startRename(): void {
        if (!editable)
            return
        error = ""
        renaming = true
    }
    /// The date dialog's request of this row (EditorDialogs.applied).
    readonly property string dateTarget: "property:" + key
    function pickDate(): void {
        const current = present && !empty ? Js.replace(rawText, "T", " ") : ""
        editor.dialogs.pickPropertyDate(row.dateTarget, key, current, type === "datetime")
    }
    Connections {
        target: row.editor ? row.editor.dialogs : null
        function onApplied(target: string, action: string, a: var, b: var) {
            if (target !== row.dateTarget || !row.present)
                return
            if (action === "remove")
                row.setValue(null)
            else if (action === "set")
                row.setValue(a)
            row.focusRow()
        }
    }
    function openMenu(item: Item, x: real, y: real): void {
        panel.openMenu(row, item, x, y)
    }
    /// A focus request for this row (BlockEditor.propertyFocus: after adding, renaming, removing).
    function takeFocusRequest(): void {
        if (!editor || editor.propertyFocus === null || editor.propertyFocus === undefined)
            return
        focusRequest.map = editor.propertyFocus
        if (focusRequest.map.key !== key)
            return
        editor.propertyFocus = null
        row.focusPart = focusRequest.map.part || ""
        focusTimer.restart()
    }
    /// The part a focus request asked for (taken once the events being handled are done).
    property string focusPart: ""
    Later {
        id: focusTimer
        onTriggered: {
            const part = row.focusPart
            if (part === "value")
                row.startValueEdit()
            else if (part === "key")
                row.startRename()
            else
                row.focusRow()
            if (part !== "value" || (row.type !== "date" && row.type !== "datetime"))
                row.fieldFocused(row)
        }
    }
    Component.onCompleted: takeFocusRequest()
    Connections {
        target: row.editor
        function onPropertyFocusChanged() { row.takeFocusRequest() }
    }
    Connections {
        target: row.controller
        function onPathAboutToChange() { row.leaving() }
    }
    onActiveFocusChanged: if (activeFocus && !editingValue && !renaming) fieldFocused(row)

    /// A field of the row has the keyboard (its keys are its own; the ones it leaves are not the
    /// row's either: ← at a field's start must not start renaming).
    readonly property bool fieldOpen: editingValue || renaming
                                      || ((valueLoader.item as PropertyListValue) !== null
                                          && (valueLoader.item as PropertyListValue).adding)
    Keys.onPressed: (event) => {
        if (row.fieldOpen)
            return
        if (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier)) {
            panel.leaveToBlocks()
        } else if (event.key === Qt.Key_Down) {
            panel.focusNext(key, 1)
        } else if (event.key === Qt.Key_Up) {
            panel.focusNext(key, -1)
        } else if (event.key === Qt.Key_Right || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            startValueEdit()
        } else if (event.key === Qt.Key_Space && type === "checkbox") {
            startValueEdit()
        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_F2) {
            startRename()
        } else if (event.key === Qt.Key_Delete
                   || (event.key === Qt.Key_Backspace && (event.modifiers & Qt.ControlModifier))) {
            remove()
        } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            openMenu(typeButton, typeButton.width / 2, typeButton.height)
        } else if (event.key === Qt.Key_Semicolon && (event.modifiers & Qt.ControlModifier)) {
            panel.openAdd()
        } else if (event.key === Qt.Key_Escape) {
            editor.editingProperty = null
            editor.listView.forceActiveFocus()
        } else if (event.matches(StandardKey.Undo)) {
            controller.undo()
        } else if (event.matches(StandardKey.Redo)) {
            controller.redo()
        } else {
            return
        }
        event.accepted = true
    }

    // ---- look -------------------------------------------------------------------------------------
    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSm
        color: row.activeFocus ? Theme.alpha(Theme.focusRing, 0.10) : rowHover.hovered ? Theme.bgHover : "transparent"
        border.width: row.activeFocus ? 1 : 0
        border.color: Theme.alpha(Theme.focusRing, 0.6)
    }
    HoverHandler { id: rowHover; enabled: !Theme.touchUi }
    // The row's menu: a right-click anywhere on it (a mouse), a long press with a finger — not on a
    // list's chip: that opens the chip's own menu (PropertyListValue; this handler sees the press
    // too, after the chip took it).
    TapHandler {
        acceptedButtons: Qt.RightButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        onTapped: (point) => row.openMenu(row, point.position.x, point.position.y)
    }
    /// Where a long press was, in the list's coordinates.
    property point pressAt
    TapHandler {
        id: rowHold
        acceptedDevices: PointerDevice.TouchScreen
        onLongPressed: {
            const list = valueLoader.item as PropertyListValue
            if (list) {
                row.pressAt = list.mapFromItem(row, rowHold.point.position.x, rowHold.point.position.y)
                if (list.chipAt(row.pressAt.x, row.pressAt.y) !== null)
                    return
            }
            row.openMenu(row, rowHold.point.position.x, rowHold.point.position.y)
        }
    }

    // The type icon: the menu.
    T.AbstractButton {
        id: typeButton
        objectName: "propertyTypeButton"
        y: (row.keyLineHeight - height) / 2
        width: row.iconWidth
        height: Theme.touchUi ? Theme.touchTarget : Math.min(row.rowHeight, 24)
        focusPolicy: Qt.NoFocus
        hoverEnabled: !Theme.touchUi
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Property menu: %1").arg(row.typeName(row.type))
        onClicked: row.openMenu(typeButton, 0, typeButton.height)
        background: Rectangle {
            radius: Theme.radiusSm
            color: typeButton.hovered || typeButton.down ? Theme.bgHover : "transparent"
        }
        contentItem: Item {
            BIcon {
                anchors.centerIn: parent
                name: row.typeIcon(row.type)
                size: Theme.iconSizeSm
                color: Theme.fgMuted
            }
        }
    }

    // The key (a field while renaming). Its tap area is the whole key cell: the row's height
    // beside the value (the key's line when stacked), up to the value.
    Text {
        id: keyText
        objectName: "propertyKey"
        visible: !row.renaming
        x: row.iconWidth + Theme.space1
        y: (row.keyLineHeight - height) / 2
        width: row.stacked ? row.width - x - Theme.space2 : row.panel.keyWidth - Theme.space1
        text: row.key
        textFormat: Text.PlainText
        elide: Text.ElideRight
        font.family: Theme.fontUi
        font.pixelSize: Theme.textBase
        color: Theme.fgMuted
    }
    Item {
        objectName: "propertyKeyCell"
        visible: !row.renaming
        x: row.iconWidth
        width: row.stacked ? row.width - x : row.valueX - x
        height: row.stacked ? row.keyLineHeight : row.height
        TapHandler {
            enabled: row.editable
            acceptedButtons: Qt.LeftButton
            onDoubleTapped: row.startRename()
            onTapped: row.focusRow()
        }
    }
    Loader {
        id: renameLoader
        active: row.renaming
        visible: active
        x: row.iconWidth
        y: (row.keyLineHeight - (Theme.touchUi ? Theme.touchTarget : Theme.controlHeightSm)) / 2
        width: row.stacked ? row.width - x : row.panel.keyWidth + row.valueWidth
        z: 2
        sourceComponent: BSuggestField {
            id: renameField
            objectName: "propertyRenameField"
            size: "small"
            text: row.key
            errorText: row.error
            accessibleName: qsTr("New name of “%1”").arg(row.key)
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            suggesting: true
            onSuggestionsRequested: (typed) => {
                renameField.suggestions = BlockViews.propertyKeyRows(row.controller.suggestPropertyKeys(typed, 8), false, true)
            }
            property bool done: false
            Component.onCompleted: {
                forceActiveFocus()
                selectAll()
                row.fieldFocused(renameField)
            }
            onPicked: (value) => { if (row.rename(value)) renameField.done = true }
            onCommitted: (text) => { if (row.rename(text)) renameField.done = true }
            onCancelled: {
                renameField.done = true
                row.error = ""
                row.focusRow()
            }
            onActiveFocusChanged: {
                if (renameField.activeFocus || renameField.done)
                    return
                renameField.done = true
                if (!row.rename(renameField.text))
                    row.renaming = false
            }
            Connections {
                target: row
                function onLeaving() {
                    if (renameField.done)
                        return
                    renameField.done = true
                    if (!row.rename(renameField.text))
                        row.editor.notice(row.error, "error", null)
                }
            }
        }
    }

    // The value.
    Loader {
        id: valueLoader
        x: row.valueX + (row.stacked ? row.iconWidth : 0)
        y: row.valueY + (row.stacked ? 0 : row.valuePadding)
        width: row.valueWidth - (row.stacked ? row.iconWidth : 0)
        sourceComponent: {
            switch (row.type) {
            case "checkbox": return checkboxValue
            case "date":
            case "datetime": return dateValue
            case "list":
            case "tags":
            case "aliases": return listValue
            case "number": return numberValue
            default: return textValue
            }
        }
    }

    Component {
        id: textValue
        Item {
            id: textBox
            readonly property real lineHeight: row.rowHeight - 2 * Theme.space1
            readonly property string valueHtml: !row.present || row.empty ? "" : BlockViews.textAt(row.entry.map, "html")
            /// The link under the mouse or the pressing finger, underlined (BlockText.qml).
            property string hoveredLink: ""
            /// The link at (x, y) of the value's cell ("" none): the cell and the text are siblings.
            function linkAtCell(x: real, y: real): string {
                return display.linkAt(x + displayCell.x - display.x, y + displayCell.y - display.y)
            }
            function trackLink(x: real, y: real): void {
                const link = row.editingValue || textBox.valueHtml === "" ? "" : textBox.linkAtCell(x, y)
                if (link !== textBox.hoveredLink)
                    textBox.hoveredLink = link
            }
            implicitHeight: row.editingValue && fieldLoader.item ? (fieldLoader.item as Item).implicitHeight
                                                                  : Math.max(lineHeight, display.implicitHeight)
            Text {
                id: display
                objectName: "propertyValue"
                visible: !row.editingValue
                width: parent.width
                y: Math.max(0, (textBox.lineHeight - Math.min(implicitHeight, textBox.lineHeight)) / 2)
                text: !row.present ? "" : row.empty ? qsTr("Empty") : TextHelper.hoveredLinkHtml(textBox.valueHtml, textBox.hoveredLink)
                textFormat: row.present && !row.empty ? Text.RichText : Text.PlainText
                wrapMode: Text.Wrap
                maximumLineCount: row.present && row.entry.map.multiline === true ? 6 : 3
                elide: Text.ElideRight
                font.family: Theme.fontUi
                font.pixelSize: Theme.textBase
                font.italic: row.empty
                color: row.empty || row.type === "unsupported" ? Theme.fgMuted : Theme.fg
                linkColor: Theme.link
            }
            // The tap area: the value's whole cell (a link opens, elsewhere the value is edited).
            Item {
                id: displayCell
                visible: !row.editingValue
                y: row.cellTop
                width: parent.width
                height: Math.max(textBox.implicitHeight, row.cellHeight)
                HoverHandler {
                    id: displayHover
                    cursorShape: textBox.linkAtCell(displayHover.point.position.x, displayHover.point.position.y) !== ""
                                 ? Qt.PointingHandCursor : row.editable ? Qt.IBeamCursor : Qt.ArrowCursor
                    onPointChanged: textBox.trackLink(displayHover.point.position.x, displayHover.point.position.y)
                    onHoveredChanged: if (!hovered && !displayTap.pressed) textBox.hoveredLink = ""
                }
                TapHandler {
                    id: displayTap
                    onPressedChanged: {
                        if (pressed)
                            textBox.trackLink(displayTap.point.position.x, displayTap.point.position.y)
                        else if (!displayHover.hovered)
                            textBox.hoveredLink = ""
                    }
                    onTapped: (point) => {
                        const link = textBox.linkAtCell(point.position.x, point.position.y)
                        if (link !== "")
                            row.editor.activateLink(row.blockRow, link)
                        else
                            row.startValueEdit()
                    }
                }
            }
            // Screen readers: the value's links as link items of their own.
            BLinkTargets {
                textItem: display
                html: textBox.valueHtml
                maxHeight: display.height
                shown: !row.editingValue
                onActivated: (href, x, y) => row.editor.activateLink(row.blockRow, href)
            }
            Loader {
                id: fieldLoader
                width: parent.width
                active: row.editingValue
                visible: active
                sourceComponent: row.present && row.entry.map.multiline === true ? areaField : lineField
            }
            Component {
                id: lineField
                BSuggestField {
                    id: valueField
                    objectName: "propertyValueField"
                    size: "small"
                    errorText: row.error
                    accessibleName: qsTr("Value of “%1”").arg(row.key)
                    suggesting: true
                    // The notebook's values of the key ("[[": pages).
                    onSuggestionsRequested: (typed) => {
                        valueField.suggestions = BlockViews.propertyValueRows(
                                    row.controller.suggestPropertyValues(row.key, typed, 8), false, row.noValues, "PropertyRow")
                    }
                    /// The value when the field opened (what it starts with).
                    property string initial: ""
                    property bool done: false
                    function commit(value: string): void {
                        if (valueField.done)
                            return
                        valueField.done = true
                        if (value !== valueField.initial)
                            row.setValue(value)
                        row.editingValue = false
                    }
                    Component.onCompleted: {
                        initial = row.present && !row.empty ? row.rawText : ""
                        text = initial
                        forceActiveFocus()
                        cursorPosition = length
                        row.fieldFocused(valueField)
                    }
                    Component.onDestruction: {
                        if (!valueField.done && valueField.text !== valueField.initial) {
                            valueField.done = true
                            row.setValue(valueField.text)
                        }
                    }
                    onPicked: (value) => { valueField.commit(value); row.focusRow() }
                    onCommitted: (text) => { valueField.commit(text); row.focusRow() }
                    onCancelled: {
                        valueField.done = true
                        row.editingValue = false
                        row.focusRow()
                    }
                    onActiveFocusChanged: if (!valueField.activeFocus) valueField.commit(valueField.text)
                    Connections {
                        target: row
                        function onLeaving() {
                            if (valueField.preeditText !== "")
                                EditorInput.commitPreedit(valueField)
                            valueField.commit(valueField.text)
                        }
                    }
                }
            }
            Component {
                id: areaField
                BTextArea {
                    id: valueArea
                    objectName: "propertyValueArea"
                    errorText: row.error
                    wrapMode: TextEdit.Wrap
                    accessibleName: qsTr("Value of “%1”").arg(row.key)
                    /// The value when the area opened (what it starts with).
                    property string initial: ""
                    property bool done: false
                    function commit(): void {
                        if (valueArea.done)
                            return
                        valueArea.done = true
                        if (valueArea.text !== valueArea.initial)
                            row.setValue(valueArea.text)
                        row.editingValue = false
                    }
                    Component.onCompleted: {
                        initial = row.present && !row.empty ? row.rawText : ""
                        text = initial
                        forceActiveFocus()
                        cursorPosition = length
                        row.fieldFocused(valueArea)
                    }
                    Component.onDestruction: {
                        if (!valueArea.done && valueArea.text !== valueArea.initial) {
                            valueArea.done = true
                            row.setValue(valueArea.text)
                        }
                    }
                    onActiveFocusChanged: if (!valueArea.activeFocus) valueArea.commit()
                    Connections {
                        target: row
                        function onLeaving() {
                            if (valueArea.preeditText !== "")
                                EditorInput.commitPreedit(valueArea)
                            valueArea.commit()
                        }
                    }
                    Keys.onPressed: (event) => {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                            valueArea.commit()
                            row.focusRow()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Escape) {
                            valueArea.done = true
                            row.editingValue = false
                            row.focusRow()
                            event.accepted = true
                        }
                    }
                }
            }
        }
    }

    Component {
        id: numberValue
        Item {
            id: numberBox
            readonly property real lineHeight: row.rowHeight - 2 * Theme.space1
            implicitHeight: row.editingValue && numberLoader.item ? (numberLoader.item as Item).implicitHeight : lineHeight
            Text {
                objectName: "propertyValue"
                visible: !row.editingValue
                width: parent.width
                height: numberBox.lineHeight
                verticalAlignment: Text.AlignVCenter
                text: !row.present ? "" : row.empty ? qsTr("Empty") : row.valueText
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.family: Theme.fontUi
                font.pixelSize: Theme.textBase
                font.italic: row.empty
                font.features: { "tnum": 1 }
                color: row.empty ? Theme.fgMuted : Theme.fg
            }
            // The tap area: the value's whole cell.
            Item {
                visible: !row.editingValue
                y: row.cellTop
                width: parent.width
                height: Math.max(numberBox.lineHeight, row.cellHeight)
                TapHandler { onTapped: row.startValueEdit() }
                HoverHandler { cursorShape: row.editable ? Qt.IBeamCursor : Qt.ArrowCursor }
            }
            Loader {
                id: numberLoader
                width: Math.min(parent.width, 220)
                active: row.editingValue
                visible: active
                sourceComponent: BTextField {
                    id: numberField
                    objectName: "propertyNumberField"
                    size: "small"
                    errorText: row.error
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    accessibleName: qsTr("Value of “%1”").arg(row.key)
                    /// The number as written when the field opened (what it starts with).
                    property string initial: ""
                    property bool done: false
                    /// Write it; the field stays open with the error when it is no number.
                    function commit(): bool {
                        if (numberField.done)
                            return true
                        if (numberField.text !== numberField.initial && !row.setValue(numberField.text))
                            return false
                        numberField.done = true
                        row.error = ""
                        row.editingValue = false
                        return true
                    }
                    Component.onCompleted: {
                        initial = row.present && !row.empty ? row.valueText : ""
                        text = initial
                        forceActiveFocus()
                        selectAll()
                        row.fieldFocused(numberField)
                    }
                    Component.onDestruction: if (!numberField.done && numberField.text !== numberField.initial) row.setValue(numberField.text)
                    Connections {
                        target: row
                        function onLeaving() {
                            if (numberField.preeditText !== "")
                                EditorInput.commitPreedit(numberField)
                            if (!numberField.commit()) {
                                numberField.done = true
                                row.noticeError()
                            }
                        }
                    }
                    onAccepted: if (numberField.commit()) row.focusRow()
                    onEscapePressed: {
                        numberField.done = true
                        row.error = ""
                        row.editingValue = false
                        row.focusRow()
                    }
                    onActiveFocusChanged: {
                        if (numberField.activeFocus || numberField.done)
                            return
                        if (!numberField.commit()) {
                            // Not a number: nothing written, the field closes (the error was said).
                            numberField.done = true
                            row.editingValue = false
                            row.noticeError()
                        }
                    }
                }
            }
        }
    }

    Component {
        id: checkboxValue
        Item {
            implicitHeight: row.rowHeight - 2 * Theme.space1
            // A box like BCheckBox's, centred on the value's line (BCheckBox lines its box up with
            // a label this row does not have); the hit area is the line's height, 44 px on touch.
            T.AbstractButton {
                id: check
                objectName: "propertyCheckBox"
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.touchUi ? Theme.touchTarget : 28
                height: Math.max(parent.height, Theme.touchUi ? Theme.touchTarget : 0)
                enabled: row.editable
                checkable: false
                checked: row.present && row.entry.map.raw === true
                focusPolicy: Qt.NoFocus
                hoverEnabled: !Theme.touchUi
                Accessible.role: Accessible.CheckBox
                Accessible.name: row.key
                Accessible.checked: checked
                onClicked: {
                    row.setValue(!check.checked)
                    row.focusRow()
                }
                background: Item {}
                contentItem: Item {
                    Rectangle {
                        id: checkBoxFrame
                        readonly property int boxSize: Theme.touchUi ? 22 : 18
                        x: 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: boxSize
                        height: boxSize
                        radius: Theme.radiusSm - 1
                        color: check.checked ? Theme.link : Theme.bgElevated
                        border.width: check.checked ? 0 : 1.5
                        border.color: check.hovered ? Theme.fgMuted : Theme.borderControl
                        antialiasing: true
                        opacity: check.enabled ? 1 : 0.5
                        BIcon {
                            anchors.centerIn: parent
                            name: "check"
                            size: checkBoxFrame.boxSize - 4
                            color: Theme.dark ? Theme.bg : "#ffffff"
                            visible: check.checked
                        }
                    }
                }
            }
        }
    }

    Component {
        id: dateValue
        Item {
            implicitHeight: row.rowHeight - 2 * Theme.space1
            T.AbstractButton {
                id: dateButton
                objectName: "propertyDateButton"
                // The value's whole cell (the row's padding included): a touch target on touch UIs;
                // what it shows keeps the value's line (insets).
                y: row.cellTop
                height: Math.max(parent.height, row.cellHeight)
                topInset: Math.max(0, -y)
                bottomInset: Math.max(0, y + height - parent.height)
                topPadding: topInset
                bottomPadding: bottomInset
                width: Math.min(parent.width, dateLabel.implicitWidth + 2 * Theme.space2 + Theme.iconSizeSm + Theme.space1)
                enabled: row.editable
                focusPolicy: Qt.NoFocus
                hoverEnabled: !Theme.touchUi
                Accessible.role: Accessible.Button
                Accessible.name: dateLabel.text
                onClicked: row.startValueEdit()
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: dateButton.hovered || dateButton.down ? Theme.bgHover : "transparent"
                }
                contentItem: Item {
                    BIcon {
                        id: dateIcon
                        x: Theme.space1
                        anchors.verticalCenter: parent.verticalCenter
                        name: row.type === "datetime" ? "calendar-clock" : "calendar"
                        size: Theme.iconSizeSm
                        color: Theme.fgMuted
                    }
                    Text {
                        id: dateLabel
                        x: dateIcon.x + dateIcon.width + Theme.space1
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, parent.width - x)
                        text: {
                            if (!row.present || row.empty)
                                return qsTr("Empty")
                            const raw = Js.replace(row.rawText, "T", " ")
                            const shown = DatesHelper.formatShort(raw)
                            return shown !== "" ? shown : raw
                        }
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textBase
                        font.italic: row.empty
                        color: row.empty ? Theme.fgMuted : Theme.fg
                    }
                }
            }
        }
    }

    Component {
        id: listValue
        PropertyListValue {
            entry: row.entry.map
            rowItem: row
        }
    }
}
