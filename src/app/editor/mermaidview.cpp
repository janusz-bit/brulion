#include "mermaidview.h"

#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

using diagram::Error;

QFont fontOf(const QString &family, qreal pixelSize, bool bold)
{
    QFont f;
    if (!family.isEmpty())
        f.setFamily(family);
    // (Whole pixels: the labels are measured and drawn in the same font.)
    f.setPixelSize(std::max(1, int(std::lround(pixelSize))));
    f.setBold(bold);
    return f;
}

QColor qcolorOf(const diagram::Paint &paint, const diagram::Palette &palette)
{
    const QString c = diagram::colorOf(paint, palette);
    if (c.isEmpty())
        return {};
    // "#rrggbbaa" (CSS) → QColor's "#aarrggbb".
    if (c.size() == 9) {
        QColor q(c.left(7));
        q.setAlpha(c.sliced(7).toInt(nullptr, 16));
        return q;
    }
    return QColor(c);
}

QPainterPath toPath(const QList<diagram::PathOp> &ops)
{
    QPainterPath p;
    for (const diagram::PathOp &op : ops) {
        switch (op.type) {
        case diagram::PathOp::Type::Move: p.moveTo(op.p); break;
        case diagram::PathOp::Type::Line: p.lineTo(op.p); break;
        case diagram::PathOp::Type::Cubic: p.cubicTo(op.c1, op.c2, op.p); break;
        case diagram::PathOp::Type::Close: p.closeSubpath(); break;
        }
    }
    return p;
}

/// The laid-out sources, most recently used last.
struct Cache {
    static constexpr int Size = 64;
    QList<std::pair<QString, std::shared_ptr<const diagram::Result>>> entries;
};

Cache &cache()
{
    static Cache c;
    return c;
}

} // namespace

MermaidView::MermaidView(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    relayout();
}

MermaidView::~MermaidView() = default;

void MermaidView::setSource(const QString &source)
{
    if (source == m_source)
        return;
    m_source = source;
    Q_EMIT sourceChanged();
    relayout();
}

void MermaidView::setFontFamily(const QString &family)
{
    if (family == m_fontFamily)
        return;
    m_fontFamily = family;
    Q_EMIT fontChanged();
    relayout();
}

void MermaidView::setFontPixelSize(qreal size)
{
    size = std::clamp(size, 6.0, 64.0);
    if (qFuzzyCompare(size, m_fontPixelSize))
        return;
    m_fontPixelSize = size;
    Q_EMIT fontChanged();
    relayout();
}

#define BRULION_MERMAID_COLOR_SETTER(name, member)                                                                     \
    void MermaidView::name(const QColor &c)                                                                            \
    {                                                                                                                  \
        if (c == member)                                                                                               \
            return;                                                                                                    \
        member = c;                                                                                                    \
        Q_EMIT colorsChanged();                                                                                        \
        update();                                                                                                      \
    }
BRULION_MERMAID_COLOR_SETTER(setTextColor, m_text)
BRULION_MERMAID_COLOR_SETTER(setMutedColor, m_muted)
BRULION_MERMAID_COLOR_SETTER(setBackgroundColor, m_background)
BRULION_MERMAID_COLOR_SETTER(setBorderColor, m_border)
BRULION_MERMAID_COLOR_SETTER(setAccentColor, m_accent)
#undef BRULION_MERMAID_COLOR_SETTER

void MermaidView::setDark(bool dark)
{
    if (dark == m_dark)
        return;
    m_dark = dark;
    Q_EMIT colorsChanged();
    update();
}

QString MermaidView::status() const
{
    if (m_result->ok())
        return u"ok"_s;
    return m_result->error->code == Error::Code::Unsupported ? u"unsupported"_s : u"error"_s;
}

QString MermaidView::message() const
{
    return m_result->ok() ? QString() : errorMessage(*m_result->error, m_result->typeName);
}

QString MermaidView::typeName() const
{
    return m_result->typeName;
}

