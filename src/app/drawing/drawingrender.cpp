#include "drawingrender.h"

#include "drawing/stroke.h"
#include "drawing/svgwrite.h"

#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>

namespace brulion::drawingview {

QColor toQColor(const drawing::Ink &ink)
{
    const drawing::Rgba c = drawing::parseColor(ink.color).value_or(*drawing::parseColor(drawing::FallbackInk));
    QColor color(c.r, c.g, c.b, c.a);
    color.setAlphaF(color.alphaF() * std::clamp(ink.opacity, 0, 100) / 100.0);
    return color;
}

QColor toQColor(QStringView hex)
{
    const std::optional<drawing::Rgba> c = drawing::parseColor(hex);
    return c ? QColor(c->r, c->g, c->b, c->a) : QColor();
}

QPainterPath outlinePath(const QList<QPointF> &outline)
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    if (outline.isEmpty())
        return path;
    path.moveTo(outline.first());
    const qsizetype max = outline.size() - 1;
    for (qsizetype i = 0; i <= max; ++i) {
        const QPointF p = outline.at(i);
        const QPointF next = i == max ? outline.first() : outline.at(i + 1);
        path.quadTo(p, (p + next) / 2);
    }
    path.closeSubpath();
    return path;
}

QPainterPath strokePath(const drawing::Stroke &stroke)
{
    return outlinePath(drawing::strokeOutline(stroke));
}

void paintPaper(QPainter &painter, const drawing::Drawing &drawing, bool dark, const QTransform &sceneToItem,
                const QSizeF &size, qreal dpr)
{
    painter.save();
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(QRectF(QPointF(0, 0), size), toQColor(drawing::displayPaper(drawing.paper, dark)));
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const double zoom = sceneToItem.m11();
    const double spacing =
            drawing.background == drawing::Background::Lines ? drawing::LineSpacing : drawing::GridSpacing;
    bool invertible = false;
    const QTransform itemToScene = sceneToItem.inverted(&invertible);
    if (drawing.background != drawing::Background::Plain && invertible && spacing * zoom >= 6) {
        const QRectF visible = itemToScene.mapRect(QRectF(QPointF(0, 0), size));
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen{toQColor(drawing::displayRule(dark))};
        pen.setCosmetic(true);
        pen.setWidthF(1);
        painter.setPen(pen);
        // Counted by index: a step added to a huge coordinate would change nothing. (At most one
        // line per 6 px of the view.)
        const auto rules = [spacing](double from, double to, const auto &line) {
            const double first = std::ceil(from / spacing);
            const double last = std::floor(to / spacing);
            if (!std::isfinite(first) || !std::isfinite(last) || last < first || last - first > 10000)
                return;
            for (int i = 0; i <= int(last - first); ++i)
                line((first + i) * spacing);
        };
        // On whole device pixels: sharp one-pixel lines.
        const auto snap = [dpr](double v) { return std::round(v * dpr) / dpr + 0.5 / dpr; };
        rules(visible.top(), visible.bottom(), [&](double y) {
            const double iy = snap(sceneToItem.map(QPointF(0, y)).y());
            painter.drawLine(QPointF(0, iy), QPointF(size.width(), iy));
        });
        if (drawing.background == drawing::Background::Grid) {
            rules(visible.left(), visible.right(), [&](double x) {
                const double ix = snap(sceneToItem.map(QPointF(x, 0)).x());
                painter.drawLine(QPointF(ix, 0), QPointF(ix, size.height()));
            });
        }
    }
    painter.restore();
}

QImage renderDrawing(const drawing::Drawing &drawing, QSize pixels, bool dark, const QRectF &frame)
{
    if (pixels.isEmpty())
        return {};
    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return {};
    image.fill(toQColor(drawing::displayPaper(drawing.paper, dark)));
    const QRectF picture = frame.isEmpty() ? drawing::pictureRect(drawing) : frame;
    const double k = std::min(pixels.width() / picture.width(), pixels.height() / picture.height());
    QTransform t;
    t.translate((pixels.width() - picture.width() * k) / 2, (pixels.height() - picture.height() * k) / 2);
    t.scale(k, k);
    t.translate(-picture.left(), -picture.top());
    QPainter p(&image);
    paintPaper(p, drawing, dark, t, QSizeF(pixels), 1);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setTransform(t);
    const QRectF visible = picture.adjusted(-1, -1, 1, 1);
    for (const drawing::Stroke &s : drawing.strokes) {
        const QPainterPath path = strokePath(s);
        if (path.isEmpty() || !path.boundingRect().intersects(visible))
            continue;
        p.fillPath(path, toQColor(drawing::displayInk(s, dark)));
    }
    p.end();
    return image;
}

namespace {

/// darkRgba() in tables: the inverted channel over 255 times each entry of the hue matrix — the
/// same floating-point steps, so every pixel equals darkRgba() of its colour.
struct DarkTables {
    std::array<std::array<double, 256>, 9> m{};

    DarkTables()
    {
        const std::array<double, 9> matrix = drawing::darkHueMatrix();
        for (int v = 0; v < 256; ++v) {
            const double inverted = drawing::darkInvertChannel(v) / 255.0;
            for (int i = 0; i < 9; ++i)
                m[i][v] = inverted * matrix[i];
        }
    }
};

const DarkTables &darkTables()
{
    static const DarkTables tables; // (thread-safe initialisation)
    return tables;
}

} // namespace

QImage darkImage(const QImage &light)
{
    if (light.isNull())
        return {};
    QImage image = light.convertToFormat(QImage::Format_ARGB32);
    const DarkTables &t = darkTables();
    const auto channel = [](double v) { return int(std::floor(std::clamp(v, 0.0, 1.0) * 255 + 0.5)); };
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb p = line[x];
            const int r = qRed(p), g = qGreen(p), b = qBlue(p);
            line[x] = qRgba(channel(t.m[0][r] + t.m[1][g] + t.m[2][b]), channel(t.m[3][r] + t.m[4][g] + t.m[5][b]),
                            channel(t.m[6][r] + t.m[7][g] + t.m[8][b]), qAlpha(p));
        }
    }
    image.setDevicePixelRatio(light.devicePixelRatio());
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

} // namespace brulion::drawingview
