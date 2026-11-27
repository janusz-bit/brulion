#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"
#include "renderedtextitem.h"

#include <QAbstractItemModel>

#include <algorithm>
#include <cmath>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

// --- PageExtents --------------------------------------------------------------------------------

std::size_t PageExtents::count() const
{
    return std::size_t(std::max(0, m_rows.count()));
}

float PageExtents::headerHeight() const
{
    return float(m_rows.heights().offsetOf(1));
}

float PageExtents::offsetOf(std::size_t row) const
{
    const int tree = PageRows::treeRow(int(std::min(row, count())));
    return float(m_rows.heights().offsetOf(tree)) - headerHeight();
}

float PageExtents::heightOf(std::size_t row) const
{
    if (row >= count() || !m_rows.isShown(int(row)))
        return 0;
    return float(m_rows.heights().height(PageRows::treeRow(int(row))));
}

bool PageExtents::isMeasured(std::size_t row) const
{
    if (row >= count())
        return false;
    return !m_rows.isShown(int(row)) || m_rows.heights().isMeasured(PageRows::treeRow(int(row)));
}

std::size_t PageExtents::rowAt(float y) const
{
    const std::size_t n = count();
    const RowHeightTree &tree = m_rows.heights();
    const float total = offsetOf(n);
    if (n == 0 || total <= 0)
        return n;
    // The model rows' span: never the header's or the footer's.
    const qreal at = std::clamp(qreal(y), 0.0, qreal(total) - 1.0 / RowHeightTree::Scale) + headerHeight();
    const int r = tree.rowAt(at);
    const int model = m_rows.modelRow(r);
    return model >= 0 ? std::size_t(model) : n;
}

void PageExtents::setMeasured(std::size_t row, float height)
{
    if (row < count())
        m_rows.setRowHeight(PageRows::treeRow(int(row)), height, true);
}

void PageExtents::markAllEstimated()
{
    m_rows.view().markAllEstimated();
}

// --- PageView -----------------------------------------------------------------------------------

namespace {

bool drawable(const QString &view)
{
    return view == u"text" || view == u"heading" || view == u"quote";
}

} // namespace

PageView::PageView(at::Context<PageView> &cx, BlockListModel *model, PageMetrics metrics, PagePalette palette)
    : m_model(model)
    , m_rows(std::make_unique<PageRows>())
    , m_extents(*m_rows)
    , m_metrics(metrics)
    , m_palette(std::move(palette))
{
    const PageMetrics m = m_metrics;
    QPointer<BlockListModel> guarded = model;
    m_rows->setEstimator([guarded, m](int row) -> qreal {
        if (!guarded)
            return m.lineHeight + 2 * m.rowPad;
        const QModelIndex index = guarded->index(row, 0);
        const QString view = index.data(BlockListModel::ViewRole).toString();
        if (view == u"heading") {
            const int level = std::clamp(index.data(BlockListModel::HeadingLevelRole).toInt(), 0, 6);
            return m.headingSizes[level] * 1.4 + 2 * m.rowPad;
        }
        if (!drawable(view))
            return 3 * m.lineHeight + 2 * m.rowPad;
        return m.lineHeight + 2 * m.rowPad;
    });
    m_rows->setShownRole(BlockListModel::VisibleRole);
    m_rows->setModel(model);
    m_rows->setHeaderHeight(0);
    m_rows->setFooterHeight(0);
    if (!model)
        return;

    // The model's changes: the list's anchor follows its row, and every window showing the page
    // draws again.
    at::App *app = &cx.app();
    const at::EntityId self = cx.entityId();
    auto notify = [app, self] {
        app->updateEntity<PageView>(self, [](PageView &, at::Context<PageView> &c) { c.notify(); });
    };
    auto update = [app, self](auto f) {
        app->updateEntity<PageView>(self, [&f](PageView &view, at::Context<PageView> &c) {
            f(view);
            c.notify();
        });
    };
    m_connections.push_back(QObject::connect(
            model, &QAbstractItemModel::rowsInserted, model, [update](const QModelIndex &parent, int first, int last) {
                if (!parent.isValid())
                    update([first, last](PageView &v) {
                        v.m_handle.rowsInserted(std::size_t(first), std::size_t(last - first + 1));
                    });
            }));
    m_connections.push_back(QObject::connect(
            model, &QAbstractItemModel::rowsRemoved, model, [update](const QModelIndex &parent, int first, int last) {
                if (!parent.isValid())
                    update([first, last](PageView &v) {
                        v.m_handle.rowsRemoved(std::size_t(first), std::size_t(last - first + 1));
                    });
            }));
    m_connections.push_back(QObject::connect(
            model, &QAbstractItemModel::rowsMoved, model,
            [update](const QModelIndex &, int first, int last, const QModelIndex &, int destination) {
                update([first, last, destination](PageView &v) {
                    const std::size_t n = std::size_t(last - first + 1);
                    v.m_handle.rowsRemoved(std::size_t(first), n);
                    v.m_handle.rowsInserted(std::size_t(destination > first ? destination - int(n) : destination), n);
                });
            }));
    m_connections.push_back(QObject::connect(model, &QAbstractItemModel::modelReset, model, [update] {
        update([](PageView &v) {
            v.m_rows->setHeaderHeight(0);
            v.m_rows->setFooterHeight(0);
        });
    }));
    // A row's text changed: its height is an estimate until it is laid out again (the rows in view
    // are, in the next frame).
    m_connections.push_back(QObject::connect(m_rows.get(), &PageRows::dataChanged, model,
                                             [update](int first, int last, const QList<int> &) {
                                                 update([first, last](PageView &v) {
                                                     for (int r = first; r <= last; ++r)
                                                         v.m_rows->view().setRowEstimated(PageRows::treeRow(r));
                                                 });
                                             }));
    m_connections.push_back(QObject::connect(m_rows.get(), &PageRows::rowsChanged, model, notify));
}

