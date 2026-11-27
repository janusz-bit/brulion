// PageView's filter row (docs/ATRAMENT.md "The block editor in Atrament (M2)", step 5, tables):
// TableBlockView's fields under the header while a table filters (PageController::
// setTableFilterOpen: the block menu's "Filter rows", "Filter by this column…") — one per column,
// as wide as it, "Filter" in it, named "Filter of column …" for screen readers; typing filters the
// rows (PageController::setTableFilter: the rows that do not match are hidden, nothing is
// written), Esc empties a field (BTextField's clearable) and in an empty one hides the row and
// selects the table, Enter leaves the field, the × empties it. A column with a filter has the
// funnel in its header (tablerows.cpp), and under the grid the line says how many rows show, with
// "Clear filters".
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"
#include "pageicons.h"
#include "tablelayout.h"
#include "tablerows_p.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

QString tableText(const char *source, int n = -1)
{
    // (TableBlockView's own texts: the same translations.)
    return QCoreApplication::translate("TableBlockView", source, nullptr, n);
}

QString toQString(std::u16string_view s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

} // namespace

std::shared_ptr<PageView::FilterField> PageView::filterField(quint64 uid, int column, const TableGrid &grid)
{
    std::shared_ptr<FilterField> &slot = m_filterFields[CellKey{uid, kFilterRow, column}];
    if (slot || !m_app)
        return slot;
    at::TextInputOptions options;
    options.placeholder = std::u16string(u16View(tableText("Filter")));
    const QString header = column < grid.columnCount() ? grid.header[std::size_t(column)].display : QString();
    options.label = std::u16string(u16View(tableText("Filter of column %1").arg(header)));
    options.text = column < int(grid.filters.size()) ? std::u16string(u16View(grid.filters[std::size_t(column)])) : std::u16string();
    options.caretColor = m_editCaret;
    options.selectionColor = m_editSelection;
    auto field = std::make_shared<FilterField>();
    field->input = m_app->newEntity<at::TextInput>(std::move(options));
    at::App *app = m_app;
    const at::EntityId self = m_selfId;
    field->changed = m_app->subscribe<at::TextInput::Changed>(
            field->input.id(), [app, self, uid, column](const at::TextInput::Changed &, at::App &) {
                app->updateEntity<PageView>(self, [&](PageView &v, at::Context<PageView> &) { v.filterChanged(uid, column); });
            });
    slot = field;
    return slot;
}

void PageView::filterChanged(quint64 uid, int column)
{
    const auto it = m_filterFields.find(CellKey{uid, kFilterRow, column});
    const int row = m_model && m_app ? m_model->rowOfUid(uid) : -1;
    if (it == m_filterFields.end() || !it->second || row < 0 || !m_controller)
        return;
    const at::TextInput &input = it->second->input.read(*m_app);
    // (A word being composed is the field's until it is committed.)
    if (input.markedRange())
        return;
    const QString text = toQString(input.text());
    const TableEntry *e = tableEntry(row);
    const QString now = e && column < int(e->grid.filters.size()) ? e->grid.filters[std::size_t(column)] : QString();
    if (text != now)
        m_controller->setTableFilter(row, column, text);
}

bool PageView::filterFocused(int row, int column) const
{
    if (!m_model || !m_app || row < 0 || row >= m_model->rowCount())
        return false;
    const auto it = m_filterFields.find(CellKey{m_model->uidAt(row), kFilterRow, column});
    if (it == m_filterFields.end() || !it->second)
        return false;
    const at::FocusHandle &focus = it->second->input.read(*m_app).focusHandle();
    return std::any_of(m_app->windows().begin(), m_app->windows().end(),
                       [&](const std::unique_ptr<at::Window> &w) { return focus.isFocused(*w); });
}

QString PageView::filterStatus(const TableGrid &grid)
{
    return tableText("%1 of %n row(s) shown", grid.rowCount()).arg(grid.shownCount);
}

bool PageView::focusFilter(int row, int column, at::Context<PageView> &cx)
{
    m_app = &cx.app();
    m_selfId = cx.entityId();
    const TableEntry *e = isTable(row) ? tableEntry(row) : nullptr;
    if (!e || !e->grid.filtering || e->grid.columnCount() == 0)
        return false;
    const int c = std::clamp(column, 0, e->grid.columnCount() - 1);
    // The cell's editing ends, a cell selection too: the keys are the field's.
    if (m_cellUid)
        stopEditingCell(cx);
    clearCellSelection(cx);
    const std::shared_ptr<FilterField> field = filterField(m_model->uidAt(row), c, e->grid);
    if (!field)
        return false;
    for (const std::unique_ptr<at::Window> &w : cx.app().windows()) {
        if (w->drewEntity(cx.entityId()))
            field->input.read(cx.app()).focusHandle().focus(*w);
    }
    if (std::find(m_drawn.begin(), m_drawn.end(), std::size_t(row)) == m_drawn.end())
        m_handle.scrollToRow(std::size_t(row), at::ListPosition::Nearest);
    cx.notify();
    if (onEditingStarted)
        onEditingStarted(); // (the host gives the view the keyboard)
    return true;
}

