#include "pageitem.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "hostedrow.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGRendererInterface>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace brulion::editor {

using namespace Qt::StringLiterals;

namespace {

/// Rows laid out and kept beyond those near the view (least recently drawn dropped first).
constexpr int KeptRows = 256;

using Vertex = QSGGeometry::ColoredPoint2D;

bool sameStyle(const RenderedTextLayout::Style &a, const RenderedTextLayout::Style &b)
{
    return a.font == b.font && a.color == b.color && a.linkColor == b.linkColor && a.lineHeight == b.lineHeight
           && a.alignment == b.alignment && a.wrap == b.wrap && a.maximumLineCount == b.maximumLineCount
           && a.boxRadius == b.boxRadius && a.inset == b.inset;
}

Vertex vertex(QPointF p, const QColor &c)
{
    // Premultiplied (QSGVertexColorMaterial).
    const qreal a = c.alphaF();
    Vertex v;
    v.set(float(p.x()), float(p.y()), uchar(std::lround(c.redF() * a * 255)), uchar(std::lround(c.greenF() * a * 255)),
          uchar(std::lround(c.blueF() * a * 255)), uchar(std::lround(a * 255)));
    return v;
}

} // namespace

/// A row the item draws: its text laid out, its geometry and decorations (GUI thread).
struct PageItem::Row {
    quint64 uid = 0;
    bool stale = true; ///< the roles must be read again (and the row laid out)
    bool drawable = false;
    RenderedTextRef source;
    QString view;
    QString kind;
    QString prevKind;
    QString marker;
    int level = 0;
    int depth = 0;
    bool listItem = false;
    bool ordered = false;
    bool hasChildren = false;
    RenderedTextLayout text;
    RenderedTextLayout::Style style; ///< what `text` was laid out with
    std::unique_ptr<RenderedTextLayout> number;
    bool laidOut = false;
    qreal textWidth = -1;
    qreal height = 0;
    qreal textX = 0;
    qreal textY = 0;
    qreal numberX = 0;
    qreal numberY = 0;
    QList<Decoration> decorations;
    quint64 decorationVersion = 0; ///< PageItem::m_serial when the decorations changed
    quint64 lastFrame = 0;
};

/// A row shown by a delegate.
struct PageItem::Hosted {
    QPointer<QQuickItem> item;
    QQmlPropertyMap *roles = nullptr; ///< the delegate's model object (a child of the item)
    int row = -1;
    quint64 lastFrame = 0;
};

/// What the render thread keeps of a drawn row (updatePaintNode() only).
struct PaintedRow {
    QSGTransformNode *root = nullptr;
    QSGNode *decorations = nullptr;
    QSGTransformNode *textAt = nullptr;
    QSGNode *text = nullptr;
    QSGTransformNode *numberAt = nullptr;
    QSGNode *number = nullptr;
    quint64 decorationVersion = 0;
};

/// The item's root node: the rows' nodes by uid.
class PageRootNode : public QSGNode {
public:
    QHash<quint64, PaintedRow> rows;
};

PageItem::PageItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setClip(true);
    m_rows.setShownRole(BlockListModel::VisibleRole);
    m_rows.setEstimator([this](int row) { return estimate(row); });
    connect(&m_rows, &PageRows::rowsChanged, this, &PageItem::rowsChanged);
    connect(&m_rows, &PageRows::dataChanged, this, &PageItem::rowDataChanged);
}

PageItem::~PageItem()
{
    for (Hosted *h : std::as_const(m_hosted)) {
        if (h->item)
            delete h->item.data();
        delete h;
    }
    qDeleteAll(m_cache);
}

// ---- Properties ------------------------------------------------------------------------------

void PageItem::setModel(BlockListModel *model)
{
    if (model == m_model)
        return;
    for (Hosted *h : std::as_const(m_hosted)) {
        if (h->item)
            h->item->deleteLater();
        delete h;
    }
    m_hosted.clear();
    qDeleteAll(m_cache);
    m_cache.clear();
    m_model = model;
    m_rows.setModel(model);
    polish();
    update();
    Q_EMIT modelChanged();
}

void PageItem::setFlickable(QQuickItem *flickable)
{
    if (flickable == m_flickable)
        return;
    if (m_flickable)
        disconnect(m_flickable, nullptr, this, nullptr);
    m_flickable = flickable;
    if (flickable) {
        connect(flickable, SIGNAL(contentYChanged()), this, SLOT(flickMoved()));
        connect(flickable, SIGNAL(movementEnded()), this, SLOT(flickMovementEnded()));
    }
    polish();
    Q_EMIT flickableChanged();
}

void PageItem::setDelegate(QQmlComponent *delegate)
{
    if (delegate == m_delegate)
        return;
    for (Hosted *h : std::as_const(m_hosted)) {
        if (h->item)
            h->item->deleteLater();
        delete h;
    }
    m_hosted.clear();
    m_delegate = delegate;
    polish();
    Q_EMIT delegateChanged();
}

void PageItem::setHeader(QQuickItem *item)
{
    if (item == m_header)
        return;
    if (m_header)
        disconnect(m_header, nullptr, this, nullptr);
    m_header = item;
    if (item) {
        item->setParentItem(this);
        connect(item, &QQuickItem::heightChanged, this, &QQuickItem::polish);
    }
    polish();
    Q_EMIT headerChanged();
}

