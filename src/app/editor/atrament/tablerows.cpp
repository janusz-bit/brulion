// PageView's tables (docs/ATRAMENT.md "The block editor in Atrament (M2)", step 5 "Blocks with
// views", tables): a table block drawn as TableBlockView draws it and its cells edited in place.
// The QML counterpart is qml/editor/TableBlockView.qml; what it does that is not here is listed
// under step 5's "Not yet".
//
// Drawing: the `tableData` role (tablelayout.h) — the header on the sunken band, a 1 px frame
// (the focus ring's colour at half while a cell is edited), lines between the rows (a stronger one
// under the header) and the columns, formula cells tinted; each cell's text is its rendering
// (`html`, the InlineRenderer's) read into RenderedText and drawn with renderedSpans() — links,
// chips, tags clickable through Text::link —, aligned as its column says (formulas right without
// one), in the header's weight for the header, on one line ending with "…" when it is longer (a
// header: its lines). The
// columns get TableBlockView's natural widths (tableColumnWidths(), measured by Atrament's text
// layout) and share what the page has beyond them in proportion to them (flex-grow), as the QML
// table stretches them; a table wider than the page scrolls sideways inside its row.
//
// Editing: a click on a cell edits it (editCell()): a TextInput in the cell over the cell's text
// (a formula as written), the controller editing no block meanwhile (TableBlockView.startCell);
// each change goes to PageController::setTableCell(…, typing) — one undo step per cell, nothing
// waits for the cell to be left. The keys (tableKey(), TableBlockView's cell field): Enter the
// cell below (a new row after the last), Tab / Shift+Tab the next / previous cell (a new row after
// the last cell), ↑/↓ the cell above / below and past the header or the last row the block there,
// ←/→ at the text's edge the cell beside, Ctrl+Enter a new row below, Alt+↑/↓ / Alt+←/→ move the
// row / the column with the cell, Alt+Enter the link at the caret, Ctrl+V of several cells pastes
// them from this one, Esc cancels the cell's typing and selects the table, Ctrl+Z / Ctrl+Y the
// page's undo with the cell edited on. ↑/↓ from the block above or below edit the header's or the
// last row's cell under the caret (the controller's editing row becoming the table:
// tableEditingRowChanged()).
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "editstyles.h"
#include "pagecontroller.h"
#include "tablelayout.h"
#include "text/textsystem.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

QString toQString(std::u16string_view s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

bool plain(const at::Keystroke &k, std::u16string_view name)
{
    return k.key == name && !k.modifiers.any();
}

QString tableText(const char *source)
{
    // (TableBlockView's own texts: the same translations.)
    return QCoreApplication::translate("TableBlockView", source);
}

} // namespace

// The table of a row as last read: the role's map, its grid, the columns' natural widths and each
// cell's rendering.
struct PageView::TableEntry {
    QVariantMap data;
    TableGrid grid;
    std::vector<float> widths;
    std::vector<markdown::RenderedText> header;
    std::vector<std::vector<markdown::RenderedText>> rows;
    std::uint64_t frame = 0;
    const markdown::RenderedText *rendered(int r, int c) const
    {
        if (c < 0 || c >= grid.columnCount())
            return nullptr;
        if (r == -1)
            return &header[std::size_t(c)];
        if (r < 0 || r >= grid.rowCount())
            return nullptr;
        return &rows[std::size_t(r)][std::size_t(c)];
    }
};

std::size_t PageView::CellKeyHash::operator()(const CellKey &k) const noexcept
{
    return std::hash<quint64>()(k.uid) ^ (std::size_t(k.r + 2) * 0x9e3779b97f4a7c15ull) ^ (std::size_t(k.c) << 20);
}

PageView::Connections::~Connections()
{
    for (const QMetaObject::Connection &c : list)
        QObject::disconnect(c);
}

void PageView::setDrawsTables(bool on, const TableLook &look, at::Context<PageView> &cx)
{
    m_app = &cx.app();
    if (!on && m_cellUid)
        stopEditingCell(cx);
    m_drawsTables = on;
    m_tableLook = look;
    m_tables.clear(); // (the columns' widths come from the look's font)
    m_extents.markAllEstimated();
    cx.notify();
}

bool PageView::isTable(int row) const
{
    if (!m_drawsTables || !m_model || row < 0 || row >= m_model->rowCount())
        return false;
    const QModelIndex index = m_model->index(row, 0);
    if (index.data(BlockListModel::ViewRole).toString() != u"table")
        return false;
    // A table whose filter row is shown stays the QML view's (its fields are TableBlockView's); a
    // table the calculator cannot read too.
    const QVariantMap data = index.data(BlockListModel::TableDataRole).toMap();
    return !data.value(QStringLiteral("header")).toStringList().isEmpty()
           && !data.value(QStringLiteral("filtering")).toBool();
}

