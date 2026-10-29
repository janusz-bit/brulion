#include "drawing/svgwrite.h"

#include "drawing/excalidraw.h"
#include "drawing/geometry.h"
#include "drawing/palette.h"
#include "drawing/stroke.h"

#include <QJsonObject>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

QString safeColor(const QString &color, const QString &fallback)
{
    const QString c = color.trimmed();
    if (c.size() != 4 && c.size() != 7 && c.size() != 9)
        return fallback;
    if (!c.startsWith(u'#'))
        return fallback;
    for (qsizetype i = 1; i < c.size(); ++i) {
        const char16_t u = c.at(i).unicode();
        if (!((u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'f') || (u >= u'A' && u <= u'F')))
            return fallback;
    }
    return c.toLower();
}

QRectF pictureRect(const Drawing &drawing)
{
    const QRectF content = bounds(drawing);
    double left = 0, top = 0, right = EmptyWidth, bottom = EmptyHeight;
    if (!content.isEmpty() || !drawing.strokes.isEmpty()) {
        left = std::floor(content.left() - SvgMargin);
        top = std::floor(content.top() - SvgMargin);
        right = std::ceil(content.right() + SvgMargin);
        bottom = std::ceil(content.bottom() + SvgMargin);
    }
    // (Strokes stay within CoordinateLimit when read or drawn; the clamp keeps the size an int
    // whatever the caller passes.)
    return QRectF(left, top, int(std::clamp(right - left, 1.0, 1e9)), int(std::clamp(bottom - top, 1.0, 1e9)));
}

namespace {

/// Text for an SVG text node or attribute: & < > " ' escaped; characters XML forbids (controls
/// but tab, lone surrogates, U+FFFE/U+FFFF) left out.
QString xmlText(const QString &text)
{
    QString out;
    out.reserve(text.size() + 8);
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        const char16_t u = c.unicode();
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            out += c;
            out += text.at(++i);
            continue;
        }
        if (c.isSurrogate() || u == 0xfffe || u == 0xffff || (u < 0x20 && u != u'\t'))
            continue;
        switch (u) {
        case u'&': out += u"&amp;"_s; break;
        case u'<': out += u"&lt;"_s; break;
        case u'>': out += u"&gt;"_s; break;
        case u'"': out += u"&quot;"_s; break;
        case u'\'': out += u"&apos;"_s; break;
        default: out += c;
        }
    }
    return out;
}

constexpr QByteArrayView SvgSourceComment = "<!-- svg-source:excalidraw -->";

/// The scene as excalidraw.com's "Embed scene" writes it into an SVG (excalidraw.h).
QByteArray sceneMetadata(const Drawing &drawing)
{
    return "<metadata><!-- payload-type:application/vnd.excalidraw+json --><!-- payload-version:2 -->"
           "<!-- payload-start -->"
           + encodePayload(sceneJson(drawing)) + "<!-- payload-end --></metadata>";
}

/// Does `text` hold an element's start tag (the first one, after the prolog)? Its name in `name`,
/// its attributes in `attributes`, the character offset after it (the reader's) in `end`. Entity
/// expansion is bounded by QXmlStreamReader's own limit.
bool firstStartTag(QStringView text, QString *name = nullptr, QXmlStreamAttributes *attributes = nullptr,
                   qint64 *end = nullptr)
{
    QXmlStreamReader xml(text);
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (name)
                *name = xml.name().toString();
            if (attributes)
                *attributes = xml.attributes();
            if (end)
                *end = xml.characterOffset();
            return true;
        }
        if (token == QXmlStreamReader::Invalid)
            return false;
    }
    return false;
}

} // namespace

QString safeDataUrl(const QJsonObject &files, const QString &fileId)
{
    if (fileId.isEmpty())
        return {};
    const QString url = files.value(fileId).toObject().value(u"dataURL"_s).toString();
    static const QStringList types = {u"png"_s, u"jpeg"_s, u"gif"_s, u"webp"_s, u"svg+xml"_s};
    for (const QString &type : types) {
        const QString prefix = u"data:image/"_s + type + u";base64,"_s;
        if (!url.startsWith(prefix))
            continue;
        for (qsizetype i = prefix.size(); i < url.size(); ++i) {
            const char16_t u = url.at(i).unicode();
            if (!((u >= u'A' && u <= u'Z') || (u >= u'a' && u <= u'z') || (u >= u'0' && u <= u'9') || u == u'+'
                  || u == u'/' || u == u'='))
                return {};
        }
        return url.size() > prefix.size() ? url : QString();
    }
    return {};
}