void PageItem::setFooter(QQuickItem *item)
{
    if (item == m_footer)
        return;
    if (m_footer)
        disconnect(m_footer, nullptr, this, nullptr);
    m_footer = item;
    if (item) {
        item->setParentItem(this);
        connect(item, &QQuickItem::heightChanged, this, &QQuickItem::polish);
    }
    polish();
    Q_EMIT footerChanged();
}

#define BRULION_PAGE_METRIC(setter, member, type)                                                                      \
    void PageItem::setter(type v)                                                                                      \
    {                                                                                                                  \
        if (member == v)                                                                                               \
            return;                                                                                                    \
        member = v;                                                                                                    \
        metricsTouched();                                                                                              \
    }

BRULION_PAGE_METRIC(setDocumentMode, m_doc, bool)
BRULION_PAGE_METRIC(setContentX, m_contentX, qreal)
BRULION_PAGE_METRIC(setContentWidth, m_contentWidth, qreal)
BRULION_PAGE_METRIC(setGutter, m_gutter, qreal)
BRULION_PAGE_METRIC(setIndentStep, m_indentStep, qreal)
BRULION_PAGE_METRIC(setBulletColumn, m_bulletColumn, qreal)
BRULION_PAGE_METRIC(setListIndent, m_listIndent, qreal)
BRULION_PAGE_METRIC(setRowPad, m_rowPad, qreal)
BRULION_PAGE_METRIC(setParagraphGap, m_paragraphGap, qreal)
BRULION_PAGE_METRIC(setListGap, m_listGap, qreal)
BRULION_PAGE_METRIC(setHeadingGapBelow, m_headingGapBelow, qreal)
BRULION_PAGE_METRIC(setHeadingGapsAbove, m_headingGapsAbove, const QList<qreal> &)
BRULION_PAGE_METRIC(setFontSizes, m_fontSizes, const QList<int> &)
BRULION_PAGE_METRIC(setLineHeights, m_lineHeights, const QList<int> &)
BRULION_PAGE_METRIC(setFirstLineCenters, m_firstLineCenters, const QList<qreal> &)
BRULION_PAGE_METRIC(setSpaces, m_spaces, const QList<qreal> &)
BRULION_PAGE_METRIC(setFontFamily, m_fontFamily, const QString &)
BRULION_PAGE_METRIC(setFontWeight, m_fontWeight, int)
BRULION_PAGE_METRIC(setHeadingWeight, m_headingWeight, int)
BRULION_PAGE_METRIC(setTextColor, m_textColor, const QColor &)
BRULION_PAGE_METRIC(setMutedColor, m_mutedColor, const QColor &)
BRULION_PAGE_METRIC(setGuideColor, m_guideColor, const QColor &)
BRULION_PAGE_METRIC(setLinkColor, m_linkColor, const QColor &)
BRULION_PAGE_METRIC(setBoxRadius, m_boxRadius, qreal)
BRULION_PAGE_METRIC(setTitleRow, m_titleRow, int)

#undef BRULION_PAGE_METRIC

void PageItem::metricsTouched()
{
    for (Row *row : std::as_const(m_cache))
        row->stale = true;
    // The heights are estimates until the rows are laid out with the new metrics.
    m_rows.view().markAllEstimated();
    m_reflow = true;
    polish();
    update();
    Q_EMIT metricsChanged();
}

#define BRULION_PAGE_FIND(setter, member, type)                                                                        \
    void PageItem::setter(type v)                                                                                      \
    {                                                                                                                  \
        if (member == v)                                                                                               \
            return;                                                                                                    \
        member = v;                                                                                                    \
        for (Row *row : std::as_const(m_cache))                                                                        \
            row->text.setOverlays(row->uid == m_hoveredUid ? m_hoveredLink : QString(), m_findQuery, m_findCase,       \
                                  m_findDiacritics, m_findWholeWord, m_findColor);                                     \
        update();                                                                                                      \
        Q_EMIT findChanged();                                                                                          \
    }

BRULION_PAGE_FIND(setFindQuery, m_findQuery, const QString &)
BRULION_PAGE_FIND(setFindMatchCase, m_findCase, bool)
BRULION_PAGE_FIND(setFindMatchDiacritics, m_findDiacritics, bool)
BRULION_PAGE_FIND(setFindWholeWord, m_findWholeWord, bool)
BRULION_PAGE_FIND(setFindColor, m_findColor, const QColor &)

#undef BRULION_PAGE_FIND

// ---- Metrics ---------------------------------------------------------------------------------

qreal PageItem::at(const QList<qreal> &list, int index, qreal fallback) const
{
    return index >= 0 && index < list.size() ? list.at(index) : fallback;
}

int PageItem::atInt(const QList<int> &list, int index, int fallback) const
{
    return index >= 0 && index < list.size() ? list.at(index) : fallback;
}