bool PageView::filterKey(quint64 uid, int column, const at::Keystroke &k, at::Window &w, at::Context<PageView> &cx)
{
    const auto it = m_filterFields.find(CellKey{uid, kFilterRow, column});
    const int row = m_model ? m_model->rowOfUid(uid) : -1;
    if (it == m_filterFields.end() || !it->second || row < 0 || !m_controller || k.modifiers.any())
        return false;
    const at::Entity<at::TextInput> input = it->second->input;
    if (input.read(cx.app()).markedRange())
        return false;
    if (k.key == u"escape") {
        if (!input.read(cx.app()).text().empty()) {
            // Esc empties the field (and its filter) first.
            input.update(cx.app(), [](at::TextInput &t, at::Context<at::TextInput> &c) { t.setText(u"", c); });
            return true;
        }
        // In an empty field: the filters go, the table is selected (the page keeps the keys).
        w.blur();
        m_controller->setTableFilterOpen(row, false);
        m_controller->setSelectedRow(row);
        cx.notify();
        return true;
    }
    if (k.key == u"enter") {
        w.blur(); // (the field is left, the filter stays)
        cx.notify();
        return true;
    }
    return false;
}

at::AnyElement PageView::renderFilterRow(std::size_t row, quint64 uid, const TableEntry &entry)
{
    const TableLook &look = m_tableLook;
    const TableGrid &grid = entry.grid;
    PageView *self = this;
    const at::EntityId selfId = m_selfId;
    at::Div tr = at::div()
                         .id(u"filters")
                         .relative()
                         .flexRow()
                         .h(look.filterHeight)
                         // The line under the header.
                         .child(at::canvas([self](at::Rect b, at::Window &w, at::App &) {
                                    w.paintQuad(at::Rect::fromXYWH(b.left(), b.top(), b.width(), 1),
                                                at::Background::solid(self->m_tableLook.borderSubtle), {},
                                                at::transparent(), {});
                                })
                                        .absolute()
                                        .inset(0));
    for (int c = 0; c < grid.columnCount(); ++c) {
        const std::shared_ptr<FilterField> field = filterField(uid, c, grid);
        if (!field)
            continue;
        const float width = c < int(entry.widths.size()) ? entry.widths[std::size_t(c)] : 56;
        const CellKey key{uid, kFilterRow, c};
        CellFrame &frame = m_cellFrames[key];
        frame.frame = m_frame;
        frame.text = at::TextLayoutHandle{};
        const bool hasText = !field->input.read(*m_app).text().empty();
        const bool focused = std::any_of(m_app->windows().begin(), m_app->windows().end(),
                                         [&](const std::unique_ptr<at::Window> &w) {
                                             return field->input.read(*m_app).focusHandle().isFocused(*w);
                                         });
        at::Div box = at::div()
                              .flex1()
                              .minW(0)
                              .h(look.buttonHeight)
                              .rounded(look.radius)
                              .border(1)
                              .borderColor(focused ? look.focusRing : look.borderStrong)
                              .bg(look.background)
                              .px(8)
                              .flexRow()
                              .itemsCenter()
                              .gap(2)
                              .cursorText()
                              .textSize(look.fieldTextSize)
                              .lineHeight(std::round(look.fieldTextSize * 1.4f))
                              .captureKeyDown([selfId, uid, c](const at::KeyDownEvent &e, at::Window &w, at::App &a) {
                                  bool handled = false;
                                  a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &cx) {
                                      handled = v.filterKey(uid, c, e.keystroke, w, cx);
                                  });
                                  if (handled)
                                      w.stopPropagation();
                              })
                              // A press in the field: the edited cell is left, cells are no longer selected.
                              .onMouseDown(at::MouseButton::Left,
                                           [selfId](const at::MouseDownEvent &, at::Window &, at::App &a) {
                                               a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &cx) {
                                                   if (v.m_cellUid)
                                                       v.stopEditingCell(cx);
                                                   v.clearCellSelection(cx);
                                                   if (v.onEditingStarted)
                                                       v.onEditingStarted();
                                               });
                                           })
                              .child(at::div().flex1().minW(0).flexCol().child(field->input));
        if (hasText) {
            // The × empties it (BTextField's clear button).
            const at::Color ink = m_palette.muted;
            std::move(box).child(at::div()
                                         .id(u"clear")
                                         .size(18)
                                         .flexShrink0()
                                         .rounded(9)
                                         .cursorPointer()
                                         .role(at::Role::Button)
                                         .ariaLabel(u16View(QCoreApplication::translate("BTextField", "Clear")))
                                         .hover([hover = look.hover](at::Style &s) { s.background = at::Background::solid(hover); })
                                         .onClick([selfId, uid, c](const at::ClickEvent &, at::Window &, at::App &a) {
                                             a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &) {
                                                 const auto it = v.m_filterFields.find(CellKey{uid, kFilterRow, c});
                                                 if (it != v.m_filterFields.end() && it->second)
                                                     it->second->input.update(a, [](at::TextInput &t, at::Context<at::TextInput> &tc) {
                                                         t.setText(u"", tc);
                                                     });
                                             });
                                         })
                                         .child(at::canvas([ink](at::Rect b, at::Window &w, at::App &) {
                                                    if (const auto icon = pageIcon(QStringLiteral("x")))
                                                        w.paintIcon(*icon, at::Rect::fromXYWH(b.center().x - 6, b.center().y - 6, 12, 12), ink);
                                                })
                                                        .absolute()
                                                        .inset(0)));
        }
        std::move(tr).child(at::div()
                                    .id(at::ElementId::number(std::uint64_t(c)))
                                    .relative()
                                    .flexBasis(at::Length::px(width))
                                    .flexGrow(width)
                                    .flexShrink0()
                                    .minW0()
                                    .flexRow()
                                    .itemsCenter()
                                    .px(2)
                                    .child(at::canvas([self, key](at::Rect b, at::Window &, at::App &) {
                                               self->m_cellFrames[key].bounds = b;
                                           })
                                                   .absolute()
                                                   .inset(0))
                                    .child(std::move(box)));
    }
    (void)row;
    return tr;
}

} // namespace brulion::editor::atr