QString fontFamilyCss(int family)
{
    switch (family) {
    case 1: return u"Virgil, Excalifont, sans-serif"_s;
    case 2: return u"Helvetica, Arial, sans-serif"_s;
    case 3: return u"Cascadia, monospace"_s;
    case 5: return u"Excalifont, Virgil, sans-serif"_s;
    case 6: return u"Nunito, sans-serif"_s;
    case 7: return u"Lilita One, sans-serif"_s;
    case 8: return u"Comic Shanns, monospace"_s;
    case 9: return u"Liberation Sans, Arial, sans-serif"_s;
    default: break;
    }
    return u"sans-serif"_s;
}

bool isMonospaceFamily(int family)
{
    return family == 3 || family == 8;
}

QByteArray toSvg(const Drawing &drawing)
{
    const QRectF picture = pictureRect(drawing);
    const double left = picture.left();
    const double top = picture.top();
    const double right = picture.right();
    const double bottom = picture.bottom();
    const int width = int(picture.width());
    const int height = int(picture.height());
    const double dx = -left;
    const double dy = -top;
    const QString w = QString::number(width);
    const QString h = QString::number(height);
    // Light and Auto: the colours as drawn in the attributes; Dark: the dark theme's.
    const bool dark = drawing.theme == PaperTheme::Dark;
    const bool autoTheme = drawing.theme == PaperTheme::Auto;

    const bool images = std::any_of(drawing.strokes.cbegin(), drawing.strokes.cend(),
                                    [](const Stroke &s) { return s.type == ElementType::Image; });
    QString svg;
    svg += u"<svg version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\""_s
           + (images ? u" xmlns:xlink=\"http://www.w3.org/1999/xlink\""_s : QString()) + u" viewBox=\"0 0 "_s + w
           + u' ' + h + u"\" width=\""_s + w + u"\" height=\""_s + h + u"\">\n"_s;
    svg += QString::fromLatin1(SvgSourceComment) + u'\n';
    svg += QString::fromLatin1(sceneMetadata(drawing)) + u'\n';

    // Auto: the dark theme's colours for browsers, by class (the style sheet goes in after the
    // metadata once the elements are written: it names only the classes used).
    QString css;
    QString body;
    const auto withClass = [autoTheme](const QString &cls) {
        return autoTheme ? u" class=\""_s + cls + u'"' : QString();
    };

    const QString paper = dark ? displayPaper(drawing.paper, true) : safeColor(drawing.paper, QString());
    if (!paper.isEmpty()) {
        body += u"<rect x=\"0\" y=\"0\" width=\""_s + w + u"\" height=\""_s + h + u"\" fill=\""_s + paper + u'"'
                + withClass(u"p"_s) + u"/>\n"_s;
        if (autoTheme)
            css += u".p{fill:"_s + displayPaper(paper, true) + u'}';
    }

    if (drawing.background != Background::Plain) {
        // The lines at the scene's multiples of the spacing (where the editor draws them).
        const double spacing = drawing.background == Background::Lines ? LineSpacing : GridSpacing;
        // Counted by index (a step added to a huge coordinate would change nothing), at most
        // MaxRules lines per direction.
        constexpr double MaxRules = 100000;
        const auto rules = [&](double from, double to, const auto &line) {
            const double first = std::ceil(from / spacing);
            const double count = std::ceil(to / spacing) - first;
            if (!std::isfinite(first) || !(count > 0) || count > MaxRules)
                return;
            for (int i = 0; i < int(count); ++i)
                line((first + i) * spacing);
        };
        QString d;
        rules(top, bottom, [&](double y) { d += u"M0,"_s + formatNumber(y + dy) + u'H' + w; });
        if (drawing.background == Background::Grid)
            rules(left, right, [&](double x) { d += u'M' + formatNumber(x + dx) + u",0V"_s + h; });
        if (!d.isEmpty()) {
            body += u"<path d=\""_s + d + u"\" fill=\"none\" stroke=\""_s + displayRule(dark)
                    + u"\" stroke-width=\"1\""_s + withClass(u"r"_s) + u"/>\n"_s;
            if (autoTheme)
                css += u".r{stroke:"_s + displayRule(true) + u'}';
        }
    }

    // Auto: one class per colour (first use first) for what is filled with ink (`k`), stroked
    // (`s`) and filled with a shape's colour (`f`); one per highlighter opacity in the dark (`h`).
    QStringList inkClasses;
    QStringList strokeClasses;
    QStringList fillClasses;
    QList<int> highlighterClasses;
    const auto classOf = [&](QStringList &classes, const QString &prefix, const QString &color,
                             const QString &property) -> QString {
        if (!autoTheme)
            return QString();
        qsizetype n = classes.indexOf(color);
        if (n < 0) {
            n = classes.size();
            classes.append(color);
            css += u'.' + prefix + QString::number(n) + u'{' + property + u':' + darkColor(color, color) + u'}';
        }
        return prefix + QString::number(n);
    };
    const auto shown = [dark](const QString &color) { return dark ? darkColor(color, color) : color; };
    const auto classAttr = [](const QString &classes) -> QString {
        const QString c = classes.trimmed();
        return c.isEmpty() ? QString() : u" class=\""_s + c + u'"';
    };
    // (The drawing's hatch lines counted: geometry.h DrawingHatchLines.)
    qsizetype hatchBudget = DrawingHatchLines;
    for (const Stroke &s : drawing.strokes) {
        const QString color = safeColor(s.color, FallbackInk.toString());
        const QString fillColor = s.fill.trimmed() == u"transparent" ? QString() : safeColor(s.fill, QString());
        const int opacity = std::clamp(s.opacity, 0, 100);
        const QPointF center = elementCenter(s);
        const bool rotated = s.angle != 0 && std::isfinite(s.angle);
        const QString rotation = rotated ? u"rotate("_s + formatNumber(s.angle * 180 / std::numbers::pi) + u' '
                                                   + formatNumber(center.x() + dx) + u' '
                                                   + formatNumber(center.y() + dy) + u')'
                                         : QString();
        QString part;
        if (s.type == ElementType::Freedraw) {
            const QString d = outlineToSvgPath(strokeOutline(s), dx, dy);
            if (d.isEmpty())
                continue;
            if (!fillColor.isEmpty() && s.points.size() >= 3) {
                const ElementShape shape = elementShape(s, false);
                part += u"<path d=\""_s + pathToSvg(shape.area, dx, dy) + u"\" fill=\""_s + shown(fillColor) + u'"';
                if (opacity < 100)
                    part += u" fill-opacity=\""_s + formatNumber(opacity / 100.0) + u'"';
                part += classAttr(classOf(fillClasses, u"f"_s, fillColor, u"fill"_s)) + u"/>\n"_s;
            }
            const Ink ink = displayInk(s, true);
            part += u"<path d=\""_s + d + u"\" fill=\""_s + (dark ? ink.color : color) + u'"';
            const int shownOpacity = dark ? ink.opacity : opacity;
            if (shownOpacity < 100)
                part += u" fill-opacity=\""_s + formatNumber(shownOpacity / 100.0) + u'"';
            if (autoTheme) {
                if (s.tool == Tool::Highlighter) {
                    // The colour as drawn; a little stronger.
                    qsizetype n = highlighterClasses.indexOf(ink.opacity);
                    if (n < 0) {
                        n = highlighterClasses.size();
                        highlighterClasses.append(ink.opacity);
                        css += u".h"_s + QString::number(n) + u"{fill-opacity:"_s + formatNumber(ink.opacity / 100.0)
                               + u'}';
                    }
                    part += withClass(u"h"_s + QString::number(n));
                } else {
                    qsizetype n = inkClasses.indexOf(color);
                    if (n < 0) {
                        n = inkClasses.size();
                        inkClasses.append(color);
                        css += u".k"_s + QString::number(n) + u"{fill:"_s + ink.color + u'}';
                    }
                    part += withClass(u"k"_s + QString::number(n));
                }
            }
            part += u"/>\n"_s;
            body += rotated ? u"<g transform=\""_s + rotation + u"\">\n"_s + part + u"</g>\n"_s : part;
            continue;
        }
        const ElementShape shape = elementShape(s, false, std::min(MaxHatchLines, hatchBudget));
        hatchBudget -= shape.hatch.size();
        const QString strokeWidth = formatNumber(std::max(0.05, s.width));
        const QList<double> dashes = dashPattern(s);
        QString dashAttr;
        if (!dashes.isEmpty()) {
            QStringList values;
            for (const double v : dashes)
                values.append(formatNumber(v));
            dashAttr = u" stroke-dasharray=\""_s + values.join(u' ') + u'"';
        }
        if (!shape.area.isEmpty() && !fillColor.isEmpty()) {
            part += u"<path d=\""_s + pathToSvg(shape.area, dx, dy) + u"\" fill=\""_s + shown(fillColor) + u'"'
                    + classAttr(classOf(fillClasses, u"f"_s, fillColor, u"fill"_s)) + u"/>\n"_s;
        }
        if (!shape.hatch.isEmpty() && !fillColor.isEmpty()) {
            QString d;
            for (const QLineF &l : shape.hatch)
                d += u'M' + formatNumber(l.x1() + dx) + u',' + formatNumber(l.y1() + dy) + u'L'
                     + formatNumber(l.x2() + dx) + u',' + formatNumber(l.y2() + dy);
            part += u"<path d=\""_s + d + u"\" fill=\"none\" stroke=\""_s + shown(fillColor) + u"\" stroke-width=\""_s
                    + formatNumber(std::max(0.05, shape.hatchWidth)) + u"\" stroke-linecap=\"round\""_s
                    + classAttr(classOf(strokeClasses, u"s"_s, fillColor, u"stroke"_s)) + u"/>\n"_s;
        }
        if (!shape.outline.isEmpty()) {
            part += u"<path d=\""_s + pathToSvg(shape.outline, dx, dy) + u"\" fill=\"none\" stroke=\""_s + shown(color)
                    + u"\" stroke-width=\""_s + strokeWidth + u'"' + dashAttr
                    + u" stroke-linecap=\"round\" stroke-linejoin=\"round\""_s
                    + classAttr(classOf(strokeClasses, u"s"_s, color, u"stroke"_s)) + u"/>\n"_s;
        }
        for (qsizetype i = 0; i < shape.heads.size(); ++i) {
            const bool full = shape.headFilled.value(i);
            part += u"<path d=\""_s + pathToSvg(shape.heads.at(i), dx, dy) + u"\" fill=\""_s
                    + (full ? shown(color) : u"none"_s) + u"\" stroke=\""_s + shown(color) + u"\" stroke-width=\""_s
                    + strokeWidth + u"\" stroke-linecap=\"round\" stroke-linejoin=\"round\""_s
                    + classAttr((full ? classOf(inkClasses, u"k"_s, color, u"fill"_s) + u' ' : QString())
                                + classOf(strokeClasses, u"s"_s, color, u"stroke"_s))
                    + u"/>\n"_s;
        }
        if (s.type == ElementType::Text) {
            const QString anchor = s.textAlign == TextAlign::Center  ? u" text-anchor=\"middle\""_s
                                   : s.textAlign == TextAlign::Right ? u" text-anchor=\"end\""_s
                                                                     : QString();
            const QString cls = classAttr(classOf(inkClasses, u"k"_s, color, u"fill"_s));
            for (const TextLine &line : textLines(s)) {
                if (line.text.isEmpty())
                    continue;
                part += u"<text x=\""_s + formatNumber(line.baseline.x() + dx) + u"\" y=\""_s
                        + formatNumber(line.baseline.y() + dy) + u"\" font-family=\""_s + fontFamilyCss(s.fontFamily)
                        + u"\" font-size=\""_s + formatNumber(s.fontSize) + u"\" fill=\""_s + shown(color) + u'"'
                        + anchor + u" xml:space=\"preserve\""_s + cls + u'>' + xmlText(line.text) + u"</text>\n"_s;
            }
        } else if (s.type == ElementType::Image) {
            const QString box = u"x=\""_s + formatNumber(s.box.x() + dx) + u"\" y=\""_s + formatNumber(s.box.y() + dy)
                                + u"\" width=\""_s + formatNumber(s.box.width()) + u"\" height=\""_s
                                + formatNumber(s.box.height()) + u'"';
            const QString url = safeDataUrl(drawing.files, s.fileId);
            if (!url.isEmpty()) {
                part += u"<image "_s + box + u" preserveAspectRatio=\"none\" xlink:href=\""_s + url + u"\"/>\n"_s;
            } else {
                const QString grey = ImagePlaceholderFill.toString();
                const QString edge = ImagePlaceholderEdge.toString();
                part += u"<rect "_s + box + u" fill=\""_s + shown(grey) + u"\" stroke=\""_s + shown(edge)
                        + u"\" stroke-width=\"1\""_s
                        + classAttr(classOf(fillClasses, u"f"_s, grey, u"fill"_s) + u' '
                                    + classOf(strokeClasses, u"s"_s, edge, u"stroke"_s))
                        + u"/>\n"_s;
            }
        }
        if (part.isEmpty())
            continue;
        if (rotated || opacity < 100) {
            body += u"<g"_s + (rotated ? u" transform=\""_s + rotation + u'"' : QString())
                    + (opacity < 100 ? u" opacity=\""_s + formatNumber(opacity / 100.0) + u'"' : QString()) + u">\n"_s
                    + part + u"</g>\n"_s;
        } else {
            body += part;
        }
    }
    // Nothing but the @media block: Qt's SVG renderer drops such a style sheet whole (svgwrite.h).
    if (autoTheme && !css.isEmpty())
        svg += u"<style>@media (prefers-color-scheme: dark){"_s + css + u"}</style>\n"_s;
    svg += body;
    svg += u"</svg>\n"_s;
    return svg.toUtf8();
}