QString MermaidView::description() const
{
    if (!m_result->scene.title.isEmpty())
        return m_result->scene.title;
    switch (m_result->kind) {
    case diagram::Kind::Flowchart: return tr("Flowchart");
    case diagram::Kind::Sequence: return tr("Sequence diagram");
    case diagram::Kind::Pie: return tr("Pie chart");
    case diagram::Kind::Unsupported:
    case diagram::Kind::Unknown: break;
    }
    return tr("Diagram");
}

QSizeF MermaidView::diagramSize() const
{
    return m_result->ok() ? m_result->scene.size : QSizeF();
}

std::shared_ptr<const diagram::Result> MermaidView::layout(const QString &source, const QString &family,
                                                           qreal pixelSize)
{
    const QString key = family + u'\x1f' + QString::number(pixelSize) + u'\x1f' + source;
    auto &entries = cache().entries;
    for (qsizetype i = entries.size() - 1; i >= 0; --i) {
        if (entries[i].first == key) {
            auto hit = entries.takeAt(i);
            entries.append(hit);
            return hit.second;
        }
    }
    auto result = std::make_shared<const diagram::Result>(diagram::render(source, fontMetrics(family, pixelSize)));
    entries.append({key, result});
    while (entries.size() > Cache::Size)
        entries.removeFirst();
    return result;
}

diagram::Metrics MermaidView::fontMetrics(const QString &family, qreal pixelSize)
{
    diagram::Metrics m;
    m.fontSize = pixelSize;
    // QFontMetricsF per size and weight, made when first needed (one layout uses a few sizes).
    auto metrics = std::make_shared<QHash<qint64, QFontMetricsF>>();
    m.measure = [family, metrics](QStringView line, qreal px, bool bold) {
        const qint64 key = (qint64(std::lround(px * 100)) << 1) | (bold ? 1 : 0);
        auto it = metrics->find(key);
        if (it == metrics->end())
            it = metrics->insert(key, QFontMetricsF(fontOf(family, px, bold)));
        return QSizeF(it->horizontalAdvance(line.toString()), it->height());
    };
    return m;
}

void MermaidView::relayout()
{
    m_result = layout(m_source, m_fontFamily, m_fontPixelSize);
    updateImplicitSize();
    Q_EMIT resultChanged();
    update();
}

void MermaidView::updateImplicitSize()
{
    const QSizeF s = diagramSize();
    setImplicitWidth(s.width());
    const qreal scale = s.width() > 0 && width() > 0 ? std::min<qreal>(1, width() / s.width()) : 1;
    setImplicitHeight(std::ceil(s.height() * scale));
}

void MermaidView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.width() != oldGeometry.width())
        updateImplicitSize();
}

diagram::Palette MermaidView::palette() const
{
    diagram::ThemeColors t;
    t.text = m_text.name(QColor::HexRgb);
    t.mutedText = m_muted.name(QColor::HexRgb);
    t.background = m_background.name(QColor::HexRgb);
    t.border = m_border.name(QColor::HexRgb);
    t.accent = m_accent.name(QColor::HexRgb);
    t.dark = m_dark;
    return diagram::themedPalette(t);
}

void MermaidView::paint(QPainter *painter)
{
    if (!m_result->ok())
        return;
    const QSizeF s = m_result->scene.size;
    if (s.width() <= 0 || s.height() <= 0)
        return;
    const qreal scale = std::min<qreal>(1, width() / s.width());
    painter->save();
    painter->translate((width() - s.width() * scale) / 2, 0);
    painter->scale(scale, scale);
    paintScene(painter, m_result->scene, palette(), m_fontFamily);
    painter->restore();
}

