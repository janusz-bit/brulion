#include "journaldayitem.h"

#include "blocklistmodel.h"
#include "editorhost.h"

#include <QHash>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGRectangleNode>
#include <QSGTexture>

#include <algorithm>
#include <cmath>
#include <utility>

namespace brulion::editor {

using namespace Qt::StringLiterals;

namespace {

// BlockDelegate's view groups (textView, framed).
bool isTextView(const QString &view)
{
    return view != u"code" && view != u"calc" && view != u"query" && view != u"math" && view != u"mermaid"
           && view != u"chart" && view != u"table" && view != u"quote" && view != u"callout" && view != u"rule"
           && view != u"frontmatter" && view != u"view";
}

bool isFramed(const QString &view)
{
    return view == u"code" || view == u"calc" || view == u"query" || view == u"math" || view == u"mermaid"
           || view == u"chart" || view == u"table" || view == u"callout" || view == u"frontmatter" || view == u"view";
}

/// The views the item draws: a paragraph's, a list item's or a heading's text, a quote, a rule.
bool drawnView(const QString &view)
{
    return view == u"text" || view == u"heading" || view == u"quote" || view == u"rule";
}

QString argb(const QColor &c)
{
    return QString::number(c.rgba(), 16);
}

/// The sprites' pictures by key and device pixel ratio (made on the render thread of any window).
struct SpriteCache {
    QMutex mutex;
    QHash<QString, QImage> images;
};

SpriteCache &spriteCache()
{
    static SpriteCache cache;
    return cache;
}

QImage canvas(qreal size, qreal dpr)
{
    const int px = std::max(1, int(std::ceil(size * dpr)));
    QImage image(px, px, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    return image;
}

/// A Lucide icon (:/icons/<name>.svg, as BIcon: rasterised at size × dpr, recoloured), turned by
/// `rotation` degrees about its centre.
QImage iconImage(const QString &name, qreal size, const QColor &color, qreal rotation, qreal dpr)
{
    QImageReader reader(u":/icons/"_s + name + u".svg"_s);
    const int px = std::max(1, int(std::round(size * dpr)));
    reader.setScaledSize(QSize(px, px));
    QImage icon = reader.read();
    if (icon.isNull())
        return {};
    icon = icon.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    {
        QPainter painter(&icon);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(icon.rect(), color);
    }
    if (rotation != 0) {
        QImage turned(icon.size(), QImage::Format_ARGB32_Premultiplied);
        turned.fill(Qt::transparent);
        QPainter painter(&turned);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.translate(turned.width() / 2.0, turned.height() / 2.0);
        painter.rotate(rotation);
        painter.translate(-icon.width() / 2.0, -icon.height() / 2.0);
        painter.drawImage(0, 0, icon);
        painter.end();
        icon = turned;
    }
    icon.setDevicePixelRatio(dpr);
    return icon;
}

QStringList splitKey(const QString &key)
{
    return key.split(u'|');
}

QColor keyColor(const QString &part)
{
    bool ok = false;
    const QRgb rgba = QRgb(part.toUInt(&ok, 16));
    return ok ? QColor::fromRgba(rgba) : QColor(Qt::transparent);
}

QImage drawSprite(const QString &key, qreal dpr)
{
    const QStringList p = splitKey(key);
    if (p.isEmpty())
        return {};
    if (p[0] == u"dot" && p.size() == 3) {
        // A round Rectangle (radius = width / 2: antialiased).
        const qreal d = p[1].toDouble();
        QImage image = canvas(d, dpr);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(keyColor(p[2]));
        painter.drawEllipse(QRectF(0, 0, d, d));
        return image;
    }
    if (p[0] == u"icon" && p.size() == 5)
        return iconImage(p[1], p[2].toDouble(), keyColor(p[3]), p[4].toDouble(), dpr);
    if (p[0] == u"box" && p.size() == 9) {
        // TaskBox's box: |box|status|size|radius|fill|border|borderWidth|mark|mark2|.
        const QString status = p[1];
        const qreal size = p[2].toDouble();
        const qreal radius = p[3].toDouble();
        const QColor fill = keyColor(p[4]);
        const QColor border = keyColor(p[5]);
        const qreal borderWidth = p[6].toDouble();
        const QColor mark = keyColor(p[7]);
        const QColor mark2 = keyColor(p[8]);
        QImage image = canvas(size, dpr);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box(0, 0, size, size);
        painter.setPen(Qt::NoPen);
        // A Rectangle's border lies inside its bounds: the fill inside the border, the border as
        // the ring between the outer and the inner rounded rectangle.
        if (borderWidth > 0) {
            QPainterPath ring;
            ring.addRoundedRect(box, radius, radius);
            const QRectF inner = box.adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth);
            QPainterPath hole;
            const qreal innerRadius = std::max<qreal>(0, radius - borderWidth);
            hole.addRoundedRect(inner, innerRadius, innerRadius);
            if (fill.alpha() > 0) {
                painter.setBrush(fill);
                painter.drawPath(hole);
            }
            painter.setBrush(border);
            painter.drawPath(ring.subtracted(hole));
        } else if (fill.alpha() > 0) {
            painter.setBrush(fill);
            painter.drawRoundedRect(box, radius, radius);
        }
        if (status == u"doing") {
            // The lower half filled (anchored 3 px in, radius 1).
            const qreal h = (size - 6) / 2;
            painter.setBrush(mark);
            painter.drawRoundedRect(QRectF(3, size - 3 - h, size - 6, h), 1, 1);
        } else if (status == u"cancelled") {
            painter.setBrush(mark);
            painter.drawRect(QRectF(3, (size - 1.5) / 2, size - 6, 1.5));
        } else if (status == u"done" || status == u"deferred") {
            const qreal s = size - 4;
            const QImage icon = iconImage(status == u"done" ? u"check"_s : u"chevron-right"_s, s,
                                          status == u"done" ? mark : mark2, 0, dpr);
            if (!icon.isNull())
                painter.drawImage(QRectF((size - s) / 2, (size - s) / 2, s, s), icon);
        }
        return image;
    }
    return {};
}

/// The scene graph node of a day: its decorations as children, the sprites' textures kept from
/// one build of the node to the next.
class DayNode : public QSGNode {
public:
    ~DayNode() override { qDeleteAll(textures); }
    QHash<QString, QSGTexture *> textures;
    qreal dpr = 1;
};

} // namespace

QImage JournalDayItem::spriteImage(const QString &key, qreal dpr)
{
    SpriteCache &cache = spriteCache();
    const QString full = key + u'@' + QString::number(dpr);
    {
        QMutexLocker lock(&cache.mutex);
        const auto it = cache.images.constFind(full);
        if (it != cache.images.cend())
            return *it;
    }
    QImage image = drawSprite(key, dpr);
    QMutexLocker lock(&cache.mutex);
    if (cache.images.size() > 512)
        cache.images.clear();
    cache.images.insert(full, image);
    return image;
}

JournalDayItem::JournalDayItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    connect(this, &JournalDayItem::metricsChanged, this, &JournalDayItem::layout);
    connect(this, &JournalDayItem::colorsChanged, this, [this] {
        for (Row &row : m_rows) {
            if (row.textItem)
                applyTextLook(row, row.textItem);
            if (row.numberItem)
                row.numberItem->setColor(row.taskStatus == u"done" || row.taskStatus == u"cancelled" ? m_faintColor
                                                                                                     : m_mutedColor);
        }
        decorate();
    });
    connect(this, &JournalDayItem::findChanged, this, [this] {
        for (Row &row : m_rows) {
            if (row.textItem)
                applyFind(row.textItem);
        }
    });
}

