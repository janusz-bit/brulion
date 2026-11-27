// PageView's cell selection (docs/ATRAMENT.md "The block editor in Atrament (M2)", step 5, tables):
// TableBlockView's rectangle of cells, made from the edited cell by Shift+arrows (a horizontal one
// at the edge of its text) or Shift+click, its end moved by Shift+click, and its keys while no cell
// is edited (TableBlockView.rangeKey): the arrows move it (with Shift its end), Home / End the
// row's first / last cell (with Ctrl the table's), Tab / Shift+Tab the next / previous cell (a new
// row after the last cell), Enter / F2 edit the cursor cell, a typed character edits it with that
// text in place of its own, Delete / Backspace empty the cells, Ctrl+C / Ctrl+X copy and cut them
// as TSV, Ctrl+V pastes the clipboard (TSV, CSV, a Markdown table, lines; one value fills the
// selection) with the pasted cells selected after, Ctrl+A selects every cell, Ctrl+Z / Ctrl+Y /
// Ctrl+Shift+Z the page's undo with the selection kept, Esc ends it and selects the table — each
// change one undo step (PageController.copyTableCells / clearTableCells / pasteClipboardCells),
// its toasts through onNotice. Drawn in the cells (tablerows.cpp): Theme.accent's tint, the cursor
// cell outlined in the focus ring; selected cells for screen readers.
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"
#include "tablelayout.h"
#include "tablerows_p.h"

#include <QCoreApplication>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

QString tableText(const char *source, int n = -1)
{
    // (TableBlockView's own texts: the same translations.)
    return QCoreApplication::translate("TableBlockView", source, nullptr, n);
}

} // namespace

namespace {

// (Rows or columns may have gone since the selection was made: an undo, a filter.)
void clampTo(PageView::CellSelection &s, const TableGrid &g)
{
    s.anchorRow = std::clamp(s.anchorRow, -1, g.rowCount() - 1);
    s.cursorRow = std::clamp(s.cursorRow, -1, g.rowCount() - 1);
    s.anchorColumn = std::clamp(s.anchorColumn, 0, std::max(0, g.columnCount() - 1));
    s.cursorColumn = std::clamp(s.cursorColumn, 0, std::max(0, g.columnCount() - 1));
}

} // namespace

PageView::CellSelection PageView::cellSelection() const
{
    const int row = m_cellSel.uid && m_model ? m_model->rowOfUid(m_cellSel.uid) : -1;
    const TableEntry *e = row >= 0 ? tableEntry(row) : nullptr;
    if (!e)
        return {};
    CellSelection s = m_cellSel.cells;
    clampTo(s, e->grid);
    s.row = row;
    return s;
}

bool PageView::cellSelected(int row, int tableRow, int column) const
{
    const CellSelection s = cellSelection();
    if (s.row < 0 || s.row != row || column < s.left() || column > s.right() || tableRow < s.top() || tableRow > s.bottom())
        return false;
    // (A row between the ends hidden by the filters is not shown, so not selected.)
    const TableEntry *e = tableEntry(row);
    return !e || tableRow == s.anchorRow || tableRow == s.cursorRow || tableRow == -1 || e->grid.rowShown(tableRow);
}

QList<int> PageView::selectedTableRows() const
{
    const CellSelection s = cellSelection();
    const TableEntry *e = s.row >= 0 ? tableEntry(s.row) : nullptr;
    return e ? e->grid.rowsBetween(s.anchorRow, s.cursorRow) : QList<int>{};
}

bool PageView::selectCells(int row, int anchorRow, int anchorColumn, int cursorRow, int cursorColumn,
                           at::Context<PageView> &cx)
{
    const TableEntry *e = isTable(row) ? tableEntry(row) : nullptr;
    if (!e)
        return false;
    const TableGrid &g = e->grid;
    auto valid = [&g](int r, int c) { return c >= 0 && c < g.columnCount() && r >= -1 && r < g.rowCount(); };
    if (!valid(anchorRow, anchorColumn) || !valid(cursorRow, cursorColumn))
        return false;
    const quint64 uid = m_model->uidAt(row);
    // The edited cell is left (its text is the document's already), no block is selected or
    // edited, and no field keeps the keys: they are the selection's (the page's root listener).
    if (m_cellUid)
        stopEditingCell(cx);
    m_cellSel.uid = uid;
    m_cellSel.cells = CellSelection{-1, anchorRow, anchorColumn, cursorRow, cursorColumn};
    if (m_controller) {
        if (m_controller->editingRow() >= 0)
            m_controller->stopEditing();
        if (m_controller->selectedRow() >= 0 || m_controller->selectionCount() > 0)
            m_controller->setSelectedRow(-1);
    }
    for (const std::unique_ptr<at::Window> &w : cx.app().windows()) {
        if (w->drewEntity(cx.entityId()))
            w->blur();
    }
    // The cursor cell comes into view: sideways in its table (its next frame), the table in the page.
    m_revealCell = CellKey{uid, cursorRow, cursorColumn};
    if (std::find(m_drawn.begin(), m_drawn.end(), std::size_t(row)) == m_drawn.end())
        m_handle.scrollToRow(std::size_t(row), at::ListPosition::Nearest);
    cx.notify();
    if (onCellSelectionChanged)
        onCellSelectionChanged(true);
    return true;
}

