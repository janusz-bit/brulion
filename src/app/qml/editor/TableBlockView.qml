pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// TableBlockView — a GFM table as a grid (docs/SYNTAX.md §9 "Table formulas"). Cells show their
// Markdown rendered like block text (bold, links, tags, date chips — the core InlineRenderer, so
// note text is never interpreted as HTML); formula cells (`=sum(above)`, `=[Cena]*[Ilość]`) show
// the result (errors as #ERR with the message as tooltip, cycles as #CYCLE). Links in cells work
// like links in blocks (a click elsewhere edits the cell). Clicking a cell edits its raw text (the formula);
// Enter / Tab / Shift+Tab move, Esc cancels the cell's edit. Typing reaches the document at once
// (PageController.setTableCell with `typing`: one undo step per cell), so a cell left open when
// the app is suspended or closed is saved like any text. Rows and columns are added with
// the buttons below the grid (shown on hover, when selected or while a cell is edited).
Item {
    id: root

    property BlockDelegate block
    readonly property var table: block.model.tableData || ({})
    readonly property var header: table.header || []
    readonly property var headerHtml: table.headerHtml || []
    readonly property var aligns: table.aligns || []
    readonly property var rows: table.rows || []
    readonly property int columns: header.length
    readonly property int cellPadding: Theme.space3
    readonly property int rowHeight: Theme.touchUi ? 40 : 32
    readonly property real firstLineCenter: rowHeight / 2
    property int editRow: -2   // -2 none, -1 header
    property int editColumn: -1
    readonly property bool active: editRow > -2 || block.model.isSelected || hover.hovered

    /// Natural column widths (clamped), stretched to fill the available width.
    readonly property var columnWidths: {
        const out = []
        let total = 0
        for (let c = 0; c < columns; ++c) {
            let w = headerMetrics.advanceWidth(header[c] || "")
            for (let r = 0; r < rows.length; ++r) {
                const cell = rows[r][c]
                if (cell)
                    w = Math.max(w, cellMetrics.advanceWidth(cell.plain !== undefined ? cell.plain : (cell.display || "")))
            }
            w = Math.min(280, Math.max(56, Math.ceil(w) + 2 * cellPadding))
            out.push(w)
            total += w
        }
        if (total > 0 && total < width) {
            const k = width / total
            for (let c = 0; c < out.length; ++c)
                out[c] = Math.floor(out[c] * k)
        }
        return out
    }
    readonly property real gridWidth: columnWidths.reduce((a, b) => a + b, 0)

    implicitHeight: frame.height + (buttons.visible ? buttons.height + Theme.space1 : 0)
    height: implicitHeight

    Accessible.role: Accessible.Table
    Accessible.name: qsTr("Table")

    FontMetrics { id: headerMetrics; font.family: Theme.fontUi; font.pixelSize: Theme.textBase; font.weight: Theme.weightSemiBold }
    FontMetrics { id: cellMetrics; font.family: Theme.fontUi; font.pixelSize: Theme.textBase }
    HoverHandler { id: hover; enabled: !Theme.touchUi }

    function focusFirstCell() {
        startCell(rows.length > 0 ? 0 : -1, 0)
    }
    function startCell(r, c) {
        if (c < 0 || c >= columns || r < -1 || r >= rows.length)
            return
        root.block.controller.selectedRow = -1
        root.block.controller.stopEditing()
        editRow = r
        editColumn = c
    }
    function rawText(r, c) {
        if (r === -1)
            return header[c] || ""
        const cell = rows[r] ? rows[r][c] : null
        return cell ? cell.text : ""
    }
    function commit(r, c, value) {
        if (value !== rawText(r, c))
            root.block.controller.setTableCell(root.block.index, r, c, value)
    }
    function finish() {
        editRow = -2
        editColumn = -1
    }

    Flickable {
        id: frame
        width: parent.width
        height: grid.height
        contentWidth: root.gridWidth
        contentHeight: grid.height
        clip: root.gridWidth > width
        interactive: root.gridWidth > width
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick

        Rectangle {
            id: grid
            width: root.gridWidth
            height: (root.rows.length + 1) * root.rowHeight
            radius: Theme.radius
            color: Theme.bgElevated
            border.width: 1
            border.color: root.editRow > -2 ? Theme.alpha(Theme.focusRing, 0.5) : Theme.border

            // Header background.
            Rectangle {
                x: 1
                y: 1
                width: parent.width - 2
                height: root.rowHeight - 1
                topLeftRadius: Theme.radius - 1
                topRightRadius: Theme.radius - 1
                color: Theme.bgSunken
            }

            Repeater {
                model: root.rows.length + 1
                delegate: Item {
                    id: rowItem
                    required property int index
                    readonly property int r: index - 1  // -1 = header
                    y: index * root.rowHeight
                    width: grid.width
                    height: root.rowHeight

                    Rectangle {
                        visible: rowItem.index > 0
                        width: parent.width
                        height: 1
                        color: rowItem.index === 1 ? Theme.borderStrong : Theme.borderSubtle
                    }

                    Repeater {
                        model: root.columns
                        delegate: Item {
                            id: cell
                            required property int index
                            readonly property int c: index
                            readonly property var cellData: rowItem.r >= 0 && root.rows[rowItem.r] ? root.rows[rowItem.r][c] : null
                            readonly property bool headerCell: rowItem.r === -1
                            readonly property bool formula: cellData ? cellData.formula : false
                            readonly property string error: cellData ? cellData.error : ""
                            readonly property string shown: headerCell ? (root.header[c] || "") : cellData ? cellData.display : ""
                            /// Rendered cell (escaped rich text from the core renderer).
                            readonly property string shownHtml: headerCell ? (root.headerHtml[c] || "")
                                                                : cellData && cellData.html !== undefined ? cellData.html : ""
                            readonly property string align: root.aligns[c] || ""
                            readonly property bool editingCell: root.editRow === rowItem.r && root.editColumn === c
                            x: root.columnWidths.slice(0, c).reduce((a, b) => a + b, 0)
                            width: root.columnWidths[c] || 0
                            height: root.rowHeight
                            Accessible.role: headerCell ? Accessible.ColumnHeader : Accessible.Cell
                            Accessible.name: shown

                            Rectangle {
                                visible: cell.c > 0
                                width: 1
                                height: parent.height
                                color: Theme.borderSubtle
                            }
                            Rectangle {
                                visible: cell.formula && !cell.editingCell
                                anchors.fill: parent
                                anchors.margins: 1
                                color: Theme.alpha(Theme.info, Theme.dark ? 0.07 : 0.06)
                            }
                            Text {
                                id: cellText
                                visible: !cell.editingCell
                                anchors.fill: parent
                                anchors.leftMargin: root.cellPadding
                                anchors.rightMargin: root.cellPadding
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: cell.align === "right" || (cell.formula && cell.align === "") ? Text.AlignRight
                                                   : cell.align === "center" ? Text.AlignHCenter : Text.AlignLeft
                                // Rich text cannot elide: one line, clipped at the cell's edge.
                                text: cell.shownHtml
                                textFormat: Text.RichText
                                wrapMode: Text.NoWrap
                                clip: contentWidth > width
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textBase
                                font.weight: cell.headerCell ? Theme.weightSemiBold : cell.formula ? Theme.weightMedium : Theme.weightNormal
                                color: cell.error !== "" ? Theme.dangerText : Theme.fg
                                linkColor: Theme.link
                                Accessible.ignored: true
                            }
                            BToolTip {
                                visible: (cell.error !== "" || cellText.contentWidth > cellText.width) && cellHover.hovered
                                text: root.block.editor.autoTextSafe(cell.error !== "" ? cell.error : cell.shown)
                            }
                            HoverHandler {
                                id: cellHover
                                enabled: !Theme.touchUi
                                cursorShape: cellText.linkAt(point.position.x - cellText.x, point.position.y - cellText.y) !== ""
                                             ? Qt.PointingHandCursor : Qt.IBeamCursor
                            }
                            TapHandler {
                                enabled: !cell.editingCell
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: (eventPoint) => {
                                    const p = eventPoint.position
                                    // Reminder chips name ranges of the cell's text, not of the
                                    // block: those cells are edited like any other.
                                    const link = cellText.linkAt(p.x - cellText.x, p.y - cellText.y)
                                    if (link !== "" && !link.startsWith("reminder:"))
                                        root.block.editor.activateLink(root.block.index, link)
                                    else
                                        root.startCell(rowItem.r, cell.c)
                                }
                            }

                            Loader {
                                anchors.fill: parent
                                active: cell.editingCell
                                sourceComponent: T.TextField {
                                    id: input
                                    leftPadding: root.cellPadding
                                    rightPadding: root.cellPadding
                                    verticalAlignment: TextInput.AlignVCenter
                                    font.family: cell.formula ? Theme.fontMono : Theme.fontUi
                                    font.pixelSize: Theme.textBase
                                    font.weight: cell.headerCell ? Theme.weightSemiBold : Theme.weightNormal
                                    color: Theme.fg
                                    selectionColor: Theme.selection
                                    selectedTextColor: Theme.selectionFg
                                    selectByMouse: true
                                    Accessible.name: qsTr("Cell %1").arg(cell.shown)
                                    background: Rectangle {
                                        color: Theme.bgElevated2
                                        border.width: 2
                                        border.color: Theme.focusRing
                                        radius: 2
                                    }
                                    property bool done: false
                                    property bool ready: false
                                    function leave(nextRow, nextColumn) {
                                        if (done)
                                            return
                                        done = true
                                        const r = rowItem.r
                                        const c = cell.c
                                        root.commit(r, c, text)
                                        if (nextRow === undefined)
                                            root.finish()
                                        else if (nextRow >= root.rows.length) {
                                            root.block.controller.addTableRow(root.block.index)
                                            Qt.callLater(() => { if (root) root.startCell(nextRow, nextColumn) })
                                        } else
                                            Qt.callLater(() => { if (root) root.startCell(nextRow, nextColumn) })
                                    }
                                    Component.onCompleted: {
                                        text = root.rawText(rowItem.r, cell.c)
                                        cursorPosition = length
                                        ready = true
                                        forceActiveFocus()
                                    }
                                    onTextChanged: {
                                        if (!ready || done)
                                            return
                                        root.block.controller.setTableCell(root.block.index, rowItem.r, cell.c, text, true)
                                    }
                                    onActiveFocusChanged: if (!activeFocus) leave()
                                    Keys.onPressed: (event) => {
                                        const r = rowItem.r
                                        const c = cell.c
                                        if (event.key === Qt.Key_Escape) {
                                            done = true
                                            root.finish()
                                            // Cancel: the cell's typing is one undo step. Undoing it
                                            // rebuilds this delegate, so keep what is needed.
                                            const controller = root.block.controller
                                            const list = root.block.editor.listView
                                            const blockRow = root.block.index
                                            controller.cancelTableCell(blockRow, r, c)
                                            controller.selectedRow = blockRow
                                            list.forceActiveFocus()
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                            leave(r + 1, c)
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_Tab) {
                                            if (c + 1 < root.columns) leave(r, c + 1)
                                            else leave(r + 1, 0)
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_Backtab) {
                                            if (c > 0) leave(r, c - 1)
                                            else if (r > -1) leave(r - 1, root.columns - 1)
                                            event.accepted = true
                                        } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Z) {
                                            done = true
                                            root.finish()
                                            const controller = root.block.controller
                                            const list = root.block.editor.listView
                                            const blockRow = root.block.index
                                            controller.undo()
                                            if (controller.editingRow < 0) {
                                                controller.selectedRow = Math.min(blockRow, controller.blocks.count - 1)
                                                list.forceActiveFocus()
                                            }
                                            event.accepted = true
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Row {
        id: buttons
        visible: root.active || Theme.touchUi
        y: frame.height + Theme.space1
        spacing: Theme.space1
        BButton {
            size: "small"
            variant: "ghost"
            iconName: "plus"
            text: qsTr("Row")
            focusPolicy: Qt.NoFocus
            onClicked: root.block.controller.addTableRow(root.block.index)
        }
        BButton {
            size: "small"
            variant: "ghost"
            iconName: "plus"
            text: qsTr("Column")
            focusPolicy: Qt.NoFocus
            onClicked: root.block.controller.addTableColumn(root.block.index)
        }
    }
}
