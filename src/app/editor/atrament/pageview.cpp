#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "editstyles.h"
#include "pagecontroller.h"
#include "renderedtextitem.h"
#include "text/find.h"

#include <QAbstractItemModel>
#include <QCoreApplication>

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
    if (foreign && std::find(foreign->begin(), foreign->end(), row) != foreign->end())
        return;
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
    return view == u"text" || view == u"heading" || view == u"quote" || view == u"callout";
}

} // namespace

PageView::PageView(at::Context<PageView> &cx, BlockListModel *model, PageMetrics metrics, PagePalette palette)
    : m_foldNames{QCoreApplication::translate("BlockDelegate", "Expand"),
                  QCoreApplication::translate("BlockDelegate", "Collapse")}
    , m_model(model)
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
    // (The spell checker's and Vim's: pagespell.cpp, pagevim.cpp.)
    for (const QMetaObject::Connection &c : m_spellConnections)
        QObject::disconnect(c);
    for (const QMetaObject::Connection &c : m_vimConnections)
        QObject::disconnect(c);
}

void PageView::dragOver(at::Point p, at::Context<PageView> &)
{
    if (!m_dragUid || !m_model || !m_controller)
        return;
    const int start = m_model->rowOfUid(m_dragUid);
    if (start < 0)
        return;
    // The row under the pointer (by the last frame's scroll; none over the header or the footer).
    const int row = rowAtY(p.y);
    if (row < 0)
        return;
    if (row == start && !m_dragSelected)
        return; // (inside the pressed block: its text's own selection)
    if (m_dragSelected && row == m_dragFocus)
        return;
    m_dragSelected = true;
    m_dragFocus = row;
    if (onDragSelect) {
        onDragSelect(start, row);
    } else {
        m_controller->stopEditing();
        m_controller->selectRange(start, row);
    }
}

void PageView::setSelectionLook(const SelectionLook &look)
{
    m_selectionLook = look;
}

void PageView::appendFindSpans(QStringView text, std::vector<at::TextSpan> &spans) const
{
    if (m_find.query.isEmpty())
        return;
    text::FindOptions options;
    options.matchCase = m_find.matchCase;
    options.matchDiacritics = m_find.matchDiacritics;
    options.wholeWord = m_find.wholeWord;
    for (const text::TextRange &r : text::findMatches(text, m_find.query, options)) {
        at::TextStyleRefinement tint;
        at::TextBackground box;
        box.color = m_find.color;
        box.radius = 2;
        tint.background = std::optional<at::TextBackground>(box);
        spans.push_back({at::Range{int(r.start), int(r.end())}, tint});
    }
}

void PageView::setFind(const QString &query, bool matchCase, bool matchDiacritics, bool wholeWord, at::Color color,
                       at::Context<PageView> &cx)
{
    m_find = {query, matchCase, matchDiacritics, wholeWord, color};
    applyEditStyler(cx);
    cx.notify();
}

void PageView::setDrawsCode(bool on, const CodeLook &look, at::Context<PageView> &cx)
{
    m_drawsCode = on;
    m_codeLook = look;
    m_extents.markAllEstimated();
    cx.notify();
}

void PageView::setEditStyles(std::shared_ptr<const EditStyler> styles, at::Context<PageView> &cx)
{
    m_styles = std::move(styles);
    applyEditStyler(cx);
    cx.notify();
}

