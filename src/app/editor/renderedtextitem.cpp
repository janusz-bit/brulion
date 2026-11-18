#include "renderedtextitem.h"

#include "markdown/inlinerenderer.h"
#include "text/find.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QSGImageNode>
#include <QSGRendererInterface>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTextNode>
#include <QSGVertexColorMaterial>
#include <QTextBoundaryFinder>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <utility>

namespace brulion::editor {

using namespace Qt::StringLiterals;
using markdown::RenderedBackground;
using markdown::RenderedRun;
using markdown::RenderedText;

namespace {

QColor cssColor(const QString &css)
{
    if (css.isEmpty())
        return {};
    if (css == u"transparent")
        return QColor(Qt::transparent);
    return QColor::fromString(css);
}

/// The scene graph's nodes of one item: shapes under the text, the text, overlays over it. With
/// the software renderer (no custom geometry) the shapes and overlays are one painted image under
/// the text.
class RootNode : public QSGNode {
public:
    explicit RootNode(bool softwareRenderer) : software(softwareRenderer)
    {
        for (QSGGeometryNode *node : {&under, &over}) {
            node->setGeometry(new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0));
            node->setFlag(QSGNode::OwnsGeometry);
            node->geometry()->setDrawingMode(QSGGeometry::DrawTriangles);
        }
        under.setMaterial(&underMaterial);
        over.setMaterial(&overMaterial);
        if (!software) {
            appendChildNode(&under);
            appendChildNode(&over);
        }
    }
    ~RootNode() override
    {
        // The geometry nodes are members: taken out before QSGNode deletes its children.
        if (!software) {
            removeChildNode(&under);
            removeChildNode(&over);
        }
    }
    const bool software;
    QSGGeometryNode under;
    QSGVertexColorMaterial underMaterial;
    QSGGeometryNode over;
    QSGVertexColorMaterial overMaterial;
    QSGTextNode *text = nullptr;   ///< (a child: deleted with the node)
    QSGImageNode *image = nullptr; ///< software: the shapes painted (a child)
};

using Vertex = QSGGeometry::ColoredPoint2D;

Vertex vertex(QPointF p, const QColor &c, qreal alpha = 1)
{
    // Premultiplied (QSGVertexColorMaterial).
    const qreal a = c.alphaF() * alpha;
    Vertex v;
    v.set(float(p.x()), float(p.y()), uchar(std::lround(c.redF() * a * 255)), uchar(std::lround(c.greenF() * a * 255)),
          uchar(std::lround(c.blueF() * a * 255)), uchar(std::lround(a * 255)));
    return v;
}

/// The outline of a rounded rectangle, clockwise from the top-left corner's arc.
QList<QPointF> outline(const QRectF &r, qreal radius)
{
    QList<QPointF> out;
    radius = std::clamp<qreal>(radius, 0, std::min(r.width(), r.height()) / 2);
    const int steps = radius < 0.5 ? 1 : std::clamp(int(radius / 1.5) + 2, 3, 10);
    struct Corner {
        QPointF centre;
        qreal start; // angle in radians
    };
    const Corner corners[4] = {{{r.left() + radius, r.top() + radius}, std::numbers::pi},
                               {{r.right() - radius, r.top() + radius}, 1.5 * std::numbers::pi},
                               {{r.right() - radius, r.bottom() - radius}, 0},
                               {{r.left() + radius, r.bottom() - radius}, 0.5 * std::numbers::pi}};
    for (const Corner &c : corners) {
        if (radius < 0.5) {
            out.append(c.centre);
            continue;
        }
        for (int k = 0; k <= steps; ++k) {
            const qreal a = c.start + (std::numbers::pi / 2) * k / steps;
            out.append(c.centre + QPointF(std::cos(a) * radius, std::sin(a) * radius));
        }
    }
    return out;
}

/// A rounded rectangle as triangles with a one-pixel antialiased edge (as Rectangle's antialiasing).
void addRoundedRect(QList<Vertex> &v, const QRectF &rect, qreal radius, const QColor &color, bool antialias)
{
    if (rect.isEmpty() || color.alpha() == 0)
        return;
    if (!antialias) {
        const Vertex a = vertex(rect.topLeft(), color), b = vertex(rect.topRight(), color),
                     c = vertex(rect.bottomRight(), color), d = vertex(rect.bottomLeft(), color);
        v.append({a, b, c, a, c, d});
        return;
    }
    const qreal half = 0.5;
    const QRectF inner = rect.adjusted(half, half, -half, -half);
    const QRectF outer = rect.adjusted(-half, -half, half, half);
    const QList<QPointF> in = outline(inner, std::max<qreal>(0, radius - half));
    const QList<QPointF> out = outline(outer, radius + half);
    const Vertex centre = vertex(rect.center(), color);
    const qsizetype n = in.size();
    for (qsizetype k = 0; k < n; ++k) {
        const qsizetype j = (k + 1) % n;
        const Vertex i0 = vertex(in[k], color), i1 = vertex(in[j], color);
        const Vertex o0 = vertex(out[k], color, 0), o1 = vertex(out[j], color, 0);
        v.append({centre, i0, i1, i0, o0, o1, i0, o1, i1});
    }
}

void fill(QSGGeometryNode &node, const QList<Vertex> &vertices)
{
    QSGGeometry *g = node.geometry();
    g->allocate(int(vertices.size()));
    if (!vertices.isEmpty())
        std::memcpy(g->vertexData(), vertices.constData(), vertices.size() * sizeof(Vertex));
    node.markDirty(QSGNode::DirtyGeometry);
}

} // namespace

