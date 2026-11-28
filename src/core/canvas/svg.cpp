#include "canvas/svg.h"

#include "canvas/geometry.h"
#include "markdown/blockclipboard.h"

#include <QRegularExpression>
#include <QSizeF>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::canvas {

namespace {

constexpr double CardRadius = 10;
constexpr double TextSize = 15;
constexpr double TextLine = 22;
constexpr double CardPadding = 14;

QString num(double v)
{
    if (!std::isfinite(v))
        return u"0"_s;
    const double r = std::round(v * 100) / 100;
    QString s = QString::number(r, 'f', 2);
    while (s.contains(u'.') && (s.endsWith(u'0') || s.endsWith(u'.')))
        s.chop(1);
    return s == u"-0" ? u"0"_s : s;
}

QString esc(const QString &s)
{
    QString out = s.toHtmlEscaped();
    // Characters XML 1.0 does not allow.
    out.removeIf([](QChar c) {
        const char16_t u = c.unicode();
        return (u < 0x20 && u != u'\t' && u != u'\n' && u != u'\r') || u == 0xFFFE || u == 0xFFFF;
    });
    return out;
}

/// Only data: URLs of picture formats, base64 (nothing from a file refers outside the picture).
bool safeDataUrl(QStringView url)
{
    static const QRegularExpression re(
            u"^data:image/(png|jpeg|gif|webp|bmp|avif|svg\\+xml);base64,[A-Za-z0-9+/=\\s]*$"_s);
    return url.size() < 64 * 1024 * 1024 && re.matchView(url).hasMatch();
}

struct Palette {
    QString paper, card, border, text, muted, edge;
};

Palette paletteFor(bool dark)
{
    if (dark)
        return {u"#1e1e1e"_s, u"#262626"_s, u"#4a4a4a"_s, u"#dadada"_s, u"#9a9a9a"_s, u"#8a8a8a"_s};
    return {u"#fafafa"_s, u"#ffffff"_s, u"#cfcfcf"_s, u"#222222"_s, u"#6b6b6b"_s, u"#8a8a8a"_s};
}

/// Wrap one line at `width` characters (by words; a long word is cut).
QStringList wrap(const QString &line, int width)
{
    QStringList out;
    if (width < 4)
        width = 4;
    // The indentation stays on the first line (a nested list item).
    qsizetype indent = 0;
    while (indent < line.size() && line.at(indent) == u' ')
        ++indent;
    QString current = line.left(indent);
    const QStringList words = line.mid(indent).split(u' ', Qt::SkipEmptyParts);
    for (QString word : words) {
        while (word.size() > width) {
            if (!current.isEmpty()) {
                out.append(current);
                current.clear();
            }
            out.append(word.left(width));
            word = word.mid(width);
        }
        if (current.trimmed().isEmpty())
            current += word;
        else if (current.size() + 1 + word.size() <= width)
            current += u' ' + word;
        else {
            out.append(current);
            current = word;
        }
    }
    if (!current.isEmpty() || out.isEmpty())
        out.append(current);
    return out;
}

QString fileName(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return slash < 0 ? path : path.mid(slash + 1);
}

} // namespace

