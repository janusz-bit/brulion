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

QString toSvg(const Canvas &c, const SvgOptions &options)
{
    const Palette p = paletteFor(options.dark);
    QRectF b = bounds(c);
    if (b.isNull())
        b = QRectF(0, 0, 200, 120);
    b.adjust(-options.padding, -options.padding - GroupLabelHeight, options.padding, options.padding);
    const QSizeF size = svgSize(c, options);
    const QString font = esc(options.fontFamily.isEmpty() ? u"sans-serif"_s : options.fontFamily);
    QString out;
    out += u"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""_s + num(size.width()) + u"\" height=\""_s
           + num(size.height()) + u"\" viewBox=\""_s + num(b.x()) + u' ' + num(b.y()) + u' ' + num(b.width()) + u' '
           + num(b.height()) + u"\" font-family=\""_s + font + u"\">\n"_s;
    out += u"<rect x=\""_s + num(b.x()) + u"\" y=\""_s + num(b.y()) + u"\" width=\""_s + num(b.width())
           + u"\" height=\""_s + num(b.height()) + u"\" fill=\""_s + p.paper + u"\"/>\n"_s;
    out += u"<defs>\n"_s;
    for (qsizetype i = 0; i < c.nodes.size(); ++i) {
        const QRectF r = nodeRect(c.nodes.at(i));
        out += u"<clipPath id=\"c"_s + QString::number(i) + u"\"><rect x=\""_s + num(r.x()) + u"\" y=\""_s + num(r.y())
               + u"\" width=\""_s + num(r.width()) + u"\" height=\""_s + num(r.height()) + u"\" rx=\""_s
               + num(CardRadius) + u"\"/></clipPath>\n"_s;
    }
    out += u"</defs>\n"_s;

    const auto text = [&](double x, double y, const QString &t, double sizePx, const QString &color, bool bold) {
        return u"<text x=\""_s + num(x) + u"\" y=\""_s + num(y) + u"\" font-size=\""_s + num(sizePx) + u"\" fill=\""_s
               + color + u'"' + (bold ? u" font-weight=\"600\""_s : QString()) + u" xml:space=\"preserve\">"_s + esc(t)
               + u"</text>\n"_s;
    };
    const auto linesIn = [&](const QRectF &r, double top, const QString &markdown, const QString &clip) {
        QString s;
        const double width = r.width() - 2 * CardPadding;
        const int chars = std::max(4, int(width / (TextSize * 0.55)));
        const int maxLines = std::max(1, int((r.bottom() - top - CardPadding / 2) / TextLine) + 1);
        QList<int> levels;
        const QStringList lines = plainLines(markdown, chars, std::min(maxLines, 400), &levels);
        s += u"<g clip-path=\"url(#"_s + clip + u")\">\n"_s;
        double y = top;
        for (qsizetype i = 0; i < lines.size(); ++i) {
            const int level = levels.value(i);
            const double sz = level == 1   ? TextSize * 1.5
                              : level == 2 ? TextSize * 1.3
                              : level > 2  ? TextSize * 1.1
                                           : TextSize;
            y += level > 0 ? sz * 1.4 : TextLine;
            s += text(r.x() + CardPadding, y - 6, lines.at(i), sz, p.text, level > 0);
        }
        s += u"</g>\n"_s;
        return s;
    };

    // Groups first (below everything), then edges, then the other cards in their order.
    for (qsizetype i = 0; i < c.nodes.size(); ++i) {
        const Node &n = c.nodes.at(i);
        if (!n.isGroup())
            continue;
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
    for (const Edge &e : c.edges) {
        const EdgeShape s = edgeShape(c, e);
        if (!s.valid)
            continue;
        const QString color = colorHex(e.color, options.dark).isEmpty() ? p.edge : colorHex(e.color, options.dark);
        out += u"<path d=\""_s + svgPath(s) + u"\" fill=\"none\" stroke=\""_s + color
               + u"\" stroke-width=\"3\" stroke-linecap=\"round\"/>\n"_s;
        for (const QList<QPointF> *arrow : {&s.startArrow, &s.endArrow}) {
            if (arrow->size() != 3)
                continue;
            out += u"<polygon points=\""_s;
            for (const QPointF &pt : *arrow)
                out += num(pt.x()) + u',' + num(pt.y()) + u' ';
            out.chop(1);
            out += u"\" fill=\""_s + color + u"\"/>\n"_s;
        }
        if (!e.label.trimmed().isEmpty()) {
            const QString label = e.label.simplified().left(200);
            const double w = label.size() * TextSize * 0.55 + 16;
            out += u"<rect x=\""_s + num(s.labelPoint.x() - w / 2) + u"\" y=\""_s + num(s.labelPoint.y() - 14)
                   + u"\" width=\""_s + num(w) + u"\" height=\"28\" rx=\"6\" fill=\""_s + p.paper + u"\"/>\n"_s;
            out += u"<text x=\""_s + num(s.labelPoint.x()) + u"\" y=\""_s + num(s.labelPoint.y() + 5)
                   + u"\" font-size=\""_s + num(TextSize) + u"\" fill=\""_s + p.text
                   + u"\" text-anchor=\"middle\" xml:space=\"preserve\">"_s + esc(label) + u"</text>\n"_s;
        }
    }
    for (qsizetype i = 0; i < c.nodes.size(); ++i) {
        const Node &n = c.nodes.at(i);
        if (n.isGroup())
            continue;
        const QRectF r = nodeRect(n);
        const QString clip = u"c"_s + QString::number(i);
        const QString color = colorHex(n.color, options.dark);
        out += u"<rect x=\""_s + num(r.x()) + u"\" y=\""_s + num(r.y()) + u"\" width=\""_s + num(r.width())
               + u"\" height=\""_s + num(r.height()) + u"\" rx=\""_s + num(CardRadius) + u"\" fill=\""_s + p.card
               + u"\"/>\n"_s;
        if (!color.isEmpty()) {
            out += u"<rect x=\""_s + num(r.x()) + u"\" y=\""_s + num(r.y()) + u"\" width=\""_s + num(r.width())
                   + u"\" height=\""_s + num(r.height()) + u"\" rx=\""_s + num(CardRadius) + u"\" fill=\""_s + color
                   + u"\" fill-opacity=\"0.1\"/>\n"_s;
        }
        if (n.isFile()) {
            const QString name = fileName(n.file) + n.subpath;
            const QString data = options.pictureDataUrl ? options.pictureDataUrl(n.file) : QString();
            if (!data.isEmpty() && safeDataUrl(data)) {
                out += u"<image x=\""_s + num(r.x()) + u"\" y=\""_s + num(r.y()) + u"\" width=\""_s + num(r.width())
                       + u"\" height=\""_s + num(r.height())
                       + u"\" preserveAspectRatio=\"xMidYMid meet\" clip-path=\"url(#"_s + clip + u")\" href=\""_s
                       + esc(data) + u"\"/>\n"_s;
            } else {
                out += u"<g clip-path=\"url(#"_s + clip + u")\">"_s
                       + text(r.x() + CardPadding, r.y() + CardPadding + TextSize, name, TextSize, p.muted, true)
                       + u"</g>\n"_s;
                const QString note = options.noteText ? options.noteText(n.file, n.subpath) : QString();
                if (!note.isEmpty())
                    out += linesIn(r, r.y() + CardPadding + TextLine, note, clip);
            }
        } else if (n.isLink()) {
            out += u"<g clip-path=\"url(#"_s + clip + u")\">"_s
                   + text(r.x() + CardPadding, r.y() + CardPadding + TextSize, n.url.trimmed(), TextSize, p.muted,
                          false)
                   + u"</g>\n"_s;
        } else {
            out += linesIn(r, r.y() + CardPadding - 4, n.text, clip);
        }
        out += u"<rect x=\""_s + num(r.x()) + u"\" y=\""_s + num(r.y()) + u"\" width=\""_s + num(r.width())
               + u"\" height=\""_s + num(r.height()) + u"\" rx=\""_s + num(CardRadius) + u"\" fill=\"none\" stroke=\""_s
               + (color.isEmpty() ? p.border : color) + u"\" stroke-width=\"2\"/>\n"_s;
    }
    out += u"</svg>\n"_s;
    return out;
}

} // namespace brulion::canvas