const PageView::TableEntry *PageView::tableEntry(int row) const
{
    if (!m_model || !m_app || row < 0 || row >= m_model->rowCount())
        return nullptr;
    const quint64 uid = m_model->uidAt(row);
    const QVariantMap data = m_model->index(row, 0).data(BlockListModel::TableDataRole).toMap();
    std::shared_ptr<TableEntry> &slot = m_tables[uid];
    if (slot && (slot->data.isSharedWith(data) || slot->data == data)) {
        slot->frame = m_frame;
        return slot.get();
    }
    auto e = std::make_shared<TableEntry>();
    e->data = data;
    e->grid = readTableGrid(data);
    at::TextSystem &ts = m_app->textSystem();
    const TableLook &look = m_tableLook;
    const at::FaceId cellFace = ts.fonts().match(look.fontFamily, at::FontWeight{at::FontWeight::Normal});
    const at::FaceId headerFace = ts.fonts().match(look.fontFamily, at::FontWeight{at::FontWeight::SemiBold});
    e->widths = tableColumnWidths(
            e->grid,
            [&](QStringView text, bool header) {
                at::FontRun run;
                run.length = int(text.size());
                run.face = header ? headerFace : cellFace;
                run.fontSize = look.fontSize;
                const std::u16string_view s(reinterpret_cast<const char16_t *>(text.utf16()), std::size_t(text.size()));
                return ts.layout(s, std::span<const at::FontRun>(&run, 1), at::ParagraphStyle{})->size().width;
            },
            look.cellPadding);
    // Each cell's rendering as the QML table shows it (its `html`).
    e->header.reserve(e->grid.header.size());
    for (const TableGridCell &h : e->grid.header)
        e->header.push_back(markdown::renderedTextFromHtml(h.html));
    e->rows.reserve(e->grid.rows.size());
    for (const std::vector<TableGridCell> &cells : e->grid.rows) {
        std::vector<markdown::RenderedText> out;
        out.reserve(cells.size());
        for (const TableGridCell &cell : cells)
            out.push_back(markdown::renderedTextFromHtml(cell.html));
        e->rows.push_back(std::move(out));
    }
    e->frame = m_frame;
    slot = std::move(e);
    return slot.get();
}

PageView::TableCell PageView::editedCell() const
{
    if (!m_cellUid || !m_model)
        return {};
    const int row = m_model->rowOfUid(m_cellUid);
    if (row < 0)
        return {};
    return {row, m_cellRow, m_cellColumn};
}

std::optional<at::Rect> PageView::cellBounds(int row, int tableRow, int column) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return std::nullopt;
    const auto it = m_cellFrames.find(CellKey{m_model->uidAt(row), tableRow, column});
    if (it == m_cellFrames.end() || it->second.frame != m_frame)
        return std::nullopt;
    return it->second.bounds;
}

const at::TextLayoutHandle *PageView::cellText(int row, int tableRow, int column) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return nullptr;
    const auto it = m_cellFrames.find(CellKey{m_model->uidAt(row), tableRow, column});
    if (it == m_cellFrames.end() || it->second.frame != m_frame || !it->second.text.layout())
        return nullptr;
    return &it->second.text;
}

int PageView::cellCursorAt(int row, int tableRow, int column, at::Point p) const
{
    const at::TextLayoutHandle *t = cellText(row, tableRow, column);
    const TableEntry *e = t ? tableEntry(row) : nullptr;
    const TableGridCell *cell = e ? e->grid.cell(tableRow, column) : nullptr;
    if (!cell)
        return -1;
    const std::optional<int> display = t->offsetAt(p);
    // A formula is edited as written, not as its result; a cell of several lines (`<br>`) shows
    // them otherwise: the caret at the end.
    if (!display || cell->formula || !cell->error.isEmpty())
        return -1;
    if (cell->text.trimmed().isEmpty())
        return int(cell->text.size());
    // The renderer's display ↔ source map of the cell's text, when it shows what was drawn.
    const markdown::RenderedText mapped = m_model->tableCellText(row, cell->text);
    const std::u16string_view drawn = t->layout()->text();
    if (u16View(mapped.text) != drawn)
        return -1;
    const qsizetype source = mapped.source.isEmpty() ? *display : mapped.sourceAt(*display);
    if (source < 0)
        return -1;
    return std::clamp(int(source), 0, int(cell->text.size()));
}

int PageView::columnAtX(quint64 uid, int columns, float x) const
{
    // (The header's cells: every column has one, shown in every frame the table is.)
    int nearest = 0;
    float best = std::numeric_limits<float>::max();
    for (int c = 0; c < columns; ++c) {
        const auto it = m_cellFrames.find(CellKey{uid, -1, c});
        if (it == m_cellFrames.end())
            continue;
        const at::Rect b = it->second.bounds;
        if (x >= b.left() && x < b.right())
            return c;
        const float d = std::min(std::abs(x - b.left()), std::abs(x - b.right()));
        if (d < best) {
            best = d;
            nearest = c;
        }
    }
    return nearest;
}

// --- drawing --------------------------------------------------------------------------------------

