#include "drawing/drawing.h"

#include "drawing/excalidraw.h"
#include "drawing/stroke.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

QRectF strokeBounds(const Stroke &stroke)
{
    const QList<QPointF> outline = strokeOutline(stroke);
    if (outline.isEmpty())
        return {};
    double minX = outline.first().x(), maxX = minX;
    double minY = outline.first().y(), maxY = minY;
    for (const QPointF &p : outline) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

QRectF bounds(const Drawing &drawing)
{
    QRectF out;
    for (const Stroke &s : drawing.strokes) {
        const QRectF b = strokeBounds(s);
        if (b.isNull())
            continue;
        out = out.isNull() ? b : out.united(b);
    }
    return out;
}

bool editable(const Drawing &drawing)
{
    return drawing.foreign.isEmpty();
}

void normalizeStroke(Stroke &stroke)
{
    QList<StrokePoint> points;
    points.reserve(stroke.points.size());
    for (const StrokePoint &p : std::as_const(stroke.points)) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            continue;
        const double pressure = std::isfinite(p.pressure) ? std::clamp(p.pressure, 0.0, 1.0) : 0.5;
        points.append({std::round(p.x * 100) / 100, std::round(p.y * 100) / 100, std::round(pressure * 1000) / 1000});
    }
    stroke.points = std::move(points);
}

QString backgroundName(Background background)
{
    switch (background) {
    case Background::Lines: return u"lines"_s;
    case Background::Grid: return u"grid"_s;
    case Background::Plain: break;
    }
    return u"plain"_s;
}

Background backgroundFromName(QStringView name)
{
    if (name == u"lines")
        return Background::Lines;
    if (name == u"grid")
        return Background::Grid;
    return Background::Plain;
}

QString paperThemeName(PaperTheme theme)
{
    switch (theme) {
    case PaperTheme::Light: return u"light"_s;
    case PaperTheme::Dark: return u"dark"_s;
    case PaperTheme::Auto: break;
    }
    return u"auto"_s;
}

PaperTheme paperThemeFromName(QStringView name)
{
    if (name == u"light")
        return PaperTheme::Light;
    if (name == u"dark")
        return PaperTheme::Dark;
    return PaperTheme::Auto;
}

std::optional<Drawing> readDrawing(QByteArrayView bytes, ReadError *error)
{
    const auto fail = [error](ReadError why) -> std::optional<Drawing> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (error)
        *error = ReadError::None;
    QByteArrayView text = bytes;
    if (text.startsWith("\xEF\xBB\xBF"))
        text = text.sliced(3);
    qsizetype i = 0;
    while (i < text.size() && (text.at(i) == ' ' || text.at(i) == '\n' || text.at(i) == '\r' || text.at(i) == '\t'))
        ++i;
    if (i < text.size() && text.at(i) == '{') {
        // A .excalidraw file.
        std::optional<Drawing> d = parseScene(text);
        return d ? d : fail(ReadError::NoScene);
    }
    bool corrupt = false;
    const std::optional<QByteArray> scene = svgPayload(text, &corrupt);
    if (!scene)
        return fail(corrupt ? ReadError::Corrupt : ReadError::NoScene);
    std::optional<Drawing> d = parseScene(*scene);
    return d ? d : fail(ReadError::Corrupt);
}

} // namespace brulion::drawing
