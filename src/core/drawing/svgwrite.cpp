#include "drawing/svgwrite.h"

#include "drawing/backdrop.h"
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
    // (parseColor() reads exactly these: "#" and 3, 6 or 8 hex digits, spaces around.)
    return parseColor(color) ? color.trimmed().toLower() : fallback;
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

constexpr QByteArrayView PayloadTypeComment = "<!-- payload-type:application/vnd.excalidraw+json -->";

/// The scene (its JSON) as excalidraw.com's "Embed scene" writes it into an SVG (excalidraw.h).
QByteArray sceneMetadata(QByteArrayView sceneJson)
{
    return "<metadata>" + PayloadTypeComment.toByteArray() + "<!-- payload-version:2 --><!-- payload-start -->"
           + encodePayload(sceneJson) + "<!-- payload-end --></metadata>";
}

/// The length of the Excalidraw payload at `from` in `bytes` as Excalidraw writes it — the
/// payload type's comment, the version's (optional), `<!-- payload-start -->`, the base64,
/// `<!-- payload-end -->`, whitespace between them —, 0 when anything else is there. Only comments
/// and base64 text: removing them leaves a well-formed picture well-formed (whatever holds them),
/// unless the bytes around them then end a CDATA section or a processing instruction
/// (svgWithScene() checks).
qsizetype payloadLength(QByteArrayView bytes, qsizetype from)
{
    qsizetype i = from;
    const auto skip = [&](QByteArrayView text) {
        if (!bytes.sliced(i).startsWith(text))
            return false;
        i += text.size();
        return true;
    };
    const auto skipSpace = [&] {
        while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t' || bytes[i] == '\n' || bytes[i] == '\r'))
            ++i;
    };
    if (!skip(PayloadTypeComment))
        return 0;
    skipSpace();
    if (skip("<!-- payload-version:")) {
        const qsizetype digits = i;
        while (i < bytes.size() && bytes[i] >= '0' && bytes[i] <= '9')
            ++i;
        if (i == digits || !skip(" -->"))
            return 0;
        skipSpace();
    }
    if (!skip("<!-- payload-start -->"))
        return 0;
    while (i < bytes.size()) {
        const char c = bytes[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/'
              || c == '=' || c == ' ' || c == '\t' || c == '\n' || c == '\r'))
            break;
        ++i;
    }
    return skip("<!-- payload-end -->") ? i - from : 0;
}

/// An element's start tag: its name, its attributes and the character offset after it (the
/// reader's).
struct StartTag {
    QString name;
    QXmlStreamAttributes attributes;
    qint64 end = 0;
};

/// The first start tag `text` holds (after the prolog); nullopt for none or a text that is not
/// well-formed before it (the reader ends at an error). Entity expansion is bounded by
/// QXmlStreamReader's own limit.
std::optional<StartTag> firstStartTag(QStringView text)
{
    QXmlStreamReader xml(text);
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement)
            return StartTag{xml.name().toString(), xml.attributes(), xml.characterOffset()};
    }
    return std::nullopt;
}

/// The path of the paper's lines (a grid's columns too) in `picture` (`w` × `h`), at the scene's
/// multiples of the spacing (where the editor draws them); "" for plain paper or no line in it.
QString rulesPath(Background background, const QRectF &picture, const QString &w, const QString &h)
{
    if (background == Background::Plain)
        return {};
    const double spacing = background == Background::Lines ? LineSpacing : GridSpacing;
    const double dx = -picture.left();
    const double dy = -picture.top();
    // Counted by index (a step added to a huge coordinate would change nothing), at most
    // MaxRules lines per direction.
    static constexpr double MaxRules = 100000;
    const auto rules = [spacing](double from, double to, const auto &line) {
        const double first = std::ceil(from / spacing);
        const double count = std::ceil(to / spacing) - first;
        if (!std::isfinite(first) || !(count > 0) || count > MaxRules)
            return;
        for (int i = 0; i < int(count); ++i)
            line((first + i) * spacing);
    };
    QString d;
    rules(picture.top(), picture.bottom(), [&](double y) { d += u"M0,"_s + formatNumber(y + dy) + u'H' + w; });
    if (background == Background::Grid)
        rules(picture.left(), picture.right(), [&](double x) { d += u'M' + formatNumber(x + dx) + u",0V"_s + h; });
    return d;
}

} // namespace

QString safeDataUrl(const QJsonObject &files, const QString &fileId)
{
    if (fileId.isEmpty())
        return {};
    return safeImageDataUrl(files.value(fileId).toObject().value(u"dataURL"_s).toString());
}