void PageView::clearCellSelection(at::Context<PageView> &cx)
{
    if (!m_cellSel.uid)
        return;
    m_cellSel = {};
    m_revealCell.reset();
    cx.notify();
    if (onCellSelectionChanged)
        onCellSelectionChanged(false);
}

void PageView::extendCellsFromEdited(int tableRow, int column, at::Context<PageView> &cx)
{
    const TableCell cell = editedCell();
    if (cell.row < 0)
        return;
    commitCellComposition(cx);
    selectCells(cell.row, cell.tableRow, cell.column, tableRow, column, cx);
}

std::pair<int, int> PageView::neighbourCell(const TableGrid &grid, int r, int c, std::u16string_view key)
{
    // TableBlockView.neighbour: Up / Down the shown row above / below (the header and the last row
    // stop), Left / Right the column beside; the cell itself at an edge.
    if (key == u"up" && r > -1)
        r = grid.nextShownRow(r, -1);
    else if (key == u"down") {
        const int next = grid.nextShownRow(r, 1);
        if (next < grid.rowCount())
            r = next;
    } else if (key == u"left" && c > 0) {
        --c;
    } else if (key == u"right" && c + 1 < grid.columnCount()) {
        ++c;
    }
    return {r, c};
}

void PageView::copyCells(bool cut, at::Context<PageView> &)
{
    const CellSelection s = cellSelection();
    PageController *ctl = m_controller.data();
    if (s.row < 0 || !ctl)
        return;
    const QList<int> rows = selectedTableRows();
    const int count = int(rows.size()) * (s.right() - s.left() + 1);
    const QVariantMap answer = ctl->copyTableCells(s.row, rows, s.left(), s.right(), u"tsv"_s, cut);
    if (!onNotice)
        return;
    if (answer.value(u"ok"_s).toBool())
        onNotice(cut ? tableText("Cut %n cell(s)", count) : tableText("Copied %n cell(s)", count), u"success"_s);
    else if (answer.value(u"reason"_s).toString() == u"noClipboard")
        onNotice(tableText("Could not copy: the clipboard is not available"), u"error"_s);
}

void PageView::clearCells(at::Context<PageView> &)
{
    const CellSelection s = cellSelection();
    if (s.row >= 0 && m_controller)
        m_controller->clearTableCells(s.row, selectedTableRows(), s.left(), s.right());
}

void PageView::pasteCells(at::Context<PageView> &cx)
{
    const CellSelection s = cellSelection();
    if (s.row < 0)
        return;
    const int rows = int(selectedTableRows().size());
    pasteCellsAt(s.row, s.top(), s.left(), rows, s.right() - s.left() + 1, cx);
}

void PageView::pasteCellsAt(int row, int tableRow, int column, int rowCount, int columnCount, at::Context<PageView> &cx)
{
    PageController *ctl = m_controller.data();
    const TableEntry *e = ctl ? tableEntry(row) : nullptr;
    if (!e)
        return;
    const QVariantMap answer = ctl->pasteClipboardCells(row, e->grid.rowsFrom(tableRow), column, rowCount, columnCount);
    if (answer.value(u"readOnly"_s).toBool())
        return;
    if (!answer.value(u"ok"_s).toBool()) {
        if (onNotice)
            onNotice(tableText("Nothing to paste"), u"info"_s);
        return;
    }
    const int pastedRows = std::max(1, answer.value(u"rows"_s).toInt());
    const int lastColumn = column + std::max(1, answer.value(u"columns"_s).toInt()) - 1;
    const int cut = answer.value(u"columnsCut"_s).toInt();
    if (onNotice) {
        onNotice(tableText("Pasted %n cell(s)", answer.value(u"cells"_s).toInt()), u"success"_s);
        if (cut > 0)
            onNotice(tableText("%n column(s) of the pasted text did not fit the table and were left out", cut), u"info"_s);
    }
    // The cells it filled are selected (the table as it is now: rows may have been added).
    const TableEntry *now = tableEntry(row);
    if (!now)
        return;
    const int toRow = now->grid.rowAfter(tableRow, pastedRows - 1);
    selectCells(row, tableRow, column, toRow, std::min(lastColumn, now->grid.columnCount() - 1), cx);
}

bool PageView::editCellWith(int row, int tableRow, int column, const QString &typed, at::Context<PageView> &cx)
{
    if (!editCell(row, tableRow, column, -1, cx) || !m_cellInput)
        return false;
    // The character in place of the cell's text, written at once (TableBlockView's typedText).
    const std::u16string value(u16View(typed));
    m_cellInput->update(cx.app(), [&](at::TextInput &t, at::Context<at::TextInput> &c) {
        t.setText(value, c);
        t.setSelection(int(value.size()), int(value.size()), c);
        t.clearUndo();
    });
    cellInputChanged(cx);
    return true;
}