qreal PageItem::topPad(int row, int lvl, bool listItem, const QString &prevKind) const
{
    // BlockDelegate's outlineTopPad / docTopPad for the rows the item draws (no frames, no title
    // swap: those are hosted).
    const bool belowProperties = prevKind == u"frontmatter";
    if (!m_doc) {
        if (belowProperties)
            return lvl == 1 ? space(5) : space(4);
        if (lvl > 0 && row > 0)
            return lvl == 1 ? space(5) : lvl == 2 ? space(4) : space(3);
        return m_rowPad;
    }
    if (prevKind.isEmpty())
        return 0;
    const qreal gapAbove = at(m_headingGapsAbove, lvl, m_paragraphGap);
    if (belowProperties)
        return lvl > 0 && !listItem ? std::max(space(6), gapAbove) : space(6);
    if (lvl > 0 && !listItem)
        return gapAbove;
    if (prevKind == u"heading")
        return m_headingGapBelow;
    if (listItem && prevKind == u"listItem")
        return m_listGap;
    return m_paragraphGap;
}

qreal PageItem::bottomPad(int lvl, bool listItem) const
{
    if (m_doc)
        return listItem ? m_listGap / 2 : 0;
    return lvl > 0 ? space(1) : m_rowPad;
}

qreal PageItem::levelX(bool listItem, int depth) const
{
    return m_doc ? m_contentX + m_gutter + (listItem ? (depth + 1) * m_listIndent : 0)
                 : m_contentX + m_gutter + depth * m_indentStep;
}

qreal PageItem::markerX(int lvl) const
{
    return m_doc ? m_contentX + m_gutter + (lvl + 1) * m_listIndent - std::round(m_listIndent * 0.55)
                 : m_contentX + m_gutter + lvl * m_indentStep - m_bulletColumn / 2;
}

qreal PageItem::estimate(int row) const
{
    // From the document's block itself (the model's rows are its blocks, in order), not the
    // model's roles: a role computes and keeps the row's shape, for every row of the page.
    // (While the model replays a change the row may name another block: an estimate all the same.)
    if (!m_model || row < 0 || row >= m_model->document().count())
        return 24;
    const markdown::BlockEntry &block = m_model->document().at(row);
    const int lvl = block.kind == BlockKind::Heading ? std::clamp(block.headingLevel, 1, 6) : 0;
    const qreal lineHeight = atInt(m_lineHeights, lvl, 24);
    const qreal fontPx = std::max(8, atInt(m_fontSizes, lvl, 15));
    const qreal width =
            std::max<qreal>(40, m_contentWidth - m_gutter - block.depth * (m_doc ? m_listIndent : m_indentStep));
    // The font's average advance is about half its size (no layout, no shaping).
    const qsizetype perLine = std::max<qsizetype>(8, qsizetype(width / (fontPx * 0.5)));
    const qsizetype lines =
            std::max<qsizetype>({1, (block.body.size() + perLine - 1) / perLine, block.body.count(u'\n') + 1});
    const qreal pads = m_doc ? m_paragraphGap : 2 * m_rowPad;
    return std::ceil(lines * lineHeight + pads + (lvl > 0 ? at(m_headingGapsAbove, lvl, 0) : 0));
}

// ---- Rows ------------------------------------------------------------------------------------

bool PageItem::drawable(int row) const
{
    const Row *e = m_model ? m_cache.value(m_model->uidAt(row)) : nullptr;
    return e && !e->stale && e->drawable;
}

