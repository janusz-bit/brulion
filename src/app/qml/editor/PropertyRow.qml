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
    required property PropertiesPanel panel

    readonly property var property: panel.propertyOf(key)
    readonly property bool present: property !== undefined && property !== null
    readonly property BlockEditor editor: panel.editor
    readonly property PageController controller: panel.controller
    readonly property int blockRow: panel.blockRow
    readonly property string source: present ? property.source : panel.source
    readonly property bool editable: present && property.editable && panel.editable
    readonly property bool empty: present && property.empty
    readonly property bool fixedType: key.toLowerCase() === "tags" || key.toLowerCase() === "tag"
                                      || key.toLowerCase() === "aliases" || key.toLowerCase() === "alias"
    /// The type the value is edited as: the value's own; an empty one takes the type chosen in
    /// the menu this session, else the key's type in the notebook.
    readonly property string type: {
        const own = present ? property.type : "text"
        if (!empty || fixedType)
            return own
        const chosen = editor ? editor.propertyTypeOverride[key] : undefined
        if (chosen)
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
    readonly property real cellTop: stacked ? 0 : -Theme.space1
    readonly property real cellHeight: height - valueY

    /// The value is being edited in a field (text, number) / the key is being renamed.
    property bool editingValue: false
    property bool renaming: false
    /// A refused edit's reason (shown under the field) and status (a status other than
    /// "invalidValue" was said in a toast already).
    property string error: ""
    property string errorStatus: ""
    /// A field closes with a refused value: its reason in a toast (unless one said it already).
    function noticeError() {
        if (error !== "" && errorStatus === "invalidValue")
            editor.notice(error, "error")
    }
    /// The editor is about to show another page: an open field writes what it holds now.
    signal leaving()

    implicitHeight: stacked ? valueY + Math.max(valueLoader.implicitHeight, rowHeight)
                            : Math.max(rowHeight, valueLoader.implicitHeight + 2 * Theme.space1)
    height: implicitHeight
    activeFocusOnTab: true

    Accessible.role: Accessible.ListItem
    Accessible.name: qsTr("Property %1: %2").arg(key).arg(present ? property.value : "")

    function typeIcon(t) {
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
    function typeName(t) {
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
    function setValue(value) {
        if (!controller || !present)
            return false
        const r = controller.setPageProperty(source, key, value, type)
        error = r.ok ? "" : r.reason
        errorStatus = r.ok ? "" : r.status
        if (!r.ok && r.status !== "invalidValue")
            editor.notice(r.reason, "error")
        return r.ok
    }
    function remove() {
        if (!controller || !editable)
            return
        // The row goes with its property: only locals after the edit.
        const ed = editor
        const ctl = controller
        const before = ctl.undoToken()
        const name = key
        const next = panel.neighbourOf(key)
        const r = ctl.removePageProperty(source, key)
        if (!r.ok) {
            ed.notice(r.reason, "error")
            return
        }
        ed.notice(qsTr("Removed property “%1”").arg(name), "info", ed.undoAction(before))
        if (next !== "")
            ed.propertyFocus = { key: next, part: "row" }
        else
            ed.listView.forceActiveFocus()
    }
    function setType(t) {
        if (!controller || !editable || t === type)
            return
        if (empty) {
            // Nothing to convert: the type is remembered for this session (a list is written `[]`).
            editor.setPropertyTypeOverride(key, t)
            if (t !== "list")
                return
        }
        const before = controller.undoToken()
        const r = controller.setPagePropertyType(source, key, t)
        if (!r.ok)
            editor.notice(r.reason, "error")
        else if (r.lost)
            editor.notice(r.reason, "info", editor.undoAction(before))
    }
    function rename(newKey) {
        const k = newKey.trim()
        if (k === "" || k === key) {
            renaming = false
            focusRow()
            return true
        }
        const ed = editor
        const r = controller.renamePageProperty(source, key, k)
        if (!r.ok) {
            error = r.reason
            return false
        }
        // (This row goes: the row of the new name takes the keyboard.)
        ed.propertyFocus = { key: k, part: "row" }
        return true
    }
    function copyValue() {
        if (!present)
            return
        const text = property.items && property.items.length > 0 ? property.items.join(", ") : String(property.value || "")
        const r = controller.copyText(text)
        if (r.ok)
            editor.notice(qsTr("Copied %1").arg(text), "success")
    }

    // ---- focus ------------------------------------------------------------------------------------
    function focusRow() {
        editingValue = false
        renaming = false
        row.forceActiveFocus()
        row.fieldFocused(row)
    }
    /// A field of this row (or the row) got the keyboard: block editing ends, the editor keeps it
    /// above the on-screen keyboard (BlockEditor.editingProperty).
    function fieldFocused(item) {
        if (controller && controller.editingRow >= 0)
            controller.stopEditing()
        editor.editingProperty = item
        editor.later(() => editor.ensureEditVisible())
    }
    function startValueEdit() {
        if (!editable)
            return
        error = ""
        switch (type) {
        case "checkbox":
            setValue(!(present && property.raw === true))
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
    function startRename() {
        if (!editable)
            return
        error = ""
        renaming = true
    }
    function pickDate() {
        const current = present && !empty ? String(property.raw).replace("T", " ") : ""
        const r = row
        editor.dialogs.pickPropertyDate((action, value) => {
            if (!r || !r.present)
                return
            if (action === "remove")
                r.setValue(null)
            else if (action === "set")
                r.setValue(value)
            r.focusRow()
        }, key, current, type === "datetime")
    }
    function openMenu(item, x, y) {
        panel.openMenu(row, item, x, y)
    }
    /// A focus request for this row (BlockEditor.propertyFocus: after adding, renaming, removing).
    function takeFocusRequest() {
        const f = editor ? editor.propertyFocus : null
        if (!f || f.key !== key)
            return
        editor.propertyFocus = null
        const part = f.part
        Qt.callLater(() => {
            if (!row)
                return
            if (part === "value")
                row.startValueEdit()
            else if (part === "key")
                row.startRename()
            else
                row.focusRow()
            if (part !== "value" || (row.type !== "date" && row.type !== "datetime"))
                row.fieldFocused(row)
        })
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
    // The row's menu: a right-click anywhere on it (a mouse), a long press with a finger.
    TapHandler {
        acceptedButtons: Qt.RightButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        onTapped: (point) => row.openMenu(row, point.position.x, point.position.y)
    }
    TapHandler {
        acceptedDevices: PointerDevice.TouchScreen
        onLongPressed: row.openMenu(row, point.position.x, point.position.y)
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
            provider: (typed) => row.controller.suggestPropertyKeys(typed, 8).map(
                          k => ({ title: k.key, icon: row.typeIcon(k.type), value: k.key }))
            property bool done: false
            Component.onCompleted: {
                forceActiveFocus()
                selectAll()
                row.fieldFocused(renameField)
            }
            onPicked: (value) => { if (row.rename(String(value))) done = true }
            onCommitted: (text) => { if (row.rename(text)) done = true }
            onCancelled: {
                done = true
                row.error = ""
                row.focusRow()
            }
            onActiveFocusChanged: {
                if (activeFocus || done)
                    return
                done = true
                if (!row.rename(text))
                    row.renaming = false
            }
            Connections {
                target: row
                function onLeaving() {
                    if (renameField.done)
                        return
                    renameField.done = true
                    if (!row.rename(renameField.text))
                        row.editor.notice(row.error, "error")
                }
            }
        }
    }

    // The value.
    Loader {
        id: valueLoader
        x: row.valueX + (row.stacked ? row.iconWidth : 0)
        y: row.valueY + (row.stacked ? 0 : Theme.space1)
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
            implicitHeight: row.editingValue && fieldLoader.item ? (fieldLoader.item as Item).implicitHeight
                                                                  : Math.max(lineHeight, display.implicitHeight)
            Text {
                id: display
                objectName: "propertyValue"
                visible: !row.editingValue
                width: parent.width
                y: Math.max(0, (textBox.lineHeight - Math.min(implicitHeight, textBox.lineHeight)) / 2)
                text: !row.present ? "" : row.empty ? qsTr("Empty") : (row.property.html || "")
                textFormat: row.present && !row.empty ? Text.RichText : Text.PlainText
                wrapMode: Text.Wrap
                maximumLineCount: row.present && row.property.multiline ? 6 : 3
                elide: Text.ElideRight
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
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
                    cursorShape: {
                        const p = displayCell.mapToItem(display, displayHover.point.position.x, displayHover.point.position.y)
                        return display.linkAt(p.x, p.y) !== "" ? Qt.PointingHandCursor
                                                               : row.editable ? Qt.IBeamCursor : Qt.ArrowCursor
                    }
                }
                TapHandler {
                    onTapped: (point) => {
                        const p = displayCell.mapToItem(display, point.position.x, point.position.y)
                        const link = display.linkAt(p.x, p.y)
                        if (link !== "")
                            row.editor.activateLink(row.blockRow, link)
                        else
                            row.startValueEdit()
                    }
                }
            }
            Loader {
                id: fieldLoader
                width: parent.width
                active: row.editingValue
                visible: active
                sourceComponent: row.present && row.property.multiline ? areaField : lineField
            }
            Component {
                id: lineField
                BSuggestField {
                    id: valueField
                    objectName: "propertyValueField"
                    size: "small"
                    errorText: row.error
                    accessibleName: qsTr("Value of “%1”").arg(row.key)
                    provider: (typed) => row.controller.suggestPropertyValues(row.key, typed, 8).map(
                                  v => ({ title: String(v.value), icon: v.kind === "page" ? "file-text" : "",
                                          subtitle: v.kind === "page" ? qsTr("page") : "", value: v.value }))
                    /// The value when the field opened (what it starts with).
                    property string initial: ""
                    property bool done: false
                    function commit(value) {
                        if (done)
                            return
                        done = true
                        if (value !== initial)
                            row.setValue(value)
                        row.editingValue = false
                    }
                    Component.onCompleted: {
                        initial = row.present && !row.empty ? String(row.property.raw) : ""
                        text = initial
                        forceActiveFocus()
                        cursorPosition = length
                        row.fieldFocused(valueField)
                    }
                    Component.onDestruction: if (!done && text !== initial) { done = true; row.setValue(text) }
                    onPicked: (value) => { commit(String(value)); row.focusRow() }
                    onCommitted: (text) => { commit(text); row.focusRow() }
                    onCancelled: {
                        done = true
                        row.editingValue = false
                        row.focusRow()
                    }
                    onActiveFocusChanged: if (!activeFocus) commit(text)
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
                    function commit() {
                        if (done)
                            return
                        done = true
                        if (text !== initial)
                            row.setValue(text)
                        row.editingValue = false
                    }
                    Component.onCompleted: {
                        initial = row.present && !row.empty ? String(row.property.raw) : ""
                        text = initial
                        forceActiveFocus()
                        cursorPosition = length
                        row.fieldFocused(valueArea)
                    }
                    Component.onDestruction: if (!done && text !== initial) { done = true; row.setValue(text) }
                    onActiveFocusChanged: if (!activeFocus) commit()
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
                            commit()
                            row.focusRow()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Escape) {
                            done = true
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
                text: !row.present ? "" : row.empty ? qsTr("Empty") : row.property.value
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
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
                    function commit() {
                        if (done)
                            return true
                        if (text !== initial && !row.setValue(text))
                            return false
                        done = true
                        row.error = ""
                        row.editingValue = false
                        return true
                    }
                    Component.onCompleted: {
                        initial = row.present && !row.empty ? String(row.property.value) : ""
                        text = initial
                        forceActiveFocus()
                        selectAll()
                        row.fieldFocused(numberField)
                    }
                    Component.onDestruction: if (!done && text !== initial) row.setValue(text)
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
                    onAccepted: if (commit()) row.focusRow()
                    onEscapePressed: {
                        done = true
                        row.error = ""
                        row.editingValue = false
                        row.focusRow()
                    }
                    onActiveFocusChanged: {
                        if (activeFocus || done)
                            return
                        if (!commit()) {
                            // Not a number: nothing written, the field closes (the error was said).
                            done = true
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
                checked: row.present && row.property.raw === true
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
                            size: parent.boxSize - 4
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
                            const raw = String(row.property.raw).replace("T", " ")
                            const shown = DatesHelper.formatShort(raw)
                            return shown !== "" ? shown : raw
                        }
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
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
            entry: row.property || ({})
            row: row
        }
    }
}