JournalDayItem::~JournalDayItem() = default;

void JournalDayItem::componentComplete()
{
    QQuickItem::componentComplete();
    reload();
}

void JournalDayItem::setModel(BlockListModel *model)
{
    if (m_model == model)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model) {
        // A day that is not edited changes rarely (another device's edit, an index change rendering
        // a link again, a fold or a task box clicked here): every change reads the rows again.
        connect(m_model, &QAbstractItemModel::dataChanged, this, &JournalDayItem::reload);
        connect(m_model, &QAbstractItemModel::rowsInserted, this, &JournalDayItem::reload);
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, &JournalDayItem::reload);
        connect(m_model, &QAbstractItemModel::rowsMoved, this, &JournalDayItem::reload);
        connect(m_model, &QAbstractItemModel::modelReset, this, &JournalDayItem::reload);
        connect(m_model, &QAbstractItemModel::layoutChanged, this, &JournalDayItem::reload);
    }
    reload();
    Q_EMIT modelChanged();
}

void JournalDayItem::setBlocked(bool blocked)
{
    if (blocked == m_blocked)
        return;
    m_blocked = blocked;
    Q_EMIT blockedChanged();
    reload();
}

void JournalDayItem::setHoveredLink(const QString &link)
{
    if (link == m_hoveredLink)
        return;
    m_hoveredLink = link;
    for (qsizetype i = 0; i < qsizetype(m_rows.size()); ++i) {
        if (m_rows[i].textItem)
            m_rows[i].textItem->setHoveredLink(i == m_hoverRow ? link : QString());
    }
    Q_EMIT hoverChanged();
}

int JournalDayItem::shownRows() const
{
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(), [](const Row &r) { return r.shown; }));
}

int JournalDayItem::textLayouts() const
{
    int n = 0;
    for (const Row &row : m_rows) {
        if (row.textItem)
            n += row.textItem->layoutCount();
        if (row.numberItem)
            n += row.numberItem->layoutCount();
    }
    return n;
}

void JournalDayItem::updateDrawable(bool drawable)
{
    if (drawable == m_drawable)
        return;
    m_drawable = drawable;
    Q_EMIT drawableChanged();
}

void JournalDayItem::discardText(RenderedTextItem *&item)
{
    if (!item)
        return;
    item->setVisible(false);
    item->setParentItem(nullptr);
    item->deleteLater();
    item = nullptr;
}

void JournalDayItem::releaseTexts()
{
    for (Row &row : m_rows) {
        discardText(row.textItem);
        discardText(row.numberItem);
    }
}