RenderedTextItem::RenderedTextItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

RenderedTextItem::~RenderedTextItem() = default;

void RenderedTextItem::componentComplete()
{
    QQuickItem::componentComplete();
    reload();
}

void RenderedTextItem::setRenderedText(const RenderedTextRef &text)
{
    if (text == m_ref)
        return;
    m_ref = text;
    reload();
    Q_EMIT renderedTextChanged();
}

void RenderedTextItem::setHtml(const QString &html)
{
    if (html == m_html)
        return;
    m_html = html;
    if (!m_ref.d)
        reload();
    Q_EMIT htmlChanged();
}

void RenderedTextItem::setFont(const QFont &font)
{
    if (font == m_font && font.resolveMask() == m_font.resolveMask())
        return;
    m_font = font;
    relayout();
    Q_EMIT fontChanged();
}

void RenderedTextItem::setColor(const QColor &color)
{
    if (color == m_color)
        return;
    m_color = color;
    m_textDirty = true;
    computeOverlays();
    update();
    Q_EMIT colorChanged();
}

void RenderedTextItem::setLinkColor(const QColor &color)
{
    if (color == m_linkColor)
        return;
    m_linkColor = color;
    relayout(); // a format's colour
    Q_EMIT linkColorChanged();
}

void RenderedTextItem::setLineHeight(qreal px)
{
    if (qFuzzyCompare(px + 1, m_lineHeight + 1))
        return;
    m_lineHeight = px;
    relayout();
    Q_EMIT lineHeightChanged();
}

void RenderedTextItem::setHorizontalAlignment(int alignment)
{
    if (alignment == m_alignment)
        return;
    m_alignment = alignment;
    relayout();
    Q_EMIT horizontalAlignmentChanged();
}

void RenderedTextItem::setWrap(bool wrap)
{
    if (wrap == m_wrap)
        return;
    m_wrap = wrap;
    relayout();
    Q_EMIT wrapChanged();
}

void RenderedTextItem::setMaximumLineCount(int lines)
{
    if (lines == m_maxLines)
        return;
    m_maxLines = lines;
    relayout();
    Q_EMIT maximumLineCountChanged();
}

void RenderedTextItem::setBoxRadius(qreal radius)
{
    if (qFuzzyCompare(radius + 1, m_boxRadius + 1))
        return;
    m_boxRadius = radius;
    m_shapesDirty = true;
    update();
    Q_EMIT boxRadiusChanged();
}

void RenderedTextItem::setInset(qreal inset)
{
    if (qFuzzyCompare(inset + 1, m_inset + 1))
        return;
    m_inset = inset;
    computeShapes();
    Q_EMIT insetChanged();
}

void RenderedTextItem::setHoveredLink(const QString &href)
{
    if (href == m_hoveredLink)
        return;
    m_hoveredLink = href;
    computeOverlays();
    Q_EMIT hoveredLinkChanged();
    Q_EMIT textChanged();
}

