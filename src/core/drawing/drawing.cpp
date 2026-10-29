#include "drawing/drawing.h"

#include "drawing/drawingfile.h"
#include "drawing/excalidraw.h"
#include "drawing/geometry.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

QRectF strokeBounds(const Stroke &stroke)
{
    return elementBounds(stroke);
}

QRectF bounds(const Drawing &drawing)
{
    QRectF out;
    for (const Stroke &s : drawing.strokes) {
        const QRectF b = elementBounds(s);
        if (b.isNull())
            continue;
        out = out.isNull() ? b : out.united(b);
    }
    return out;
}

bool isShape(ElementType type)
{
    return type == ElementType::Rectangle || type == ElementType::Ellipse || type == ElementType::Diamond;
}

bool isLinear(ElementType type)
{
    return type == ElementType::Line || type == ElementType::Arrow;
}

bool hasPoints(ElementType type)
{
    return type == ElementType::Freedraw || isLinear(type);
}

QString elementTypeName(ElementType type)
{
    switch (type) {
    case ElementType::Freedraw: return u"freedraw"_s;
    case ElementType::Rectangle: return u"rectangle"_s;
    case ElementType::Ellipse: return u"ellipse"_s;
    case ElementType::Diamond: return u"diamond"_s;
    case ElementType::Line: return u"line"_s;
    case ElementType::Arrow: return u"arrow"_s;
    case ElementType::Text: return u"text"_s;
    case ElementType::Image: return u"image"_s;
    }
    return u"freedraw"_s;
}

std::optional<ElementType> elementTypeFromName(QStringView name)
{
    static const ElementType all[] = {ElementType::Freedraw, ElementType::Rectangle, ElementType::Ellipse,
                                      ElementType::Diamond,  ElementType::Line,      ElementType::Arrow,
                                      ElementType::Text,     ElementType::Image};
    for (const ElementType t : all) {
        if (name == elementTypeName(t))
            return t;
    }
    return std::nullopt;
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
    std::optional<DrawingFile> file = readDrawingFile(bytes, error);
    if (!file)
        return std::nullopt;
    return std::move(file->drawing);
}

} // namespace brulion::drawing