QStringList plainLines(const QString &markdown, int charsPerLine, int maxLines, QList<int> *headingLevels)
{
    QStringList out;
    if (headingLevels)
        headingLevels->clear();
    QString text = markdown;
    text.replace(u"\r\n"_s, u"\n"_s);
    bool fenced = false;
    const QStringList lines = text.split(u'\n');
    static const QRegularExpression heading(u"^\\s{0,3}(#{1,6})\\s+(.*)$"_s);
    static const QRegularExpression bullet(u"^(\\s*)([-*+]|\\d+[.)])\\s+(\\[.\\]\\s+)?(.*)$"_s);
    for (const QString &raw : lines) {
        if (out.size() >= maxLines)
            break;
        const QString trimmed = raw.trimmed();
        if (trimmed.startsWith(u"```"_s) || trimmed.startsWith(u"~~~"_s)) {
            fenced = !fenced;
            continue;
        }
        QString line;
        int level = 0;
        if (fenced) {
            line = raw;
        } else if (const auto m = heading.match(raw); m.hasMatch()) {
            level = int(m.capturedLength(1));
            line = markdown::stripInlineMarkup(m.captured(2));
        } else if (const auto b = bullet.match(raw); b.hasMatch()) {
            const QString box = b.captured(3).trimmed();
            const QString mark = box.isEmpty() ? u"•"_s : box == u"[ ]"_s ? u"[ ]"_s : u"[x]"_s;
            line = QString(b.capturedLength(1) / 2, u' ') + mark + u' ' + markdown::stripInlineMarkup(b.captured(4));
        } else if (trimmed.startsWith(u'>')) {
            line = u"│ "_s + markdown::stripInlineMarkup(trimmed.mid(1).trimmed());
        } else {
            line = markdown::stripInlineMarkup(raw);
        }
        const QStringList wrapped = wrap(line, level > 0 ? std::max(4, charsPerLine * 3 / 4) : charsPerLine);
        for (const QString &w : wrapped) {
            if (out.size() >= maxLines)
                break;
            out.append(w);
            if (headingLevels)
                headingLevels->append(level);
        }
    }
    while (!out.isEmpty() && out.constLast().trimmed().isEmpty()) {
        out.removeLast();
        if (headingLevels)
            headingLevels->removeLast();
    }
    return out;
}

QString polygonOf(const QList<QPointF> &points, const QString &attrs)
{
    QString pts;
    for (const QPointF &pt : points)
        pts += num(pt.x()) + u',' + num(pt.y()) + u' ';
    return u"<polygon points=\""_s + pts.trimmed() + u"\" "_s + attrs + u"/>\n"_s;
}

QString ellipseOf(QPointF centre, double rx, double ry, const QString &attrs)
{
    return u"<ellipse cx=\""_s + num(centre.x()) + u"\" cy=\""_s + num(centre.y()) + u"\" rx=\""_s + num(rx)
           + u"\" ry=\""_s + num(ry) + u"\" "_s + attrs + u"/>\n"_s;
}

/// A document's outline: its bottom a wave.
QString documentOf(const QRectF &r, const QString &attrs)
{
    const double wave = std::min(r.height() / 6, 16.0);
    const QString low = num(r.bottom() - wave);
    return u"<path d=\"M"_s + num(r.x()) + u' ' + num(r.y()) + u" H"_s + num(r.right()) + u" V"_s + low + u" Q"_s
           + num(r.right() - r.width() / 4) + u' ' + num(r.bottom() - 2 * wave) + u' ' + num(r.center().x()) + u' '
           + low + u" T"_s + num(r.x()) + u' ' + low + u" Z\" "_s + attrs + u"/>\n"_s;
}

/// A rectangle's outline (a pill: round ends, a database: square, else the card's corners) with a
/// predefined process's two inner lines or a database's lid.
QString boxOf(const QRectF &r, const QString &shape, const QString &attrs)
{
    const double radius = shape == u"pill" ? r.height() / 2 : shape == u"database" ? 0 : CardRadius;
    QString out = u"<rect x=\""_s + num(r.x()) + u"\" y=\""_s + num(r.y()) + u"\" width=\""_s + num(r.width())
                  + u"\" height=\""_s + num(r.height()) + u"\" rx=\""_s + num(radius) + u"\" "_s + attrs + u"/>\n"_s;
    if (shape == u"predefined-process") {
        const double inset = std::min(r.width() / 10, 20.0);
        for (const double at : {r.left() + inset, r.right() - inset})
            out += u"<line x1=\""_s + num(at) + u"\" y1=\""_s + num(r.y()) + u"\" x2=\""_s + num(at) + u"\" y2=\""_s
                   + num(r.bottom()) + u"\" "_s + attrs + u"/>\n"_s;
    } else if (shape == u"database") {
        out += ellipseOf({r.center().x(), r.y()}, r.width() / 2, std::min(r.height() / 6, 12.0), attrs);
    }
    return out;
}