void RenderedTextItem::setFindQuery(const QString &query)
{
    if (query == m_findQuery)
        return;
    m_findQuery = query;
    computeOverlays();
    Q_EMIT findChanged();
}

void RenderedTextItem::setFindMatchCase(bool on)
{
    if (on == m_findCase)
        return;
    m_findCase = on;
    computeOverlays();
    Q_EMIT findChanged();
}

void RenderedTextItem::setFindMatchDiacritics(bool on)
{
    if (on == m_findDiacritics)
        return;
    m_findDiacritics = on;
    computeOverlays();
    Q_EMIT findChanged();
}

void RenderedTextItem::setFindWholeWord(bool on)
{
    if (on == m_findWholeWord)
        return;
    m_findWholeWord = on;
    computeOverlays();
    Q_EMIT findChanged();
}

void RenderedTextItem::setFindColor(const QColor &color)
{
    if (color == m_findColor)
        return;
    m_findColor = color;
    computeOverlays();
    Q_EMIT findChanged();
}

QString RenderedTextItem::plainText() const
{
    return m_shown.text;
}

QString RenderedTextItem::text() const
{
    return markdown::InlineRenderer::withHoveredLink(markdown::runsToHtml(m_shown), m_hoveredLink);
}

void RenderedTextItem::reload()
{
    m_laidOutWidth = -1;
    relayout();
}

QFont RenderedTextItem::runFont(const RenderedRun &run) const
{
    QFont f = m_font;
    if (run.style & RenderedRun::Bold)
        f.setWeight(QFont::Bold);
    if (run.style & RenderedRun::Italic)
        f.setItalic(true);
    if (run.style & RenderedRun::Code)
        f.setFamilies({u"monospace"_s});
    return f;
}