at::AnyElement PageView::renderTableRow(std::size_t row, const QModelIndex &index, float textX, at::Div line)
{
    m_drawn.push_back(row);
    // The cells of tables not drawn for a while are forgotten.
    if (m_tablesPurged != m_frame) {
        m_tablesPurged = m_frame;
        std::erase_if(m_cellFrames, [this](const auto &kv) { return kv.second.frame + 2 < m_frame; });
        std::erase_if(m_tables, [this](const auto &kv) { return !kv.second || kv.second->frame + 2 < m_frame; });
    }
    const TableEntry *entry = tableEntry(int(row));
    if (!entry)
        return std::move(line).h(1);
    const TableGrid &grid = entry->grid;
    const TableLook &look = m_tableLook;
    const quint64 uid = m_model->uidAt(int(row)); // (tableEntry()'s key)
    const bool editing = m_cellInput && m_cellUid == uid;
    const bool editable = m_controller && !m_controller->readOnly();
    float total = 0;
    for (const float w : entry->widths)
        total += w;

    // The grid: the header, then the rows shown (those the filters pass, the edited one).
    at::Div rows = at::div()
                           .id(u"grid")
                           .relative()
                           .flexCol()
                           .flexGrow(1)
                           .flexShrink0()
                           .flexBasis(at::Length::px(total))
                           .bg(look.background)
                           .rounded(look.radius)
                           .role(at::Role::Table)
                           .ariaLabel(u16View(tableText("Table")));
    {
        at::Div header = at::div().id(u"header").flexRow().bg(look.headerBackground).roundedT(look.radius).role(at::Role::Row);
        for (int c = 0; c < grid.columnCount(); ++c)
            std::move(header).child(renderTableCell(row, uid, grid, -1, c, false));
        std::move(rows).child(std::move(header));
    }
    bool first = true;
    for (const int r : grid.shownRows(editing ? m_cellRow : -2)) {
        at::Div tr = at::div().id(at::ElementId::number(std::uint64_t(r) + 1)).flexRow().role(at::Role::Row);
        for (int c = 0; c < grid.columnCount(); ++c)
            std::move(tr).child(renderTableCell(row, uid, grid, r, c, first));
        std::move(rows).child(std::move(tr));
        first = false;
    }
    // The frame over the cells (TableBlockView's grid border).
    std::move(rows).child(at::div()
                                  .absolute()
                                  .inset(0)
                                  .border(1)
                                  .borderColor(editing ? look.focusRing.fadedBy(0.5f) : look.border)
                                  .rounded(look.radius)
                                  .ariaHidden());

    // Wider than the page: scrolled sideways (TableBlockView's Flickable).
    at::Div column = at::div()
                             .ml(textX)
                             .flex1()
                             .minW(0)
                             .flexCol()
                             .fontFamily(look.fontFamily)
                             .textSize(look.fontSize)
                             .lineHeight(look.lineHeight)
                             .child(at::div().id(u"table").flexRow().overflowXScroll().child(std::move(rows)));
    // "+ Row" / "+ Column" while a cell is edited or the table is selected, always on touch UIs.
    if (editable && (editing || index.data(BlockListModel::IsSelectedRole).toBool() || m_metrics.touch)) {
        const at::EntityId self = m_selfId;
        auto button = [&](std::u16string_view id, const QString &label, bool addColumn) {
            const at::Color ink = m_palette.text;
            return at::div()
                    .id(id)
                    .flexRow()
                    .itemsCenter()
                    .gap(5)
                    .px(8)
                    .h(look.buttonHeight)
                    .rounded(look.radius)
                    .cursorPointer()
                    .textSize(look.buttonTextSize)
                    .lineHeight(std::round(look.buttonTextSize * 1.3f))
                    .fontMedium()
                    .role(at::Role::Button)
                    .hover([hover = look.hover](at::Style &s) { s.background = at::Background::solid(hover); })
                    .onClick([self, row, addColumn](const at::ClickEvent &, at::Window &, at::App &a) {
                        a.updateEntity<PageView>(self, [&](PageView &v, at::Context<PageView> &) {
                            if (!v.m_controller)
                                return;
                            if (addColumn)
                                v.m_controller->addTableColumn(int(row));
                            else
                                v.m_controller->addTableRow(int(row));
                        });
                    })
                    // The plus (Atrament has no icons yet: two bars).
                    .child(at::canvas([ink](at::Rect b, at::Window &w, at::App &) {
                               const at::Point c = b.center();
                               const float arm = 5, t = 1.5f;
                               w.paintQuad(at::Rect::fromXYWH(c.x - arm, c.y - t / 2, 2 * arm, t), at::Background::solid(ink),
                                           {}, at::transparent(), at::Corners::all(t / 2));
                               w.paintQuad(at::Rect::fromXYWH(c.x - t / 2, c.y - arm, t, 2 * arm), at::Background::solid(ink),
                                           {}, at::transparent(), at::Corners::all(t / 2));
                           })
                                   .size(12)
                                   .flexShrink0())
                    .child(at::text(u16View(label)));
        };
        std::move(column).child(at::div()
                                        .flexRow()
                                        .gap(4)
                                        .mt(4)
                                        .child(button(u"addRow", tableText("Row"), false))
                                        .child(button(u"addColumn", tableText("Column"), true)));
    }
    if (editing) {
        const at::EntityId self = m_selfId;
        std::move(line).captureKeyDown([self](const at::KeyDownEvent &e, at::Window &w, at::App &a) {
            bool handled = false;
            a.updateEntity<PageView>(self, [&](PageView &v, at::Context<PageView> &c) { handled = v.tableKey(e.keystroke, w, c); });
            if (handled)
                w.stopPropagation();
        });
    }
    return std::move(line).child(std::move(column));
}