void JournalDayItem::reload()
{
    if (!isComponentComplete() || m_reloading)
        return;
    m_reloading = true;
    const int count = m_model ? m_model->rowCount() : 0;
    // The text items stay with their row's place (a changed text lays only itself out again).
    std::vector<Row> rows(size_t(std::max(0, count)));
    for (int i = 0; i < count && i < int(m_rows.size()); ++i) {
        rows[size_t(i)].textItem = std::exchange(m_rows[size_t(i)].textItem, nullptr);
        rows[size_t(i)].numberItem = std::exchange(m_rows[size_t(i)].numberItem, nullptr);
    }
    releaseTexts(); // rows that are gone
    m_rows = std::move(rows);
    bool drawable = !m_blocked;
    for (int i = 0; i < count && drawable; ++i) {
        Row &row = m_rows[size_t(i)];
        const QModelIndex index = m_model->index(i);
        row.shown = index.data(BlockListModel::ShownRole).toBool();
        if (!row.shown)
            continue;
        row.view = index.data(BlockListModel::ViewRole).toString();
        row.kind = index.data(BlockListModel::KindRole).toString();
        if (!drawnView(row.view) || row.kind == u"frontmatter" || index.data(BlockListModel::IsEditingRole).toBool()
            || index.data(BlockListModel::IsSelectedRole).toBool()
            || !index.data(BlockListModel::TransclusionsRole).toList().isEmpty()
            || !index.data(BlockListModel::TagTintRole).toString().isEmpty()) { // the tint: BlockPartTagTint
            drawable = false;
            break;
        }
        if (row.view != u"rule") {
            if (!index.data(BlockListModel::EmbedsRole).toList().isEmpty()) {
                drawable = false;
                break;
            }
            row.text = index.data(BlockListModel::RenderedTextRole).value<RenderedTextRef>();
            if (!row.text.drawable()) {
                drawable = false;
                break;
            }
        }
        row.taskStatus = index.data(BlockListModel::TaskStatusRole).toString();
        row.taskDated = index.data(BlockListModel::TaskDatedRole).toBool();
        row.listMarker = index.data(BlockListModel::ListMarkerRole).toString();
        row.prevKind = index.data(BlockListModel::PrevKindRole).toString();
        row.hasChildren = index.data(BlockListModel::HasChildrenRole).toBool();
        row.collapsed = index.data(BlockListModel::CollapsedRole).toBool();
        row.depth = index.data(BlockListModel::DepthRole).toInt();
        row.headingLevel = index.data(BlockListModel::HeadingLevelRole).toInt();
    }
    m_reloading = false;
    if (!drawable) {
        // The editor's rows take over: nothing of the rows is kept.
        releaseTexts();
        m_rows.clear();
        m_decorations.clear();
        m_nodesDirty = true;
        update();
        updateDrawable(false);
        return;
    }
    updateDrawable(true);
    layout();
}

qreal JournalDayItem::level(const QList<qreal> &list, int level, qreal fallback) const
{
    if (level >= 0 && level < list.size())
        return list.at(level);
    return list.isEmpty() ? fallback : list.at(0);
}

qreal JournalDayItem::space(int n) const
{
    // Theme.space1 … space6 (4, 8, 12, 16, 20, 24 unless the QML says otherwise).
    if (n >= 1 && n <= m_spaces.size())
        return m_spaces.at(n - 1);
    return 4.0 * n;
}

qreal JournalDayItem::markerX(int level) const
{
    // BlockDelegate.markerX(level).
    return m_documentMode ? m_contentX + m_gutter + (level + 1) * m_listIndent - std::round(m_listIndent * 0.55)
                          : m_contentX + m_gutter + level * m_indentStep - m_bulletColumn / 2;
}

void JournalDayItem::applyFind(RenderedTextItem *text) const
{
    text->setFindQuery(m_findQuery);
    text->setFindMatchCase(m_findCase);
    text->setFindMatchDiacritics(m_findDiacritics);
    text->setFindWholeWord(m_findWholeWord);
    text->setFindColor(m_findColor);
}

void JournalDayItem::applyTextLook(Row &row, RenderedTextItem *text) const
{
    // BlockText's RunsText: the same font, colours and line height.
    const bool quoted = row.view == u"quote";
    const bool closed = row.taskStatus == u"done" || row.taskStatus == u"cancelled";
    QFont font;
    font.setFamily(m_fontFamily);
    font.setPixelSize(std::max(1, int(level(m_fontSizes, row.level, 15))));
    font.setWeight(QFont::Weight(row.level > 0 ? m_semiBoldWeight : m_normalWeight));
    font.setItalic(quoted);
    font.setStrikeOut(row.taskStatus == u"cancelled");
    text->setFont(font);
    text->setLineHeight(level(m_lineHeights, row.level, 24));
    text->setColor(closed ? m_faintColor : quoted ? m_mutedColor : m_textColor);
    text->setLinkColor(m_linkColor);
    text->setBoxRadius(m_boxRadius);
}