at::AnyElement PageView::renderCodeRow(std::size_t row, const QModelIndex &index, float textX, bool edited)
{
    // CodeBlockView: a sunken panel, the language small at its top right, the code in the code
    // face at Theme.textSm with 1.5 line height, from space4 in and space3 down.
    const CodeLook &c = m_codeLook;
    const QString lang = index.data(BlockListModel::LangRole).toString();
    const quint64 uid = index.data(BlockListModel::UidRole).toString().toULongLong();
    at::Div panel = at::div()
                            .ml(textX)
                            .flex1()
                            .minW(0)
                            .flexCol()
                            .bg(c.background)
                            .border(1)
                            .borderColor(c.border)
                            .rounded(c.radius)
                            .pt(12)
                            .pb(12)
                            .pl(16)
                            .pr(16)
                            .fontFamily(m_palette.codeFamily)
                            .textSize(c.fontSize)
                            .lineHeight(c.lineHeight);
    if (!lang.isEmpty())
        std::move(panel).pr(16 + c.labelSize * 0.62f * float(lang.size()) + 8)
                .child(at::div()
                               .absolute()
                               .top(8)
                               .right(12)
                               .textSize(c.labelSize)
                               .lineHeight(c.labelSize * 1.3f)
                               .textColor(c.label)
                               .ariaHidden()
                               .child(at::text(u16View(lang))));
    const QString code = m_model->editTextAt(int(row));
    if (c.lineNumbers) {
        // CodeBlockView's gutter (pagecode.cpp): Theme.space3 in, the numbers, Theme.space3 to the code.
        std::move(panel).pl(12).flexRow().itemsStart().child(codeLineNumbers(row, code, edited));
    }
    auto content = [&c](at::AnyElement e) {
        return at::div().flex1().minW(0).when(c.lineNumbers, [](at::Div &&d) { return std::move(d).ml(12); }).child(std::move(e));
    };
    if (edited)
        return std::move(panel).child(content(*m_input));
    const std::u16string_view text = u16View(code);
    at::Text body = at::text(text).id(u"code");
    if (m_styles) {
        const std::vector<at::TextSpan> spans =
                m_styles->spans(text, MarkdownMode::Code, lang == u"luau" || lang == u"lua");
        std::move(body).spans(spans);
    }
    std::move(body).trackLayout(m_rowText[uid]);
    m_rowTextFrame[uid] = m_frame;
    return std::move(panel).child(content(std::move(body)));
}

void PageView::setCalloutLook(const CalloutLook &look)
{
    m_calloutLook = look;
    m_extents.markAllEstimated();
}

at::Color PageView::CalloutLook::tint(const QString &type) const
{
    // (BlockPartCallout's mapping of Obsidian's types.)
    if (type == u"tip" || type == u"success" || type == u"check" || type == u"done")
        return success;
    if (type == u"warning" || type == u"caution" || type == u"attention")
        return warning;
    if (type == u"danger" || type == u"error" || type == u"bug" || type == u"failure")
        return danger;
    if (type == u"quote" || type == u"cite")
        return quote;
    return info;
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

bool PageView::isCode(int row) const
{
    return m_drawsCode && m_model && row >= 0 && row < m_model->rowCount()
           && m_model->index(row, 0).data(BlockListModel::ViewRole).toString() == u"code";
}

bool PageView::drawsRow(int row) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return false;
    if (isPanel(row) || isDiagram(row) || isMath(row)) // (charts, diagrams, formulas: diagramrows.cpp)
        return true;
    if (isTable(row)) // (tablerows.cpp)
        return true;
    if (const std::optional<bool> media = drawsMediaRow(row)) // pictures, embeds, queries (mediarows.cpp)
        return *media;
    const QModelIndex index = m_model->index(row, 0);
    const auto text = index.data(BlockListModel::RenderedTextRole).value<RenderedTextRef>();
    return text.drawable() && drawable(index.data(BlockListModel::ViewRole).toString());
}