int RenderedTextItem::layoutText(const QString &text)
{
    QString shown = text;
    shown.replace(u'\n', QChar::LineSeparator);
    m_layout = std::make_unique<QTextLayout>(shown, m_font);
    m_layout->setCacheEnabled(true);
    QTextOption option;
    option.setWrapMode(m_wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    option.setAlignment(Qt::Alignment(m_alignment == Qt::AlignHCenter ? Qt::AlignHCenter : Qt::AlignLeft));
    m_layout->setTextOption(option);
    QList<QTextLayout::FormatRange> formats;
    for (const RenderedRun &run : std::as_const(m_shown.runs)) {
        if (run.from >= shown.size())
            break;
        QTextCharFormat f;
        if (run.style & RenderedRun::Bold)
            f.setFontWeight(QFont::Bold);
        if (run.style & RenderedRun::Italic)
            f.setFontItalic(true);
        if (run.style & RenderedRun::Strike)
            f.setFontStrikeOut(true);
        if (run.style & RenderedRun::Underline)
            f.setFontUnderline(true);
        if (run.style & RenderedRun::Code)
            f.setFontFamilies(QStringList{u"monospace"_s});
        if (run.style & RenderedRun::Sup)
            f.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
        else if (run.style & RenderedRun::Sub)
            f.setVerticalAlignment(QTextCharFormat::AlignSubScript);
        if (const QColor c = cssColor(run.foreground); c.isValid())
            f.setForeground(c);
        else if (!run.href.isEmpty())
            f.setForeground(m_linkColor);
        if (f.isEmpty())
            continue;
        formats.append({int(run.from), int(std::min<qsizetype>(run.to, shown.size()) - run.from), f});
    }
    m_layout->setFormats(formats);
    const qreal width = std::max<qreal>(0, this->width());
    m_layout->beginLayout();
    if (shown.isEmpty()) { // no line, no height
        m_layout->endLayout();
        ++m_layoutCount;
        return 0;
    }
    qreal top = 0;
    for (;;) {
        QTextLine line = m_layout->createLine();
        if (!line.isValid())
            break;
        if (m_wrap)
            line.setLineWidth(width);
        else
            line.setLineWidth(std::max(width, qreal(1e6)));
        if (m_lineHeight > 0) {
            // QTextDocumentLayout's FixedHeight (26.6 fixed point): the baseline at 4/5 of the line.
            const qint64 height = std::llround(m_lineHeight * 64);
            const qint64 baseline = height * 4 / 5;
            const qint64 ascent = std::llround((line.ascent() + std::max<qreal>(line.leading(), 0)) * 64);
            line.setPosition(QPointF(0, top + qreal(baseline - ascent) / 64));
            top += qreal(height) / 64;
        } else {
            line.setPosition(QPointF(0, top));
            top += line.height();
        }
    }
    m_layout->endLayout();
    ++m_layoutCount;
    return m_layout->lineCount();
}

void RenderedTextItem::relayout()
{
    if (!isComponentComplete())
        return;
    const RenderedText source = m_ref.d ? *m_ref.d : markdown::renderedTextFromHtml(m_html);
    m_shown = source;
    m_shownLength = source.text.size();
    int lines = layoutText(m_shown.text);
    bool cut = false;
    if (m_maxLines > 0 && lines > m_maxLines) {
        // The text up to the end of the last line shown, shortened until "…" fits on it.
        cut = true;
        const QTextLine last = m_layout->lineAt(m_maxLines - 1);
        const qsizetype lineStart = last.textStart();
        qsizetype end = last.textStart() + last.textLength();
        QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, source.text);
        for (int attempt = 0; attempt < 400; ++attempt) {
            qsizetype keep = end;
            while (keep > lineStart && source.text.at(keep - 1).isSpace())
                --keep;
            m_shown = source;
            m_shown.text = source.text.left(keep) + u'…';
            m_shownLength = keep;
            QList<RenderedRun> runs;
            for (RenderedRun r : std::as_const(source.runs)) {
                if (r.from >= keep)
                    break;
                r.to = std::min(r.to, keep);
                runs.append(r);
            }
            m_shown.runs = runs;
            m_shown.objects.removeIf([keep](const markdown::RenderedObject &o) { return o.to > keep; });
            lines = layoutText(m_shown.text);
            if (lines <= m_maxLines || keep <= lineStart)
                break;
            graphemes.setPosition(end);
            const qsizetype previous = graphemes.toPreviousBoundary();
            if (previous < 0 || previous <= lineStart)
                end = lineStart;
            else
                end = previous;
        }
    }
    m_laidOutWidth = width();
    if (cut != m_truncated) {
        m_truncated = cut;
        Q_EMIT truncatedChanged();
    }
    qreal height = 0;
    if (m_lineHeight > 0)
        height = lines * m_lineHeight;
    else if (lines > 0)
        height = m_layout->lineAt(lines - 1).y() + m_layout->lineAt(lines - 1).height();
    qreal natural = 0;
    for (int k = 0; k < lines; ++k)
        natural = std::max(natural, m_layout->lineAt(k).naturalTextWidth());
    setImplicitSize(std::ceil(natural), height);
    m_textDirty = true;
    computeShapes();
    computeOverlays();
    Q_EMIT layoutChanged();
    Q_EMIT textChanged();
}

QList<RenderedTextItem::Segment> RenderedTextItem::segments(qsizetype from, qsizetype to) const
{
    // RichTextPills' line segments (richtext_p.h: lineSegments), in item coordinates.
    QList<Segment> out;
    if (!m_layout)
        return out;
    for (int i = 0; i < m_layout->lineCount(); ++i) {
        const QTextLine line = m_layout->lineAt(i);
        const qsizetype lineEnd = line.textStart() + line.textLength();
        const qsizetype start = std::max<qsizetype>(from, line.textStart());
        const qsizetype end = std::min(to, lineEnd);
        if (start >= end)
            continue;
        qreal x1 = line.cursorToX(int(start));
        qreal x2 = line.cursorToX(int(end));
        if (x2 < x1)
            std::swap(x1, x2);
        if (to > lineEnd)
            x2 = std::min(x2, line.naturalTextRect().right());
        if (x2 - x1 < 0.5)
            continue;
        out.append({line, x1, x2});
    }
    return out;
}