void JournalDayItem::layout()
{
    if (!isComponentComplete() || !m_drawable)
        return;
    const bool doc = m_documentMode;
    const qreal s1 = space(1), s2 = space(2), s3 = space(3), s4 = space(4), s5 = space(5), s6 = space(6);
    const qreal rowPad = m_touchUi ? 4 : 2;
    const qreal body = level(m_lineHeights, 0, 24);
    qreal y = 0;
    for (int i = 0; i < int(m_rows.size()); ++i) {
        Row &row = m_rows[size_t(i)];
        row.y = y;
        if (!row.shown) {
            row.height = 0;
            if (row.textItem)
                row.textItem->setVisible(false);
            if (row.numberItem)
                row.numberItem->setVisible(false);
            continue;
        }
        // BlockDelegate's properties, one by one.
        const bool listItem = row.kind == u"listItem";
        const bool isTask = !row.taskStatus.isEmpty() && listItem;
        const bool closed = row.taskStatus == u"done" || row.taskStatus == u"cancelled";
        const bool textView = isTextView(row.view);
        const bool datable = isTask && !closed && !row.taskDated && textView && !m_readOnly;
        const bool addable = isTask && !closed && row.taskDated && textView && !m_readOnly;
        row.dateButtonRoom = datable || addable ? (m_touchUi ? 32 : 28) : 0;
        row.level = row.view == u"heading" ? std::max(1, row.headingLevel) : 0;
        const int lvl = row.level;
        row.levelX = doc ? m_contentX + m_gutter + (listItem ? (row.depth + 1) * m_listIndent : 0)
                         : m_contentX + m_gutter + row.depth * m_indentStep;
        row.bulletX = markerX(doc && !listItem ? -1 : row.depth);
        row.contentLeft = row.levelX + (isTask && !doc ? m_taskBoxWidth : 0);
        row.contentRight = m_contentX + m_contentWidth;
        const bool framed = isFramed(row.view);
        const bool belowProperties = row.prevKind == u"frontmatter";
        qreal outlineTopPad = rowPad;
        if (belowProperties)
            outlineTopPad = lvl == 1 ? s5 : s4;
        else if (lvl > 0 && i > 0)
            outlineTopPad = lvl == 1 ? s5 : lvl == 2 ? s4 : s3;
        else if (framed)
            outlineTopPad = s2;
        else
            outlineTopPad = row.view == u"rule" ? s2 : rowPad;
        qreal docTopPad = m_paragraphGap;
        if (row.prevKind.isEmpty())
            docTopPad = 0;
        else if (belowProperties)
            docTopPad = lvl > 0 && !listItem ? std::max(s6, level(m_headingGapsAbove, lvl, s6)) : s6;
        else if (lvl > 0 && !listItem)
            docTopPad = level(m_headingGapsAbove, lvl, s6);
        else if (row.prevKind == u"heading")
            docTopPad = framed || row.view == u"quote" ? m_headingGapBelow + s2 : m_headingGapBelow;
        else if (listItem && row.prevKind == u"listItem")
            docTopPad = m_listGap;
        row.topPad = doc ? docTopPad : outlineTopPad;
        const qreal bottomPad = doc ? (listItem ? m_listGap / 2 : 0) : framed ? s2 : lvl > 0 ? s1 : rowPad;

        // The content: a text block's BlockText, a quote's frame with its BlockText, a rule's line.
        qreal contentHeight = 0;
        qreal firstLine = 0;
        if (row.view == u"rule") {
            if (row.textItem)
                row.textItem->setVisible(false);
            contentHeight = body;
            firstLine = body / 2;
            row.textX = row.contentLeft;
            row.textWidth = std::max<qreal>(40, row.contentRight - row.contentLeft);
            row.lines = 0;
        } else {
            const bool quote = row.view == u"quote";
            const qreal lineHeight = level(m_lineHeights, quote ? 0 : lvl, 24);
            if (quote) {
                const qreal frameX = row.levelX;
                const qreal frameWidth = std::max<qreal>(40, row.contentRight - frameX);
                row.textX = frameX + s4;
                row.textWidth = frameWidth - s4;
                firstLine = level(m_firstLineCenters, 0, 16);
            } else {
                row.textX = row.contentLeft;
                row.textWidth = std::max<qreal>(40, row.contentRight - row.dateButtonRoom - row.contentLeft);
                firstLine = level(m_firstLineCenters, lvl, 16);
            }
            RenderedTextItem *text = row.textItem;
            const bool fresh = text == nullptr;
            if (fresh) {
                text = new RenderedTextItem(this);
                // One layout with every property set (not one per property).
                static_cast<QQmlParserStatus *>(text)->classBegin();
                row.textItem = text;
            }
            text->setVisible(true);
            applyTextLook(row, text);
            applyFind(text);
            text->setHoveredLink(i == m_hoverRow ? m_hoveredLink : QString());
            text->setRenderedText(row.text);
            text->setPosition(QPointF(row.textX, y + row.topPad));
            text->setWidth(row.textWidth);
            if (fresh)
                text->componentComplete();
            row.lines = text->lineCount();
            // BlockText's height: whole fixed lines, at least one.
            contentHeight = lineHeight > 0
                                    ? std::max<qreal>(1, std::round(text->implicitHeight() / lineHeight)) * lineHeight
                                    : text->implicitHeight();
            contentHeight = std::max(lineHeight, contentHeight);
        }
        row.contentHeight = contentHeight;
        row.firstLineCenter = row.topPad + firstLine;
        row.height = std::ceil(row.topPad + contentHeight + bottomPad);

        // A document's numbered list item: its number right-aligned in the marker column.
        if (doc && listItem && !isTask && !row.listMarker.isEmpty() && row.listMarker.at(0) >= u'0'
            && row.listMarker.at(0) <= u'9') {
            RenderedTextItem *number = row.numberItem;
            const bool fresh = number == nullptr;
            if (fresh) {
                number = new RenderedTextItem(this);
                static_cast<QQmlParserStatus *>(number)->classBegin();
                row.numberItem = number;
            }
            QFont font;
            font.setFamily(m_fontFamily);
            font.setPixelSize(std::max(1, int(level(m_fontSizes, 0, 15))));
            font.setFeature(QFont::Tag("tnum"), 1);
            number->setFont(font);
            number->setWrap(false); // (a Text's default: its natural width)
            number->setLineHeight(body);
            number->setColor(closed ? m_faintColor : m_mutedColor);
            number->setLinkColor(m_linkColor);
            number->setHtml(row.listMarker.toHtmlEscaped());
            if (fresh)
                number->componentComplete();
            number->setWidth(number->implicitWidth());
            const qreal gap = std::round(m_listIndent * 0.25);
            number->setPosition(QPointF(row.levelX - number->width() - gap,
                                        y + row.firstLineCenter - level(m_firstLineCenters, 0, 16)));
            number->setVisible(true);
        } else {
            discardText(row.numberItem);
        }
        y += row.height;
    }
    if (!qFuzzyCompare(y + 1, m_contentHeight + 1)) {
        m_contentHeight = y;
        setImplicitHeight(y);
        Q_EMIT contentHeightChanged();
    } else {
        setImplicitHeight(y);
    }
    decorate();
}