PageItem::Row *PageItem::layoutRow(int row)
{
    const quint64 uid = m_model->uidAt(row);
    Row *&e = m_cache[uid];
    if (!e) {
        e = new Row;
        e->uid = uid;
    }
    if (!e->stale)
        return e;
    e->stale = false;
    const QModelIndex i = m_model->index(row);
    const auto role = [&](int r) { return m_model->data(i, r); };
    e->view = role(BlockListModel::ViewRole).toString();
    e->kind = role(BlockListModel::KindRole).toString();
    e->prevKind = role(BlockListModel::PrevKindRole).toString();
    e->marker = role(BlockListModel::ListMarkerRole).toString();
    e->depth = role(BlockListModel::DepthRole).toInt();
    e->hasChildren = role(BlockListModel::HasChildrenRole).toBool();
    e->listItem = e->kind == u"listItem";
    e->ordered = e->listItem && !e->marker.isEmpty() && e->marker.at(0).isDigit();
    e->level = e->view == u"heading" ? std::clamp(role(BlockListModel::HeadingLevelRole).toInt(), 1, 6) : 0;
    // What the item draws: text rows without anything that needs a delegate (a styled tag's
    // tint is the delegate's: BlockPartTagTint).
    const bool textView = e->view == u"text" || e->view == u"heading";
    bool drawn = textView && !m_forceHosted.contains(uid) && role(BlockListModel::VisibleRole).toBool()
                 && role(BlockListModel::TaskStatusRole).toString().isEmpty()
                 && role(BlockListModel::TagTintRole).toString().isEmpty()
                 && !role(BlockListModel::IsEditingRole).toBool()
                 && role(BlockListModel::SelectionEdgeRole).toString().isEmpty()
                 && !role(BlockListModel::CollapsedRole).toBool()
                 && !(m_titleRow >= 0 && row >= m_titleRow - 1 && row <= m_titleRow + 1);
    if (drawn)
        drawn = role(BlockListModel::EmbedsRole).toList().isEmpty()
                && role(BlockListModel::TransclusionsRole).toList().isEmpty();
    RenderedTextRef source;
    if (drawn) {
        source = role(BlockListModel::RenderedTextRole).value<RenderedTextRef>();
        drawn = source.drawable();
    }
    e->drawable = drawn;
    if (!drawn) {
        e->laidOut = false;
        e->source = {};
        return e;
    }

    // The text (BlockText's style), laid out again only when the text, the metrics or the width
    // that breaks its lines changed.
    const int lvl = e->level;
    const qreal lineHeight = atInt(m_lineHeights, lvl, 24);
    e->textX = levelX(e->listItem, e->depth);
    const qreal width = std::max<qreal>(40, m_contentX + m_contentWidth - e->textX);
    RenderedTextLayout::Style style;
    style.font.setFamily(m_fontFamily);
    style.font.setPixelSize(atInt(m_fontSizes, lvl, 15));
    style.font.setWeight(QFont::Weight(lvl > 0 ? m_headingWeight : m_fontWeight));
    style.color = m_textColor;
    style.linkColor = m_linkColor;
    style.lineHeight = lineHeight;
    style.boxRadius = m_boxRadius;
    const bool sameText = e->laidOut && source == e->source && sameStyle(style, e->style);
    if (!sameText || !e->text.keepsLayoutAt(width)) {
        e->style = style;
        e->text.setStyle(style);
        e->text.layout(*source.d, width);
        e->text.setOverlays(uid == m_hoveredUid ? m_hoveredLink : QString(), m_findQuery, m_findCase, m_findDiacritics,
                            m_findWholeWord, m_findColor);
        ++m_layoutCount;
    }
    e->source = source;
    e->laidOut = true;
    e->textWidth = width;
    const qreal top = topPad(row, lvl, e->listItem, e->prevKind);
    e->textY = top;
    const qreal shown = lineHeight > 0
                                ? std::max<qreal>(1, std::round(e->text.laidOutHeight() / lineHeight)) * lineHeight
                                : e->text.laidOutHeight();
    const qreal textHeight = std::max(lineHeight, shown);
    e->height = std::ceil(top + textHeight + bottomPad(lvl, e->listItem));

    // Decorations (BlockDelegate's guide lines, bullet, number).
    e->decorations.clear();
    const qreal firstLineCenter = top + at(m_firstLineCenters, lvl, lineHeight / 2);
    const int guides = !m_doc || e->listItem ? e->depth : 0;
    for (int k = 0; k < guides; ++k)
        e->decorations.append({QRectF(std::round(markerX(k)), 0, 1, e->height), m_guideColor, false});
    const qreal bulletX = markerX(m_doc && !e->listItem ? -1 : e->depth);
    if (e->listItem && e->hasChildren) {
        const qreal y = std::round(firstLineCenter + 9);
        e->decorations.append(
                {QRectF(std::round(bulletX), y, 1, std::max<qreal>(0, e->height - y)), m_guideColor, false});
    }
    if (e->listItem && !(m_doc && e->ordered))
        e->decorations.append({QRectF(bulletX - 3, firstLineCenter - 3, 6, 6), m_mutedColor, true});
    if (m_doc && e->ordered) {
        if (!e->number)
            e->number = std::make_unique<RenderedTextLayout>();
        RenderedTextLayout::Style ns = style;
        ns.font.setPixelSize(atInt(m_fontSizes, 0, 15));
        ns.font.setWeight(QFont::Weight(m_fontWeight));
        ns.font.setFeature(QFont::Tag("tnum"), 1);
        ns.color = m_mutedColor;
        ns.lineHeight = atInt(m_lineHeights, 0, 24);
        ns.wrap = false;
        e->number->setStyle(ns);
        markdown::RenderedText marker;
        marker.text = e->marker;
        marker.valid = true;
        marker.drawable = true;
        e->number->layout(marker, 1e6);
        const qreal gap = std::round(m_listIndent * 0.25);
        e->numberX = e->textX - std::ceil(e->number->naturalWidth()) - gap;
        e->numberY = firstLineCenter - at(m_firstLineCenters, 0, ns.lineHeight / 2);
    } else {
        e->number.reset();
    }
    e->decorationVersion = ++m_serial;
    return e;
}

QQuickItem *PageItem::hostedItem(int row)
{
    const quint64 uid = m_model->uidAt(row);
    if (Hosted *h = m_hosted.value(uid)) {
        if (h->item) {
            if (h->row != row) {
                h->row = row;
                hostedrow::setIndex(h->item, row);
            }
            return h->item;
        }
        delete h;
        m_hosted.remove(uid);
    }
    // The delegate with the row's roles, its index and a model object of its own (hostedrow.h).
    QQmlPropertyMap *roles = nullptr;
    QQuickItem *item = hostedrow::create(m_delegate, this, m_model, row, &roles);
    if (!item)
        return nullptr;
    auto *h = new Hosted;
    h->item = item;
    h->roles = roles;
    h->row = row;
    m_hosted.insert(uid, h);
    connect(item, &QQuickItem::heightChanged, this, &QQuickItem::polish);
    return item;
}

void PageItem::releaseHosted(quint64 uid)
{
    Hosted *h = m_hosted.take(uid);
    if (!h)
        return;
    hostedrow::release(h->item);
    delete h;
}