PageView::~PageView()
{
    for (const QMetaObject::Connection &c : m_connections)
        QObject::disconnect(c);
    for (const QMetaObject::Connection &c : m_controllerConnections)
        QObject::disconnect(c);
}

void PageView::setSelectionLook(const SelectionLook &look)
{
    m_selectionLook = look;
}

void PageView::setStyle(const PageMetrics &metrics, const PagePalette &palette)
{
    m_metrics = metrics;
    m_palette = palette;
    m_extents.markAllEstimated();
}

int PageView::drawnRows() const
{
    std::vector<std::size_t> rows = m_drawn;
    std::sort(rows.begin(), rows.end());
    return int(std::unique(rows.begin(), rows.end()) - rows.begin());
}

std::vector<std::size_t> PageView::foreignRows() const
{
    std::vector<std::size_t> rows = m_foreign;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

bool PageView::drawsRow(int row) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return false;
    const QModelIndex index = m_model->index(row, 0);
    const auto text = index.data(BlockListModel::RenderedTextRole).value<RenderedTextRef>();
    return text.drawable() && drawable(index.data(BlockListModel::ViewRole).toString());
}

at::AnyElement PageView::render(at::Window &window, at::Context<PageView> &cx)
{
    m_drawn.clear();
    m_foreign.clear();
    // Editing: the field takes the focus when a row of Atrament's starts being edited, and the
    // edited row is scrolled into view (a split made a row under the view's end, ↑/↓ went off it).
    if (const int edited = editedRow(); edited >= 0 && m_input) {
        if (m_focusInput)
            m_input->read(cx.app()).focusHandle().focus(window);
        if (m_revealEdited)
            m_handle.scrollToRow(std::size_t(edited), at::ListPosition::Nearest);
    }
    m_focusInput = false;
    m_revealEdited = false;
    // The text of rows not drawn for a while is forgotten (the handles of the rows on screen stay).
    const std::uint64_t frame = window.frameCount();
    for (auto it = m_rowTextFrame.begin(); it != m_rowTextFrame.end();) {
        if (it->second + 2 < frame) {
            m_rowText.erase(it->first);
            it = m_rowTextFrame.erase(it);
        } else {
            ++it;
        }
    }
    m_frame = frame;
    m_selfId = cx.entityId();
    PageView *self = this;
    return at::div()
            .sizeFull()
            .bg(m_palette.background)
            .flexCol()
            .child(at::list(u"page", m_extents,
                            [self](std::size_t row, at::Window &, at::App &) -> at::AnyElement {
                                return self->renderRow(row);
                            })
                           .trackScroll(m_handle)
                           .ariaLabel(u"Page")
                           .flex1()
                           .textSize(m_metrics.fontSize)
                           .lineHeight(m_metrics.lineHeight)
                           .textColor(m_palette.text)
                           .fontFamily(m_palette.bodyFamily));
}