at::AnyElement PageView::render(at::Window &window, at::Context<PageView> &cx)
{
    m_drawn.clear();
    m_foreign.clear();
    m_foreignFrames.clear();
    m_extents.foreign = &m_foreign;
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
    mediaFrame(); // (the pictures and texts of the last frame forgotten, whether media rows show or not)
    m_selfId = cx.entityId();
    m_viewWidth = window.viewportSize().width;
    PageView *self = this;
    const at::EntityId rootSelf = m_selfId;
    return at::div()
            .sizeFull()
            .bg(m_palette.background)
            .flexCol()
            // A mouse's release ends a press that may have selected blocks (after the rows' click).
            .onMouseUp(at::MouseButton::Left,
                       [rootSelf](const at::MouseUpEvent &e, at::Window &, at::App &a) {
                           a.updateEntity<PageView>(rootSelf, [&](PageView &v, at::Context<PageView> &c) {
                               v.m_dragUid = 0;
                               if (v.m_drag.kind != DragKind::None)
                                   v.dragReleased(e.cancelled, c);
                           });
                       })
            // The pressed mouse moving over the page: into another block, the blocks between are
            // selected (the row under the pointer from the list's rows, wherever the press is). A
            // drag of blocks begun (pagedrag.cpp) follows it until its own layer does.
            .onMouseMove([rootSelf](const at::MouseMoveEvent &e, at::Window &, at::App &a) {
                a.updateEntity<PageView>(rootSelf, [&](PageView &v, at::Context<PageView> &c) {
                    if (v.m_drag.kind != DragKind::None) {
                        if (v.followsPointer(e.pointer))
                            v.dragMoved(e.position, c);
                    } else if (e.pointer == at::PointerKind::Mouse && e.pressedButton == at::MouseButton::Left)
                        v.dragOver(e.position, c);
                });
            })
            .child(at::list(u"page", m_extents,
                            [self](std::size_t row, at::Window &, at::App &) -> at::AnyElement {
                                return self->renderRow(row);
                            })
                           .trackScroll(m_handle)
                           .header([self](at::Window &, at::App &) {
                               return self->edgeElement(ForeignFrame::Kind::Header);
                           })
                           .footer([self](at::Window &, at::App &) {
                               return self->edgeElement(ForeignFrame::Kind::Footer);
                           })
                           .ariaLabel(u"Page")
                           .flex1()
                           .textSize(m_metrics.fontSize)
                           .lineHeight(m_metrics.lineHeight)
                           .textColor(m_palette.text)
                           .fontFamily(m_palette.bodyFamily))
            // After the list's prepaint (its rows measured): the page's height, when it changed.
            .child(at::canvas([self](at::Rect, at::Window &, at::App &) {
                       const float h = self->contentHeight();
                       if (std::abs(h - self->m_reportedHeight) < 0.5f)
                           return;
                       self->m_reportedHeight = h;
                       if (self->onContentHeight)
                           self->onContentHeight(h);
                   })
                           .absolute()
                           .size(0))
            // A drag of blocks: the drop line, the listener that follows it (pagedrag.cpp).
            .child(dragLayer())
            // A block selection's keys while no block is edited (pageselection.cpp): the page keeps
            // the keyboard.
            .child(keyLayer());
}

bool PageView::reveal(at::Rect r, float margin, float viewportHeight, at::Context<PageView> &cx)
{
    float dy = 0;
    if (r.top() < margin)
        dy = r.top() - margin;
    else if (r.bottom() > viewportHeight - margin)
        dy = std::min(r.bottom() - (viewportHeight - margin), r.top() - margin);
    if (std::abs(dy) < 0.5f)
        return false;
    // By rows: the view's new top as an offset into the row under the area (rows below the view
    // are estimates, and a pixel scroll stops where the last frame's estimated height ends).
    const int row = rowAtY(r.top());
    const std::optional<at::Rect> bounds = row >= 0 ? rowBounds(row) : std::nullopt;
    if (bounds) {
        // (The list fills the window from its top: the view's top is at 0.)
        m_handle.scrollToAnchor(std::size_t(row), dy - bounds->top());
    } else if (!m_handle.scrollBy(dy)) {
        return false;
    }
    cx.notify();
    return true;
}

bool PageView::revealRow(int row, at::Context<PageView> &cx)
{
    if (!m_model || row < 0 || row >= m_model->rowCount() || m_extents.isHidden(std::size_t(row)))
        return false;
    m_handle.scrollToRow(std::size_t(row), at::ListPosition::Nearest);
    cx.notify();
    return true;
}