bool JournalDayItem::bulletShown(const Row &row) const
{
    const bool listItem = row.kind == u"listItem";
    const bool isTask = !row.taskStatus.isEmpty() && listItem;
    const bool ordered =
            listItem && !row.listMarker.isEmpty() && row.listMarker.at(0) >= u'0' && row.listMarker.at(0) <= u'9';
    return listItem && !(m_documentMode && (isTask || ordered));
}

bool JournalDayItem::taskShown(const Row &row) const
{
    return !row.taskStatus.isEmpty() && row.kind == u"listItem";
}

bool JournalDayItem::numberShown(const Row &row) const
{
    return row.numberItem != nullptr;
}

bool JournalDayItem::arrowShown(const Row &row) const
{
    if (!row.hasChildren || m_documentMode)
        return false;
    const int index = int(&row - m_rows.data());
    return row.collapsed || m_touchUi || (index == m_hoverRow && !m_touchUi);
}

QRectF JournalDayItem::arrowRect(const Row &row) const
{
    // BlockDelegate's arrow Loader: left of the bullet's hit area.
    const qreal w = m_arrowColumn;
    const qreal h = std::max<qreal>(m_arrowColumn, 24);
    const qreal bulletBoxWidth = m_touchUi ? m_bulletColumn + 6 : std::max<qreal>(24, m_bulletColumn);
    const qreal x = (m_touchUi ? row.bulletX - m_bulletColumn / 2 : row.levelX - bulletBoxWidth) - w + 2;
    return {x, row.y + row.firstLineCenter - h / 2, w, h};
}

QRectF JournalDayItem::arrowHitRect(const Row &row) const
{
    const QRectF r = arrowRect(row);
    if (!m_touchUi)
        return r;
    return {r.center().x() - m_touchTarget / 2, r.center().y() - m_touchTarget / 2, m_touchTarget, m_touchTarget};
}

QRectF JournalDayItem::bulletRect(const Row &row) const
{
    // BlockDelegate's bulletBox: its hit area, the text column its right edge.
    const bool besideArrow = !m_documentMode && row.hasChildren;
    const qreal w =
            m_touchUi ? (besideArrow ? m_bulletColumn + 6 : m_touchTarget) : std::max<qreal>(24, m_bulletColumn);
    const qreal h = m_touchUi ? m_touchTarget : std::max<qreal>(24, m_bulletColumn);
    const qreal x = m_touchUi && besideArrow ? row.bulletX - w / 2
                    : m_documentMode         ? std::max<qreal>(0, row.levelX - w)
                                             : row.levelX - w;
    return {x, row.y + row.firstLineCenter - h / 2, w, h};
}

QRectF JournalDayItem::taskRect(const Row &row) const
{
    // The TaskBox (its hit area: 24 px, a touch target on touch UIs).
    const qreal size = m_touchUi ? m_touchTarget : 24;
    const qreal boxSize = m_touchUi ? 18 : 16;
    const qreal x = m_documentMode ? row.bulletX - size / 2
                                   : row.levelX + (m_taskBoxWidth - boxSize) / 2 - 2 - (size - boxSize) / 2;
    return {x, row.y + row.firstLineCenter - size / 2, size, size};
}

QRectF JournalDayItem::numberHitRect(const Row &row) const
{
    if (!row.numberItem)
        return {};
    const RenderedTextItem *number = row.numberItem;
    const qreal size = m_touchUi ? m_touchTarget : 24;
    const qreal gap = std::round(m_listIndent * 0.25);
    const qreal textEdge = number->width() + gap;
    const qreal w = std::max(size, textEdge);
    const qreal numberHeight = level(m_lineHeights, 0, 24);
    const qreal h = std::max(size, numberHeight);
    const qreal x = std::max(-number->x(), textEdge - w);
    return {number->x() + x, number->y() + (numberHeight - h) / 2, w, h};
}

QRectF JournalDayItem::dateButtonRect(const Row &row) const
{
    if (row.dateButtonRoom <= 0)
        return {};
    // BIconButton size "small": Theme.controlHeightSm (≈ the room it is centred in) — the QML
    // places the real button there (rowGeometry()); the item only reserves the room.
    const qreal size = row.dateButtonRoom;
    return {row.contentRight - row.dateButtonRoom, row.y + row.firstLineCenter - size / 2, size, size};
}