void PageItem::rowsChanged()
{
    // Hosted delegates follow their rows (QQmlDelegateModel's index); removed rows go.
    if (m_model) {
        for (auto it = m_hosted.begin(); it != m_hosted.end();) {
            Hosted *h = it.value();
            const int row = m_model->rowOfUid(it.key());
            if (row < 0 || !h->item) {
                hostedrow::release(h->item);
                delete h;
                it = m_hosted.erase(it);
                continue;
            }
            if (row != h->row) {
                h->row = row;
                hostedrow::setIndex(h->item, row);
            }
            ++it;
        }
    }
    // (Entries of rows that are gone are never drawn again: the oldest go first.)
    polish();
    update();
    Q_EMIT contentHeightChanged();
}

void PageItem::rowDataChanged(int first, int last, const QList<int> &roles)
{
    if (!m_model)
        return;
    // The rows below an edit are told their new line numbers: nothing the item draws.
    const bool linesOnly = !roles.isEmpty() && std::all_of(roles.cbegin(), roles.cend(), [](int role) {
        return role == BlockListModel::LineRole;
    });
    for (int row = std::max(0, first); row <= last && row < m_model->count(); ++row) {
        const quint64 uid = m_model->uidAt(row);
        if (!linesOnly) {
            if (Row *e = m_cache.value(uid))
                e->stale = true;
            // The row's height is an estimate until it is laid out again (its text may have
            // changed): kept as the estimate, so nothing moves before the next frame.
            if (m_rows.heights().height(PageRows::treeRow(row)) > 0)
                m_rows.view().setRowEstimated(PageRows::treeRow(row));
        }
        Hosted *h = m_hosted.value(uid);
        if (!h || !h->item)
            continue;
        hostedrow::update(h->item, h->roles, m_model, row, roles);
    }
    polish();
    update();
}

// ---- Layout ----------------------------------------------------------------------------------

void PageItem::updatePolish()
{
    ScrollAnchor &view = m_rows.view();
    view.settle();
    view.setViewportHeight(height());
    if (m_header && std::abs(m_header->height() - m_rows.heights().height(0)) > 0.01)
        m_rows.setHeaderHeight(m_header->height());
    if (m_footer && std::abs(m_footer->height() - m_rows.heights().height(m_rows.footerRow())) > 0.01)
        m_rows.setFooterHeight(m_footer->height());
    ++m_frame;
    const qreal margin = height() / 2;
    QSet<quint64> hostedNow;
    if (m_model && m_positionUid) {
        // "Scroll to block": the row measured first, then placed with its own height.
        const int row = m_model->rowOfUid(m_positionUid);
        if (row >= 0) {
            const int t = PageRows::treeRow(row);
            if (m_rows.heights().height(t) > 0 || !m_rows.heights().isMeasured(t)) {
                Row *e = layoutRow(row);
                const QQuickItem *item = e->drawable ? nullptr : hostedItem(row);
                const qreal h = e->drawable ? e->height : item ? item->height() : m_rows.heights().height(t);
                m_rows.setRowHeight(t, h, e->drawable || item != nullptr);
            }
            view.positionAtRow(t, ScrollAnchor::Position(m_positionMode));
        }
        m_positionUid = 0;
    }
    if (m_model) {
        // Measure what the view and its margins show; measuring moves rows (the anchor keeps the
        // screen), so again until nothing new is measured.
        for (int pass = 0; pass < 6; ++pass) {
            const auto [first, last] = view.rowsShown(margin, margin);
            bool changed = false;
            for (int t = std::max(0, first); t >= 0 && t <= last; ++t) {
                const int row = m_rows.modelRow(t);
                if (row < 0)
                    continue;
                const RowHeightTree &tree = m_rows.heights();
                if (tree.height(t) <= 0 && tree.isMeasured(t))
                    continue; // hidden
                Row *e = layoutRow(row);
                qreal h = 0;
                if (e->drawable) {
                    e->lastFrame = m_frame;
                    if (m_hosted.contains(e->uid))
                        releaseHosted(e->uid);
                    h = e->height;
                } else {
                    QQuickItem *item = hostedItem(row);
                    hostedNow.insert(e->uid);
                    if (Hosted *hosted = m_hosted.value(e->uid))
                        hosted->lastFrame = m_frame;
                    h = item ? item->height() : estimate(row);
                }
                if (!tree.isMeasured(t) || std::abs(tree.height(t) - h) > 0.01) {
                    m_rows.setRowHeight(t, h, true,
                                        m_reflow ? ScrollAnchor::Resize::Reflow : ScrollAnchor::Resize::Content);
                    changed = true;
                }
            }
            if (!changed)
                break;
        }
    }
    m_reflow = false;

    // Hosted rows away from the view go, unless edited or holding the keyboard. Those within one
    // and a half screens stay: a new width or a scroll back and forth over the margin's edge
    // would make and drop the same delegates again (a delegate is the costly part of a row).
    QQuickItem *focus = window() ? window()->activeFocusItem() : nullptr;
    const auto [keepFirst, keepLast] = view.rowsShown(height() * 1.5, height() * 1.5);
    for (auto it = m_hosted.begin(); it != m_hosted.end();) {
        Hosted *h = it.value();
        const int t = PageRows::treeRow(h->row);
        const bool near = t >= keepFirst && t <= keepLast && m_rows.heights().height(t) > 0;
        const bool keep = h->item
                          && (hostedNow.contains(it.key()) || near || h->item->property("isEditing").toBool()
                              || (focus && (focus == h->item || h->item->isAncestorOf(focus))));
        if (keep) {
            ++it;
            continue;
        }
        hostedrow::release(h->item);
        delete h;
        it = m_hosted.erase(it);
    }
    // The cache keeps the rows near the view and the most recently drawn others.
    const int bound = KeptRows + 3 * std::max(1, int(height() / 20));
    if (m_cache.size() > bound) {
        QList<QPair<quint64, quint64>> byAge; // last frame, uid
        for (const Row *e : std::as_const(m_cache))
            byAge.append({e->lastFrame, e->uid});
        std::sort(byAge.begin(), byAge.end());
        for (qsizetype k = 0; k < byAge.size() - bound; ++k) {
            if (byAge.at(k).first == m_frame)
                break;
            delete m_cache.take(byAge.at(k).second);
        }
    }

    // Place the hosted rows, the header and the footer; what the frame shows.
    const qreal y = view.y();
    for (Hosted *h : std::as_const(m_hosted)) {
        if (!h->item)
            continue;
        const int t = PageRows::treeRow(h->row);
        h->item->setY(m_rows.heights().offsetOf(t) - y);
        h->item->setX(0);
        h->item->setVisible(m_rows.heights().height(t) > 0 || h->item->height() > 0);
    }
    if (m_header)
        m_header->setY(-y);
    if (m_footer)
        m_footer->setY(m_rows.heights().offsetOf(m_rows.footerRow()) - y);
    const auto [first, last] = view.rowsShown();
    int firstShown = -1;
    int lastShown = -1;
    int drawn = 0;
    for (int t = std::max(0, first); t >= 0 && t <= last; ++t) {
        const int row = m_rows.modelRow(t);
        if (row < 0 || m_rows.heights().height(t) <= 0)
            continue;
        if (firstShown < 0)
            firstShown = row;
        lastShown = row;
        if (drawable(row))
            ++drawn;
    }
    m_firstShown = firstShown;
    m_lastShown = lastShown;
    m_drawnCount = drawn;
    syncFlickable();
    update();
    Q_EMIT shownChanged();
    Q_EMIT viewYChanged();
    Q_EMIT contentHeightChanged();
}