/// The outline of a card of `shape` (nodeStyle()) in `r` as SVG, `attrs` its fill and stroke.
QString shapeOf(const QRectF &r, const QString &shape, const QString &attrs)
{
    if (shape == u"circle")
        return ellipseOf(r.center(), r.width() / 2, r.height() / 2, attrs);
    if (shape == u"diamond")
        return polygonOf({{r.center().x(), r.top()}, {r.right(), r.center().y()}, {r.center().x(), r.bottom()},
                          {r.left(), r.center().y()}},
                         attrs);
    if (shape == u"parallelogram") {
        const double skew = std::min(r.width() / 4, r.height() / 2);
        return polygonOf({{r.left() + skew, r.top()}, {r.right(), r.top()}, {r.right() - skew, r.bottom()},
                          {r.left(), r.bottom()}},
                         attrs);
    }
    if (shape == u"document")
        return documentOf(r, attrs);
    return boxOf(r, shape, attrs);
}

/// The stroke of a card's outline: its colour, 2 px, dashed or dotted; none when invisible.
QString strokeOf(const QString &color, const QString &border)
{
    if (border == u"invisible")
        return u"fill=\"none\" stroke=\"none\""_s;
    QString out = u"fill=\"none\" stroke=\""_s + color + u"\" stroke-width=\"2\""_s;
    if (border == u"dashed")
        out += u" stroke-dasharray=\"8 6\""_s;
    else if (border == u"dotted")
        out += u" stroke-dasharray=\"2 4\""_s;
    return out;
}

/// `inner` (a card's lines, drawn from its left padding) aligned as `align` says in `r`.
QString aligned(const QString &inner, const QString &align, const QRectF &r)
{
    if (align.isEmpty())
        return inner;
    const bool centre = align == u"center";
    const double dx = centre ? r.width() / 2 - CardPadding : r.width() - 2 * CardPadding;
    return u"<g text-anchor=\""_s + (centre ? u"middle"_s : u"end"_s) + u"\" transform=\"translate("_s + num(dx)
           + u" 0)\">\n"_s + inner + u"</g>\n"_s;
}

QSizeF svgSize(const Canvas &c, const SvgOptions &options)
{
    QRectF b = bounds(c);
    if (b.isNull())
        b = QRectF(0, 0, 200, 120);
    // Group labels above their frames.
    b.adjust(-options.padding, -options.padding - GroupLabelHeight, options.padding, options.padding);
    const double scale = std::min(1.0, options.maxPixels / std::max(b.width(), b.height()));
    return {std::max(1.0, std::round(b.width() * scale)), std::max(1.0, std::round(b.height() * scale))};
}

namespace {

/// The picture of a canvas as it is written: the palette, the options, the SVG so far.
struct Writer {
    Palette p;
    const SvgOptions &options;
    QString out;

    static QString text(double x, double y, const QString &t, double sizePx, const QString &color, bool bold)
    {
        return u"<text x=\""_s + num(x) + u"\" y=\""_s + num(y) + u"\" font-size=\""_s + num(sizePx) + u"\" fill=\""_s
               + color + u'"' + (bold ? u" font-weight=\"600\""_s : QString()) + u" xml:space=\"preserve\">"_s + esc(t)
               + u"</text>\n"_s;
    }

    /// The size of a line of heading `level` (0: body text).
    static double lineSize(int level)
    {
        static constexpr double scale[] = {1.0, 1.5, 1.3, 1.1};
        return TextSize * scale[std::clamp(level, 0, 3)];
    }