QRectF JournalDayItem::textRect(const Row &row) const
{
    if (row.view == u"rule")
        return {row.textX, row.y + row.topPad, row.textWidth, row.contentHeight};
    return {row.textX, row.y + row.topPad, row.textWidth, row.contentHeight};
}

int JournalDayItem::rowIndexAt(qreal y) const
{
    for (int i = 0; i < int(m_rows.size()); ++i) {
        const Row &row = m_rows[size_t(i)];
        if (row.shown && y >= row.y && y < row.y + row.height)
            return i;
    }
    return -1;
}

int JournalDayItem::rowAt(qreal y) const
{
    return rowIndexAt(y);
}

QString JournalDayItem::partAt(qreal x, qreal y) const
{
    if (y >= m_contentHeight && y < m_contentHeight + m_footerHeight && x >= 0 && x < width())
        return u"footer"_s;
    const int i = rowIndexAt(y);
    if (i < 0)
        return {};
    const Row &row = m_rows[size_t(i)];
    const QPointF p(x, y);
    // Topmost first: the text (BlockText's overlay, declared last), the date button, the task
    // box, the number, the bullet, the fold arrow.
    if (textRect(row).contains(p))
        return u"text"_s;
    if (i == m_hoverRow && !m_touchUi && dateButtonRect(row).contains(p))
        return u"dateButton"_s;
    if (taskShown(row) && taskRect(row).contains(p))
        return u"taskBox"_s;
    if (numberShown(row) && numberHitRect(row).contains(p))
        return u"number"_s;
    if (bulletShown(row) && bulletRect(row).contains(p))
        return u"bullet"_s;
    if (arrowShown(row) && arrowHitRect(row).contains(p))
        return u"arrow"_s;
    return u"row"_s;
}

QString JournalDayItem::linkAt(qreal x, qreal y) const
{
    const int i = rowIndexAt(y);
    if (i < 0)
        return {};
    const Row &row = m_rows[size_t(i)];
    if (!row.textItem || !row.textItem->isVisible())
        return {};
    return row.textItem->linkAt(x - row.textItem->x(), y - row.textItem->y());
}

int JournalDayItem::textPositionAt(qreal x, qreal y) const
{
    const int i = rowIndexAt(y);
    if (i < 0 || !m_rows[size_t(i)].textItem)
        return -1;
    const RenderedTextItem *text = m_rows[size_t(i)].textItem;
    return text->positionAt(x - text->x(), y - text->y());
}

void JournalDayItem::setHover(qreal x, qreal y)
{
    const int row = rowIndexAt(y);
    const QString part = row >= 0 ? partAt(x, y) : QString();
    if (row == m_hoverRow && part == m_hoverPart)
        return;
    const int before = m_hoverRow;
    m_hoverRow = row;
    m_hoverPart = part;
    if (before != row) {
        // The underline belongs to the row under the pointer.
        if (before >= 0 && before < int(m_rows.size()) && m_rows[size_t(before)].textItem)
            m_rows[size_t(before)].textItem->setHoveredLink(QString());
        if (row >= 0 && m_rows[size_t(row)].textItem)
            m_rows[size_t(row)].textItem->setHoveredLink(m_hoveredLink);
    }
    decorate();
    Q_EMIT hoverChanged();
}

void JournalDayItem::clearHover()
{
    if (m_hoverRow < 0 && m_hoverPart.isEmpty())
        return;
    if (m_hoverRow >= 0 && m_hoverRow < int(m_rows.size()) && m_rows[size_t(m_hoverRow)].textItem)
        m_rows[size_t(m_hoverRow)].textItem->setHoveredLink(QString());
    m_hoverRow = -1;
    m_hoverPart.clear();
    decorate();
    Q_EMIT hoverChanged();
}

QVariantMap JournalDayItem::rowGeometry(int index) const
{
    if (index < 0 || index >= int(m_rows.size()))
        return {};
    const Row &row = m_rows[size_t(index)];
    return {{u"shown"_s, row.shown},
            {u"y"_s, row.y},
            {u"height"_s, row.height},
            {u"topPad"_s, row.topPad},
            {u"firstLineCenter"_s, row.firstLineCenter},
            {u"textX"_s, row.textX},
            {u"textY"_s, row.y + row.topPad},
            {u"textWidth"_s, row.textWidth},
            {u"lines"_s, row.lines},
            {u"bulletX"_s, row.bulletX},
            {u"levelX"_s, row.levelX},
            {u"contentRight"_s, row.contentRight},
            {u"dateButtonRoom"_s, row.dateButtonRoom}};
}

qreal JournalDayItem::rowY(int row) const
{
    return row >= 0 && row < int(m_rows.size()) ? m_rows[size_t(row)].y : 0;
}

qreal JournalDayItem::rowHeight(int row) const
{
    return row >= 0 && row < int(m_rows.size()) ? m_rows[size_t(row)].height : 0;
}

qreal JournalDayItem::firstLineCenterOf(int row) const
{
    return row >= 0 && row < int(m_rows.size()) ? m_rows[size_t(row)].firstLineCenter : 0;
}