void PageItem::forceLayout()
{
    updatePolish();
}

void PageItem::syncFlickable()
{
    if (!m_flickable)
        return;
    const qreal total = contentHeight();
    const qreal y = viewY();
    const qreal flickY = m_flickable->property("contentY").toReal();
    const qreal flickHeight = m_flickable->property("contentHeight").toReal();
    m_writing = true;
    if (m_flickable->property("moving").toBool()) {
        // A write of contentY stops the flick: the difference waits in the slack.
        m_slack = y - flickY;
        const qreal h = std::max(height(), total - m_slack);
        if (std::abs(h - flickHeight) > 0.01)
            m_flickable->setProperty("contentHeight", h);
    } else {
        m_slack = 0;
        // Never a contentY past the end of contentHeight: the Flickable would animate back.
        if (total >= flickHeight && std::abs(total - flickHeight) > 0.01)
            m_flickable->setProperty("contentHeight", total);
        if (std::abs(y - flickY) > 0.01)
            m_flickable->setProperty("contentY", y);
        if (total < flickHeight && std::abs(total - flickHeight) > 0.01)
            m_flickable->setProperty("contentHeight", total);
    }
    m_writing = false;
}

void PageItem::flickMoved()
{
    if (m_writing || !m_flickable)
        return;
    m_rows.view().scrollTo(m_flickable->property("contentY").toReal() + m_slack);
    polish();
    Q_EMIT viewYChanged();
}

void PageItem::flickMovementEnded()
{
    if (std::abs(m_slack) > 0.01)
        syncFlickable();
}

void PageItem::positionAtRow(int row, int mode)
{
    if (!m_model || row < 0 || row >= m_model->count())
        return;
    if (m_flickable && m_flickable->property("moving").toBool())
        QMetaObject::invokeMethod(m_flickable, "cancelFlick");
    ScrollAnchor::Position position = ScrollAnchor::Position::Contain;
    switch (mode) {
    case 0: position = ScrollAnchor::Position::Beginning; break;
    case 1: position = ScrollAnchor::Position::Center; break;
    case 2: position = ScrollAnchor::Position::End; break;
    default: break;
    }
    // Placed now (the next frames' rows are measured around it) and again in the polish once the
    // row itself is measured: its offset follows from its real height, not the estimate.
    m_rows.view().positionAtRow(PageRows::treeRow(row), position);
    m_positionUid = m_model->uidAt(row);
    m_positionMode = int(position);
    polish();
}

void PageItem::positionAtBeginning()
{
    m_rows.view().scrollTo(0);
    polish();
}

void PageItem::ensureVisible(qreal y, qreal h, qreal margin)
{
    const qreal bottom = height() - margin;
    qreal dy = 0;
    if (y < margin)
        dy = y - margin;
    else if (y + h > bottom)
        dy = std::min(y + h - bottom, y - margin); // a range taller than the view: its top
    if (std::abs(dy) < 0.5)
        return;
    if (m_flickable && m_flickable->property("moving").toBool())
        QMetaObject::invokeMethod(m_flickable, "cancelFlick");
    m_rows.view().scrollBy(dy);
    polish();
}