void RenderedTextItem::computeShapes()
{
    QList<Shape> shapes;
    QList<RichTextPill> icons;
    const QList<RenderedRun> &runs = m_shown.runs;
    for (qsizetype k = 0; k < runs.size(); ++k) {
        const RenderedRun &first = runs[k];
        if (first.background == RenderedBackground::None)
            continue;
        // Adjacent runs of one tint are one shape (a highlight with bold words in it, a nested tag).
        qsizetype last = k;
        while (last + 1 < runs.size() && runs[last + 1].from == runs[last].to
               && runs[last + 1].backgroundColor == first.backgroundColor
               && runs[last + 1].background == first.background && runs[last + 1].icon == first.icon)
            ++last;
        const QFontMetricsF metrics(runFont(first));
        const QColor color = cssColor(first.backgroundColor);
        for (const Segment &segment : segments(first.from, runs[last].to)) {
            // Centred on the span's glyphs, not on the fixed-height line box.
            const qreal baseline = segment.line.y() + segment.line.ascent();
            const qreal top = baseline - metrics.ascent() + m_inset;
            const qreal bottom = baseline + metrics.descent() - m_inset;
            const QRectF rect(segment.x1, top, segment.x2 - segment.x1, std::max<qreal>(1, bottom - top));
            if (!first.icon.isEmpty()) {
                RichTextPill pill;
                pill.x = rect.x();
                pill.y = rect.y();
                pill.width = rect.width();
                pill.height = rect.height();
                pill.kind = u"icon"_s;
                pill.icon = first.icon;
                pill.color = cssColor(first.foreground);
                if (!pill.color.isValid())
                    pill.color = m_linkColor;
                pill.from = int(first.from);
                pill.to = int(runs[last].to);
                icons.append(pill);
            }
            if (color.alpha() > 0)
                shapes.append({first.background == RenderedBackground::Pill ? Shape::Kind::Pill : Shape::Kind::Box, rect,
                               color});
        }
        k = last;
    }
    if (shapes != m_shapes) {
        m_shapes = std::move(shapes);
        m_shapesDirty = true;
    }
    if (icons != m_iconShapes) {
        m_iconShapes = std::move(icons);
        Q_EMIT iconShapesChanged();
    }
    update();
}

void RenderedTextItem::computeOverlays()
{
    QList<Shape> overlays;
    if (m_layout) {
        // The find bar's matches (InlineRenderer::withFindHighlight: a tinted span per match; in a
        // link or a chip, a span with a text colour, a pill).
        if (!m_findQuery.isEmpty() && m_findColor.alpha() > 0) {
            const QList<text::TextRange> matches = text::findMatches(
                    QStringView(m_shown.text).first(m_shownLength), m_findQuery,
                    {m_findCase, m_findDiacritics, m_findWholeWord});
            for (const text::TextRange &match : matches) {
                qsizetype at = match.start;
                while (at < match.end()) {
                    const RenderedRun *run = m_shown.runAt(at);
                    const bool pill = run && !run->foreground.isEmpty() && !(run->style & RenderedRun::Code);
                    qsizetype end = match.end();
                    if (run)
                        end = std::min(end, run->to);
                    // Text outside runs (plain text) up to the next run.
                    if (!run) {
                        for (const RenderedRun &r : m_shown.runs) {
                            if (r.from > at) {
                                end = std::min(end, r.from);
                                break;
                            }
                        }
                    }
                    const QFontMetricsF metrics(run ? runFont(*run) : m_font);
                    for (const Segment &segment : segments(at, end)) {
                        const qreal baseline = segment.line.y() + segment.line.ascent();
                        const qreal top = baseline - metrics.ascent() + m_inset;
                        const qreal bottom = baseline + metrics.descent() - m_inset;
                        overlays.append({pill ? Shape::Kind::Pill : Shape::Kind::Find,
                                         QRectF(segment.x1, top, segment.x2 - segment.x1, std::max<qreal>(1, bottom - top)),
                                         m_findColor});
                    }
                    at = std::max(end, at + 1);
                }
            }
        }
        // The hovered link: underlined where withHoveredLink() underlines it (links and the parts
        // of a nested tag and a page icon's anchor; not chips or whole tags).
        if (!m_hoveredLink.isEmpty()) {
            for (const markdown::RenderedElement &e : std::as_const(m_shown.elements)) {
                if (e.tag != markdown::RenderedElement::Tag::Anchor || e.href != m_hoveredLink || e.to <= e.from)
                    continue;
                const qsizetype semicolon = e.style.indexOf(u';');
                if (!e.style.startsWith(u"color:") || semicolon < 0)
                    continue;
                const QStringView rest = QStringView(e.style).sliced(semicolon);
                if (rest != u";text-decoration:none" && !rest.startsWith(u";text-decoration:none;background-color:"))
                    continue;
                for (qsizetype at = e.from; at < std::min(e.to, m_shownLength);) {
                    const RenderedRun *run = m_shown.runAt(at);
                    const qsizetype end = run ? std::min(run->to, e.to) : e.to;
                    const QFontMetricsF metrics(run ? runFont(*run) : m_font);
                    QColor color = run ? cssColor(run->foreground) : QColor();
                    if (!color.isValid())
                        color = m_color;
                    for (const Segment &segment : segments(at, end)) {
                        const qreal baseline = segment.line.y() + segment.line.ascent();
                        const qreal width = std::max<qreal>(1, std::round(metrics.lineWidth()));
                        const qreal y = std::round(baseline + metrics.underlinePos());
                        overlays.append({Shape::Kind::Underline, QRectF(segment.x1, y, segment.x2 - segment.x1, width),
                                         color});
                    }
                    at = std::max(end, at + 1);
                }
            }
        }
    }
    if (overlays != m_overlays) {
        m_overlays = std::move(overlays);
        m_shapesDirty = true;
        update();
    }
}

void RenderedTextItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (isComponentComplete() && !qFuzzyCompare(newGeometry.width() + 1, m_laidOutWidth + 1))
        relayout();
}

void RenderedTextItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) {
        m_textDirty = true;
        m_shapesDirty = true;
    }
    QQuickItem::itemChange(change, value);
}

QSGNode *RenderedTextItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    QQuickWindow *win = window();
    if (!win) {
        delete old;
        return nullptr;
    }
    const bool software = win->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
    auto *root = static_cast<RootNode *>(old);
    if (root && root->software != software) {
        delete root;
        root = nullptr;
    }
    if (!root) {
        root = new RootNode(software);
        m_textDirty = true;
        m_shapesDirty = true;
    }
    if (m_shapesDirty && !software) {
        QList<Vertex> under;
        QList<Vertex> over;
        for (const Shape &s : std::as_const(m_shapes))
            addRoundedRect(under, s.rect, s.kind == Shape::Kind::Pill ? s.rect.height() / 2 : m_boxRadius, s.color, true);
        for (const Shape &s : std::as_const(m_overlays)) {
            if (s.kind == Shape::Kind::Underline)
                addRoundedRect(over, s.rect, 0, s.color, false);
            else
                addRoundedRect(under, s.rect, s.kind == Shape::Kind::Pill ? s.rect.height() / 2 : m_boxRadius, s.color,
                               true);
        }
        fill(root->under, under);
        fill(root->over, over);
        m_shapesDirty = false;
    }
    if (m_shapesDirty && software) {
        // One image of the item's size with every shape (only when there are any).
        if (root->image) {
            root->removeChildNode(root->image);
            delete std::exchange(root->image, nullptr);
        }
        if (!m_shapes.isEmpty() || !m_overlays.isEmpty()) {
            const qreal dpr = win->effectiveDevicePixelRatio();
            QRectF bounds;
            for (const QList<Shape> *list : {&m_shapes, &m_overlays}) {
                for (const Shape &s : *list)
                    bounds = bounds.united(s.rect);
            }
            bounds = bounds.adjusted(-1, -1, 1, 1).toAlignedRect();
            QImage canvas((bounds.size() * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
            canvas.setDevicePixelRatio(dpr);
            canvas.fill(Qt::transparent);
            QPainter painter(&canvas);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.translate(-bounds.topLeft());
            for (const QList<Shape> *list : {&m_shapes, &m_overlays}) {
                for (const Shape &s : *list) {
                    painter.setBrush(s.color);
                    const qreal radius = s.kind == Shape::Kind::Pill        ? s.rect.height() / 2
                                         : s.kind == Shape::Kind::Underline ? 0
                                                                            : m_boxRadius;
                    painter.drawRoundedRect(s.rect, radius, radius);
                }
            }
            painter.end();
            root->image = win->createImageNode();
            root->image->setTexture(win->createTextureFromImage(canvas));
            root->image->setOwnsTexture(true);
            root->image->setRect(bounds);
            root->prependChildNode(root->image);
        }
        m_shapesDirty = false;
    }
    if (m_textDirty) {
        if (!root->text) {
            root->text = win->createTextNode();
            if (software)
                root->appendChildNode(root->text);
            else
                root->insertChildNodeAfter(root->text, &root->under);
        }
        root->text->clear();
        root->text->setColor(m_color);
        root->text->setLinkColor(m_linkColor);
        root->text->setFiltering(smooth() ? QSGTexture::Linear : QSGTexture::Nearest);
        switch (QQuickWindow::textRenderType()) {
        case QQuickWindow::NativeTextRendering: root->text->setRenderType(QSGTextNode::NativeRendering); break;
        case QQuickWindow::CurveTextRendering: root->text->setRenderType(QSGTextNode::CurveRendering); break;
        default: root->text->setRenderType(QSGTextNode::QtRendering); break;
        }
        if (m_layout && m_layout->lineCount() > 0)
            root->text->addTextLayout(QPointF(0, 0), m_layout.get());
        m_textDirty = false;
    }
    return root;
}

qreal RenderedTextItem::lineTop(int index) const
{
    if (!m_layout || index < 0 || index >= m_layout->lineCount())
        return 0;
    if (m_lineHeight > 0)
        return index * m_lineHeight;
    return m_layout->lineAt(index).y();
}

int RenderedTextItem::lineAtY(qreal y) const
{
    if (!m_layout || m_layout->lineCount() == 0 || y < 0)
        return -1;
    const int n = m_layout->lineCount();
    if (m_lineHeight > 0) {
        const int k = int(std::floor(y / m_lineHeight));
        return k < n ? k : -1;
    }
    for (int k = 0; k < n; ++k) {
        const QTextLine line = m_layout->lineAt(k);
        if (y < line.y() + line.height())
            return k;
    }
    return -1;
}

QString RenderedTextItem::linkAt(qreal x, qreal y) const
{
    const int k = lineAtY(y);
    if (k < 0)
        return {};
    const QTextLine line = m_layout->lineAt(k);
    // On the line's glyphs, not in the rest of the fixed-height line box: what the rich text's
    // exact hit test (QTextDocument's anchorAt) gives.
    const QRectF natural = line.naturalTextRect();
    if (x < natural.left() || x > natural.right() || y < line.y() || y > line.y() + line.height())
        return {};
    const int position = line.xToCursor(x, QTextLine::CursorOnCharacter);
    if (position >= m_shownLength)
        return {};
    const RenderedRun *run = m_shown.runAt(position);
    return run ? run->href : QString();
}

int RenderedTextItem::displayPositionAt(qreal x, qreal y) const
{
    if (!m_layout || m_layout->lineCount() == 0)
        return 0;
    int k = lineAtY(std::max<qreal>(0, y));
    if (k < 0)
        k = m_layout->lineCount() - 1;
    const int position = m_layout->lineAt(k).xToCursor(x);
    return int(std::min<qsizetype>(position, m_shownLength));
}

int RenderedTextItem::positionAt(qreal x, qreal y) const
{
    const int display = displayPositionAt(x, y);
    if (m_shown.source.isEmpty())
        return display;
    return int(m_shown.sourceAt(display));
}

QRectF RenderedTextItem::rectForSource(int from, int to) const
{
    qsizetype a = from;
    qsizetype b = to;
    if (!m_shown.source.isEmpty()) {
        const auto range = m_shown.displayRange(from, to);
        a = range.first;
        b = range.second;
    }
    if (a < 0 || !m_layout)
        return {};
    b = std::min(b, m_shownLength);
    QRectF out;
    if (a == b) {
        const QTextLine line = m_layout->lineForTextPosition(int(a));
        if (!line.isValid())
            return {};
        const int k = line.lineNumber();
        const qreal h = m_lineHeight > 0 ? m_lineHeight : line.height();
        return QRectF(line.cursorToX(int(a)), lineTop(k), 0, h);
    }
    for (const Segment &segment : segments(a, b)) {
        const int k = segment.line.lineNumber();
        const QRectF r(segment.x1, lineTop(k), segment.x2 - segment.x1,
                       m_lineHeight > 0 ? m_lineHeight : segment.line.height());
        out = out.isNull() ? r : out.united(r);
    }
    return out;
}

} // namespace brulion::editor