at::AnyElement PageView::renderTableCell(std::size_t row, quint64 uid, const TableGrid &grid, int r, int c,
                                         bool firstBodyRow)
{
    const TableLook &look = m_tableLook;
    const TableEntry *entry = m_tables.at(uid).get(); // (renderTableRow's tableEntry())
    const TableGridCell &cell = *grid.cell(r, c);
    const markdown::RenderedText &rendered = *entry->rendered(r, c);
    const bool header = r == -1;
    const bool edited = m_cellInput && m_cellUid == uid && m_cellRow == r && m_cellColumn == c;
    const bool editable = m_controller && !m_controller->readOnly();
    const float width = c < int(entry->widths.size()) ? entry->widths[std::size_t(c)] : 56;
    const float vpad = std::max(0.0f, (look.rowHeight - look.lineHeight) / 2);
    const CellKey key{uid, r, c};
    CellFrame &frame = m_cellFrames[key];
    frame.frame = m_frame;
    PageView *self = this;
    const at::EntityId selfId = m_selfId;

    // A column's share: its natural width and, beyond the widths of all, a part of the room left in
    // proportion to it (TableBlockView's stretch); never narrower (a wider table scrolls).
    at::Div d = at::div()
                        .id(at::ElementId::number(std::uint64_t(c)))
                        .relative()
                        .flexBasis(at::Length::px(width))
                        .flexGrow(width)
                        .flexShrink0()
                        .minW0()
                        .overflowHidden()
                        .flexRow()
                        .itemsCenter()
                        .px(look.cellPadding)
                        .py(vpad)
                        .minH(look.rowHeight)
                        .role(header ? at::Role::ColumnHeader : at::Role::Cell);
    if (!header && c < grid.columnCount())
        // (A screen reader says which column a value is in.)
        std::move(d).ariaDescription(u16View(grid.header[std::size_t(c)].display));
    // Under the text: the edited cell's field, a formula's tint, the lines to the row above and the
    // column on the left (TableBlockView's Rectangles); where the cell is, for clicks and ↑/↓.
    const at::Color top = firstBodyRow ? look.borderStrong : look.borderSubtle;
    const bool formulaTint = cell.formula && !edited;
    std::move(d).child(
            at::canvas([self, key, top, header, c, edited, formulaTint](at::Rect b, at::Window &w, at::App &) {
                const TableLook &look = self->m_tableLook;
                self->m_cellFrames[key].bounds = b;
                if (edited)
                    w.paintQuad(b, at::Background::solid(look.input), at::Edges::all(2), look.focusRing, at::Corners::all(2));
                else if (formulaTint)
                    w.paintQuad(b.inset(at::Edges::all(1)), at::Background::solid(look.formula), {}, at::transparent(), {});
                if (!header)
                    w.paintQuad(at::Rect::fromXYWH(b.left(), b.top(), b.width(), 1), at::Background::solid(top), {},
                                at::transparent(), {});
                if (c > 0)
                    w.paintQuad(at::Rect::fromXYWH(b.left(), b.top(), 1, b.height()),
                                at::Background::solid(look.borderSubtle), {}, at::transparent(), {});
            })
                    .absolute()
                    .inset(0));
    // Its text, as wide as the cell (a column: the text stretched across it), aligned as its
    // column says.
    at::Div body = at::div().flex1().minW(0).flexCol();
    if (header)
        std::move(d).fontSemibold();
    else if (cell.formula && !edited)
        std::move(d).fontMedium();
    if (edited) {
        // The field over the cell's text (a formula as written, in the code face), from the left
        // as TableBlockView's TextField.
        if (cell.formula)
            std::move(d).fontFamily(look.monoFamily);
        frame.text = at::TextLayoutHandle{};
        std::move(body).child(*m_cellInput);
        return std::move(d).child(std::move(body));
    }
    // One line (a header: its `<br>` lines), a longer text ending with "…" (TableBlockView's elide).
    std::move(body).lineClamp(header ? grid.headerLines : 1);
    switch (shownAlign(grid, r, c)) {
    case TableAlign::Right: std::move(body).textRight(); break;
    case TableAlign::Center: std::move(body).textCenter(); break;
    default: break;
    }
    if (!cell.error.isEmpty())
        std::move(d).textColor(look.error);
    std::vector<at::TextSpan> spans = renderedSpans(rendered, m_palette);
    appendFindSpans(rendered.text, spans);
    at::Text text = at::text(u16View(rendered.text)).id(u"text").spans(spans).trackLayout(frame.text);
    // Each link a hit region (adjacent runs of one href are one link), as in a block's text.
    for (qsizetype i = 0; i < rendered.runs.size();) {
        const QString &href = rendered.runs[i].href;
        qsizetype j = i + 1;
        while (j < rendered.runs.size() && rendered.runs[j].href == href && rendered.runs[j].from == rendered.runs[j - 1].to)
            ++j;
        if (!href.isEmpty())
            std::move(text).link(at::Range{int(rendered.runs[i].from), int(rendered.runs[j - 1].to)},
                                 [selfId, row, r, c, href](const at::ClickEvent &click, at::Window &, at::App &a) {
                                     a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &cx) {
                                         v.cellLinkClicked(row, r, c, href, click.up.position, cx);
                                     });
                                 });
        i = j;
    }
    std::move(body).child(std::move(text));
    std::move(d).child(std::move(body));
    // A right-click: the block menu with the cell's Row and Column (also in reading mode: it copies).
    std::move(d).onMouseDown(at::MouseButton::Right,
                             [selfId, row, r, c](const at::MouseDownEvent &e, at::Window &w, at::App &a) {
                                 a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &) {
                                     if (v.onCellMenu)
                                         v.onCellMenu(int(row), r, c, e.position);
                                     else if (v.onBlockMenu)
                                         v.onBlockMenu(int(row), e.position);
                                 });
                                 w.stopPropagation();
                             });
    if (editable)
        std::move(d).cursorText().onClick([selfId, row, r, c](const at::ClickEvent &click, at::Window &, at::App &a) {
            a.updateEntity<PageView>(selfId,
                                     [&](PageView &v, at::Context<PageView> &cx) { v.cellClicked(row, r, c, click, cx); });
        });
    return d;
}