void PageItem::ensureItemVisible(QQuickItem *item, const QRectF &rect, qreal margin)
{
    if (!item)
        return;
    const QRectF r = item->mapRectToItem(this, rect);
    ensureVisible(r.y(), r.height(), margin);
}

void PageItem::hostRow(int row)
{
    if (!m_model || row < 0 || row >= m_model->count())
        return;
    const quint64 uid = m_model->uidAt(row);
    m_forceHosted.insert(uid);
    if (Row *e = m_cache.value(uid))
        e->stale = true;
    polish();
}

void PageItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        polish();
}

void PageItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) {
        for (Row *row : std::as_const(m_cache)) {
            row->text.markDirty();
            if (row->number)
                row->number->markDirty();
            row->decorationVersion = ++m_serial;
        }
    }
    QQuickItem::itemChange(change, value);
}

// ---- Questions -------------------------------------------------------------------------------

int PageItem::rowAt(qreal, qreal y) const
{
    const int t = m_rows.heights().rowAt(viewY() + y);
    if (t < 0 || viewY() + y < 0 || viewY() + y >= contentHeight())
        return -1;
    return m_rows.modelRow(t);
}

QRectF PageItem::rowRect(int row) const
{
    if (row < 0 || row >= m_rows.count())
        return {};
    const int t = PageRows::treeRow(row);
    return QRectF(0, m_rows.heights().offsetOf(t) - viewY(), width(), m_rows.heights().height(t));
}

QQuickItem *PageItem::rowItem(int row) const
{
    if (!m_model || row < 0 || row >= m_model->count())
        return nullptr;
    const Hosted *h = m_hosted.value(m_model->uidAt(row));
    return h ? h->item.data() : nullptr;
}

QString PageItem::linkAt(qreal x, qreal y) const
{
    const int row = rowAt(x, y);
    if (row < 0 || !drawable(row))
        return {};
    const Row *e = m_cache.value(m_model->uidAt(row));
    const QPointF p = textPoint(row, x, y);
    return e->text.linkAt(p.x(), p.y());
}

QPointF PageItem::textPoint(int row, qreal x, qreal y) const
{
    const QRectF r = rowRect(row);
    const Row *e = m_model && row >= 0 && row < m_model->count() ? m_cache.value(m_model->uidAt(row)) : nullptr;
    if (!e || !e->drawable)
        return {x, y - r.y()};
    return {x - e->textX, y - r.y() - e->textY};
}

QString PageItem::rowText(int row) const
{
    if (row < 0 || !m_model || row >= m_model->count() || !drawable(row))
        return {};
    return m_cache.value(m_model->uidAt(row))->text.plainText();
}

int PageItem::rowLineCount(int row) const
{
    if (row < 0 || !m_model || row >= m_model->count() || !drawable(row))
        return 0;
    return m_cache.value(m_model->uidAt(row))->text.lines();
}

void PageItem::setPointer(qreal x, qreal y)
{
    QString link;
    quint64 uid = 0;
    if (x >= 0) {
        const int row = rowAt(x, y);
        link = linkAt(x, y);
        uid = !link.isEmpty() && m_model ? m_model->uidAt(row) : 0;
    }
    if (link == m_hoveredLink && uid == m_hoveredUid)
        return;
    const quint64 before = m_hoveredUid;
    m_hoveredLink = link;
    m_hoveredUid = uid;
    for (const quint64 changed : {before, uid}) {
        if (Row *e = m_cache.value(changed))
            e->text.setOverlays(changed == uid ? link : QString(), m_findQuery, m_findCase, m_findDiacritics,
                                m_findWholeWord, m_findColor);
    }
    update();
    Q_EMIT hoveredLinkChanged();
}

// ---- Drawing ---------------------------------------------------------------------------------