    /// A card's Markdown as lines from `top` down, clipped to the card.
    QString linesIn(const QRectF &r, double top, const QString &markdown, const QString &clip) const
    {
        const double width = r.width() - 2 * CardPadding;
        const int chars = std::max(4, int(width / (TextSize * 0.55)));
        const int maxLines = std::max(1, int((r.bottom() - top - CardPadding / 2) / TextLine) + 1);
        QList<int> levels;
        const QStringList lines = plainLines(markdown, chars, std::min(maxLines, 400), &levels);
        QString s = u"<g clip-path=\"url(#"_s + clip + u")\">\n"_s;
        double y = top;
        for (qsizetype i = 0; i < lines.size(); ++i) {
            const int level = levels.value(i);
            const double sz = lineSize(level);
            y += level > 0 ? sz * 1.4 : TextLine;
            s += text(r.x() + CardPadding, y - 6, lines.at(i), sz, p.text, level > 0);
        }
        return s + u"</g>\n"_s;
    }

    void group(const Node &n)
    {
        const QRectF r = nodeRect(n);
        const QString color = colorHex(n.color, options.dark);
        const QString stroke = color.isEmpty() ? p.border : color;
        out += u"<rect x=\""_s + num(r.x()) + u"\" y=\""_s + num(r.y()) + u"\" width=\""_s + num(r.width())
               + u"\" height=\""_s + num(r.height()) + u"\" rx=\""_s + num(CardRadius) + u"\" fill=\""_s
               + (color.isEmpty() ? p.card : color) + u"\" fill-opacity=\""_s
               + (color.isEmpty() ? u"0.35"_s : u"0.08"_s) + u"\" stroke=\""_s + stroke
               + u"\" stroke-width=\"2\"/>\n"_s;
        if (!n.label.isEmpty())
            out += text(r.x() + 4, r.y() - 10, n.label.simplified(), TextSize * 1.15, color.isEmpty() ? p.muted : color,
                        true);
    }

    void edge(const Canvas &c, const Edge &e)
    {
        const EdgeShape s = edgeShape(c, e);
        if (!s.valid)
            return;
        const QString own = colorHex(e.color, options.dark);
        const QString color = own.isEmpty() ? p.edge : own;
        out += u"<path d=\""_s + svgPath(s) + u"\" fill=\"none\" stroke=\""_s + color
               + u"\" stroke-width=\"3\" stroke-linecap=\"round\"/>\n"_s;
        for (const QList<QPointF> *arrow : {&s.startArrow, &s.endArrow}) {
            if (arrow->size() == 3)
                out += polygonOf(*arrow, u"fill=\""_s + color + u'"');
        }
        if (!e.label.trimmed().isEmpty())
            edgeLabel(s.labelPoint, e.label.simplified().left(200));
    }

    void edgeLabel(QPointF at, const QString &label)
    {
        const double w = label.size() * TextSize * 0.55 + 16;
        out += u"<rect x=\""_s + num(at.x() - w / 2) + u"\" y=\""_s + num(at.y() - 14) + u"\" width=\""_s + num(w)
               + u"\" height=\"28\" rx=\"6\" fill=\""_s + p.paper + u"\"/>\n"_s;
        out += u"<text x=\""_s + num(at.x()) + u"\" y=\""_s + num(at.y() + 5) + u"\" font-size=\""_s + num(TextSize)
               + u"\" fill=\""_s + p.text + u"\" text-anchor=\"middle\" xml:space=\"preserve\">"_s + esc(label)
               + u"</text>\n"_s;
    }

    /// A one-line muted title at a card's top (a file's name, a link's address).
    QString titleLine(const QRectF &r, const QString &clip, const QString &title, bool bold) const
    {
        return u"<g clip-path=\"url(#"_s + clip + u")\">"_s
               + text(r.x() + CardPadding, r.y() + CardPadding + TextSize, title, TextSize, p.muted, bold) + u"</g>\n"_s;
    }