// --- editing --------------------------------------------------------------------------------------

void PageView::connectTables(at::Context<PageView> &cx)
{
    for (const QMetaObject::Connection &c : m_tableConnections.list)
        QObject::disconnect(c);
    m_tableConnections.list.clear();
    m_cellUid = 0;
    PageController *controller = m_controller.data();
    if (!controller)
        return;
    at::App *app = &cx.app();
    const at::EntityId self = cx.entityId();
    // (Made before the view's own connection: this one sees the block edited before.)
    m_tableConnections.list.push_back(QObject::connect(controller, &PageController::editingRowChanged, controller, [app, self] {
        app->updateEntity<PageView>(self, [](PageView &v, at::Context<PageView> &c) { v.tableEditingRowChanged(c); });
    }));
    // Blocks selected around the table (Shift+click, the QML list's keys): the cell's editing ends.
    m_tableConnections.list.push_back(QObject::connect(controller, &PageController::selectionChanged, controller, [app, self] {
        app->updateEntity<PageView>(self, [](PageView &v, at::Context<PageView> &c) {
            if (v.m_cellUid && v.m_controller && v.m_controller->selectionCount() > 0)
                v.stopEditingCell(c);
        });
    }));
}

void PageView::tableEditingRowChanged(at::Context<PageView> &cx)
{
    PageController *ctl = m_controller.data();
    const int row = ctl ? ctl->editingRow() : -1;
    if (row < 0)
        return;
    // Another block edited: the cell's editing ends.
    if (m_cellUid)
        stopEditingCell(cx);
    if (!isTable(row) || ctl->readOnly())
        return;
    // The controller edits a table Atrament draws (↑/↓ from the block above or below, the QML
    // around): a cell is edited instead — the header's from above, the last row's from below, in
    // the column under the caret.
    at::App &app = cx.app();
    std::optional<float> x;
    bool fromBelow = false;
    if (m_tableNavX) {
        x = m_tableNavX;
        fromBelow = m_tableNavUp;
        m_tableNavX.reset();
    } else {
        const int previous = m_editUid ? m_model->rowOfUid(m_editUid) : -1;
        if (previous >= 0) {
            fromBelow = previous > row;
            const at::TextInput &input = m_input->read(app);
            if (input.layout() && input.bounds())
                x = input.bounds()->left() + input.layout()->caretX(input.selection().focus);
        } else {
            // (Asked with the caret at the end: from below, as editorKey's ↑ asks.)
            const int length = int(m_model->editTextAt(row).size());
            fromBelow = length > 0 && ctl->cursorPosition() >= length;
        }
    }
    const TableEntry *e = tableEntry(row);
    if (!e)
        return;
    const std::vector<int> shown = e->grid.shownRows(-2);
    const int r = fromBelow && !shown.empty() ? shown.back() : -1;
    const quint64 uid = m_model->uidAt(row);
    const int c = x ? columnAtX(uid, e->grid.columnCount(), *x) : 0;
    int cursor = -1;
    if (x) {
        if (const at::TextLayoutHandle *t = cellText(row, r, c); t && !t->layout()->lines().empty()) {
            const at::TextLine &l = fromBelow ? t->layout()->lines().back() : t->layout()->lines().front();
            cursor = cellCursorAt(row, r, c, at::Point{*x, t->bounds().top() + l.top + l.height / 2});
        }
    }
    // (Nested: this connection and the view's see no row edited.)
    ctl->stopEditing();
    editCell(row, r, c, cursor, cx);
}

