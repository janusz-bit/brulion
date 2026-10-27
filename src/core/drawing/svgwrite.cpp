#include "drawing/svgwrite.h"

#include "drawing/excalidraw.h"
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

QByteArray toSvg(const Drawing &drawing)
{
    const QRectF content = bounds(drawing);
    double left = 0, top = 0, right = EmptyWidth, bottom = EmptyHeight;
    if (!content.isEmpty() || !drawing.strokes.isEmpty()) {
        left = std::floor(content.left() - SvgMargin);
        top = std::floor(content.top() - SvgMargin);
        right = std::ceil(content.right() + SvgMargin);
        bottom = std::ceil(content.bottom() + SvgMargin);
    }
    // (Strokes stay within CoordinateLimit when read or drawn; the clamp keeps the int in range
    // whatever the caller passes.)
    const int width = int(std::clamp(right - left, 1.0, 1e9));
    const int height = int(std::clamp(bottom - top, 1.0, 1e9));
    const double dx = -left;
    const double dy = -top;
    const QString w = QString::number(width);
    const QString h = QString::number(height);

    QString svg;
    svg += u"<svg version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "_s + w + u' ' + h
           + u"\" width=\""_s + w + u"\" height=\""_s + h + u"\">\n"_s;
    svg += u"<!-- svg-source:excalidraw -->\n"_s;
    svg += u"<metadata><!-- payload-type:application/vnd.excalidraw+json --><!-- payload-version:2 -->"
           "<!-- payload-start -->"_s
           + QString::fromLatin1(encodePayload(sceneJson(drawing))) + u"<!-- payload-end --></metadata>\n"_s;

    const QString paper = safeColor(drawing.paper, QString());
    if (!paper.isEmpty())
        svg += u"<rect x=\"0\" y=\"0\" width=\""_s + w + u"\" height=\""_s + h + u"\" fill=\""_s + paper + u"\"/>\n"_s;

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
        if (!d.isEmpty())
            svg += u"<path d=\""_s + d + u"\" fill=\"none\" stroke=\""_s + RuleColor.toString()
                   + u"\" stroke-width=\"1\"/>\n"_s;
    }

    for (const Stroke &s : drawing.strokes) {
        const QString d = outlineToSvgPath(strokeOutline(s), dx, dy);
        if (d.isEmpty())
            continue;
        svg += u"<path d=\""_s + d + u"\" fill=\""_s + safeColor(s.color, u"#1e1e1e"_s) + u'"';
        if (s.opacity < 100)
            svg += u" fill-opacity=\""_s + formatNumber(std::clamp(s.opacity, 0, 100) / 100.0) + u'"';
        svg += u"/>\n"_s;
    }
    svg += u"</svg>\n"_s;
    return svg.toUtf8();
}

} // namespace brulion::drawing