    /// A file card's content: its picture, else its name and the note's text.
    QString fileContent(const Node &n, const QRectF &r, const QString &clip) const
    {
        const QString data = options.pictureDataUrl ? options.pictureDataUrl(n.file) : QString();
        if (!data.isEmpty() && safeDataUrl(data))
            return u"<image x=\""_s + num(r.x()) + u"\" y=\""_s + num(r.y()) + u"\" width=\""_s + num(r.width())
                   + u"\" height=\""_s + num(r.height()) + u"\" preserveAspectRatio=\"xMidYMid meet\" clip-path=\"url(#"_s
                   + clip + u")\" href=\""_s + esc(data) + u"\"/>\n"_s;
        QString s = titleLine(r, clip, fileName(n.file) + n.subpath, true);
        const QString note = options.noteText ? options.noteText(n.file, n.subpath) : QString();
        if (!note.isEmpty())
            s += linesIn(r, r.y() + CardPadding + TextLine, note, clip);
        return s;
    }

    void card(const Node &n, qsizetype index)
    {
        const QRectF r = nodeRect(n);
        const QString clip = u"c"_s + QString::number(index);
        const QString color = colorHex(n.color, options.dark);
        const NodeStyle style = nodeStyle(n);
        out += shapeOf(r, style.shape, u"fill=\""_s + p.card + u"\" stroke=\"none\""_s);
        if (!color.isEmpty())
            out += shapeOf(r, style.shape, u"fill=\""_s + color + u"\" fill-opacity=\"0.1\" stroke=\"none\""_s);
        if (n.isFile())
            out += fileContent(n, r, clip);
        else if (n.isLink())
            out += titleLine(r, clip, n.url.trimmed(), false);
        else
            out += aligned(linesIn(r, r.y() + CardPadding - 4, n.text, clip), style.textAlign, r);
        out += shapeOf(r, style.shape, strokeOf(color.isEmpty() ? p.border : color, style.border));
    }
};

} // namespace

QString toSvg(const Canvas &c, const SvgOptions &options)
{
    Writer w{paletteFor(options.dark), options, {}};
    QRectF b = bounds(c);
    if (b.isNull())
        b = QRectF(0, 0, 200, 120);
    b.adjust(-options.padding, -options.padding - GroupLabelHeight, options.padding, options.padding);
    const QSizeF size = svgSize(c, options);
    const QString font = esc(options.fontFamily.isEmpty() ? u"sans-serif"_s : options.fontFamily);
    w.out += u"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""_s + num(size.width()) + u"\" height=\""_s
             + num(size.height()) + u"\" viewBox=\""_s + num(b.x()) + u' ' + num(b.y()) + u' ' + num(b.width()) + u' '
             + num(b.height()) + u"\" font-family=\""_s + font + u"\">\n"_s;
    w.out += u"<rect x=\""_s + num(b.x()) + u"\" y=\""_s + num(b.y()) + u"\" width=\""_s + num(b.width())
             + u"\" height=\""_s + num(b.height()) + u"\" fill=\""_s + w.p.paper + u"\"/>\n"_s;
    w.out += u"<defs>\n"_s;
    for (qsizetype i = 0; i < c.nodes.size(); ++i) {
        const QRectF r = nodeRect(c.nodes.at(i));
        w.out += u"<clipPath id=\"c"_s + QString::number(i) + u"\"><rect x=\""_s + num(r.x()) + u"\" y=\""_s
                 + num(r.y()) + u"\" width=\""_s + num(r.width()) + u"\" height=\""_s + num(r.height()) + u"\" rx=\""_s
                 + num(CardRadius) + u"\"/></clipPath>\n"_s;
    }
    w.out += u"</defs>\n"_s;
    // Groups first (below everything), then edges, then the other cards in their order.
    for (const Node &n : c.nodes) {
        if (n.isGroup())
            w.group(n);
    }
    for (const Edge &e : c.edges)
        w.edge(c, e);
    for (qsizetype i = 0; i < c.nodes.size(); ++i) {
        if (!c.nodes.at(i).isGroup())
            w.card(c.nodes.at(i), i);
    }
    return w.out + u"</svg>\n"_s;
}

} // namespace brulion::canvas