bool PageView::editCell(int row, int tableRow, int column, int cursor, at::Context<PageView> &cx)
{
    if (!m_controller || m_controller->readOnly() || !isTable(row))
        return false;
    m_app = &cx.app();
    // (The cell left first: its last word may change the table.)
    commitCellComposition(cx);
    const TableEntry *e = tableEntry(row);
    const TableGridCell *cell = e ? e->grid.cell(tableRow, column) : nullptr;
    if (!cell)
        return false;
    const QString text = cell->text;
    const QString shown = cell->display;
    const bool formula = cell->formula;
    const quint64 uid = m_model->uidAt(row);
    // No block is edited or selected meanwhile (TableBlockView.startCell).
    m_cellUid = 0;
    m_controller->setSelectedRow(-1);
    m_controller->stopEditing();
    if (!m_cellInput) {
        at::TextInputOptions options;
        options.multiline = true; // (wrapped in its column, as the cell's text; its line breaks are the keys')
        options.caretColor = m_editCaret;
        options.selectionColor = m_editSelection;
        m_cellInput = cx.newEntity<at::TextInput>(std::move(options));
        m_cellSubscriptions.push_back(cx.subscribe<at::TextInput::Changed>(
                *m_cellInput,
                [](PageView &v, const at::TextInput::Changed &, at::Context<PageView> &c) { v.cellInputChanged(c); }));
    }
    m_cellUid = uid;
    m_cellRow = tableRow;
    m_cellColumn = column;
    m_cellLastText = text;
    // The highlighter's Inline rules over a cell's Markdown (a formula: plain, in the code face).
    at::TextInput::Styler styler;
    if (!formula)
        styler = m_styles ? m_styles->styler(MarkdownMode::Inline) : m_editStyler;
    const std::u16string value(u16View(text));
    const QString label = tableText("Cell %1").arg(shown);
    m_cellInput->update(cx.app(), [&](at::TextInput &t, at::Context<at::TextInput> &c) {
        t.setColors(m_editCaret, m_editSelection, c);
        t.setLabel(u16View(label), c);
        t.setStyler(styler, c);
        if (t.text() != value)
            t.setText(value, c);
        const int caret = cursor < 0 ? int(value.size()) : std::clamp(cursor, 0, int(value.size()));
        t.setSelection(caret, caret, c);
        // The page's undo stack is the controller's.
        t.clearUndo();
    });
    focusCellInput(cx);
    // A table not on screen is scrolled to (the field then keeps its caret in view itself).
    if (std::find(m_drawn.begin(), m_drawn.end(), std::size_t(row)) == m_drawn.end())
        m_handle.scrollToRow(std::size_t(row), at::ListPosition::Nearest);
    cx.notify();
    if (onEditingStarted)
        onEditingStarted();
    return true;
}

void PageView::focusCellInput(at::Context<PageView> &cx)
{
    if (!m_cellInput)
        return;
    for (const std::unique_ptr<at::Window> &w : cx.app().windows()) {
        if (w->drewEntity(cx.entityId()))
            m_cellInput->read(cx.app()).focusHandle().focus(*w);
    }
}

void PageView::stopEditingCell(at::Context<PageView> &cx)
{
    if (!m_cellUid)
        return;
    commitCellComposition(cx);
    m_cellUid = 0;
    m_cellRow = -1;
    m_cellColumn = -1;
    // The field is gone: the keyboard with it.
    if (m_cellInput) {
        for (const std::unique_ptr<at::Window> &w : cx.app().windows()) {
            if (m_cellInput->read(cx.app()).focusHandle().isFocused(*w))
                w->blur();
        }
    }
    cx.notify();
}

void PageView::commitCellComposition(at::Context<PageView> &cx)
{
    if (!m_cellInput || !m_cellInput->read(cx.app()).markedRange())
        return;
    m_cellInput->update(cx.app(), [](at::TextInput &t, at::Context<at::TextInput> &c) { t.unmark(c); });
    cellInputChanged(cx);
}