bool PageView::cellRangeKey(const at::Keystroke &k, at::Context<PageView> &cx)
{
    if (!m_cellSel.uid)
        return false;
    const int row = m_model ? m_model->rowOfUid(m_cellSel.uid) : -1;
    const TableEntry *e = row >= 0 && isTable(row) ? tableEntry(row) : nullptr;
    PageController *ctl = m_controller.data();
    if (!e || !ctl) {
        clearCellSelection(cx);
        return false;
    }
    const TableGrid &g = e->grid;
    CellSelection &s = m_cellSel.cells;
    clampTo(s, g);
    const at::Modifiers &m = k.modifiers;
    const bool ctrl = m.control || m.platform;
    const bool shift = m.shift;
    if (m.alt)
        return false;
    const int ar = s.anchorRow, ac = s.anchorColumn, cr = s.cursorRow, cc = s.cursorColumn;
    auto select = [&](int anchorRow, int anchorColumn, int toRow, int toColumn) {
        selectCells(row, anchorRow, anchorColumn, toRow, toColumn, cx);
    };
    const std::u16string_view key = k.key;
    if (key == u"up" || key == u"down" || key == u"left" || key == u"right") {
        if (ctrl)
            return false;
        const auto [r, c] = neighbourCell(g, cr, cc, key);
        if (shift)
            select(ar, ac, r, c);
        else
            select(r, c, r, c);
        return true;
    }
    if (key == u"home" || key == u"end") {
        const int c = key == u"home" ? 0 : g.columnCount() - 1;
        const int r = ctrl ? (key == u"home" ? -1 : g.nextShownRow(g.rowCount(), -1)) : cr;
        if (shift)
            select(ar, ac, r, c);
        else
            select(r, c, r, c);
        return true;
    }
    if (key == u"tab" && !ctrl) {
        if (!shift && cc + 1 < g.columnCount()) {
            select(cr, cc + 1, cr, cc + 1);
        } else if (shift && cc > 0) {
            select(cr, cc - 1, cr, cc - 1);
        } else if (!shift) {
            const int next = g.nextShownRow(cr, 1);
            if (next >= g.rowCount() && !ctl->readOnly()) {
                // The last cell: a new row (as in a cell being edited).
                ctl->addTableRow(row);
                select(next, 0, next, 0);
            } else if (next < g.rowCount()) {
                select(next, 0, next, 0);
            }
        } else if (cr > -1) {
            const int previous = g.nextShownRow(cr, -1);
            select(previous, g.columnCount() - 1, previous, g.columnCount() - 1);
        }
        return true;
    }
    if (key == u"enter" || key == u"f2") {
        if (ctrl)
            return false;
        editCell(row, cr, cc, -1, cx);
        return true;
    }
    if (key == u"escape" && !ctrl && !shift) {
        // The selection ends, the table is selected (the page keeps the keys: the block's).
        clearCellSelection(cx);
        ctl->setSelectedRow(row);
        return true;
    }
    if ((key == u"delete" || key == u"backspace") && !shift && !ctrl) {
        clearCells(cx);
        return true;
    }
    if (ctrl) {
        if (!shift && (key == u"c" || key == u"insert")) {
            copyCells(false, cx);
            return true;
        }
        if (!shift && key == u"x") {
            copyCells(true, cx);
            return true;
        }
        if (!shift && key == u"v") {
            pasteCells(cx);
            return true;
        }
        if (!shift && key == u"a") {
            select(-1, 0, g.nextShownRow(g.rowCount(), -1), g.columnCount() - 1);
            return true;
        }
        if (key == u"z" || key == u"y") {
            // The page's undo with the selection kept (the step may select the table or move the
            // focus: the cells stay selected unless a block is edited after it).
            const bool redo = key == u"y" || shift;
            const CellSelectionState kept = m_cellSel;
            m_keepCellSelection = true;
            if (redo)
                ctl->redo();
            else
                ctl->undo();
            m_keepCellSelection = false;
            if (ctl->editingRow() >= 0 || m_cellUid)
                return true;
            m_cellSel = kept;
            if (ctl->selectedRow() >= 0)
                ctl->setSelectedRow(-1);
            cx.notify();
            return true;
        }
        return false;
    }
    if (shift && key == u"insert") {
        pasteCells(cx);
        return true;
    }
    if (shift && key == u"delete") {
        copyCells(true, cx);
        return true;
    }
    // A character typed over the cell: the cell is edited with that text in place of its own.
    if (!k.keyChar.empty() && k.keyChar.front() >= u' ' && k.keyChar.front() != 0x7f && !ctl->readOnly()) {
        editCellWith(row, cr, cc, QString(reinterpret_cast<const QChar *>(k.keyChar.data()), qsizetype(k.keyChar.size())), cx);
        return true;
    }
    return false;
}

} // namespace brulion::editor::atr
