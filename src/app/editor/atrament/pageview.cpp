#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
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

at::AnyElement PageView::render(at::Window &, at::Context<PageView> &)
{
    m_drawn.clear();
    m_foreign.clear();
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
    auto line = [&] {
        at::Div d = at::div().id(id).pl(m.contentX).py(m.rowPad);
        if (m.contentWidth > 0)
            std::move(d).maxW(m.contentX + m.contentWidth);
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
    at::Text body = at::text(u16View(text.text)).id(u"text").spans(spans);
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
    if (closed)
        std::move(column).lineThrough().textColor(m_palette.muted);
    std::move(column).child(std::move(body));
    std::move(content).child(std::move(column));
    return line().child(std::move(content));
}

} // namespace brulion::editor::atr