void PageView::cellInputChanged(at::Context<PageView> &cx)
{
    const TableCell cell = editedCell();
    if (!m_cellInput || cell.row < 0 || !m_controller)
        return;
    const at::TextInput &input = m_cellInput->read(cx.app());
    // A word being composed is the field's until it is committed (as a block's).
    if (input.markedRange())
        return;
    std::u16string text = input.text();
    const QString before = m_cellLastText;
    if (text.find(u'\n') != std::u16string::npos || text.find(u'\r') != std::u16string::npos) {
        // A cell is one line: an input method's Enter (Android commits "\n") is Enter's, a pasted
        // line break a space.
        const std::u16string_view old = u16View(before);
        if (text.size() == old.size() + 1 && text.find(u'\n') != std::u16string::npos) {
            std::u16string without = text;
            without.erase(without.find(u'\n'), 1);
            if (without == old) {
                m_cellInput->update(cx.app(), [&](at::TextInput &t, at::Context<at::TextInput> &c) {
                    const int caret = int(t.selection().focus) - 1;
                    t.setText(without, c);
                    t.setSelection(caret, caret, c);
                    t.clearUndo();
                });
                const TableEntry *e = tableEntry(cell.row);
                if (e)
                    goToCell(cell.row, e->grid.nextShownRow(cell.tableRow, 1), cell.column, -1, cx);
                return;
            }
        }
        std::replace(text.begin(), text.end(), u'\n', u' ');
        std::replace(text.begin(), text.end(), u'\r', u' ');
        m_cellInput->update(cx.app(), [&](at::TextInput &t, at::Context<at::TextInput> &c) {
            const int caret = t.selection().focus;
            t.setText(text, c);
            t.setSelection(caret, caret, c);
        });
    }
    const QString value = toQString(text);
    if (value == m_cellLastText)
        return;
    m_cellLastText = value;
    m_controller->setTableCell(cell.row, cell.tableRow, cell.column, value, true);
}

void PageView::goToCell(int row, int tableRow, int column, int cursor, at::Context<PageView> &cx)
{
    commitCellComposition(cx);
    const TableEntry *e = tableEntry(row);
    if (!e || !m_controller)
        return;
    // Past the last row: a new one (TableBlockView's leave()).
    if (tableRow >= e->grid.rowCount())
        m_controller->addTableRow(row);
    editCell(row, tableRow, column, cursor, cx);
}

void PageView::leaveTable(int row, bool up, float x, at::Context<PageView> &cx)
{
    PageController *ctl = m_controller.data();
    const int target = up ? ctl->previousVisibleRow(row) : ctl->nextVisibleRow(row);
    if (target < 0)
        return; // the page's first / last block: the cell stays edited
    stopEditingCell(cx);
    if (isTable(target)) {
        m_tableNavX = x;
        m_tableNavUp = up;
    }
    int cursor = -1;
    if (editsRow(target))
        cursor = editCursorOnLine(target, x, up);
    if (cursor < 0)
        cursor = up ? -1 : 0;
    ctl->startEditing(target, cursor);
    m_tableNavX.reset();
    cx.notify();
}

void PageView::cellClicked(std::size_t row, int tableRow, int column, const at::ClickEvent &click,
                           at::Context<PageView> &cx)
{
    if (!m_controller || m_controller->readOnly() || click.isKeyboard)
        return;
    // The release of a press that selected blocks by dragging edits nothing.
    if (m_dragSelected) {
        m_dragSelected = false;
        return;
    }
    // (The edited cell's field places its caret itself.)
    if (m_cellUid && m_cellUid == m_model->uidAt(int(row)) && m_cellRow == tableRow && m_cellColumn == column)
        return;
    const int cursor = click.fromAccessibility ? -1 : cellCursorAt(int(row), tableRow, column, click.up.position);
    if (onCellEditRequest && onCellEditRequest(int(row), tableRow, column, cursor, click.up.modifiers))
        return;
    editCell(int(row), tableRow, column, cursor, cx);
}

void PageView::cellLinkClicked(std::size_t row, int tableRow, int column, const QString &href, at::Point at,
                               at::Context<PageView> &)
{
    if (onCellLink)
        onCellLink(int(row), tableRow, column, href, at);
    else if (onLink)
        onLink(int(row), href);
}

