pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// DatabaseView — a database view (docs/SYNTAX.md §10 "Views", "Obsidian Bases"; docs/UI.md
// "Database views"): the rows of a ```query block with `view:`, of a ```base block or of a .base
// file as a table, a board, a gallery, a calendar or a list (ViewResultModel over the host's
// QueryHost::runView). Table: columns of built-ins, properties and formulas, a header that sorts
// (written into the query or the base), hides and adds columns, cells edited in place — text,
// numbers, dates and lists in a field, checkboxes toggled, a task's status and priority from a menu
// — and summaries under the columns; wider than the column it scrolls sideways. In a cell's field Tab /
// Shift+Tab edit the next / previous cell that has a field (editBeside(): the written value first),
// Up / Down the cell above / below, Left / Right at the text's edge the one beside it; Enter and
// Escape end the editing as before. Board: a column per
// group (a task's status by default), cards dragged between columns with the mouse or, on touch
// screens, after a long press (the card's status, priority, date or property becomes the column's).
// Gallery: cards with the page's cover or first picture. Calendar: a month's grid, rows on the day
// of their date, dragged to another day. "+" makes a new row: a page in the view's folder with its
// tag and values (a board column's, a calendar day's), or a task. An edit on this page is one undo
// step of the page; in another file the toast's Undo puts it back (toastRequested(…, true)).
//
// Laid out with bindings, not Layouts: the block has its final height when the editor's list
// creates it (the first run comes when the model is completed; docs/UI.md "Scrolling performance").
Item {
    id: root

    property PageController controller
    property string source
    property string language: "query"
    property string basePath
    /// A base embedded in a note: the note (`this` in its expressions).
    property string thisPath
    property int blockRow: -1
    property string viewName
    /// The block editor showing the view (link menus, revealing a row of this page); null on a base page.
    property BlockEditor editor: null
    /// A toast about an edit; `undo`: another file was written (results.undoLast() puts it back).
    signal toastRequested(string text, string kind, bool undo)

    readonly property alias results: results
    readonly property bool touch: Theme.touchUi
    readonly property int rowHeight: touch ? Theme.touchTarget : 32
    readonly property int textSize: Theme.textSm
    readonly property int lineH: Theme.lineHeightPx(Theme.textSm, 1.4)
    readonly property int smallLineH: Theme.lineHeightPx(Theme.textXs, 1.4)
    readonly property bool editable: results.ok && root.controller !== null && !root.controller.readOnly

    implicitHeight: column.height
    height: implicitHeight

    Accessible.role: Accessible.Grouping
    Accessible.name: root.kindLabel(results.kind)

    ViewResultModel {
        id: results
        controller: root.controller
        source: root.source
        language: root.language
        basePath: root.basePath
        thisPath: root.thisPath
        blockRow: root.blockRow
        viewName: root.viewName
    }

    MapHolder { id: answer }

    function kindLabel(kind: string): string {
        switch (kind) {
        case "board": return qsTr("Board")
        case "gallery": return qsTr("Gallery")
        case "calendar": return qsTr("Calendar")
        case "list": return qsTr("List")
        default: return qsTr("Table")
        }
    }
    function kindIcon(kind: string): string {
        switch (kind) {
        case "board": return "kanban"
        case "gallery": return "layout-grid"
        case "calendar": return "calendar"
        case "list": return "list"
        default: return "table"
        }
    }
    function countText(): string {
        if (!results.ok)
            return ""
        const n = results.shownRows.length
        const what = results.type === "tasks" ? qsTr("%n task(s)", "", n) : qsTr("%n row(s)", "", n)
        return results.total > n && results.kind !== "calendar" ? qsTr("%1 of %2").arg(what).arg(results.total) : what
    }
    /// What an edit's answer says, as a toast (an error, or the Undo of another file's edit).
    function report(): void {
        if (answer.map.ok === true) {
            if (answer.map.undo === true)
                root.toastRequested(qsTr("Changed “%1”").arg("" + answer.map.title), "success", true)
            return
        }
        const error = "" + answer.map.error
        if (error !== "" && error !== "undefined")
            root.toastRequested(error, "warning", false)
    }
    function openRow(row: int): void {
        answer.map = results.open(row, false)
        if (answer.map.action === "scrollTo" && root.editor !== null)
            root.editor.revealRow(answer.map.row)
    }
    function activateLink(row: int, link: string): void {
        answer.map = results.activateLink(row, link, false)
        if (answer.map.action === "scrollTo" && root.editor !== null)
            root.editor.revealRow(answer.map.row)
    }

    // ---- what is being edited, dragged, added -------------------------------------------------------
    property int editRow: -1
    property int editColumn: -1
    /// The cell being edited, in the view's coordinates.
    property point editAt
    property string editType: ""
    function startEdit(row: int, column: int): void {
        if (!results.cellEditable(row, column))
            return
        const type = results.columnType(column)
        if (type === "checkbox") {
            answer.map = results.editCell(row, column, !(results.cellValue(row, column) === true))
            root.report()
            return
        }
        root.editRow = row
        root.editColumn = column
        root.editType = type
        if (type === "status" || type === "priority") {
            choiceMenu.row = row
            choiceMenu.column = column
            choiceMenu.status = type === "status"
            choiceMenu.popup()
            return
        }
        results.hold(cellEditor)
        cellEditor.open(results.cellEditText(row, column))
    }
    function finishEdit(commit: bool, text: string): void {
        if (root.editRow < 0)
            return
        const row = root.editRow
        const column = root.editColumn
        root.editRow = -1
        root.editColumn = -1
        if (commit && text !== results.cellEditText(row, column)) {
            answer.map = results.editCell(row, column, text)
            root.report()
        }
        results.release(cellEditor)
    }

    property int dragRow: -1
    property int dragFrom: -1
    property bool dragging: false
    property int addGroup: -1
    property int addDay: -1
    property bool adding: false
    function startAdding(group: int, day: int): void {
        root.addGroup = group
        root.addDay = day
        root.adding = true
        addField.text = ""
        addField.forceActiveFocus()
    }
    /// Enter / Escape in the cell's field: written (or not), the field hidden, the keyboard back
    /// where it belongs (returnFocus()) — never left on the hidden field.
    function closeCellEditor(commit: bool): void {
        const focused = editField.activeFocus
        cellEditor.visible = false
        root.finishEdit(commit, commit ? editField.text : "")
        if (focused)
            root.returnFocus(editField)
    }
    /// The cell the keys move the edit to from (row, column): `dir` "next" / "previous" (Tab: along
    /// the row, then the next row), "down" / "up" (the same column), "right" / "left" (the same
    /// row). Only cells that open a field count (not a checkbox, a task's status or priority: their
    /// menus and toggles are a click's). x: the column, y: the row; y -1: there is none.
    function fieldBeside(row: int, column: int, dir: string): point {
        const columns = results.columnTotal
        let r = row
        let c = column
        for (let guard = 0; guard < 100000; ++guard) {
            if (dir === "next" || dir === "right") {
                ++c
                if (c >= columns) {
                    if (dir === "right")
                        break
                    c = 0
                    r = root.rowBeside(r, 1)
                }
            } else if (dir === "previous" || dir === "left") {
                --c
                if (c < 0) {
                    if (dir === "left")
                        break
                    c = columns - 1
                    r = root.rowBeside(r, -1)
                }
            } else {
                r = root.rowBeside(r, dir === "down" ? 1 : -1)
            }
            if (r < 0)
                break
            if (root.opensField(r, c))
                return Qt.point(c, r)
        }
        return Qt.point(-1, -1)
    }
    function rowBeside(row: int, step: int): int {
        return results.revision >= 0 ? results.neighbourRow(row, step) : -1
    }
    /// Editing the cell opens a text field (dates, numbers, text, lists).
    function opensField(row: int, column: int): bool {
        if (!results.cellEditable(row, column))
            return false
        const type = results.columnType(column)
        return type !== "checkbox" && type !== "status" && type !== "priority"
    }
    /// The edit of the cell being edited moves on: its text is written, then the cell `fieldBeside()`
    /// names is edited, shown (the table scrolls sideways, the page up or down). Tab with no cell
    /// left ends the editing as Enter does; the other keys then stay in the field.
    function editBeside(dir: string): void {
        if (root.editRow < 0)
            return
        const to = root.fieldBeside(root.editRow, root.editColumn, dir)
        const toRow = to.y
        const toColumn = to.x
        if (toRow < 0) {
            if (dir === "next" || dir === "previous")
                root.closeCellEditor(true)
            return
        }
        EditorInput.commitPreedit(editField)
        const text = editField.text
        cellEditor.visible = false
        root.finishEdit(true, text)
        const table = content.item as TableArea
        if (table !== null)
            table.revealColumn(toColumn)
        root.startEdit(toRow, toColumn)
        const cell = root.cellItem(toRow, toColumn)
        if (cell !== null && root.editor !== null)
            root.editor.ensureVisible(cell, Qt.rect(0, 0, cell.width, cell.height))
    }
    function closeAddField(commit: bool): void {
        const focused = addField.activeFocus
        root.finishAdding(commit)
        if (focused)
            root.returnFocus(addField)
    }
    /// A field of the view closed with the keyboard: the page's editor takes the keys again (as after
    /// a table block's field), on a base's own page the view. The field and the focus scopes up to
    /// the editor's list (the Loaders holding the view, which its forceActiveFocus() gave the focus)
    /// give it up first: a hidden item keeps it, and the list would hand it back down to them.
    function returnFocus(field: Item): void {
        if (root.editor === null) {
            field.focus = false
            root.forceActiveFocus()
            return
        }
        const list = root.editor.listView
        for (let item = field; item !== null && item !== list; item = item.parent)
            item.focus = false
        list.forceActiveFocus()
    }
    function finishAdding(commit: bool): void {
        if (commit && Js.trim(addField.text) !== "")
            root.addRowNamed(Js.trim(addField.text), root.addGroup, root.addDay)
        root.adding = false
    }
    function addRowNamed(title: string, group: int, day: int): void {
        answer.map = results.addRow(title, group, day)
        if (answer.map.ok !== true)
            root.toastRequested(qsTr("The new row could not be made."), "warning", false)
    }

    // ---- layout ----------------------------------------------------------------------------------------
    Item {
        id: column
        width: parent.width
        readonly property real headerBottom: header.height
        readonly property real noticeBottom: noticeText.visible ? noticeText.y + noticeText.height : headerBottom
        readonly property real contentBottom: content.visible ? content.y + content.height : noticeBottom
        readonly property real addBottom: addRow.visible ? addRow.y + addRow.height : contentBottom
        height: addBottom

        Item {
            id: header
            width: parent.width
            height: Math.max(Theme.touchUi ? 40 : 28, headerTitle.implicitHeight)
            BIcon {
                id: headerIcon
                anchors.verticalCenter: parent.verticalCenter
                name: root.kindIcon(results.kind)
                size: Theme.iconSizeSm
                color: Theme.fgMuted
            }
            Text {
                id: headerTitle
                x: headerIcon.width + Theme.space2
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, Math.max(0, headerTools.x - x - Theme.space2))
                text: results.viewNames.length > 0 && results.viewIndex < results.viewNames.length
                      ? results.viewNames[results.viewIndex] : root.kindLabel(results.kind)
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                font.weight: Theme.weightSemiBold
                color: Theme.fgMuted
            }
            Text {
                x: headerTitle.x + headerTitle.width + Theme.space2
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, headerTools.x - x - Theme.space2)
                text: root.countText()
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: Theme.fgFaint
            }
            Row {
                id: headerTools
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space1
                BSpinner {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: results.available && !results.complete
                    size: 14
                    running: visible
                }
                // A base's views.
                BIconButton {
                    visible: results.viewNames.length > 1
                    size: "small"
                    iconName: "chevron-down"
                    label: qsTr("Views of this base")
                    focusPolicy: Qt.NoFocus
                    onClicked: viewsMenu.popup(this, 0, height)
                }
                // A calendar's month.
                BIconButton {
                    objectName: "viewPreviousMonth"
                    visible: results.kind === "calendar"
                    size: "small"
                    iconName: "chevron-left"
                    label: qsTr("Previous month")
                    focusPolicy: Qt.NoFocus
                    onClicked: results.moveMonth(-1)
                }
                Text {
                    visible: results.kind === "calendar"
                    anchors.verticalCenter: parent.verticalCenter
                    text: results.monthTitle
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fg
                    TapHandler { onTapped: results.moveMonth(0) }
                }
                BIconButton {
                    objectName: "viewNextMonth"
                    visible: results.kind === "calendar"
                    size: "small"
                    iconName: "chevron-right"
                    label: qsTr("Next month")
                    focusPolicy: Qt.NoFocus
                    onClicked: results.moveMonth(1)
                }
                BIconButton {
                    objectName: "viewAddRow"
                    visible: results.canAddRow && results.kind !== "board"
                    size: "small"
                    iconName: "plus"
                    label: results.type === "tasks" ? qsTr("New task") : qsTr("New page")
                    focusPolicy: Qt.NoFocus
                    onClicked: root.startAdding(-1, -1)
                }
                BIconButton {
                    objectName: "viewColumnsMenu"
                    visible: results.canChangeView && results.ok
                    size: "small"
                    iconName: "columns-2"
                    label: qsTr("Columns")
                    focusPolicy: Qt.NoFocus
                    onClicked: columnsMenu.popup(this, 0, height)
                }
            }
        }

        // Errors, a base's problems, what is shown differently.
        Text {
            id: noticeText
            y: column.headerBottom + Theme.space1
            width: parent.width
            visible: text !== ""
            text: !results.ready ? "" : !results.ok ? (results.error !== "" ? results.error : qsTr("This view cannot be shown."))
                                  : results.problems.length > 0 ? results.problems.join("\n") + (results.notice !== "" ? "\n" + results.notice : "")
                                  : results.notice !== "" ? results.notice
                                  : results.shownRows.length === 0 ? (results.complete ? qsTr("Nothing matches this view.") : qsTr("Nothing yet — the notebook is still being indexed."))
                                  : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            color: results.ok ? Theme.fgFaint : Theme.dangerText
        }

        Loader {
            id: content
            y: column.noticeBottom + Theme.space2
            width: parent.width
            visible: results.ok && (results.shownRows.length > 0 || results.kind === "board" || results.kind === "calendar")
            height: content.implicitHeight
            sourceComponent: results.kind === "board" ? boardComponent
                             : results.kind === "gallery" ? galleryComponent
                             : results.kind === "calendar" ? calendarComponent
                             : results.kind === "list" ? listComponent : tableComponent
        }

        // A new row's title.
        Item {
            id: addRow
            y: column.contentBottom + Theme.space2
            width: parent.width
            visible: root.adding
            height: visible ? addField.height : 0
            BTextField {
                id: addField
                objectName: "viewNewRowTitle"
                width: Math.min(parent.width, 360)
                size: "small"
                placeholderText: results.type === "tasks" ? qsTr("New task") : qsTr("Title of the new page")
                accessibleName: placeholderText
                onAccepted: root.closeAddField(true)
                onEscapePressed: root.closeAddField(false)
                onActiveFocusChanged: if (!activeFocus && root.adding) root.finishAdding(Js.trim(addField.text) !== "")
            }
        }
    }

    // ---- menus -----------------------------------------------------------------------------------------
    BMenu {
        id: viewsMenu
        Instantiator {
            model: results.viewEntries
            delegate: BMenuItem {
                required property string name
                required property int view
                text: name
                checkable: true
                checked: view === results.viewIndex
                onTriggered: results.viewIndex = view
            }
            onObjectAdded: (index, object) => viewsMenu.insertItem(index, object as Item)
            onObjectRemoved: (index, object) => viewsMenu.removeItem(object as Item)
        }
    }

    BMenu {
        id: columnsMenu
        BMenu {
            id: addColumnMenu
            title: qsTr("Add column")
            iconName: "plus"
            Instantiator {
                model: root.suggestions()
                delegate: BMenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: results.addColumn(modelData)
                }
                onObjectAdded: (index, object) => addColumnMenu.insertItem(index, object as Item)
                onObjectRemoved: (index, object) => addColumnMenu.removeItem(object as Item)
            }
            BMenuSeparator {}
            BMenuItem {
                text: qsTr("Other…")
                onTriggered: columnField.open()
            }
        }
        BMenu {
            id: hideColumnMenu
            title: qsTr("Hide column")
            iconName: "eye-off"
            Instantiator {
                model: results.columnModel
                delegate: BMenuItem {
                    required property string label
                    required property int number
                    text: label
                    onTriggered: results.hideColumn(number)
                }
                onObjectAdded: (index, object) => hideColumnMenu.insertItem(index, object as Item)
                onObjectRemoved: (index, object) => hideColumnMenu.removeItem(object as Item)
            }
        }
        BMenuItem {
            text: qsTr("Remove the sort")
            iconName: "arrow-down"
            onTriggered: results.sortBy(-1, false)
        }
    }

    /// The menu of a column's header: sort, move, hide.
    BMenu {
        id: headerMenu
        property int column: -1
        BMenuItem { text: qsTr("Sort ascending"); iconName: "arrow-up"; onTriggered: results.sortBy(headerMenu.column, false) }
        BMenuItem { text: qsTr("Sort descending"); iconName: "arrow-down"; onTriggered: results.sortBy(headerMenu.column, true) }
        BMenuSeparator {}
        BMenuItem { text: qsTr("Move left"); iconName: "chevron-left"; onTriggered: results.moveColumn(headerMenu.column, headerMenu.column - 1) }
        BMenuItem { text: qsTr("Move right"); iconName: "chevron-right"; onTriggered: results.moveColumn(headerMenu.column, headerMenu.column + 1) }
        BMenuSeparator {}
        BMenuItem { text: qsTr("Hide column"); iconName: "eye-off"; onTriggered: results.hideColumn(headerMenu.column) }
    }

    /// A task's status or priority.
    BMenu {
        id: choiceMenu
        property int row: -1
        property int column: -1
        property bool status: true
        onClosed: { root.editRow = -1; root.editColumn = -1 }
        function choose(value: string): void {
            answer.map = results.editCell(choiceMenu.row, choiceMenu.column, value)
            root.report()
        }
        BMenuItem { visible: choiceMenu.status; text: qsTr("To do"); onTriggered: choiceMenu.choose("todo") }
        BMenuItem { visible: choiceMenu.status; text: qsTr("In progress"); onTriggered: choiceMenu.choose("doing") }
        BMenuItem { visible: choiceMenu.status; text: qsTr("Done"); onTriggered: choiceMenu.choose("done") }
        BMenuItem { visible: choiceMenu.status; text: qsTr("Deferred"); onTriggered: choiceMenu.choose("deferred") }
        BMenuItem { visible: choiceMenu.status; text: qsTr("Cancelled"); onTriggered: choiceMenu.choose("cancelled") }
        BMenuItem { visible: !choiceMenu.status; text: qsTr("Highest"); onTriggered: choiceMenu.choose("highest") }
        BMenuItem { visible: !choiceMenu.status; text: qsTr("High"); onTriggered: choiceMenu.choose("high") }
        BMenuItem { visible: !choiceMenu.status; text: qsTr("Medium"); onTriggered: choiceMenu.choose("medium") }
        BMenuItem { visible: !choiceMenu.status; text: qsTr("Normal"); onTriggered: choiceMenu.choose("") }
        BMenuItem { visible: !choiceMenu.status; text: qsTr("Low"); onTriggered: choiceMenu.choose("low") }
        BMenuItem { visible: !choiceMenu.status; text: qsTr("Lowest"); onTriggered: choiceMenu.choose("lowest") }
    }

    /// A column's name typed ("koszt", "Brutto = koszt * 1.23").
    BDialog {
        id: columnField
        title: qsTr("Add column")
        acceptText: qsTr("Add")
        acceptEnabled: Js.trim(columnName.text) !== ""
        initialFocusItem: columnName
        onAccepted: results.addColumn(columnName.text)
        onOpened: columnName.text = ""
        BTextField {
            id: columnName
            objectName: "viewColumnName"
            Layout.fillWidth: true
            placeholderText: root.language === "base" ? qsTr("A property, e.g. status or formula.total") : qsTr("A property, or Label = expression")
            accessibleName: placeholderText
            onAccepted: columnField.accept()
        }
    }

    // ---- the cell editor (one, over the cell being edited) ------------------------------------------------
    Item {
        id: cellEditor
        z: 10
        visible: false
        property bool opening: false
        function open(text: string): void {
            cellEditor.opening = true
            editField.text = text
            cellEditor.visible = true
            cellEditor.place()
            editField.forceActiveFocus()
            editField.selectAll()
            cellEditor.opening = false
        }
        function place(): void {
            const cell = root.cellItem(root.editRow, root.editColumn)
            if (cell === null)
                return
            root.editAt = root.mapFromItem(cell, 0, 0)
            cellEditor.x = root.editAt.x
            cellEditor.y = root.editAt.y
            cellEditor.width = Math.max(cell.width, 160)
            cellEditor.height = cell.height
        }
        BTextField {
            id: editField
            objectName: "viewCellEditor"
            anchors.fill: parent
            size: "small"
            placeholderText: root.editType === "date" || root.editType === "taskdate" ? "2026-10-15"
                             : root.editType === "list" || root.editType === "tags" ? qsTr("a, b, [[Page]]") : ""
            accessibleName: results.columnLabel(root.editColumn)
            onAccepted: root.closeCellEditor(true)
            onEscapePressed: root.closeCellEditor(false)
            // Tab and the arrows move to the neighbouring cell (an arrow only when the caret cannot go that way).
            Keys.onPressed: (event) => {
                if ((event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) !== 0)
                    return
                let dir = ""
                if (event.key === Qt.Key_Tab)
                    dir = "next"
                else if (event.key === Qt.Key_Backtab)
                    dir = "previous"
                else if ((event.modifiers & Qt.ShiftModifier) !== 0 || editField.preeditText !== "")
                    return
                else if (event.key === Qt.Key_Down)
                    dir = "down"
                else if (event.key === Qt.Key_Up)
                    dir = "up"
                else if (event.key === Qt.Key_Left && editField.cursorPosition === 0 && editField.selectedText === "")
                    dir = "left"
                else if (event.key === Qt.Key_Right && editField.cursorPosition === editField.length && editField.selectedText === "")
                    dir = "right"
                if (dir === "")
                    return
                event.accepted = true
                root.editBeside(dir)
            }
            onActiveFocusChanged: {
                if (!activeFocus && cellEditor.visible && !cellEditor.opening) {
                    cellEditor.visible = false
                    root.finishEdit(true, editField.text)
                }
            }
        }
    }
    /// The table cell item of (row, column), or null.
    function cellItem(row: int, column: int): Item {
        const table = content.item as TableArea
        return table !== null ? table.cellAt(row, column) : null
    }

    // ---- the card being dragged (boards, calendars) ------------------------------------------------------
    Rectangle {
        id: ghost
        objectName: "viewDragGhost" // (the shell's self-test)
        z: 20
        visible: root.dragging
        width: root.touch ? 220 : 240
        height: ghostText.implicitHeight + 2 * Theme.space2
        radius: Theme.radius
        color: Theme.bgElevated
        border.color: Theme.accent
        border.width: 2
        opacity: 0.92
        BRichText {
            id: ghostText
            x: Theme.space2
            y: Theme.space2
            width: parent.width - 2 * Theme.space2
            html: root.dragRow >= 0 ? results.rowHeadline(root.dragRow) : ""
            maximumLineCount: 2
            font.family: Theme.fontEditor
            font.pixelSize: root.textSize
            lineHeight: root.lineH
            color: Theme.fg
        }
    }
    /// A drag starts at (x, y) of `item`: the ghost follows from there.
    /// Where the pointer is, in the view's coordinates.
    property point dragAt
    function beginDrag(row: int, from: int, item: Item, x: real, y: real): void {
        root.dragRow = row
        root.dragFrom = from
        root.dragAt = root.mapFromItem(item, x, y)
        ghost.x = root.dragAt.x - ghost.width / 2
        ghost.y = root.dragAt.y - ghost.height / 2
        root.dragging = true
        results.hold(ghost)
    }
    function moveDrag(item: Item, x: real, y: real): void {
        root.dragAt = root.mapFromItem(item, x, y)
        ghost.x = root.dragAt.x - ghost.width / 2
        ghost.y = root.dragAt.y - ghost.height / 2
    }
    /// Where the drag ended: the board's column or the calendar's day under the ghost's centre.
    function endDrag(cancel: bool): void {
        if (!root.dragging)
            return
        const row = root.dragRow
        const from = root.dragFrom
        const drop = cancel ? -1 : root.dropTarget()
        root.dragging = false
        root.dragRow = -1
        results.release(ghost)
        root.dropRow(row, from, drop)
    }
    /// The board's column or the calendar's day under the dragged copy's centre, -1 none.
    property point dropAt
    function dropTarget(): int {
        const target = content.item as Item
        if (target === null)
            return -1
        root.dropAt = root.mapToItem(target, ghost.x + ghost.width / 2, ghost.y + ghost.height / 2)
        const board = target as BoardArea
        if (board !== null)
            return board.groupAt(root.dropAt.x, root.dropAt.y)
        const calendar = target as CalendarArea
        return calendar !== null ? calendar.dayAt(root.dropAt.x, root.dropAt.y) : -1
    }
    function dropRow(row: int, from: int, drop: int): void {
        if (drop < 0)
            return
        if (results.kind === "board")
            answer.map = results.moveRow(row, from, drop)
        else
            answer.map = results.moveRowToDay(row, drop)
        root.report()
    }

    // ---- table ---------------------------------------------------------------------------------------------
    function colWidth(c: int): real {
        if (results.revision < 0)
            return 0
        return results.columnWidth(c, root.touch)
    }
    function colX(c: int): real {
        if (results.revision < 0)
            return 0
        return results.columnOffset(c, root.touch)
    }
    readonly property real tableWidth: results.revision >= 0 ? results.tableWidth(root.touch) : 0
    function summaryOf(c: int): string {
        return results.columnSummary(c)
    }
    readonly property bool hasSummaries: {
        if (results.revision < 0)
            return false
        for (let c = 0; c < results.columnTotal; ++c) {
            if (root.summaryOf(c) !== "")
                return true
        }
        return false
    }
    function linesOfTable(): list<int> {
        if (results.revision < 0)
            return root.noRows
        return results.tableLines()
    }
    function rowsOfGroup(g: int): list<int> {
        if (results.revision < 0)
            return root.noRows
        return results.groupRows(g)
    }
    function rowsOfDay(d: int): list<int> {
        if (results.revision < 0)
            return root.noRows
        return results.dayRows(d)
    }
    function suggestions(): list<string> {
        if (results.revision < 0)
            return root.noCells
        return results.columnSuggestions()
    }
    function detailsOf(row: int): list<string> {
        if (results.revision < 0)
            return root.noCells
        return results.rowDetails(row)
    }
    function cellsOf(row: int): list<string> {
        if (results.revision < 0 || row < 0)
            return root.noCells
        return results.rowCells(row)
    }
    readonly property list<int> noRows: []

    component TableArea: Flickable {
        id: tableFlick
        width: root.width
        readonly property real lines: 1 + results.shownRows.length + (results.groupCount > 0 ? results.groupCount : 0)
                                      + (root.hasSummaries ? 1 : 0)
        implicitHeight: tableFlick.lines * root.rowHeight
        height: implicitHeight
        contentWidth: Math.max(width, root.tableWidth)
        contentHeight: height
        interactive: root.tableWidth > width && !root.dragging
        clip: root.tableWidth > width
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        onContentXChanged: if (root.editRow >= 0) cellEditor.place()
        /// Scroll sideways until `column` is shown whole (as far as it fits).
        function revealColumn(column: int): void {
            const left = root.colX(column)
            const right = left + root.colWidth(column)
            if (left < tableFlick.contentX)
                tableFlick.contentX = left
            else if (right > tableFlick.contentX + tableFlick.width)
                tableFlick.contentX = Math.max(0, Math.min(right - tableFlick.width, tableFlick.contentWidth - tableFlick.width))
        }

        Rectangle {
            width: tableFlick.contentWidth
            height: root.rowHeight
            color: Theme.bgSunken
            radius: Theme.radiusSm
        }
        // Header: a tap sorts (ascending, descending, none), the right button or a long press opens its menu.
        Repeater {
            model: results.columnModel
            delegate: Item {
                id: headerCell
                required property string label
                required property int number
                required property int sorted
                required property string sortKey
                objectName: "viewHeader" + number
                x: root.colX(headerCell.number)
                width: root.colWidth(headerCell.number)
                height: root.rowHeight
                Text {
                    x: Theme.space2
                    width: parent.width - 2 * Theme.space2 - (sortIcon.visible ? sortIcon.width : 0)
                    anchors.verticalCenter: parent.verticalCenter
                    text: headerCell.label
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textXs
                    font.weight: Theme.weightSemiBold
                    font.capitalization: Font.AllUppercase
                    color: Theme.fgMuted
                }
                BIcon {
                    id: sortIcon
                    visible: headerCell.sorted !== 0
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space1
                    anchors.verticalCenter: parent.verticalCenter
                    name: headerCell.sorted > 0 ? "arrow-up" : "arrow-down"
                    size: 12
                    color: Theme.fgMuted
                }
                TapHandler {
                    enabled: results.canChangeView && headerCell.sortKey !== ""
                    onTapped: results.sortBy(headerCell.sorted === 0 ? headerCell.number : (headerCell.sorted > 0 ? headerCell.number : -1), headerCell.sorted > 0)
                }
                TapHandler {
                    enabled: results.canChangeView
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                    onTapped: { headerMenu.column = headerCell.number; headerMenu.popup() }
                }
                TapHandler {
                    enabled: results.canChangeView && Theme.touchUi
                    acceptedDevices: PointerDevice.TouchScreen
                    onLongPressed: { headerMenu.column = headerCell.number; headerMenu.popup() }
                }
            }
        }
        // Rows (a grouped table: each group's header line, then its rows).
        Repeater {
            id: linesRepeater
            model: root.linesOfTable()
            delegate: TableLine {}
        }
        /// The cell item of (row, column), or null.
        function cellAt(row: int, column: int): Item {
            const line = linesRepeater.itemAt(results.lineOfRow(row)) as TableLine
            return line !== null ? line.cellAt(column) : null
        }
        // Summaries.
        Item {
            visible: root.hasSummaries
            y: (tableFlick.lines - 1) * root.rowHeight
            width: tableFlick.contentWidth
            height: root.rowHeight
            Rectangle { width: parent.width; height: 1; color: Theme.borderStrong }
            Repeater {
                model: results.columnModel
                delegate: Item {
                    id: summaryCell
                    required property int number
                    required property string summaryText
                    required property string summaryLabel
                    x: root.colX(summaryCell.number) + Theme.space2
                    width: root.colWidth(summaryCell.number) - 2 * Theme.space2
                    height: root.rowHeight
                    visible: summaryCell.summaryText !== ""
                    // The value always shows; its name takes what room is left.
                    Text {
                        width: Math.max(0, parent.width - summaryValue.implicitWidth - Theme.space1)
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        text: summaryCell.summaryLabel
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textXs
                        color: Theme.fgFaint
                    }
                    Text {
                        id: summaryValue
                        anchors.right: parent.right
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        text: summaryCell.summaryText
                        textFormat: Text.PlainText
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textXs
                        font.weight: Theme.weightSemiBold
                        color: Theme.fgMuted
                    }
                }
            }
        }
    }
    component TableLine: Item {
        id: line
        required property int index
        required property int modelData
        readonly property bool groupLine: modelData < 0
        readonly property int row: modelData
        readonly property list<string> cells: root.cellsOf(line.groupLine ? -1 : line.row)
        function cellAt(c: int): Item {
            return cellRepeater.itemAt(c)
        }
        property string hoveredLink: ""
        y: (line.index + 1) * root.rowHeight
        width: Math.max(root.width, root.tableWidth)
        height: root.rowHeight
        Rectangle { width: parent.width; height: 1; color: Theme.borderSubtle }
        Rectangle {
            visible: !line.groupLine && lineHover.hovered
            anchors.fill: parent
            anchors.topMargin: 1
            color: Theme.bgHover
        }
        Text {
            visible: line.groupLine
            x: Theme.space2
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            text: line.groupLine ? results.groupLabel(-1 - line.modelData) + "  ·  " + root.rowsOfGroup(-1 - line.modelData).length : ""
            textFormat: Text.PlainText
            font.family: Theme.fontUi
            font.pixelSize: Theme.textXs
            font.weight: Theme.weightSemiBold
            font.capitalization: Font.AllUppercase
            color: Theme.fgMuted
        }
        Repeater {
            id: cellRepeater
            model: line.groupLine ? 0 : line.cells.length
            delegate: Item {
                id: cell
                required property int index
                x: root.colX(cell.index)
                width: root.colWidth(cell.index)
                height: root.rowHeight
                objectName: "viewCell" + line.row + "_" + cell.index
                readonly property string type: results.columnType(cell.index)
                BCheckBox {
                    visible: cell.type === "checkbox"
                    anchors.verticalCenter: parent.verticalCenter
                    x: Theme.space2
                    checked: results.revision >= 0 && results.cellValue(line.row, cell.index) === true
                    focusPolicy: Qt.NoFocus
                    Accessible.name: results.columnLabel(cell.index)
                    onClicked: root.startEdit(line.row, cell.index)
                }
                BRichText {
                    id: cellText
                    visible: cell.type !== "checkbox"
                    x: Theme.space2
                    width: parent.width - 2 * Theme.space2
                    anchors.verticalCenter: parent.verticalCenter
                    html: cell.index < line.cells.length ? line.cells[cell.index] : ""
                    hoveredLink: line.hoveredLink
                    maximumLineCount: 1
                    font.family: Theme.fontEditor
                    font.pixelSize: root.textSize
                    lineHeight: root.lineH
                    color: Theme.fg
                }
                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: (eventPoint) => {
                        const at = cellText.mapFromItem(cell, eventPoint.position.x, eventPoint.position.y)
                        const link = cellText.linkAt(at.x, at.y)
                        if (link !== "") {
                            root.activateLink(line.row, link)
                            return
                        }
                        if (results.cellEditable(line.row, cell.index))
                            root.startEdit(line.row, cell.index)
                        else if (cell.index === 0)
                            root.openRow(line.row)
                    }
                }
            }
        }
        HoverHandler {
            id: lineHover
            enabled: !Theme.touchUi && !line.groupLine
        }
    }
    readonly property list<string> noCells: []

    Component {
        id: tableComponent
        TableArea {}
    }

    // ---- list ----------------------------------------------------------------------------------------------
    Component {
        id: listComponent
        Column {
            width: root.width
            Repeater {
                model: results.shownRows
                delegate: Item {
                    id: listRow
                    required property int modelData
                    width: root.width
                    height: root.rowHeight
                    BRichText {
                        id: listText
                        x: Theme.space2
                        width: parent.width - 2 * Theme.space2
                        anchors.verticalCenter: parent.verticalCenter
                        html: results.revision >= 0 ? results.rowLine(listRow.modelData) : ""
                        maximumLineCount: 1
                        font.family: Theme.fontEditor
                        font.pixelSize: root.textSize
                        lineHeight: root.lineH
                        color: Theme.fg
                    }
                    TapHandler {
                        onTapped: (eventPoint) => {
                            const at = listText.mapFromItem(listRow, eventPoint.position.x, eventPoint.position.y)
                            const link = listText.linkAt(at.x, at.y)
                            if (link !== "")
                                root.activateLink(listRow.modelData, link)
                            else
                                root.openRow(listRow.modelData)
                        }
                    }
                }
            }
        }
    }
    // ---- cards (boards and galleries) ----------------------------------------------------------------------
    readonly property real coverHeight: root.touch ? 96 : 120
    readonly property int maxDetails: 3
    function cardHeight(row: int, gallery: bool): real {
        const cover = gallery || results.rowCover(row) !== "" || results.rowCoverColor(row) !== ""
        const details = Math.min(root.maxDetails, results.rowDetails(row).length)
        return 2 * Theme.space2 + (cover ? root.coverHeight + Theme.space2 : 0) + 2 * root.lineH
               + details * root.smallLineH
    }
    component Card: Rectangle {
        id: card
        required property int row
        property int group: -1
        property bool gallery: false
        property string hoveredLink: ""
        readonly property string cover: results.revision >= 0 ? results.rowCover(card.row) : ""
        readonly property string coverColor: results.revision >= 0 ? results.rowCoverColor(card.row) : ""
        readonly property list<string> details: root.detailsOf(card.row)
        objectName: "viewCard" + card.row
        height: results.revision >= 0 ? root.cardHeight(card.row, card.gallery) : 0
        radius: Theme.radius
        color: Theme.bgElevated
        border.width: 1
        border.color: cardMouse.containsMouse ? Theme.borderStrong : Theme.border
        opacity: root.dragging && root.dragRow === card.row ? 0.4 : 1
        clip: true
        Accessible.role: Accessible.ListItem
        Accessible.name: results.rowText(card.row)

        Rectangle {
            id: coverBox
            visible: card.gallery || card.cover !== "" || card.coverColor !== ""
            x: 1
            y: 1
            width: parent.width - 2
            height: visible ? root.coverHeight : 0
            topLeftRadius: Theme.radius - 1
            topRightRadius: Theme.radius - 1
            color: card.coverColor !== "" ? card.coverColor : Theme.bgSunken
            Image {
                anchors.fill: parent
                visible: card.cover !== ""
                source: card.cover
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: 480
            }
            BIcon {
                visible: card.cover === "" && card.coverColor === ""
                anchors.centerIn: parent
                name: "image"
                size: Theme.iconSize
                color: Theme.fgFaint
            }
        }
        BRichText {
            id: headline
            x: Theme.space2
            y: (coverBox.visible ? coverBox.height + Theme.space2 : 0) + Theme.space2
            width: parent.width - 2 * Theme.space2
            html: results.revision >= 0 ? results.rowHeadline(card.row) : ""
            hoveredLink: card.hoveredLink
            maximumLineCount: 2
            font.family: Theme.fontEditor
            font.pixelSize: root.textSize
            font.weight: Theme.weightMedium
            lineHeight: root.lineH
            color: Theme.fg
        }
        Repeater {
            model: Math.min(root.maxDetails, card.details.length)
            delegate: BRichText {
                required property int index
                x: Theme.space2
                y: headline.y + 2 * root.lineH + index * root.smallLineH
                width: card.width - 2 * Theme.space2
                html: card.details[index]
                maximumLineCount: 1
                font.family: Theme.fontUi
                font.pixelSize: Theme.textXs
                lineHeight: root.smallLineH
                color: Theme.fgMuted
            }
        }
        // A click opens the row (a link in its title follows); dragging moves the card — at once
        // with a mouse, after a long press on a touch screen (the page keeps scrolling otherwise).
        MouseArea {
            id: cardMouse
            anchors.fill: parent
            hoverEnabled: !Theme.touchUi
            pressAndHoldInterval: 400
            property point pressed
            property bool moved: false
            readonly property bool canDrag: !card.gallery && results.movable
            onPressed: (mouse) => {
                cardMouse.pressed = Qt.point(mouse.x, mouse.y)
                cardMouse.moved = false
            }
            onPressAndHold: (mouse) => {
                if (!cardMouse.canDrag || root.dragging)
                    return
                cardMouse.preventStealing = true
                root.beginDrag(card.row, card.group, card, mouse.x, mouse.y)
            }
            onPositionChanged: (mouse) => {
                if (root.dragging) {
                    root.moveDrag(card, mouse.x, mouse.y)
                    return
                }
                const dx = mouse.x - cardMouse.pressed.x
                const dy = mouse.y - cardMouse.pressed.y
                if (!Theme.touchUi && cardMouse.pressedButtons === Qt.LeftButton && cardMouse.canDrag
                        && dx * dx + dy * dy > 64) {
                    cardMouse.preventStealing = true
                    cardMouse.moved = true
                    root.beginDrag(card.row, card.group, card, mouse.x, mouse.y)
                }
            }
            onReleased: (mouse) => {
                cardMouse.preventStealing = false
                if (root.dragging) {
                    root.endDrag(false)
                    return
                }
                if (cardMouse.moved)
                    return
                const at = headline.mapFromItem(card, mouse.x, mouse.y)
                const link = headline.linkAt(at.x, at.y)
                if (link !== "")
                    root.activateLink(card.row, link)
                else
                    root.openRow(card.row)
            }
            onCanceled: {
                cardMouse.preventStealing = false
                root.endDrag(true)
            }
        }
    }

    // ---- board ---------------------------------------------------------------------------------------------
    readonly property real boardColumnWidth: root.touch ? 232 : 260
    readonly property real boardHeaderHeight: Math.max(32, root.lineH + Theme.space3)
    function boardColumnHeight(group: int): real {
        let h = root.boardHeaderHeight + Theme.space2
        const n = results.groupRowCount(group)
        for (let i = 0; i < n; ++i)
            h += root.cardHeight(results.groupRowAt(group, i), false) + Theme.space2
        return h + (results.canAddRow ? Theme.controlHeightSm + Theme.space2 : 0)
    }
    readonly property real boardHeight: {
        if (results.revision < 0)
            return 0
        let h = root.boardHeaderHeight + Theme.space4
        for (let g = 0; g < results.groupCount; ++g)
            h = Math.max(h, root.boardColumnHeight(g))
        return h
    }
    component BoardArea: Flickable {
        id: board
        width: root.width
        implicitHeight: root.boardHeight
        height: implicitHeight
        contentWidth: results.groupCount * (root.boardColumnWidth + Theme.space2)
        contentHeight: height
        interactive: contentWidth > width && !root.dragging
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        /// The group whose column holds (x, y) of the board, -1 none.
        function groupAt(x: real, y: real): int {
            const cx = x + board.contentX
            const g = Math.floor(cx / (root.boardColumnWidth + Theme.space2))
            return y >= -root.rowHeight && y <= board.height + root.rowHeight && g >= 0 && g < results.groupCount ? g : -1
        }
        // While a card is dragged near an edge, the board scrolls.
        Timer {
            interval: 30
            repeat: true
            running: root.dragging && board.contentWidth > board.width
            onTriggered: {
                const p = ghost.mapToItem(board, ghost.width / 2, 0)
                if (p.x > board.width - 40)
                    board.contentX = Math.min(board.contentWidth - board.width, board.contentX + 12)
                else if (p.x < 40)
                    board.contentX = Math.max(0, board.contentX - 12)
            }
        }
        Repeater {
            model: results.groupModel
            delegate: Rectangle {
                id: boardColumn
                required property string label
                required property int count
                required property bool droppable
                required property string summary
                required property int group
                objectName: "viewBoardColumn" + group
                x: boardColumn.group * (root.boardColumnWidth + Theme.space2)
                width: root.boardColumnWidth
                height: board.height
                radius: Theme.radius
                color: root.dragging && boardColumn.droppable ? Theme.alpha(Theme.accent, 0.06) : Theme.bgSunken
                border.width: root.dragging && boardColumn.droppable ? 1 : 0
                border.color: Theme.alpha(Theme.accent, 0.4)
                Accessible.role: Accessible.List
                Accessible.name: boardColumn.label
                Text {
                    id: columnTitle
                    x: Theme.space3
                    width: parent.width - 2 * Theme.space3 - columnAdd.width
                    height: root.boardHeaderHeight
                    verticalAlignment: Text.AlignVCenter
                    text: boardColumn.label + "  ·  " + boardColumn.count + (boardColumn.summary !== "" ? "  ·  " + boardColumn.summary : "")
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textXs
                    font.weight: Theme.weightSemiBold
                    font.capitalization: Font.AllUppercase
                    color: Theme.fgMuted
                }
                BIconButton {
                    id: columnAdd
                    visible: results.canAddRow
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space1
                    y: Math.round((root.boardHeaderHeight - height) / 2)
                    size: "small"
                    iconName: "plus"
                    label: qsTr("New in “%1”").arg(boardColumn.label)
                    focusPolicy: Qt.NoFocus
                    onClicked: root.startAdding(boardColumn.group, -1)
                }
                Column {
                    x: Theme.space2
                    y: root.boardHeaderHeight
                    width: parent.width - 2 * Theme.space2
                    spacing: Theme.space2
                    Repeater {
                        model: root.rowsOfGroup(boardColumn.group)
                        delegate: Card {
                            required property int modelData
                            row: modelData
                            group: boardColumn.group
                            width: root.boardColumnWidth - 2 * Theme.space2
                        }
                    }
                }
            }
        }
    }
    Component {
        id: boardComponent
        BoardArea {}
    }

    // ---- gallery -------------------------------------------------------------------------------------------
    readonly property real galleryCardWidth: root.touch ? Math.max(140, Math.floor((root.width - Theme.space2) / 2)) : 200
    readonly property int galleryColumns: Math.max(1, Math.floor((root.width + Theme.space2) / (root.galleryCardWidth + Theme.space2)))
    readonly property real galleryCardHeight: 2 * Theme.space2 + root.coverHeight + Theme.space2 + 2 * root.lineH + root.maxDetails * root.smallLineH
    Component {
        id: galleryComponent
        Item {
            width: root.width
            implicitHeight: Math.ceil(results.shownRows.length / root.galleryColumns) * (root.galleryCardHeight + Theme.space2)
            height: implicitHeight
            Repeater {
                model: results.shownRows
                delegate: Card {
                    required property int index
                    required property int modelData
                    row: modelData
                    gallery: true
                    x: (index % root.galleryColumns) * (root.galleryCardWidth + Theme.space2)
                    y: Math.floor(index / root.galleryColumns) * (root.galleryCardHeight + Theme.space2)
                    width: root.galleryCardWidth
                }
            }
        }
    }

    // ---- calendar ------------------------------------------------------------------------------------------
    readonly property real dayWidth: Math.floor(root.width / 7)
    readonly property real dayHeight: root.touch ? 76 : 96
    readonly property int dayItems: root.touch ? 2 : 3
    component CalendarArea: Item {
        id: calendar
        width: root.width
        implicitHeight: weekRow.height + 6 * root.dayHeight
        height: implicitHeight
        /// The grid's day (0 … 41) at (x, y), -1 none.
        function dayAt(x: real, y: real): int {
            const c = Math.floor(x / root.dayWidth)
            const r = Math.floor((y - weekRow.height) / root.dayHeight)
            return c >= 0 && c < 7 && r >= 0 && r < 6 ? r * 7 + c : -1
        }
        Row {
            id: weekRow
            height: root.smallLineH + Theme.space1
            Repeater {
                model: results.weekDays
                delegate: Text {
                    required property string modelData
                    width: root.dayWidth
                    text: modelData
                    textFormat: Text.PlainText
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textXs
                    color: Theme.fgMuted
                }
            }
        }
        Repeater {
            model: 42
            delegate: Rectangle {
                id: day
                required property int index
                readonly property list<int> rows: root.rowsOfDay(day.index)
                objectName: "viewDay" + index
                x: (day.index % 7) * root.dayWidth
                y: weekRow.height + Math.floor(day.index / 7) * root.dayHeight
                width: root.dayWidth
                height: root.dayHeight
                color: results.revision >= 0 && !results.dayInMonth(day.index) ? Theme.bgSunken : Theme.bgElevated
                border.width: 1
                border.color: Theme.borderSubtle
                Text {
                    x: Theme.space1
                    y: 2
                    text: results.revision >= 0 ? "" + results.dayNumber(day.index) : ""
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textXs
                    font.weight: results.revision >= 0 && results.dayIsToday(day.index) ? Theme.weightBold : Theme.weightNormal
                    color: results.revision >= 0 && results.dayIsToday(day.index) ? Theme.accentText : Theme.fgMuted
                }
                TapHandler {
                    enabled: results.canAddRow
                    onDoubleTapped: root.startAdding(-1, day.index)
                }
                Repeater {
                    model: Math.min(root.dayItems, day.rows.length)
                    delegate: Rectangle {
                        id: entry
                        required property int index
                        readonly property int row: day.rows[index]
                        objectName: "viewDayEntry" + entry.row
                        x: 2
                        y: root.smallLineH + 4 + entry.index * (root.smallLineH + 2)
                        width: day.width - 4
                        height: root.smallLineH
                        radius: 3
                        color: Theme.accentBg
                        opacity: root.dragging && root.dragRow === entry.row ? 0.4 : 1
                        Text {
                            x: 3
                            width: parent.width - 6
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            text: results.rowText(entry.row)
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textXs
                            color: Theme.accentText
                        }
                        MouseArea {
                            id: entryMouse
                            anchors.fill: parent
                            pressAndHoldInterval: 400
                            property point pressed
                            property bool moved: false
                            onPressed: (mouse) => { entryMouse.pressed = Qt.point(mouse.x, mouse.y); entryMouse.moved = false }
                            onPressAndHold: (mouse) => {
                                if (!results.movable || root.dragging)
                                    return
                                entryMouse.preventStealing = true
                                root.beginDrag(entry.row, -1, entry, mouse.x, mouse.y)
                            }
                            onPositionChanged: (mouse) => {
                                if (root.dragging) {
                                    root.moveDrag(entry, mouse.x, mouse.y)
                                    return
                                }
                                const dx = mouse.x - entryMouse.pressed.x
                                const dy = mouse.y - entryMouse.pressed.y
                                if (!Theme.touchUi && results.movable && dx * dx + dy * dy > 64) {
                                    entryMouse.preventStealing = true
                                    entryMouse.moved = true
                                    root.beginDrag(entry.row, -1, entry, mouse.x, mouse.y)
                                }
                            }
                            onReleased: {
                                entryMouse.preventStealing = false
                                if (root.dragging)
                                    root.endDrag(false)
                                else if (!entryMouse.moved)
                                    root.openRow(entry.row)
                            }
                            onCanceled: { entryMouse.preventStealing = false; root.endDrag(true) }
                        }
                    }
                }
                Text {
                    visible: day.rows.length > root.dayItems
                    x: Theme.space1
                    y: root.smallLineH + 4 + root.dayItems * (root.smallLineH + 2)
                    text: qsTr("+%1 more").arg(day.rows.length - root.dayItems)
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textXs
                    color: Theme.fgFaint
                }
            }
        }
    }
    Component {
        id: calendarComponent
        CalendarArea {}
    }
}
