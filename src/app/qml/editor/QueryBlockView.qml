pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// QueryBlockView — a ```query block (docs/SYNTAX.md §10) with its live results: the query's
// filters as chips ("type: tasks", "tag: shopping"), then the matches from the vault index
// (QueryResultModel over the host's QueryHost): tasks with a checkbox that changes the task in
// its own file (this page: an undoable edit; right-click / long press: the status menu), blocks
// and pages rendered like the editor renders them (links, tags, date chips), `group:` results
// under group headers, the page of each item (unless grouped by page) as a quiet link; tags and
// chips are rounded shapes, an item ends with "…" after 4 lines (BRichText), the mouse pointer
// resting on a [[page]] or ((block)) link shows its preview (LinkPreviews). A click
// on an item opens it at its block; the results follow the index (other devices, edits
// elsewhere; while a result's status menu is open they wait, QueryResultModel.hold(), and follow
// once it closed). The header, the filters (or Enter when the block is selected) edit the query
// source in place; while editing, the results follow the typed query. Without a host that runs
// queries (QueryHost) only the filters show. A ```dataview block (docs/SYNTAX.md §10 "Dataview
// queries") is shown the same way with Dataview's results: LIST rows, TABLE rows under their
// column headers (equal columns, each cell at most 4 lines), TASK results grouped by page.
Rectangle {
    id: root

    /// The delegate (an Item: BlockDelegate passes itself, and a file cannot store itself into
    /// a property of its own type — compiled, a module-qualified cast crashes).
    property Item blockItem
    final property BlockDelegate block: blockItem as BlockDelegate
    readonly property BlockEditor editor: block.editor
    readonly property list<var> filters: root.block.rowData.query.filters
    readonly property list<string> terms: root.block.rowData.query.terms
    readonly property real firstLineCenter: Theme.space3 + headerRow.height / 2
    readonly property bool live: results.available
    readonly property bool emptySource: Js.trim(root.block.rowData.editText) === ""
    /// A ```dataview block (else a ```query block).
    readonly property bool dataview: root.block.rowData.lang === "dataview"
    /// Dataview TABLE results: rows of cells under column headers.
    readonly property bool table: results.type === "table"
    readonly property int columnCount: Math.max(1, results.columns.length)
    readonly property real columnWidth: resultList.width / root.columnCount
    readonly property int itemFontSize: Theme.textEditor
    readonly property int itemLineHeight: Theme.lineHeightPx(itemFontSize, 1.45)

    implicitHeight: column.height + 2 * Theme.space3
    height: implicitHeight
    radius: Theme.radius
    color: Theme.bgElevated
    border.width: 1
    border.color: root.block.editing ? Theme.alpha(Theme.focusRing, 0.5) : Theme.border

    Accessible.role: Accessible.Grouping
    Accessible.name: root.dataview ? qsTr("Dataview query") : qsTr("Query")

    /// The live results (tests, the playground's self test).
    readonly property alias results: results

    QueryResultModel {
        id: results
        controller: root.block.controller
        source: root.block.rowData.editText
        language: root.dataview ? "dataview" : "query"
        // The block is created with its rows (while the page scrolls): on a phone 20 of them are
        // about a screenful and take less than half the time of 50 ("Show more" adds the rest).
        maxItems: Theme.touchUi ? 20 : 50
    }

    /// What results.open() / activateLink() answered: { action, row, … }.
    MapHolder { id: answer }
    /// The result row whose link menu is open (its link opens there: onLinkMenuOpenRequested).
    property int menuRow: -1

    function filterLabel(key: string): string {
        switch (key) {
        case "type": return qsTr("type")
        case "status": return qsTr("status")
        case "tag": return qsTr("tag")
        case "page": return qsTr("page")
        case "path": return qsTr("path")
        case "due": return qsTr("due")
        case "scheduled": return qsTr("scheduled")
        case "start": return qsTr("start")
        case "done": return qsTr("done")
        case "remind": return qsTr("reminder")
        case "priority": return qsTr("priority")
        case "sort": return qsTr("sort")
        case "group": return qsTr("group")
        case "limit": return qsTr("limit")
        default: return key
        }
    }
    function chipVariant(key: string): string {
        switch (key) {
        case "tag": return "tag"
        case "due": return "due"
        case "scheduled": return "scheduled"
        case "remind": return "reminder"
        case "status": return "done"
        default: return "neutral"
        }
    }
    /// "3 tasks", "12 of 40 blocks" — what the header says about the results.
    function countText(): string {
        if (!results.ready || !results.ok || root.emptySource || results.itemCount === 0)
            return ""
        const n = results.itemCount
        let what
        switch (results.type) {
        case "tasks": what = qsTr("%n task(s)", "", n); break
        case "pages": what = qsTr("%n page(s)", "", n); break
        case "reminders": what = qsTr("%n reminder(s)", "", n); break
        case "list": what = qsTr("%n result(s)", "", n); break
        case "table": what = qsTr("%n row(s)", "", n); break
        default: what = qsTr("%n block(s)", "", n); break
        }
        return results.total > n ? qsTr("%1 of %2").arg(what).arg(results.total) : what
    }
    /// The page of an item for its quiet link: daily notes by their date.
    function pageLabel(title: string, journal: string): string {
        return journal !== "" ? DatesHelper.formatHuman(journal) : title
    }
    /// Opening an item: on this page the editor scrolls to the block.
    function openResult(index: int): void {
        answer.map = results.open(index, false)
        if (answer.map.action === "scrollTo")
            root.editor.revealRow(answer.map.row)
    }
    /// What results.activateLink() answered: scroll to a row of this page, or say it was not found.
    function followAnswer(): void {
        if (answer.map.action === "scrollTo")
            root.editor.revealRow(answer.map.row)
        else if (answer.map.action === "notFound")
            root.editor.noticeNotFound(answer.map)
    }
    /// The link at (x, y) of `text` ("" none).
    function linkIn(text: BRichText, x: real, y: real): string {
        return text.linkAt(x, y)
    }
    /// The menu of the token or link at `pos` of a result row (`row`: its delegate); false: none.
    function resultMenu(index: int, path: string, line: int, raw: string, text: BRichText, x: real, y: real): bool {
        const link = root.linkIn(text, x, y)
        if (link === "" || link === "open:")
            return false
        if (root.editor.openTokenMenuIn(path, line, raw, link, text, x, y))
            return true
        root.menuRow = index
        return root.editor.openLinkMenu(link, path, root.block.index, root, text, x, y)
    }
    Connections {
        target: root.editor
        function onLinkMenuOpenRequested(requester: Item, link: string, newPane: bool) {
            if (requester !== root)
                return
            answer.map = results.activateLink(root.menuRow, link, newPane)
            root.followAnswer()
        }
    }
    function activateResultLink(index: int, link: string): void {
        if (link === "open:") {
            openResult(index)
            return
        }
        answer.map = results.activateLink(index, link, false)
        root.followAnswer()
    }

    // A column laid out with bindings, not a ColumnLayout: the block has its final height when the
    // editor's list creates it (with its first results, QueryResultModel), not after the next
    // polish — a block growing after the list had placed it pushed the text being read while the
    // page scrolled (docs/UI.md "Scrolling performance"). Like a ColumnLayout, the spacing goes
    // between the visible parts only.
    Item {
        id: column
        readonly property int spacing: Theme.space2
        readonly property real headerBottom: headerRow.height
        readonly property real filtersBottom: filtersFlow.visible ? filtersFlow.y + filtersFlow.height : headerBottom
        readonly property real sourceBottom: sourceLoader.visible ? sourceLoader.y + sourceLoader.height : filtersBottom
        readonly property real separatorBottom: resultsSeparator.visible ? resultsSeparator.y + resultsSeparator.height : sourceBottom
        readonly property real headersBottom: columnHeaders.visible ? columnHeaders.y + columnHeaders.height : separatorBottom
        readonly property real resultsBottom: resultList.visible ? resultList.y + resultList.height : separatorBottom
        readonly property real moreBottom: moreButton.visible ? moreButton.y + moreButton.implicitHeight : resultsBottom
        x: Theme.space4
        y: Theme.space3
        width: parent.width - 2 * Theme.space4
        height: messageRow.visible ? messageRow.y + messageRow.height : moreBottom

        // ---- Header: what the block is, how many results; a tap edits the query -------------
        Item {
            id: headerRow
            width: parent.width
            // As tall as with the edit button shown (on hover): the block keeps its height while
            // the pointer passes over it.
            height: Math.max(queryLabel.implicitHeight, editQuery.implicitHeight, Theme.touchUi ? 32 : 22)

            RowLayout {
                id: headerContent
                anchors.fill: parent
                spacing: Theme.space2
                BIcon { name: "filter"; size: Theme.iconSizeSm; color: Theme.fgMuted }
                Text {
                    id: queryLabel
                    text: root.dataview ? qsTr("Dataview") : qsTr("Query")
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    font.weight: Theme.weightSemiBold
                    color: Theme.fgMuted
                }
                Text {
                    Layout.fillWidth: true
                    text: root.countText()
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgFaint
                    elide: Text.ElideRight
                }
                BSpinner {
                    visible: root.live && !results.complete
                    size: 14
                    running: visible
                }
                BIconButton {
                    id: editQuery
                    visible: !root.block.editing && (headerHover.hovered || Theme.touchUi)
                    size: "small"
                    iconName: "pencil"
                    label: qsTr("Edit query")
                    focusPolicy: Qt.NoFocus
                    onClicked: root.editor.startEditing(root.block.index, -1)
                }
            }
            HoverHandler { id: headerHover; enabled: !Theme.touchUi }
            TapHandler {
                enabled: !root.block.editing
                onTapped: if (!root.editor.blockTapped(root.block.index)) root.editor.startEditing(root.block.index, -1)
            }
            // A finger held on it selects the block (touch UIs).
            TapHandler {
                enabled: !root.block.editing && Theme.touchUi
                acceptedDevices: PointerDevice.TouchScreen
                onLongPressed: root.editor.longPressText(root.block.index)
            }
        }

        // ---- The filters as chips (a tap edits the query) ----------------------------------------
        Flow {
            id: filtersFlow
            y: column.headerBottom + column.spacing
            width: parent.width
            visible: !root.block.editing && (root.filters.length > 0 || root.terms.length > 0)
            spacing: 6
            // The Flow's own height comes with its next polish: the same lines here (a chip goes
            // to the next line when it does not fit). (`revision`: itemAt() notifies nothing, items
            // may come after the count; read for real — a bare `void` read is compiled away.)
            height: {
                if (filterChips.revision < 0 || termChips.revision < 0)
                    return 0
                let x = 0
                let line = 0
                let total = 0
                const filterCount = filterChips.count
                for (let i = 0; i < filterCount + termChips.count; ++i) {
                    const chip = i < filterCount ? filterChips.itemAt(i) : termChips.itemAt(i - filterCount)
                    if (!chip)
                        continue
                    if (x > 0 && x + chip.width > filtersFlow.width) {
                        total += line + filtersFlow.spacing
                        x = 0
                        line = 0
                    }
                    x += chip.width + filtersFlow.spacing
                    line = Math.max(line, chip.height)
                }
                return total + line
            }
            Repeater {
                id: filterChips
                property int revision: 0
                onItemAdded: revision++
                onItemRemoved: revision++
                model: root.filters
                delegate: BChip {
                    id: filterChip
                    required property string key
                    required property string value
                    required property bool negated
                    size: "small"
                    variant: root.chipVariant(filterChip.key)
                    showIcon: variant !== "neutral" && variant !== "done"
                    text: (filterChip.negated ? "≠ " : "") + root.filterLabel(filterChip.key) + ": " + filterChip.value
                }
            }
            Repeater {
                id: termChips
                property int revision: 0
                onItemAdded: revision++
                onItemRemoved: revision++
                model: root.terms
                delegate: BChip {
                    required property string modelData
                    size: "small"
                    iconName: "search"
                    text: "„" + modelData + "”"
                }
            }
            TapHandler {
                enabled: !root.block.editing
                onTapped: if (!root.editor.blockTapped(root.block.index)) root.editor.startEditing(root.block.index, -1)
            }
            // A finger held on it selects the block (touch UIs).
            TapHandler {
                enabled: !root.block.editing && Theme.touchUi
                acceptedDevices: PointerDevice.TouchScreen
                onLongPressed: root.editor.longPressText(root.block.index)
            }
        }

        Loader {
            id: sourceLoader
            y: column.filtersBottom + column.spacing
            width: parent.width
            active: root.block.editing
            visible: active
            sourceComponent: InlineEditor {
                width: sourceLoader.width
                block: root.block
                multiline: true
                highlightMode: "code"
                font.family: Theme.fontMono
                font.pixelSize: Theme.textSm
                lineHeightPx: Theme.lineHeightPx(Theme.textSm, 1.5)
            }
        }

        // ---- Results ---------------------------------------------------------------------------
        Rectangle {
            id: resultsSeparator
            y: column.sourceBottom + column.spacing + Theme.space1
            width: parent.width
            visible: root.live && results.ready && !root.emptySource && (results.itemCount > 0 || !results.ok)
            height: 1
            color: Theme.borderSubtle
        }

        // Dataview TABLE: the column headers.
        Item {
            id: columnHeaders
            y: column.separatorBottom + column.spacing
            width: parent.width
            visible: root.live && results.ok && !root.emptySource && root.table && results.itemCount > 0
            height: visible ? Math.ceil(Theme.lineHeightPx(Theme.textXs, 1.4)) : 0
            Repeater {
                model: results.columns
                delegate: Text {
                    required property int index
                    required property string modelData
                    x: index * root.columnWidth
                    width: root.columnWidth - Theme.space2
                    text: modelData
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textXs
                    font.weight: Theme.weightSemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.4
                    color: Theme.fgMuted
                    elide: Text.ElideRight
                }
            }
        }

        Column {
            id: resultList
            objectName: "queryResultList" // (the editor's self-tests)
            y: column.headersBottom + column.spacing
            width: parent.width
            visible: root.live && results.ok && !root.emptySource
            spacing: Theme.touchUi ? 0 : 2
            // (`revision`: itemAt() notifies nothing, items may come after the count, and new results
            // with as many rows replace them without a count change; read for real — qmlcachegen
            // compiles a bare `void resultRows.revision` away with the dependency: the list kept the
            // old rows' height.)
            height: {
                if (resultRows.revision < 0)
                    return 0
                let h = 0
                for (let i = 0; i < resultRows.count; ++i) {
                    const row = resultRows.itemAt(i)
                    if (row)
                        h += row.height + (i > 0 ? spacing : 0)
                }
                return h
            }

            Repeater {
                id: resultRows
                property int revision: 0
                onItemAdded: revision++
                onItemRemoved: revision++
                model: root.live ? results : null
                delegate: Item {
                    id: row
                    required property int index
                    required property string kind
                    required property string label
                    required property int count
                    required property string path
                    required property string title
                    required property string html
                    required property string status
                    required property bool isTask
                    required property bool showPage
                    required property string journal
                    required property string text
                    required property int line
                    required property string raw
                    required property list<string> cells
                    readonly property bool group: kind === "group"
                    /// A Dataview TABLE row: cells instead of one text.
                    readonly property bool tableRow: root.table && !group
                    readonly property BRichText textItem: itemText
                    readonly property bool closed: status === "done" || status === "cancelled"
                    /// The link under the mouse or the pressing finger, underlined (BlockText.qml).
                    property string hoveredLink: ""
                    /// The pointer at (x, y) of the row: the link there underlined; at() its place in the text.
                    property point at
                    /// Where a right-click was, in the scene (the block menu's place).
                    property point scene
                    /// The text the link preview was asked for (a TABLE row's cell under the pointer).
                    property Item previewText: itemText
                    /// The text under x of the row: a TABLE row's cell, else the item's text.
                    function textAt(x: real): Item {
                        if (!row.tableRow)
                            return itemText
                        const cell = cellRepeater.itemAt(Math.min(cellRepeater.count - 1, Math.max(0, Math.floor(x / root.columnWidth))))
                        return cell !== null ? cell : itemText
                    }
                    function trackLink(x: real, y: real): void {
                        const text = row.textAt(x)
                        row.at = text.mapFromItem(row, x, y)
                        const link = row.group ? "" : root.linkIn(text as BRichText, row.at.x, row.at.y)
                        const h = link !== "" && !Js.startsWith(link, "task:") && !root.editor.editsOnClick(link) ? link : ""
                        if (h !== row.hoveredLink)
                            row.hoveredLink = h
                    }
                    /// The menu of the token or link at (x, y) of the row; false: none there.
                    function menuAt(x: real, y: real): bool {
                        const text = row.textAt(x)
                        row.at = text.mapFromItem(row, x, y)
                        return root.resultMenu(row.index, row.path, row.line, row.raw, text as BRichText, row.at.x, row.at.y)
                    }
                    readonly property string itemHtml: row.group ? "" : row.html + (row.showPage && row.title !== ""
                        ? "&nbsp;&nbsp;<a href=\"open:\" style=\"color:" + BlockViews.colorName(Theme.fgFaint) + ";text-decoration:none\">·&nbsp;"
                          + TextHelper.escapeHtml(root.pageLabel(row.title, row.journal)) + "</a>" : "")
                    /// Touch: one line of text centred in a 44 px row (the checkbox's target).
                    readonly property real pad: Theme.touchUi ? Math.max(3, Math.round((Theme.touchTarget - root.itemLineHeight) / 2)) : 1
                    width: resultList.width
                    height: group ? groupLabel.implicitHeight + (index > 0 ? Theme.space3 : Theme.space1) + Theme.space1
                                  : Math.max((row.tableRow ? cellsRow.height : itemText.implicitHeight) + 2 * pad,
                                             Theme.touchUi ? Theme.touchTarget : 0)

                    // Each row has only the parts of its kind: up to 50 rows are created with the
                    // block (while the page scrolls). Group header: "Bathroom renovation · 3".
                    Loader {
                        id: groupLabel
                        active: row.group
                        y: row.index > 0 ? Theme.space3 : Theme.space1
                        width: parent.width
                        sourceComponent: Text {
                            text: row.label + "  ·  " + row.count
                            textFormat: Text.PlainText
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textXs
                            font.weight: Theme.weightSemiBold
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.4
                            color: Theme.fgMuted
                            elide: Text.ElideRight
                        }
                    }

                    Rectangle {
                        visible: !row.group && itemHover.hovered
                        x: -Theme.space2
                        width: parent.width + 2 * Theme.space2
                        height: parent.height
                        radius: Theme.radiusSm
                        color: Theme.bgHover
                    }

                    Loader {
                        active: !row.group && row.isTask
                        sourceComponent: TaskBox {
                            id: resultBox
                            objectName: "queryTaskBox"
                            x: -Math.round((width - boxSize) / 2) + 2
                            y: Math.round(row.pad + root.itemLineHeight / 2 - height / 2)
                            status: row.status
                            Accessible.name: qsTr("Task: %1").arg(row.text)
                            onToggled: results.toggleTask(row.index)
                            onStatusChosen: (status) => results.setTaskStatus(row.index, status)
                            // The results wait while its menu is open: new rows would take it away
                            // (a box destroyed with its menu open lets go by itself).
                            onMenuOpenChanged: {
                                if (menuOpen)
                                    results.hold(resultBox)
                                else
                                    results.release(resultBox)
                            }
                        }
                    }

                    // A TABLE row's cells side by side; the row is as tall as its tallest cell.
                    // (`revision`: itemAt() notifies nothing; read for real, see resultList.)
                    Item {
                        id: cellsRow
                        visible: row.tableRow
                        Accessible.role: Accessible.Row
                        Accessible.name: row.text
                        y: row.pad
                        width: parent.width
                        height: {
                            if (!row.tableRow || cellRepeater.revision < 0)
                                return 0
                            let h = root.itemLineHeight
                            for (let i = 0; i < cellRepeater.count; ++i) {
                                const cell = cellRepeater.itemAt(i)
                                if (cell)
                                    h = Math.max(h, cell.implicitHeight)
                            }
                            return h
                        }
                        Repeater {
                            id: cellRepeater
                            property int revision: 0
                            onItemAdded: revision++
                            onItemRemoved: revision++
                            model: row.cells
                            delegate: BRichText {
                                required property int index
                                required property string modelData
                                x: index * root.columnWidth
                                width: root.columnWidth - Theme.space2
                                html: modelData
                                hoveredLink: row.hoveredLink
                                maximumLineCount: 4
                                font.family: Theme.fontEditor
                                font.pixelSize: root.itemFontSize
                                lineHeight: root.itemLineHeight
                                color: Theme.fg
                                Accessible.role: Accessible.Cell
                                Accessible.name: index < results.columns.length ? results.columns[index] : ""
                            }
                        }
                    }

                    BRichText {
                        id: itemText
                        visible: !row.group && !row.tableRow
                        x: row.isTask ? root.editor.taskBoxWidth + 2 : 0
                        y: row.pad
                        width: parent.width - x
                        html: row.itemHtml
                        hoveredLink: row.hoveredLink
                        maximumLineCount: 4
                        font.family: Theme.fontEditor
                        font.pixelSize: root.itemFontSize
                        font.strikeout: row.status === "cancelled"
                        lineHeight: root.itemLineHeight
                        color: row.closed ? Theme.fgFaint : Theme.fg
                        Accessible.role: Accessible.Link
                        Accessible.name: row.text + (row.showPage ? " — " + row.title : "")
                    }
                    // Screen readers: the item's links as link items of their own (the row stays a
                    // link to the item; at most its 4 shown lines).
                    BLinkTargets {
                        x: itemText.x
                        y: itemText.y
                        textItem: itemText.textItem
                        html: row.itemHtml
                        lineHeight: root.itemLineHeight
                        maxHeight: 4 * root.itemLineHeight
                        shown: !row.group && !row.tableRow
                        onActivated: (href, x, y) => {
                            if (root.editor.editsOnClick(href))
                                root.resultMenu(row.index, row.path, row.line, row.raw, itemText, x, y)
                            else
                                root.activateResultLink(row.index, href)
                        }
                    }
                    HoverHandler {
                        id: itemHover
                        enabled: !row.group && !Theme.touchUi
                        cursorShape: Qt.PointingHandCursor
                        onPointChanged: {
                            // The link under the resting pointer: its preview (LinkPreviews).
                            row.trackLink(itemHover.point.position.x, itemHover.point.position.y)
                            const text = row.textAt(itemHover.point.position.x)
                            if (text !== row.previewText)
                                LinkPreviews.leave(row.previewText)
                            row.previewText = text
                            LinkPreviews.hover(text, row.hoveredLink, row.path, root.block.controller ? root.block.controller.host : null,
                                               row.at.x, row.at.y)
                        }
                        onHoveredChanged: {
                            if (!hovered) {
                                LinkPreviews.leave(row.previewText)
                                if (!itemTap.pressed)
                                    row.hoveredLink = ""
                            }
                        }
                    }
                    TapHandler {
                        id: itemTap
                        enabled: !row.group
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onPressedChanged: {
                            if (pressed)
                                row.trackLink(itemTap.point.position.x, itemTap.point.position.y)
                            else if (!itemHover.hovered)
                                row.hoveredLink = ""
                        }
                        onTapped: (eventPoint) => {
                            const x = eventPoint.position.x
                            const y = eventPoint.position.y
                            const text = row.textAt(x)
                            row.at = text.mapFromItem(row, x, y)
                            const link = root.linkIn(text as BRichText, row.at.x, row.at.y)
                            if (link !== "")
                                root.activateResultLink(row.index, link)
                            else
                                root.openResult(row.index)
                        }
                    }
                    // A right-click or a finger held on a token or a link of the item: its menu
                    // (the change goes to the item's own file); elsewhere the block's own gestures.
                    TapHandler {
                        enabled: !row.group
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                        acceptedButtons: Qt.RightButton
                        gesturePolicy: TapHandler.WithinBounds
                        onTapped: (eventPoint) => {
                            const x = eventPoint.position.x
                            const y = eventPoint.position.y
                            row.scene = eventPoint.scenePosition
                            if (!row.menuAt(x, y))
                                root.editor.openBlockMenuAt(root.block.index, row.scene)
                        }
                    }
                    TapHandler {
                        id: resultHold
                        enabled: !row.group && Theme.touchUi
                        acceptedDevices: PointerDevice.TouchScreen
                        onLongPressed: {
                            if (!root.editor.selectionMode)
                                row.menuAt(resultHold.point.position.x, resultHold.point.position.y)
                        }
                    }
                }
            }
        }

        // ---- States: more results, nothing found, errors, no live results ------------------------
        BButton {
            id: moreButton
            y: column.resultsBottom + column.spacing
            visible: root.live && results.ok && results.hiddenCount > 0 && !root.emptySource
            size: "small"
            variant: "ghost"
            iconName: "chevron-down"
            text: qsTr("Show %n more", "", Math.min(50, results.hiddenCount))
            focusPolicy: Qt.NoFocus
            onClicked: results.showMore()
        }

        Item {
            id: messageRow
            y: column.moreBottom + column.spacing
            width: parent.width
            height: Math.max(messageIcon.y + messageIcon.height, message.implicitHeight)
            visible: message.text !== ""
            BIcon {
                id: messageIcon
                y: 1
                name: !root.live ? "info" : !results.ok ? "circle-alert" : root.emptySource ? "info" : "search"
                size: Theme.iconSizeSm
                color: root.live && !results.ok ? Theme.dangerText : Theme.fgFaint
            }
            Text {
                id: message
                x: messageIcon.width + Theme.space2
                width: Math.max(0, parent.width - x)
                text: {
                    if (!root.live)
                        return qsTr("Query results are shown when a notebook is open.")
                    if (!results.ready)
                        return ""
                    if (!results.ok)
                        return results.error !== "" ? results.error : qsTr("This query cannot be run.")
                    if (root.emptySource)
                        return root.dataview ? qsTr("Write a Dataview query, e.g. TABLE status FROM #projekt")
                                             : qsTr("Write filters, e.g. type:tasks status:open tag:zakupy")
                    if (results.itemCount === 0)
                        return results.complete ? qsTr("Nothing matches this query.") : qsTr("Nothing yet — the notebook is still being indexed.")
                    if (!results.complete)
                        return qsTr("The notebook is still being indexed: more results may appear.")
                    return ""
                }
                textFormat: Text.PlainText
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: root.live && !results.ok ? Theme.dangerText : Theme.fgFaint
                wrapMode: Text.Wrap
            }
        }
    }
}