QString safeImageDataUrl(const QString &url)
{
    static const QStringList prefixes = {u"data:image/png;base64,"_s, u"data:image/jpeg;base64,"_s,
                                         u"data:image/gif;base64,"_s, u"data:image/webp;base64,"_s,
                                         u"data:image/svg+xml;base64,"_s};
    for (const QString &prefix : prefixes) {
        if (!url.startsWith(prefix))
            continue;
        const QStringView data = QStringView(url).sliced(prefix.size());
        const bool base64 = std::ranges::all_of(data, [](QChar c) {
            const char16_t u = c.unicode();
            return (u >= u'A' && u <= u'Z') || (u >= u'a' && u <= u'z') || (u >= u'0' && u <= u'9') || u == u'+'
                   || u == u'/' || u == u'=';
        });
        return base64 && !data.isEmpty() ? url : QString();
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

QByteArray toSvg(const Drawing &drawing, const SvgOptions &options)
{
    const QRectF picture = pictureRect(drawing);
    const double dx = -picture.left();
    const double dy = -picture.top();
    const QString w = QString::number(int(picture.width()));
    const QString h = QString::number(int(picture.height()));
    // A backdrop (backdrop.h) and the dark raster of it the app passed, if any.
    const qsizetype backdrop = backdropIndex(drawing);
    const QString darkBackdrop = backdrop >= 0 && drawing.theme == PaperTheme::Dark
                                         ? safeImageDataUrl(options.pictureImages.value(drawing.strokes.first().fileId))
                                         : QString();
    // Light and Auto: the colours as drawn in the attributes; Dark: the dark theme's — with a
    // backdrop only when its dark raster is there (else the Light picture: never light ink over a
    // light slide). Auto with a backdrop: no style sheet (svgwrite.h).
    const bool dark = drawing.theme == PaperTheme::Dark && (backdrop < 0 || !darkBackdrop.isEmpty());
    const bool autoTheme = drawing.theme == PaperTheme::Auto && backdrop < 0;

    const bool images =
            std::ranges::any_of(drawing.strokes, [](const Stroke &s) { return s.type == ElementType::Image; });
    QString svg;
    svg += u"<svg version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\""_s
           + (images ? u" xmlns:xlink=\"http://www.w3.org/1999/xlink\""_s : QString()) + u" viewBox=\"0 0 "_s + w + u' '
           + h + u"\" width=\""_s + w + u"\" height=\""_s + h + u"\">\n"_s;
    svg += QString::fromLatin1(SvgSourceComment) + u'\n';
    svg += QString::fromLatin1(sceneMetadata(sceneJson(drawing))) + u'\n';

    // Auto: the dark theme's colours for browsers, by class (the style sheet goes in after the
    // metadata once the elements are written: it names only the classes used).
    QString css;
    QString body;
    // ` class="…"` for the classes of an Auto picture's element ("" for none and the other themes).
    const auto classAttr = [autoTheme](const QString &classes) -> QString {
        const QString c = classes.trimmed();
        return !autoTheme || c.isEmpty() ? QString() : u" class=\""_s + c + u'"';
    };

    const QString paper = dark ? displayPaper(drawing.paper, true) : safeColor(drawing.paper, QString());
    if (!paper.isEmpty()) {
        body += u"<rect x=\"0\" y=\"0\" width=\""_s + w + u"\" height=\""_s + h + u"\" fill=\""_s + paper + u'"'
                + classAttr(u"p"_s) + u"/>\n"_s;
        if (autoTheme)
            css += u".p{fill:"_s + displayPaper(paper, true) + u'}';
    }

    if (const QString d = rulesPath(drawing.background, picture, w, h); !d.isEmpty()) {
        body += u"<path d=\""_s + d + u"\" fill=\"none\" stroke=\""_s + displayRule(dark, drawing.paper)
                + u"\" stroke-width=\"1\""_s + classAttr(u"r"_s) + u"/>\n"_s;
        if (autoTheme)
            css += u".r{stroke:"_s + displayRule(true, drawing.paper) + u'}';
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
    // (The drawing's hatch lines counted: geometry.h DrawingHatchLines.)
    qsizetype hatchBudget = DrawingHatchLines;
    for (qsizetype index = 0; index < drawing.strokes.size(); ++index) {
        const Stroke &s = drawing.strokes.at(index);
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
            if (autoTheme && s.tool == Tool::Highlighter) {
                // The colour as drawn; a little stronger.
                qsizetype n = highlighterClasses.indexOf(ink.opacity);
                if (n < 0) {
                    n = highlighterClasses.size();
                    highlighterClasses.append(ink.opacity);
                    css += u".h"_s + QString::number(n) + u"{fill-opacity:"_s + formatNumber(ink.opacity / 100.0)
                           + u'}';
                }
                part += classAttr(u"h"_s + QString::number(n));
            } else {
                // (A pen's dark ink is darkColor() of its colour.)
                part += classAttr(classOf(inkClasses, u"k"_s, color, u"fill"_s));
            }
            part += u"/>\n"_s;
            body += rotated ? u"<g transform=\""_s + rotation + u"\">\n"_s + part + u"</g>\n"_s : part;
            continue;
        }
        const ElementShape shape = elementShape(s, false, std::min(MaxHatchLines, hatchBudget));
        hatchBudget -= shape.hatch.size();
        const QString strokeWidth = formatNumber(std::max(0.05, s.width));
        QString dashAttr;
        for (const QList<double> dashes = dashPattern(s); const double v : dashes)
            dashAttr += (dashAttr.isEmpty() ? u" stroke-dasharray=\""_s : u" "_s) + formatNumber(v);
        if (!dashAttr.isEmpty())
            dashAttr += u'"';
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
            for (const QList<TextLine> lines = textLines(s); const TextLine &line : lines) {
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
            // (The backdrop of a Dark drawing: its dark raster; other pictures keep their colours.)
            const QString url = index == backdrop && dark ? darkBackdrop : safeDataUrl(drawing.files, s.fileId);
            if (!url.isEmpty()) {
                // Mirrored within its box: about the box's centre, inside the element's rotation.
                const QString mirror = s.flipX || s.flipY
                                               ? u" transform=\"translate("_s + formatNumber(s.box.center().x() + dx)
                                                         + u' ' + formatNumber(s.box.center().y() + dy) + u") scale("_s
                                                         + (s.flipX ? u"-1"_s : u"1"_s) + u' '
                                                         + (s.flipY ? u"-1"_s : u"1"_s) + u") translate("_s
                                                         + formatNumber(-(s.box.center().x() + dx)) + u' '
                                                         + formatNumber(-(s.box.center().y() + dy)) + u")\""_s
                                               : QString();
                part += u"<image "_s + box + mirror + u" preserveAspectRatio=\"none\" xlink:href=\""_s + url
                        + u"\"/>\n"_s;
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
    return svgWithScene(picture, QByteArrayView(sceneJson(drawing)));
}

std::optional<QByteArray> svgWithScene(QByteArrayView picture, QByteArrayView sceneJson)
{
    constexpr QByteArrayView Bom = "\xEF\xBB\xBF";
    const qsizetype bom = picture.startsWith(Bom) ? Bom.size() : 0;
    // Only the head is read: the root's start tag must end where looksLikeExcalidrawSvg() still
    // sees the payload that follows it.
    constexpr qsizetype TagLimit = HeadBytes - 512;
    const QByteArrayView head = picture.sliced(bom, std::min(picture.size(), TagLimit) - bom);
    const QString text = QString::fromUtf8(head);
    const std::optional<StartTag> root = firstStartTag(text);
    if (!root || root->name != u"svg")
        return std::nullopt;
    // The tag ends at `end` exactly: the text up to it holds the tag, one character less does
    // not (whatever the reader counts in an expanded entity or a line break).
    const qint64 end = root->end;
    if (end < 2 || end > text.size() || text.at(end - 1) != u'>' || text.at(end - 2) == u'/'
        || !firstStartTag(QStringView(text).first(end)) || firstStartTag(QStringView(text).first(end - 1)))
        return std::nullopt;
    // The bytes before it: the same text (no byte that is not UTF-8 there), no scene's marker.
    const QByteArray before = QStringView(text).first(end).toUtf8();
    if (!head.startsWith(before) || before.contains("payload-"))
        return std::nullopt;
    for (const QStringView size : {u"width", u"height"}) {
        QStringView value = root->attributes.value(size).trimmed();
        if (value.endsWith(u"px"))
            value.chop(2);
        bool number = false;
        const double v = value.toDouble(&number);
        if (number && !(v > 0))
            return std::nullopt; // (also NaN)
    }
    const qsizetype at = bom + before.size();
    // A scene the picture had: replaced by this one. Only a payload as Excalidraw writes it is
    // removed (other programs' comments stay); a scene's marker in any other shape refuses the
    // picture (the scene read from the file would not be this one).
    constexpr QByteArrayView PayloadType = "payload-type:application/vnd.excalidraw+json";
    constexpr qsizetype CommentStart = 5; // "<!-- "
    QByteArray rest;
    rest.reserve(picture.size() - at);
    qsizetype copied = at;
    for (qsizetype marker = picture.indexOf(PayloadType, at); marker >= 0;
         marker = picture.indexOf(PayloadType, copied)) {
        const qsizetype from = marker - CommentStart;
        const qsizetype length = from >= copied ? payloadLength(picture, from) : 0;
        if (length == 0)
            return std::nullopt;
        rest += picture.sliced(copied, from - copied);
        copied = from + length;
    }
    rest += picture.sliced(copied);
    if (copied != at) {
        // The bytes around a removed payload must not make a marker of their own, nor the end of a
        // CDATA section or a processing instruction (`]]` or `?` before it, `>` after it): the
        // rest of the picture would be read another way, or not at all. (A payload holds neither.)
        const QByteArrayView tail = picture.sliced(at);
        if (rest.contains(PayloadType) || QByteArrayView(rest).count("]]>") != tail.count("]]>")
            || QByteArrayView(rest).count("?>") != tail.count("?>"))
            return std::nullopt;
    }
    const QByteArray metadata = sceneMetadata(sceneJson);
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