QRectF JournalDayItem::textRectOf(int row) const
{
    if (row < 0 || row >= int(m_rows.size()) || !m_rows[size_t(row)].shown)
        return {};
    return textRect(m_rows[size_t(row)]);
}

QRectF JournalDayItem::partRect(int index, const QString &part) const
{
    if (index < 0 || index >= int(m_rows.size()) || !m_rows[size_t(index)].shown)
        return {};
    const Row &row = m_rows[size_t(index)];
    if (part == u"text")
        return textRect(row);
    if (part == u"taskBox")
        return taskShown(row) ? taskRect(row) : QRectF();
    if (part == u"number")
        return numberHitRect(row);
    if (part == u"bullet")
        return bulletShown(row) ? bulletRect(row) : QRectF();
    if (part == u"arrow")
        return arrowShown(row) ? arrowHitRect(row) : QRectF();
    if (part == u"dateButton")
        return dateButtonRect(row);
    return {};
}

QPointF JournalDayItem::menuPoint(int index, const QString &part) const
{
    if (index < 0 || index >= int(m_rows.size()))
        return {};
    const Row &row = m_rows[size_t(index)];
    if (part == u"bullet") {
        const QRectF r = bulletRect(row);
        return {row.bulletX, r.bottom() - row.y};
    }
    if (part == u"number" && row.numberItem)
        return {row.numberItem->x(), row.numberItem->y() - row.y + level(m_lineHeights, 0, 24)};
    if (part == u"taskBox") {
        const QRectF r = taskRect(row);
        return {r.x(), r.bottom() - row.y};
    }
    return {};
}

QRectF JournalDayItem::dateButtonArea(int index) const
{
    if (index < 0 || index >= int(m_rows.size()) || !m_rows[size_t(index)].shown)
        return {};
    return dateButtonRect(m_rows[size_t(index)]);
}

bool JournalDayItem::addableAt(int index) const
{
    if (index < 0 || index >= int(m_rows.size()))
        return false;
    const Row &row = m_rows[size_t(index)];
    const bool closed = row.taskStatus == u"done" || row.taskStatus == u"cancelled";
    return taskShown(row) && !closed && row.taskDated && isTextView(row.view) && !m_readOnly;
}

RenderedTextItem *JournalDayItem::textItemOf(int row) const
{
    if (row < 0 || row >= int(m_rows.size()))
        return nullptr;
    return m_rows[size_t(row)].textItem;
}

void JournalDayItem::decorate()
{
    QList<Decoration> out;
    const bool doc = m_documentMode;
    auto rect = [&out](const QRectF &r, const QColor &c) {
        if (r.width() > 0 && r.height() > 0 && c.alpha() > 0)
            out.append({Decoration::Kind::Rect, r, c, {}});
    };
    auto sprite = [&out](const QRectF &r, const QString &key) {
        if (r.width() > 0 && r.height() > 0)
            out.append({Decoration::Kind::Sprite, r, {}, key});
    };
    const qreal taskBoxSize = m_touchUi ? 18 : 16;
    for (int i = 0; i < int(m_rows.size()); ++i) {
        const Row &row = m_rows[size_t(i)];
        if (!row.shown)
            continue;
        const bool listItem = row.kind == u"listItem";
        const bool isTask = taskShown(row);
        const bool closed = row.taskStatus == u"done" || row.taskStatus == u"cancelled";
        const bool hovered = i == m_hoverRow && !m_touchUi;
        // Guide lines: one per ancestor level, over the row's full height.
        const int guides = !doc || listItem ? row.depth : 0;
        for (int k = 0; k < guides; ++k)
            rect(QRectF(std::round(markerX(k)), row.y, 1, row.height), m_guideColor);
        // From the bullet down to the (visible) children.
        if (listItem && row.hasChildren && !row.collapsed) {
            const qreal top = std::round(row.firstLineCenter + (doc && isTask ? taskBoxSize / 2 + 3 : 9));
            rect(QRectF(std::round(row.bulletX), row.y + top, 1, std::max<qreal>(0, row.height - top)), m_guideColor);
        }
        // The fold arrow.
        if (arrowShown(row)) {
            const QRectF box = arrowRect(row);
            const bool arrowHovered = hovered && m_hoverPart == u"arrow";
            QColor color = arrowHovered ? m_textColor : m_mutedColor;
            color.setAlphaF(color.alphaF() * (row.collapsed ? 0.9 : 0.6));
            const qreal s = m_iconSize;
            sprite(QRectF(box.center().x() - s / 2, box.center().y() - s / 2, s, s),
                   u"icon|chevron-right|%1|%2|%3"_s.arg(s).arg(argb(color)).arg(row.collapsed ? 0 : 90));
        }
        // The bullet (a halo when folded or hovered) — at the marker centre, on the first line.
        if (bulletShown(row)) {
            const bool bulletHovered = hovered && m_hoverPart == u"bullet";
            const qreal cy = row.y + row.firstLineCenter;
            if (!doc && (row.collapsed || bulletHovered)) {
                const QColor halo = row.collapsed ? m_pressedColor : m_hoverColor;
                sprite(QRectF(row.bulletX - 8, cy - 8, 16, 16), u"dot|16|%1"_s.arg(argb(halo)));
            }
            const QColor dot = closed ? m_faintColor : bulletHovered && !doc ? m_textColor : m_mutedColor;
            sprite(QRectF(row.bulletX - 3, cy - 3, 6, 6), u"dot|6|%1"_s.arg(argb(dot)));
        }
        // The task box.
        if (isTask) {
            const QRectF hit = taskRect(row);
            const bool boxHovered = hovered && m_hoverPart == u"taskBox";
            const QString &status = row.taskStatus;
            QColor fill = Qt::transparent;
            if (status == u"done")
                fill = m_successColor;
            else if (status == u"doing") {
                fill = m_warningColor;
                fill.setAlphaF(0.18);
            } else if (boxHovered)
                fill = m_hoverColor;
            const QColor border = status == u"doing" ? m_warningTextColor
                                  : closed           ? m_faintColor
                                  : boxHovered       ? m_textColor
                                                     : m_controlBorderColor;
            const qreal borderWidth = status == u"done" ? 0 : 1.5;
            const QColor mark = status == u"doing"       ? m_warningTextColor
                                : status == u"cancelled" ? m_faintColor
                                                         : m_checkColor;
            const QRectF box(hit.x() + (hit.width() - taskBoxSize) / 2, hit.y() + (hit.height() - taskBoxSize) / 2,
                             taskBoxSize, taskBoxSize);
            sprite(box, u"box|%1|%2|%3|%4|%5|%6|%7|%8"_s.arg(status)
                                .arg(taskBoxSize)
                                .arg(m_boxRadius + 1)
                                .arg(argb(fill), argb(border))
                                .arg(borderWidth)
                                .arg(argb(mark), argb(m_mutedColor)));
        }
        // A quote's bar, a rule's line.
        if (row.view == u"quote")
            rect(QRectF(row.levelX, row.y + row.topPad, 3, row.contentHeight), m_quoteBarColor);
        if (row.view == u"rule")
            rect(QRectF(row.textX, row.y + row.topPad + (row.contentHeight - 1) / 2, row.textWidth, 1), m_ruleColor);
        // A page's app icon in the text (RenderedTextItem.iconShapes; BlockText's Repeater of BIcon).
        if (row.textItem) {
            const QPointF at = row.textItem->position();
            for (const RichTextPill &pill : row.textItem->iconShapes()) {
                const qreal size = std::round(std::min(pill.width, pill.height) * 0.9);
                sprite(QRectF(at.x() + pill.x + (pill.width - size) / 2, at.y() + pill.y + (pill.height - size) / 2,
                              size, size),
                       u"icon|%1|%2|%3|0"_s.arg(pill.icon).arg(size).arg(argb(pill.color)));
            }
        }
    }
    if (out.size() != m_decorations.size()
        || !std::equal(out.cbegin(), out.cend(), m_decorations.cbegin(), [](const Decoration &a, const Decoration &b) {
               return a.kind == b.kind && a.rect == b.rect && a.color == b.color && a.sprite == b.sprite;
           })) {
        m_decorations = std::move(out);
        m_nodesDirty = true;
        update();
    }
}

void JournalDayItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    // (The rows' geometry follows the editor's contentX / contentWidth, not this item's width.)
}

void JournalDayItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) {
        m_nodesDirty = true;
        update();
    }
    QQuickItem::itemChange(change, value);
}

QSGNode *JournalDayItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    QQuickWindow *win = window();
    if (!win) {
        delete old;
        return nullptr;
    }
    const qreal dpr = win->effectiveDevicePixelRatio();
    // One root node for the item's life (Qt Quick removes the node it gave out itself when another
    // one comes back: the item must not delete it); its children are made again when the
    // decorations change, the sprites' textures kept.
    auto *root = static_cast<DayNode *>(old);
    if (!root) {
        root = new DayNode;
        root->dpr = dpr;
        m_nodesDirty = true;
    }
    if (!qFuzzyCompare(root->dpr, dpr)) {
        qDeleteAll(root->textures);
        root->textures.clear();
        root->dpr = dpr;
        m_nodesDirty = true;
    }
    if (!m_nodesDirty)
        return root;
    while (QSGNode *child = root->firstChild()) {
        root->removeChildNode(child);
        delete child;
    }
    QHash<QString, QSGTexture *> used;
    for (const Decoration &deco : std::as_const(m_decorations)) {
        if (deco.kind == Decoration::Kind::Rect) {
            QSGRectangleNode *node = win->createRectangleNode();
            node->setRect(deco.rect);
            node->setColor(deco.color);
            root->appendChildNode(node);
            continue;
        }
        QSGTexture *texture = used.value(deco.sprite);
        if (!texture) {
            texture = root->textures.take(deco.sprite);
            if (!texture) {
                const QImage image = spriteImage(deco.sprite, dpr);
                if (image.isNull())
                    continue;
                texture = win->createTextureFromImage(
                        image, QQuickWindow::CreateTextureOptions(QQuickWindow::TextureCanUseAtlas)
                                       | QQuickWindow::TextureHasAlphaChannel);
            }
            used.insert(deco.sprite, texture);
        }
        QSGImageNode *node = win->createImageNode();
        node->setTexture(texture);
        node->setOwnsTexture(false);
        node->setRect(deco.rect);
        node->setFiltering(QSGTexture::Linear);
        root->appendChildNode(node);
    }
    qDeleteAll(root->textures); // sprites no longer drawn
    root->textures = std::move(used);
    m_nodesDirty = false;
    return root;
}

} // namespace brulion::editor
