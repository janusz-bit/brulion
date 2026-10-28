#include "drawing/svgwrite.h"

#include "drawing/excalidraw.h"
#include "drawing/palette.h"
#include "drawing/stroke.h"

#include <algorithm>
#include <cmath>

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

    QString svg;
    svg += u"<svg version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "_s + w + u' ' + h
           + u"\" width=\""_s + w + u"\" height=\""_s + h + u"\">\n"_s;
    svg += u"<!-- svg-source:excalidraw -->\n"_s;
    svg += u"<metadata><!-- payload-type:application/vnd.excalidraw+json --><!-- payload-version:2 -->"
           "<!-- payload-start -->"_s
           + QString::fromLatin1(encodePayload(sceneJson(drawing))) + u"<!-- payload-end --></metadata>\n"_s;

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

    // Auto: one class per pen colour (first use first), one per highlighter opacity in the dark.
    QStringList inkClasses;
    QList<int> highlighterClasses;
    for (const Stroke &s : drawing.strokes) {
        const QString d = outlineToSvgPath(strokeOutline(s), dx, dy);
        if (d.isEmpty())
            continue;
        const QString color = safeColor(s.color, FallbackInk.toString());
        const int opacity = std::clamp(s.opacity, 0, 100);
        const Ink ink = displayInk(s, true);
        body += u"<path d=\""_s + d + u"\" fill=\""_s + (dark ? ink.color : color) + u'"';
        const int shown = dark ? ink.opacity : opacity;
        if (shown < 100)
            body += u" fill-opacity=\""_s + formatNumber(shown / 100.0) + u'"';
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
                body += withClass(u"h"_s + QString::number(n));
            } else {
                qsizetype n = inkClasses.indexOf(color);
                if (n < 0) {
                    n = inkClasses.size();
                    inkClasses.append(color);
                    css += u".k"_s + QString::number(n) + u"{fill:"_s + ink.color + u'}';
                }
                body += withClass(u"k"_s + QString::number(n));
            }
        }
        body += u"/>\n"_s;
    }
    // Nothing but the @media block: Qt's SVG renderer drops such a style sheet whole (svgwrite.h).
    if (autoTheme && !css.isEmpty())
        svg += u"<style>@media (prefers-color-scheme: dark){"_s + css + u"}</style>\n"_s;
    svg += body;
    svg += u"</svg>\n"_s;
    return svg.toUtf8();
}

} // namespace brulion::drawing