bool PageView::tableKey(const at::Keystroke &k, at::Window &, at::Context<PageView> &cx)
{
    const TableCell cell = editedCell();
    if (!m_cellInput || !m_controller)
        return false;
    if (cell.row < 0) {
        stopEditingCell(cx); // (its table is gone)
        return false;
    }
    PageController *ctl = m_controller.data();
    const TableEntry *e = tableEntry(cell.row);
    if (!e)
        return false;
    const TableGrid &grid = e->grid;
    const int row = cell.row;
    const int r = cell.tableRow;
    const int c = cell.column;
    const at::Modifiers &m = k.modifiers;
    const bool ctrl = m.control || m.platform;
    at::App &app = cx.app();
    const at::TextInput &input = m_cellInput->read(app);
    const at::InputHandler::Selection selection = input.selection();
    const int caret = selection.focus;
    const int length = int(input.text().size());
    const bool composing = input.markedRange().has_value();
    const at::TextLayout *layout = input.layout();
    auto lineOf = [layout](int offset) { return layout ? layout->lineIndex(offset) : 0; };
    auto lineCount = [layout] { return layout ? int(layout->lines().size()) : 1; };

    if (ctrl && !m.alt) {
        if (k.key == u"z" || k.key == u"y") {
            // The page's undo, the cell edited on with the text the step left in it (the touch
            // toolbar's undoInCell); a step that took the caret to a block leaves the table.
            commitCellComposition(cx);
            const bool redo = k.key == u"y" || m.shift;
            const quint64 uid = m_cellUid;
            stopEditingCell(cx);
            if (onUndo)
                onUndo(redo);
            else if (redo)
                ctl->redo();
            else
                ctl->undo();
            if (ctl->editingRow() >= 0)
                return true;
            const int now = m_model->rowOfUid(uid);
            if (now >= 0 && editCell(now, r, c, -1, cx))
                return true;
            if (now >= 0)
                ctl->setSelectedRow(now);
            if (onEscape)
                onEscape();
            return true;
        }
        if (k.key == u"enter" && !m.shift) {
            // A new row below, its cell edited (the header: the first row).
            commitCellComposition(cx);
            ctl->insertTableRow(row, r + 1);
            editCell(row, r + 1, c, -1, cx);
            return true;
        }
        if (k.key == u"v" && !m.shift && ctl->clipboardCellCount() >= 2) {
            // Several cells: pasted from this one down and right (TableBlockView's pasteCells()).
            commitCellComposition(cx);
            const QList<int> rows = grid.rowsFrom(r);
            stopEditingCell(cx);
            ctl->pasteClipboardCells(row, rows, c, 1, 1);
            editCell(row, r, c, -1, cx);
            return true;
        }
        return false;
    }
    if (m.alt && !ctrl) {
        if ((k.key == u"up" || k.key == u"down") && !m.shift) {
            // The row moves, the cell with it.
            const int to = k.key == u"up" ? r - 1 : r + 1;
            if (r >= 0 && to >= 0 && to < grid.rowCount()) {
                commitCellComposition(cx);
                ctl->moveTableRow(row, r, to);
                editCell(row, to, c, -1, cx);
            }
            return true;
        }
        if ((k.key == u"left" || k.key == u"right") && !m.shift) {
            const int to = k.key == u"left" ? c - 1 : c + 1;
            if (to >= 0 && to < grid.columnCount()) {
                commitCellComposition(cx);
                ctl->moveTableColumn(row, c, to);
                editCell(row, r, to, -1, cx);
            }
            return true;
        }
        if (k.key == u"enter") {
            // The link at the caret.
            const QString url = ctl->linkInText(row, toQString(input.text()), caret);
            if (!url.isEmpty() && onLink)
                onLink(row, url);
            return true;
        }
        return false;
    }
    if (k.key == u"enter" && !m.control && !m.alt && !m.platform) {
        // The cell below (a new row after the last).
        goToCell(row, grid.nextShownRow(r, 1), c, -1, cx);
        return true;
    }
    if (k.key == u"tab" && !ctrl && !m.alt) {
        if (!m.shift) {
            if (c + 1 < grid.columnCount())
                goToCell(row, r, c + 1, -1, cx);
            else
                goToCell(row, grid.nextShownRow(r, 1), 0, -1, cx);
        } else if (c > 0) {
            goToCell(row, r, c - 1, -1, cx);
        } else if (r > -1) {
            goToCell(row, grid.nextShownRow(r, -1), grid.columnCount() - 1, -1, cx);
        }
        return true;
    }
    if (plain(k, u"escape")) {
        // The cell's typing undone (its one undo step), the table selected; the QML around takes
        // the keys (onEscape: BlockEditor.stopEditing).
        commitCellComposition(cx);
        const quint64 uid = m_cellUid;
        stopEditingCell(cx);
        ctl->cancelTableCell(row, r, c);
        const int now = m_model->rowOfUid(uid);
        if (now >= 0)
            ctl->setSelectedRow(now);
        if (onEscape)
            onEscape();
        return true;
    }
    if ((k.key == u"up" || k.key == u"down") && !m.any()) {
        const bool up = k.key == u"up";
        if (composing || (up ? lineOf(caret) > 0 : lineOf(caret) < lineCount() - 1))
            return false; // the field moves between its own lines
        const float x = (input.bounds() ? input.bounds()->left() : 0) + (layout ? layout->caretX(caret) : 0);
        const int to = grid.nextShownRow(r, up ? -1 : 1);
        if (up ? r == -1 : to >= grid.rowCount()) {
            leaveTable(row, up, x, cx);
            return true;
        }
        // The cell above / below at the caret's x.
        int cursor = caret;
        if (const at::TextLayoutHandle *t = cellText(row, to, c); t && !t->layout()->lines().empty()) {
            const at::TextLine &l = up ? t->layout()->lines().back() : t->layout()->lines().front();
            cursor = cellCursorAt(row, to, c, at::Point{x, t->bounds().top() + l.top + l.height / 2});
        }
        goToCell(row, to, c, cursor, cx);
        return true;
    }
    if ((k.key == u"left" || k.key == u"right") && !m.any()) {
        // At the text's edge (nothing selected): the cell beside, the caret at its near end.
        const bool left = k.key == u"left";
        if (composing || !selection.range().isEmpty() || (left ? caret > 0 : caret < length))
            return false;
        const int to = left ? c - 1 : c + 1;
        if (to >= 0 && to < grid.columnCount())
            goToCell(row, r, to, left ? -1 : 0, cx);
        return true;
    }
    return false;
}

} // namespace brulion::editor::atr