float PageView::contentHeight() const
{
    // The header above the rows and the footer below them are the list's too.
    return m_headerHeight + m_extents.offsetOf(m_extents.count()) + m_footerHeight;
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
    const at::EntityId menuSelf = m_selfId;
    const quint64 rowUid = index.data(BlockListModel::UidRole).toString().toULongLong();
    auto line = [&] {
        at::Div d = at::div().id(id).pl(m.contentX).py(m.rowPad);
        if (m_controller && !m_controller->readOnly()) {
            // A mouse's press here may become a drag that selects blocks (dragOver).
            std::move(d)
                    .onMouseDown(at::MouseButton::Left,
                                 [menuSelf, rowUid](const at::MouseDownEvent &e, at::Window &, at::App &a) {
                                     if (e.pointer != at::PointerKind::Mouse || e.modifiers.shift)
                                         return;
                                     a.updateEntity<PageView>(menuSelf, [&](PageView &v, at::Context<PageView> &) {
                                         v.m_dragUid = rowUid;
                                         v.m_dragSelected = false;
                                         v.m_dragFocus = -1;
                                     });
                                 });
        }
        if (onBlockMenu || onSpellingMenu || onTextMenu)
            // A right-click on a block: its menu (BlockEditor.openBlockMenuAt) — in the edited text
            // the text's (pagetextmenu.cpp), on a misspelled word the spelling's (pagespell.cpp).
            std::move(d).onMouseDown(at::MouseButton::Right,
                                     [menuSelf, row](const at::MouseDownEvent &e, at::Window &w, at::App &a) {
                                         a.updateEntity<PageView>(menuSelf, [&](PageView &v, at::Context<PageView> &c) {
                                             if (v.textMenuAt(int(row), e.position, c))
                                                 return;
                                             if (!v.spellingMenuAt(int(row), e.position) && v.onBlockMenu)
                                                 v.onBlockMenu(int(row), e.position);
                                         });
                                         w.stopPropagation();
                                     });
        if (m.contentWidth > 0)
            std::move(d).maxW(m.contentX + m.contentWidth);
        // (A block with pictures or embedded blocks has one band under all of it: mediaRow()'s.)
        if (!edge.isEmpty() && !m_mediaInner) {
            // (pageselection.cpp: the band and the touch selection mode's grips.)
            if (std::optional<at::AnyElement> band = selectionBand(int(row)))
                std::move(d).child(std::move(*band));
        }
        return d;
    };

    if (isDiagram(int(row)) || isMath(int(row))) { // charts, diagrams, formulas (diagramrows.cpp)
        m_drawn.push_back(row);
        return renderViewRow(row, index, textX, line());
    }
    if (isPanel(int(row))) {
        const quint64 codeUid = index.data(BlockListModel::UidRole).toString().toULongLong();
        const bool codeEdited = m_input && codeUid == m_editUid;
        m_drawn.push_back(row);
        at::Div codeRow = line().child(view == u"calc"   ? renderCalcRow(row, index, textX, codeEdited)
                                       : view == u"rule" ? renderRuleRow(textX, codeEdited)
                                                         : renderCodeRow(row, index, textX, codeEdited));
        const at::EntityId codeSelf = m_selfId;
        if (codeEdited)
            return std::move(codeRow).captureKeyDown([codeSelf](const at::KeyDownEvent &e, at::Window &w, at::App &a) {
                bool handled = false;
                a.updateEntity<PageView>(codeSelf, [&](PageView &v, at::Context<PageView> &c) {
                    handled = v.editorKey(e.keystroke, w, c);
                });
                if (handled)
                    w.stopPropagation();
            });
        if (m_controller) {
            if (!m_controller->readOnly())
                std::move(codeRow).cursorText();
            std::move(codeRow).role(at::Role::None).onClick(
                    [codeSelf, row](const at::ClickEvent &click, at::Window &w, at::App &a) {
                        a.updateEntity<PageView>(codeSelf, [&](PageView &v, at::Context<PageView> &c) {
                            v.rowClicked(row, click, w, c);
                        });
                    });
        }
        return codeRow;
    }
    if (view == u"table" && isTable(int(row))) // a table drawn by Atrament (tablerows.cpp)
        return renderTableRow(row, index, textX, line());
    if (std::optional<at::AnyElement> media = m_mediaInner ? std::nullopt : mediaRow(row, index)) // (mediarows.cpp)
        return std::move(*media);
    if (!rendered.drawable() || !drawable(view)) {
        // A foreign row: its QML delegate's place (foreignrows.cpp).
        m_foreign.push_back(row);
        return foreignRow(row);
    }
    m_drawn.push_back(row);

    const markdown::RenderedText &text = *rendered.d;
    std::vector<at::TextSpan> spans = renderedSpans(text, m_palette);
    appendFindSpans(text.text, spans);
    appendRowSpelling(int(row), text, spans); // (pagespell.cpp)
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
            at::TextLink::MenuListener menu;
            if (onLinkMenu)
                menu = [self, row, href](at::Point p, at::Window &, at::App &) {
                    if (self->onLinkMenu)
                        self->onLinkMenu(int(row), href, p);
                };
            std::move(body).link(
                    at::Range{int(text.runs[i].from), int(text.runs[j - 1].to)},
                    [self, row, href](const at::ClickEvent &click, at::Window &, at::App &) {
                        if (self->onLinkAt)
                            self->onLinkAt(int(row), href, click.up.position);
                        else if (self->onLink)
                            self->onLink(int(row), href);
                    },
                    std::move(menu));
        }
        i = j;
    }

    // (As wide as the line: a callout's frame and a selected row's band span the text column.)
    at::Div content = at::div().flex1().minW(0).flexRow().itemsStart().pl(textX - marker);
    float firstLine = m.lineHeight;
    if (view == u"heading") {
        const int level = std::clamp(index.data(BlockListModel::HeadingLevelRole).toInt(), 0, 6);
        firstLine = std::round(m.headingSizes[level] * 1.3f);
        std::move(content).textSize(m.headingSizes[level]).lineHeight(firstLine).fontBold();
    }
    const at::EntityId foldSelf = m_selfId;
    if (!m.documentMode && index.data(BlockListModel::HasChildrenRole).toBool()) {
        // The fold arrow (BlockPartFoldArrow): left of the bullet, shown while folded, under the
        // mouse or on touch; a chevron pointing right when folded, down when open.
        const bool collapsed = index.data(BlockListModel::CollapsedRole).toBool();
        const bool shown = collapsed || m.touch || uid == m_hoveredUid;
        const at::Color ink = m_palette.muted.fadedBy(collapsed ? 0.9f : 0.6f);
        const float w = m.arrowColumn;
        std::move(content).child(
                at::div()
                        .id(u"fold")
                        .absolute()
                        .left(textX - marker - w + 2)
                        .top(0)
                        .w(w)
                        .h(firstLine)
                        .opacity(shown ? 1.0f : 0.0f)
                        .cursorPointer()
                        .role(at::Role::Button)
                        .expanded(!collapsed)
                        .ariaLabel(collapsed ? u16View(m_foldNames[0]) : u16View(m_foldNames[1]))
                        .onClick([foldSelf, row](const at::ClickEvent &, at::Window &, at::App &a) {
                            a.updateEntity<PageView>(foldSelf, [row](PageView &v, at::Context<PageView> &) {
                                if (v.m_controller)
                                    v.m_controller->toggleFold(int(row));
                            });
                        })
                        .child(at::canvas([collapsed, ink](at::Rect b, at::Window &win, at::App &) {
                                   const at::Point c{b.left() + b.width() / 2, b.top() + b.height() / 2};
                                   const float r = 3.5f; // half the chevron's span
                                   const float t = 1.6f; // its stroke
                                   win.paintPath(ink, [&](at::Window::PathPainter &p) {
                                       if (collapsed) { // >
                                           p.moveTo({c.x - r / 2 - t / 2, c.y - r});
                                           p.lineTo({c.x + r / 2 + t / 2, c.y});
                                           p.lineTo({c.x - r / 2 - t / 2, c.y + r});
                                           p.lineTo({c.x - r / 2 - t / 2 - t * 0.8f, c.y + r - t * 0.6f});
                                           p.lineTo({c.x + r / 2 + t / 2 - t * 1.4f, c.y});
                                           p.lineTo({c.x - r / 2 - t / 2 - t * 0.8f, c.y - r + t * 0.6f});
                                       } else { // v
                                           p.moveTo({c.x - r, c.y - r / 2 - t / 2});
                                           p.lineTo({c.x, c.y + r / 2 + t / 2});
                                           p.lineTo({c.x + r, c.y - r / 2 - t / 2});
                                           p.lineTo({c.x + r - t * 0.6f, c.y - r / 2 - t / 2 - t * 0.8f});
                                           p.lineTo({c.x, c.y + r / 2 + t / 2 - t * 1.4f});
                                           p.lineTo({c.x - r + t * 0.6f, c.y - r / 2 - t / 2 - t * 0.8f});
                                       }
                                       p.close();
                                   });
                               }).sizeFull()));
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
        if (onTaskMenu) {
            // Its status menu (BlockPartTaskBox's): a right-click or a finger held on the box.
            std::move(box)
                    .onMouseDown(at::MouseButton::Right,
                                 [self, row](const at::MouseDownEvent &e, at::Window &w, at::App &) {
                                     if (self->onTaskMenu)
                                         self->onTaskMenu(int(row), e.position);
                                     w.stopPropagation();
                                 })
                    .onLongPress([self, row](at::Point p, at::Window &, at::App &) {
                        if (self->onTaskMenu)
                            self->onTaskMenu(int(row), p);
                    });
        }
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
        if (!m.documentMode && onBulletClick) {
            // The outline's bullet zooms into the block (BlockPartBullet; a document's is text).
            const at::EntityId bulletSelf = m_selfId;
            std::move(bullet).id(u"bullet").cursorPointer().onClick(
                    [bulletSelf, row](const at::ClickEvent &, at::Window &, at::App &a) {
                        a.updateEntity<PageView>(bulletSelf, [row](PageView &v, at::Context<PageView> &) {
                            if (v.onBulletClick)
                                v.onBulletClick(int(row));
                        });
                    });
        }
        if (m_controller && !m_controller->readOnly()) {
            // A mouse's press on the marker dragged a few px moves the block (pagedrag.cpp).
            const at::EntityId dragSelf = m_selfId;
            std::move(bullet).id(u"bullet").onMouseDown(
                    at::MouseButton::Left, [dragSelf, row](const at::MouseDownEvent &e, at::Window &w, at::App &a) {
                        if (e.pointer != at::PointerKind::Mouse || e.modifiers.any())
                            return;
                        a.updateEntity<PageView>(dragSelf, [&](PageView &v, at::Context<PageView> &c) {
                            v.bulletPressed(int(row), e.position, c);
                        });
                        w.stopPropagation(); // no drag selection from the row under it
                    });
        }
        std::move(content).child(std::move(bullet));
    }
    at::Div column = at::div().flex1().minW(0);
    if (view == u"quote")
        std::move(column).borderL(3).borderColor(m_palette.guide).pl(12).textColor(m_palette.muted);
    if (view == u"callout") {
        // BlockPartCallout: a tinted, rounded frame with a bar of its type's colour on the left.
        const at::Color tint = m_calloutLook.tint(index.data(BlockListModel::CalloutTypeRole).toString());
        std::move(column)
                .bg(tint.fadedBy(m_calloutLook.fill))
                .rounded(m_calloutLook.radius)
                .borderL(3)
                .borderColor(tint)
                .pl(16)
                .pr(12)
                .py(12);
    }
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
                // (A tap on a misspelled word on touch UIs: its corrections; pagespell.cpp.)
                .onMouseUp(at::MouseButton::Left,
                           [selfId, row](const at::MouseUpEvent &e, at::Window &, at::App &a) {
                               a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &) {
                                   v.editedRowTapped(int(row), e);
                               });
                           })
                .child(std::move(content));
    }
    std::move(column).child(std::move(body));
    std::move(content).child(std::move(column));
    at::Div drawn = line().child(std::move(content));
    if (!m.documentMode && index.data(BlockListModel::HasChildrenRole).toBool())
        std::move(drawn).onHover([selfId, uid](bool hovered, at::Window &, at::App &a) {
            a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &c) {
                const quint64 now = hovered ? uid : (v.m_hoveredUid == uid ? 0 : v.m_hoveredUid);
                if (now != v.m_hoveredUid) {
                    v.m_hoveredUid = now;
                    c.notify();
                }
            });
        });
    if (onLongPressRow)
        // The touch selection mode, the finger then extending it or moving the selected blocks
        // (pagedrag.cpp).
        std::move(drawn).onLongPress([selfId, row](at::Point p, at::Window &w, at::App &a) {
            a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &c) {
                v.rowLongPressed(int(row), p, w, c);
            });
        });
    if (m_controller) {
        // A click or a tap edits the block at the character under it (links and the task's box
        // take their own clicks first; in reading mode the editor around says why it does not).
        // No node of its own for screen readers: the row's text is.
        if (!m_controller->readOnly())
            std::move(drawn).cursorText();
        std::move(drawn)
                .role(at::Role::None)
                .onClick([selfId, row](const at::ClickEvent &click, at::Window &w, at::App &a) {
                    a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &c) {
                        v.rowClicked(row, click, w, c);
                    });
                });
    }
    return drawn;
}

} // namespace brulion::editor::atr