std::optional<QByteArray> svgWithScene(QByteArrayView picture, const Drawing &drawing)
{
    constexpr QByteArrayView Bom = "\xEF\xBB\xBF";
    const qsizetype bom = picture.startsWith(Bom) ? Bom.size() : 0;
    // Only the head is read: the root's start tag must end where looksLikeExcalidrawSvg() still
    // sees the payload that follows it.
    constexpr qsizetype TagLimit = HeadBytes - 512;
    const QByteArrayView head = picture.sliced(bom, std::min(picture.size(), TagLimit) - bom);
    const QString text = QString::fromUtf8(head);
    QString name;
    QXmlStreamAttributes attributes;
    qint64 end = 0;
    if (!firstStartTag(text, &name, &attributes, &end) || name != u"svg")
        return std::nullopt;
    // The tag ends at `end` exactly: the text up to it holds the tag, one character less does
    // not (whatever the reader counts in an expanded entity or a line break).
    if (end < 2 || end > text.size() || text.at(end - 1) != u'>' || text.at(end - 2) == u'/'
        || !firstStartTag(QStringView(text).first(end)) || firstStartTag(QStringView(text).first(end - 1)))
        return std::nullopt;
    // The bytes before it: the same text (no byte that is not UTF-8 there), no scene's marker.
    const QByteArray before = QStringView(text).first(end).toUtf8();
    if (!head.startsWith(before) || before.contains("payload-"))
        return std::nullopt;
    for (const QStringView size : {u"width", u"height"}) {
        QStringView value = attributes.value(size).trimmed();
        if (value.endsWith(u"px"))
            value.chop(2);
        bool number = false;
        const double v = value.toDouble(&number);
        if (number && !(v > 0))
            return std::nullopt; // (also NaN)
    }
    const qsizetype at = bom + before.size();
    // A scene the picture had: replaced by this one.
    constexpr QByteArrayView PayloadComment = "<!-- payload-type:";
    constexpr QByteArrayView PayloadEnd = "<!-- payload-end -->";
    QByteArray rest = picture.sliced(at).toByteArray();
    for (qsizetype from = rest.indexOf(PayloadComment); from >= 0; from = rest.indexOf(PayloadComment, from)) {
        const qsizetype stop = rest.indexOf(PayloadEnd, from);
        if (stop < 0)
            return std::nullopt;
        rest.remove(from, stop + PayloadEnd.size() - from);
    }
    if (rest.contains("payload-type:application/vnd.excalidraw+json"))
        return std::nullopt; // a marker written another way: no whole payload
    const QByteArray metadata = sceneMetadata(drawing);
    QByteArray out;
    out.reserve(at + SvgSourceComment.size() + metadata.size() + rest.size() + 3);
    out += picture.first(at);
    out += '\n';
    if (!rest.contains(SvgSourceComment)) {
        out += SvgSourceComment;
        out += '\n';
    }
    out += metadata;
    out += '\n';
    out += rest;
    return out;
}

} // namespace brulion::drawing
