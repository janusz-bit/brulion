pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// QueryBlockView — a ```query block (docs/SYNTAX.md §10) with its live results: the query's
// filters as chips ("typ: zadania", "tag: zakupy"), then the matches from the vault index
// (QueryResultModel over the host's QueryHost): tasks with a checkbox that changes the task in
// its own file (this page: an undoable edit; right-click / long press: the status menu), blocks
// and pages rendered like the editor renders them (links, tags, date chips), `group:` results
// under group headers, the page of each item (unless grouped by page) as a quiet link. A click
// on an item opens it at its block; the results follow the index (other devices, edits
// elsewhere; while a result's status menu is open they wait, QueryResultModel.hold(), and follow
// once it closed). The header, the filters (or Enter when the block is selected) edit the query
// source in place; while editing, the results follow the typed query. Without a host that runs
// queries (QueryHost) only the filters show.
Rectangle {
    id: root

    property BlockDelegate block
    readonly property BlockEditor editor: block.editor
    readonly property var query: block.model.query || ({})
    readonly property var filters: query.filters || []
    readonly property var terms: query.terms || []
    readonly property real firstLineCenter: Theme.space3 + headerRow.height / 2
    readonly property bool live: results.available
    readonly property bool emptySource: (block.model.editText || "").trim() === ""
    readonly property int itemFontSize: Theme.textEditor
    readonly property int itemLineHeight: Theme.lineHeightPx(itemFontSize, 1.45)

    implicitHeight: column.height + 2 * Theme.space3
    height: implicitHeight
    radius: Theme.radius
    color: Theme.bgElevated
    border.width: 1
    border.color: root.block.editing ? Theme.alpha(Theme.focusRing, 0.5) : Theme.border

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Query")

    /// The live results (tests, the playground's self test).
    readonly property alias results: results

    QueryResultModel {
        id: results
        controller: root.block.controller
        source: root.block.model.editText
        // The block is created with its rows (while the page scrolls): on a phone 20 of them are
        // about a screenful and take less than half the time of 50 ("Show more" adds the rest).
        maxItems: Theme.touchUi ? 20 : 50
    }

    function filterLabel(key) {
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
    function chipVariant(key) {
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
    function countText() {
        if (!results.ready || !results.ok || root.emptySource || results.itemCount === 0)
            return ""
        const n = results.itemCount
        let what
        switch (results.type) {
        case "tasks": what = qsTr("%n task(s)", "", n); break
        case "pages": what = qsTr("%n page(s)", "", n); break
        case "reminders": what = qsTr("%n reminder(s)", "", n); break
        default: what = qsTr("%n block(s)", "", n); break
        }
        return results.total > n ? qsTr("%1 of %2").arg(what).arg(results.total) : what
    }
    /// The page of an item for its quiet link: daily notes by their date.
    function pageLabel(title, journal) {
        return journal !== "" ? DatesHelper.formatHuman(journal) : title
    }
    /// Opening an item: on this page the editor scrolls to the block.
    function openResult(index) {
        const r = results.open(index, false)
        if (r && r.action === "scrollTo")
            root.editor.revealRow(r.row)
    }
    function activateResultLink(index, link) {
        if (link === "open:") {
            openResult(index)
            return
        }
        const r = results.activateLink(index, link, false)
        if (r && r.action === "scrollTo")
            root.editor.revealRow(r.row)
        else if (r && r.action === "notFound")
            root.editor.noticeNotFound(r)
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
                    text: qsTr("Query")
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
                const repeaters = [filterChips, termChips]
                for (const r of repeaters) {
                    for (let i = 0; i < r.count; ++i) {
                        const chip = r.itemAt(i)
                        if (!chip)
                            continue
                        if (x > 0 && x + chip.width > width) {
                            total += line + spacing
                            x = 0
                            line = 0
                        }
                        x += chip.width + spacing
                        line = Math.max(line, chip.height)
                    }
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
                    required property var modelData
                    size: "small"
                    variant: root.chipVariant(modelData.key)
                    showIcon: variant !== "neutral" && variant !== "done"
                    text: root.editor.autoTextSafe((modelData.negated ? "≠ " : "") + root.filterLabel(modelData.key) + ": " + modelData.value)
                }
            }
            Repeater {
                id: termChips
                property int revision: 0
                onItemAdded: revision++
                onItemRemoved: revision++
                model: root.terms
                delegate: BChip {
                    required property var modelData
                    size: "small"
                    iconName: "search"
                    text: root.editor.autoTextSafe("„" + modelData + "”")
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

        Column {
            id: resultList
            objectName: "queryResultList" // (the editor's self-tests)
            y: column.separatorBottom + column.spacing
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
                    readonly property bool group: kind === "group"
                    readonly property bool closed: status === "done" || status === "cancelled"
                    /// Touch: one line of text centred in a 44 px row (the checkbox's target).
                    readonly property real pad: Theme.touchUi ? Math.max(3, Math.round((Theme.touchTarget - root.itemLineHeight) / 2)) : 1
                    width: resultList.width
                    height: group ? groupLabel.implicitHeight + (index > 0 ? Theme.space3 : Theme.space1) + Theme.space1
                                  : Math.max(itemText.implicitHeight + 2 * pad, Theme.touchUi ? Theme.touchTarget : 0)

                    // Each row has only the parts of its kind: up to 50 rows are created with the
                    // block (while the page scrolls). Group header: "Remont łazienki · 3".
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

                    Text {
                        id: itemText
                        visible: !row.group
                        x: row.isTask ? root.editor.taskBoxWidth + 2 : 0
                        y: row.pad
                        width: parent.width - x
                        textFormat: Text.RichText
                        text: row.group ? "" : row.html + (row.showPage && row.title !== ""
                              ? "&nbsp;&nbsp;<a href=\"open:\" style=\"color:" + Theme.fgFaint + ";text-decoration:none\">·&nbsp;"
                                + TextHelper.escapeHtml(root.pageLabel(row.title, row.journal)) + "</a>" : "")
                        wrapMode: Text.Wrap
                        maximumLineCount: 4
                        elide: Text.ElideRight
                        font.family: Theme.fontEditor
                        font.pixelSize: root.itemFontSize
                        font.strikeout: row.status === "cancelled"
                        lineHeightMode: Text.FixedHeight
                        lineHeight: root.itemLineHeight
                        color: row.closed ? Theme.fgFaint : Theme.fg
                        linkColor: Theme.link
                        Accessible.role: Accessible.Link
                        Accessible.name: row.text + (row.showPage ? " — " + row.title : "")
                    }
                    HoverHandler {
                        id: itemHover
                        enabled: !row.group && !Theme.touchUi
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        enabled: !row.group
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: (eventPoint) => {
                            const p = itemText.mapFromItem(row, eventPoint.position.x, eventPoint.position.y)
                            const link = itemText.linkAt(p.x, p.y)
                            if (link !== "")
                                root.activateResultLink(row.index, link)
                            else
                                root.openResult(row.index)
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
                        return qsTr("Write filters, e.g. type:tasks status:open tag:zakupy")
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