at::AnyElement PageView::renderRow(std::size_t row)
{
    const PageMetrics &m = m_metrics;
    if (!m_model || int(row) >= m_model->rowCount())
        return at::div().h(1);
    const QModelIndex index = m_model->index(int(row), 0);
    const QString view = index.data(BlockListModel::ViewRole).toString();
    const auto rendered = index.data(BlockListModel::RenderedTextRole).value<RenderedTextRef>();
    const int depth = std::max(0, index.data(BlockListModel::DepthRole).toInt());
    const bool listItem = index.data(BlockListModel::KindRole).toString() == u"listItem";
    const QString task = index.data(BlockListModel::TaskStatusRole).toString();
    // Where the text starts (BlockDelegate's outline geometry: the gutter holds the fold arrow and
    // the bullet column left of the text), the column's width.
    // (PageItem::textX: a document indents list items by listIndent, an outline every row by
    // indentStep.)
    const bool marked = listItem || !task.isEmpty();
    const float textX = m.documentMode ? m.gutter + (marked ? float(depth + 1) * m.listIndent : 0)
                                       : m.gutter + float(depth) * m.indentStep;
    const float marker = marked ? (m.documentMode ? m.listIndent : m.bulletColumn) : 0;
    const at::ElementId id = at::ElementId::number(index.data(BlockListModel::UidRole).toString().toULongLong());
    // A row: the text column (contentX, contentWidth) inside the list's width.
    // A selected block is drawn on a band (BlockPartSelectionBand: one band over a range of
    // blocks, rounded at its ends, a line down its left edge; a block alone framed).
    const QString edge = index.data(BlockListModel::SelectionEdgeRole).toString();
    auto line = [&] {
        at::Div d = at::div().id(id).pl(m.contentX).py(m.rowPad);
        if (m.contentWidth > 0)
            std::move(d).maxW(m.contentX + m.contentWidth);
        if (!edge.isEmpty()) {
            const bool start = edge == u"single" || edge == u"first";
            const bool end = edge == u"single" || edge == u"last";
            const float r = m_selectionLook.radius;
            std::move(d).bg(m_selectionLook.band).roundedT(start ? r : 0).roundedB(end ? r : 0);
            if (edge == u"single")
                std::move(d).border(1).borderColor(m_selectionLook.ring.fadedBy(0.65f));
            else
                std::move(d).borderL(2).borderColor(m_selectionLook.ring.fadedBy(0.4f));
        }
        return d;
    };

    if (!rendered.drawable() || !drawable(view)) {
        // A foreign row: what the QML delegate will show over it, its height reserved.
        m_foreign.push_back(row);
        const float h = float(m_rows->heights().height(PageRows::treeRow(int(row))));
        return line().child(at::div()
                                    .ml(textX)
                                    .h(std::max(h - 2 * m.rowPad, m.lineHeight))
                                    .rounded(6)
                                    .bg(m_palette.codeBackground));
    }
    m_drawn.push_back(row);

    const markdown::RenderedText &text = *rendered.d;
    const std::vector<at::TextSpan> spans = renderedSpans(text, m_palette);
    const bool closed = task == u"done" || task == u"cancelled";
    const quint64 uid = index.data(BlockListModel::UidRole).toString().toULongLong();
    const bool edited = m_input && uid == m_editUid && editableView(view, true);
    at::Text body = at::text(u16View(text.text)).id(u"text").spans(spans);
    // Where a click lands in the text, and ↑/↓ into the row, after the frame.
    if (!edited) {
        std::move(body).trackLayout(m_rowText[uid]);
        m_rowTextFrame[uid] = m_frame;
    }
    // Each link a hit region of the text (adjacent runs of one href are one link).
    PageView *self = this;
    for (qsizetype i = 0; i < text.runs.size();) {
        const QString &href = text.runs[i].href;
        qsizetype j = i + 1;
        while (j < text.runs.size() && text.runs[j].href == href && text.runs[j].from == text.runs[j - 1].to)
            ++j;
        if (!href.isEmpty()) {
            // (The listener lives in the frame arena, which destroys it — the href with it.)
            std::move(body).link(at::Range{int(text.runs[i].from), int(text.runs[j - 1].to)},
                                 [self, row, href](const at::ClickEvent &, at::Window &, at::App &) {
                                     if (self->onLink)
                                         self->onLink(int(row), href);
                                 });
        }
        i = j;
    }

    at::Div content = at::div().flexRow().itemsStart().pl(textX - marker);
    if (view == u"heading") {
        const int level = std::clamp(index.data(BlockListModel::HeadingLevelRole).toInt(), 0, 6);
        std::move(content)
                .textSize(m.headingSizes[level])
                .lineHeight(std::round(m.headingSizes[level] * 1.3f))
                .fontBold();
    }
    if (!task.isEmpty()) {
        // The task's box in the bullet's place: filled when done or cancelled.
        at::Div box = at::div()
                              .size(m.taskBox)
                              .rounded(3)
                              .border(1.5f)
                              .borderColor(m_palette.muted)
                              .role(at::Role::CheckBox)
                              .checked(closed)
                              .ariaLabel(u16View(text.text))
                              .cursorPointer()
                              .id(u"task")
                              .onClick([self, row](const at::ClickEvent &, at::Window &, at::App &) {
                                  if (self->onToggleTask)
                                      self->onToggleTask(int(row));
                              });
        if (closed)
            std::move(box).bg(m_palette.muted);
        std::move(content).child(at::div()
                                         .h(m.lineHeight)
                                         .w(marker)
                                         .flexShrink0()
                                         .flexRow()
                                         .itemsCenter()
                                         .justifyCenter()
                                         .child(std::move(box)));
    } else if (listItem) {
        const QString number = index.data(BlockListModel::ListMarkerRole).toString();
        const bool numbered = !number.isEmpty() && number.front().isDigit();
        at::Div bullet = at::div()
                                 .w(marker)
                                 .flexShrink0()
                                 .h(m.lineHeight)
                                 .flexRow()
                                 .itemsCenter()
                                 .justifyCenter()
                                 .textColor(m_palette.muted)
                                 .ariaHidden();
        if (numbered)
            std::move(bullet).child(at::text(u16View(number)));
        else
            std::move(bullet).child(at::div().size(6).rounded(3).bg(m_palette.muted));
        std::move(content).child(std::move(bullet));
    }
    at::Div column = at::div().flex1().minW(0);
    if (view == u"quote")
        std::move(column).borderL(3).borderColor(m_palette.guide).pl(12).textColor(m_palette.muted);
    // (The edited text of a closed task is written plainly: InlineEditor strikes nothing.)
    if (closed && !edited)
        std::move(column).lineThrough().textColor(m_palette.muted);
    const at::EntityId selfId = m_selfId;
    if (edited) {
        // The edited row: the field over the block's editText in the text's place, and the
        // editor's keys caught before the field sees them.
        std::move(column).child(*m_input);
        std::move(content).child(std::move(column));
        return line()
                .captureKeyDown([selfId](const at::KeyDownEvent &e, at::Window &w, at::App &a) {
                    bool handled = false;
                    a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &c) {
                        handled = v.editorKey(e.keystroke, w, c);
                    });
                    if (handled)
                        w.stopPropagation();
                })
                .child(std::move(content));
    }
    std::move(column).child(std::move(body));
    std::move(content).child(std::move(column));
    at::Div drawn = line().child(std::move(content));
    if (m_controller && !m_controller->readOnly()) {
        // A click or a tap edits the block at the character under it (links and the task's box
        // take their own clicks first). No node of its own for screen readers: the row's text is.
        std::move(drawn)
                .role(at::Role::None)
                .cursorText()
                .onClick([selfId, row](const at::ClickEvent &click, at::Window &w, at::App &a) {
                    a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &c) {
                        v.rowClicked(row, click, w, c);
                    });
                });
    }
    return drawn;
}

} // namespace brulion::editor::atr