namespace {

/// The decorations of a row as one node: geometry (rectangles, round dots) with a GPU renderer,
/// one painted image with the software renderer.
QSGNode *decorationNode(QSGNode *old, const QList<QRectF> &rects, const QList<QColor> &colors, const QList<bool> &round,
                        QQuickWindow *win, bool software)
{
    if (software) {
        auto *node = old ? static_cast<QSGImageNode *>(old) : win->createImageNode();
        QRectF bounds;
        for (const QRectF &r : rects)
            bounds = bounds.united(r);
        bounds = bounds.isNull() ? QRectF(0, 0, 1, 1) : QRectF(bounds.adjusted(-1, -1, 1, 1).toAlignedRect());
        const qreal dpr = win->effectiveDevicePixelRatio();
        QImage canvas((bounds.size() * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
        canvas.setDevicePixelRatio(dpr);
        canvas.fill(Qt::transparent);
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.translate(-bounds.topLeft());
        for (qsizetype k = 0; k < rects.size(); ++k) {
            painter.setBrush(colors.at(k));
            if (round.at(k))
                painter.drawEllipse(rects.at(k));
            else
                painter.fillRect(rects.at(k), colors.at(k));
        }
        painter.end();
        node->setTexture(win->createTextureFromImage(canvas, QQuickWindow::TextureHasAlphaChannel));
        node->setOwnsTexture(true);
        node->setRect(bounds);
        return node;
    }
    auto *node = static_cast<QSGGeometryNode *>(old);
    if (!node) {
        node = new QSGGeometryNode;
        node->setGeometry(new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0));
        node->geometry()->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setMaterial(new QSGVertexColorMaterial);
        node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
    }
    QList<Vertex> v;
    for (qsizetype k = 0; k < rects.size(); ++k) {
        const QRectF &r = rects.at(k);
        const QColor &c = colors.at(k);
        if (round.at(k)) {
            // A disc: a fan of 24 triangles inside, a one-pixel ring fading out around it (as
            // Rectangle's antialiasing: a 6 px bullet is round, not a polygon).
            const QPointF centre = r.center();
            const qreal inner = std::max<qreal>(0, r.width() / 2 - 0.5);
            const qreal outer = r.width() / 2 + 0.5;
            const QColor clear(c.red(), c.green(), c.blue(), 0);
            constexpr int steps = 24;
            for (int s = 0; s < steps; ++s) {
                const qreal a0 = 2 * std::numbers::pi * s / steps;
                const qreal a1 = 2 * std::numbers::pi * (s + 1) / steps;
                const QPointF d0(std::cos(a0), std::sin(a0));
                const QPointF d1(std::cos(a1), std::sin(a1));
                const Vertex i0 = vertex(centre + d0 * inner, c), i1 = vertex(centre + d1 * inner, c);
                const Vertex o0 = vertex(centre + d0 * outer, clear), o1 = vertex(centre + d1 * outer, clear);
                v.append({vertex(centre, c), i0, i1, i0, o0, o1, i0, o1, i1});
            }
        } else {
            const Vertex a = vertex(r.topLeft(), c), b = vertex(r.topRight(), c), d = vertex(r.bottomLeft(), c),
                         e = vertex(r.bottomRight(), c);
            v.append({a, b, e, a, e, d});
        }
    }
    if (v.isEmpty()) // (a node without vertices is left out of its batch for good: one invisible triangle)
        v = {vertex({}, Qt::transparent), vertex({}, Qt::transparent), vertex({}, Qt::transparent)};
    QSGGeometry *g = node->geometry();
    g->allocate(int(v.size()));
    std::memcpy(g->vertexData(), v.constData(), v.size() * sizeof(Vertex));
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}

} // namespace

QSGNode *PageItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    QQuickWindow *win = window();
    if (!win || !m_model) {
        delete old;
        return nullptr;
    }
    const bool software = win->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
    auto *root = static_cast<PageRootNode *>(old);
    if (!root)
        root = new PageRootNode;
    const qreal y = viewY();
    const auto [first, last] = m_rows.view().rowsShown();
    QSet<quint64> painted;
    for (int t = std::max(0, first); t >= 0 && t <= last; ++t) {
        const int row = m_rows.modelRow(t);
        if (row < 0 || m_rows.heights().height(t) <= 0 || !drawable(row))
            continue;
        Row *e = m_cache.value(m_model->uidAt(row));
        if (!e || !e->laidOut)
            continue;
        painted.insert(e->uid);
        PaintedRow &p = root->rows[e->uid];
        if (!p.root) {
            p.root = new QSGTransformNode;
            root->appendChildNode(p.root);
            p.textAt = new QSGTransformNode;
            p.root->appendChildNode(p.textAt);
        }
        QTransform at;
        at.translate(0, std::round(m_rows.heights().offsetOf(t) - y));
        p.root->setMatrix(at);
        if (p.decorationVersion != e->decorationVersion) {
            QList<QRectF> rects;
            QList<QColor> colors;
            QList<bool> round;
            for (const Decoration &deco : std::as_const(e->decorations)) {
                rects.append(deco.rect);
                colors.append(deco.color);
                round.append(deco.round);
            }
            QSGNode *node = decorationNode(p.decorations, rects, colors, round, win, software);
            if (node != p.decorations) {
                p.root->prependChildNode(node);
                p.decorations = node;
            }
            p.decorationVersion = e->decorationVersion;
        }
        QTransform textAt;
        textAt.translate(e->textX, e->textY);
        p.textAt->setMatrix(textAt);
        QSGNode *text = e->text.updateNode(p.text, win, smooth());
        if (text != p.text) {
            // (A node replaced was deleted by updateNode(), which takes it out of its parent.)
            if (text)
                p.textAt->appendChildNode(text);
            p.text = text;
        }
        if (e->number) {
            if (!p.numberAt) {
                p.numberAt = new QSGTransformNode;
                p.root->appendChildNode(p.numberAt);
            }
            QTransform numberAt;
            numberAt.translate(e->numberX, e->numberY);
            p.numberAt->setMatrix(numberAt);
            QSGNode *number = e->number->updateNode(p.number, win, smooth());
            if (number != p.number) {
                if (number)
                    p.numberAt->appendChildNode(number);
                p.number = number;
            }
        } else if (p.numberAt) {
            p.root->removeChildNode(p.numberAt);
            delete p.numberAt; // with its child
            p.numberAt = nullptr;
            p.number = nullptr;
        }
    }
    // Rows no longer drawn: their nodes go.
    for (auto it = root->rows.begin(); it != root->rows.end();) {
        if (painted.contains(it.key())) {
            ++it;
            continue;
        }
        root->removeChildNode(it->root);
        delete it->root; // with its children
        it = root->rows.erase(it);
    }
    return root;
}

} // namespace brulion::editor