void MermaidView::paintScene(QPainter *painter, const diagram::Scene &scene, const diagram::Palette &palette,
                             const QString &family)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    for (const diagram::Item &it : scene.items) {
        if (it.type == diagram::Item::Type::Path) {
            const QPainterPath path = toPath(it.path);
            const QColor fill = qcolorOf(it.fill, palette);
            if (fill.isValid())
                painter->fillPath(path, fill);
            const QColor stroke = qcolorOf(it.stroke, palette);
            if (stroke.isValid() && it.strokeWidth > 0) {
                QPen pen(stroke, it.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                if (it.dash != diagram::Dash::Solid) {
                    const qreal w = std::max<qreal>(0.5, it.strokeWidth);
                    pen.setCapStyle(Qt::FlatCap);
                    pen.setDashPattern(it.dash == diagram::Dash::Dotted ? QList<qreal>{2 / w, 3 / w}
                                                                        : QList<qreal>{6 / w, 4 / w});
                }
                painter->strokePath(path, pen);
            }
            continue;
        }
        const QColor bg = qcolorOf(it.background, palette);
        if (bg.isValid()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->drawRoundedRect(it.box, 3, 3);
        }
        const QColor color = qcolorOf(it.color, palette);
        if (!color.isValid())
            continue;
        painter->setFont(fontOf(family, it.fontSize, it.bold));
        painter->setPen(color);
        const qreal total = it.lineHeight * it.lines.size();
        const qreal top = it.box.center().y() - total / 2;
        const Qt::Alignment h =
                it.align & Qt::AlignHorizontal_Mask ? it.align & Qt::AlignHorizontal_Mask : Qt::AlignHCenter;
        for (qsizetype i = 0; i < it.lines.size(); ++i) {
            // A little wider than the box: the font's own rounding never wraps or cuts a line.
            const QRectF line(it.box.left() - 50, top + i * it.lineHeight, it.box.width() + 100, it.lineHeight);
            QRectF r = line;
            if (h & Qt::AlignLeft)
                r.setLeft(it.box.left());
            else if (h & Qt::AlignRight)
                r.setRight(it.box.right());
            painter->drawText(r, int(h | Qt::AlignVCenter | Qt::TextSingleLine), it.lines[i]);
        }
    }
}

QString MermaidView::errorMessage(const Error &error, const QString &typeName)
{
    const QString line = QString::number(error.line);
    QString text;
    switch (error.code) {
    case Error::Code::Empty: return tr("The diagram is empty.");
    case Error::Code::UnknownType:
        return tr("Line %1: the diagram does not start with a Mermaid type (graph, flowchart, sequenceDiagram, pie…).")
                .arg(line);
    case Error::Code::Unsupported:
        return tr("Brulion does not draw “%1” diagrams yet: this is their source.").arg(typeName);
    case Error::Code::Syntax: return tr("Line %1: cannot read “%2”.").arg(line, error.detail);
    case Error::Code::UnterminatedText: return tr("Line %1: a quote or a bracket is not closed.").arg(line);
    case Error::Code::UnexpectedEnd: return tr("Line %1: “end” without a block to close.").arg(line);
    case Error::Code::UnclosedBlock: return tr("Line %1: this block has no “end”.").arg(line);
    case Error::Code::InvalidValue: return tr("Line %1: “%2” is not a number.").arg(line, error.detail);
    case Error::Code::NoData: return tr("The chart has no value above zero.");
    case Error::Code::TooLarge: {
        if (error.detail == u"nodes")
            text = tr("more than %1 nodes").arg(diagram::limits::MaxNodes);
        else if (error.detail == u"links")
            text = tr("more than %1 links").arg(diagram::limits::MaxEdges);
        else if (error.detail == u"subgraphs")
            text = tr("more than %1 subgraphs").arg(diagram::limits::MaxClusters);
        else if (error.detail == u"nesting")
            text = tr("blocks nested more than %1 deep").arg(diagram::limits::MaxNesting);
        else if (error.detail == u"participants")
            text = tr("more than %1 participants").arg(diagram::limits::MaxParticipants);
        else if (error.detail == u"messages")
            text = tr("more than %1 messages and notes").arg(diagram::limits::MaxMessages);
        else if (error.detail == u"slices")
            text = tr("more than %1 slices").arg(diagram::limits::MaxSlices);
        else if (error.detail == u"source")
            text = tr("more than %1 kB of text").arg(diagram::limits::MaxSourceBytes / 1024);
        else
            text = tr("too many long links");
        return error.line > 0 ? tr("Line %1: the diagram is too large to draw (%2).").arg(line, text)
                              : tr("The diagram is too large to draw (%1).").arg(text);
    }
    }
    return tr("Line %1: cannot read the diagram.").arg(line);
}

} // namespace brulion::editor
