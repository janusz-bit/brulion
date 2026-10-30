#include "drawingrender.h"

#include "drawing/backdrop.h"
#include "drawing/merge.h"
#include "drawing/stroke.h"
#include "drawing/svgwrite.h"

#include <QBuffer>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImageReader>
#include <QPainter>
#include <QReadWriteLock>
#include <QSvgRenderer>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

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

namespace {

struct TextFonts {
    QReadWriteLock lock;
    QString sans;
    QString mono;
};

TextFonts &textFonts()
{
    static TextFonts fonts;
    return fonts;
}

QColor shownColor(const QString &stored, bool dark, const QString &fallback)
{
    const QString hex = drawing::safeColor(stored, fallback);
    if (hex.isEmpty())
        return {};
    return toQColor(dark ? drawing::darkColor(hex, hex) : hex);
}

bool transparentFill(const QString &fill)
{
    const QString f = fill.trimmed();
    return f.isEmpty() || f == u"transparent" || f == u"none";
}

} // namespace

void setTextFonts(const QString &sans, const QString &mono)
{
    TextFonts &f = textFonts();
    QWriteLocker locker(&f.lock);
    f.sans = sans;
    f.mono = mono;
}

QString textFontFamily(int family)
{
    TextFonts &f = textFonts();
    {
        QReadLocker locker(&f.lock);
        const QString chosen = drawing::isMonospaceFamily(family) ? f.mono : f.sans;
        if (!chosen.isEmpty())
            return chosen;
    }
    return drawing::isMonospaceFamily(family) ? QFontDatabase::systemFont(QFontDatabase::FixedFont).family()
                                              : QGuiApplication::font().family();
}

QFont textFont(int family, double pixelSize)
{
    QFont font(textFontFamily(family));
    font.setPixelSize(std::clamp(int(std::lround(pixelSize)), 1, 1000));
    if (drawing::isMonospaceFamily(family))
        font.setStyleHint(QFont::Monospace);
    return font;
}

drawing::TextMetrics textMetrics()
{
    drawing::TextMetrics m;
    m.width = [](QStringView line, int family, double size) {
        return QFontMetricsF(textFont(family, size)).horizontalAdvance(line.toString());
    };
    return m;
}

QPainterPath toPainterPath(const drawing::Path &path)
{
    QPainterPath out;
    for (const drawing::PathOp &op : path) {
        switch (op.kind) {
        case drawing::PathOp::Move: out.moveTo(op.a); break;
        case drawing::PathOp::Line: out.lineTo(op.a); break;
        case drawing::PathOp::Quad: out.quadTo(op.a, op.b); break;
        case drawing::PathOp::Cubic: out.cubicTo(op.a, op.b, op.c); break;
        case drawing::PathOp::Close: out.closeSubpath(); break;
        }
    }
    return out;
}

Prepared prepare(const drawing::Stroke &e, qsizetype hatchLines)
{
    Prepared p;
    p.key = drawing::mergeKey(e);
    p.version = e.version;
    p.nonce = e.versionNonce;
    p.updated = e.updated;
    if (e.type == drawing::ElementType::Freedraw) {
        // The outline as the SVG has it (quadratic curves through the midpoints), rotated.
        p.ink = strokePath(e);
        if (!transparentFill(e.fill) && e.points.size() >= 3) {
            QPolygonF polygon;
            for (const drawing::StrokePoint &pt : e.points)
                polygon.append(QPointF(pt.x, pt.y));
            p.area.addPolygon(polygon);
            p.area.closeSubpath();
        }
        if (e.angle != 0 && std::isfinite(e.angle)) {
            const QPointF c = drawing::elementCenter(e);
            QTransform t;
            t.translate(c.x(), c.y());
            t.rotateRadians(e.angle);
            t.translate(-c.x(), -c.y());
            p.ink = t.map(p.ink);
            p.area = t.map(p.area);
        }
        p.ink.setFillRule(Qt::WindingFill);
        p.bounds = p.ink.boundingRect().united(p.area.boundingRect());
        p.light = toQColor(drawing::displayInk(e, false));
        p.dark = toQColor(drawing::displayInk(e, true));
    } else {
        const drawing::ElementShape shape = drawing::elementShape(e, true, hatchLines);
        p.area = toPainterPath(shape.area);
        p.outline = toPainterPath(shape.outline);
        p.hatch = shape.hatch;
        p.hatchWidth = shape.hatchWidth;
        for (const drawing::Path &h : shape.heads)
            p.heads.append(toPainterPath(h));
        p.headFilled = shape.headFilled;
        p.bounds = shape.bounds;
        p.light = shownColor(e.color, false, drawing::FallbackInk.toString());
        p.dark = shownColor(e.color, true, drawing::FallbackInk.toString());
    }
    if (!transparentFill(e.fill)) {
        p.fillLight = shownColor(e.fill, false, QString());
        p.fillDark = shownColor(e.fill, true, QString());
        if (e.type == drawing::ElementType::Freedraw) {
            // (A freehand line's opacity is in its colours.)
            const double k = std::clamp(e.opacity, 0, 100) / 100.0;
            p.fillLight.setAlphaF(p.fillLight.alphaF() * k);
            p.fillDark.setAlphaF(p.fillDark.alphaF() * k);
        }
    }
    return p;
}

QString darkPictureKey(const QString &fileId)
{
    return fileId + u"\x1fdark"_s;
}

QString fileIdOfPictureKey(const QString &key)
{
    const qsizetype separator = key.indexOf(QChar(0x1f));
    return separator < 0 ? key : key.left(separator);
}

QImage decodePicture(const QJsonObject &files, const QString &fileId, const QSize &atMost)
{
    const QString url = drawing::safeDataUrl(files, fileId);
    if (url.isEmpty())
        return {};
    const qsizetype comma = url.indexOf(u',');
    const QByteArray bytes = QByteArray::fromBase64(QStringView(url).sliced(comma + 1).toLatin1());
    if (atMost.isValid() && !atMost.isEmpty() && !url.startsWith(u"data:image/svg+xml"_s)) {
        // At the size it is shown (a preview of a note full of slides decodes no full rasters).
        QBuffer buffer;
        buffer.setData(bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setAllocationLimit(256);
        const QSize own = reader.size();
        if (own.isValid() && (own.width() > atMost.width() || own.height() > atMost.height()))
            reader.setScaledSize(own.scaled(atMost, Qt::KeepAspectRatio).expandedTo(QSize(1, 1)));
        return reader.read();
    }
    if (url.startsWith(u"data:image/svg+xml"_s)) {
        QSvgRenderer svg(bytes);
        if (!svg.isValid())
            return {};
        QSize size = svg.defaultSize();
        if (size.isEmpty())
            size = QSize(256, 256);
        size = size.scaled(std::min(size.width(), 2048), std::min(size.height(), 2048), Qt::KeepAspectRatio);
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull())
            return {};
        image.fill(Qt::transparent);
        QPainter painter(&image);
        svg.render(&painter);
        return image;
    }
    return QImage::fromData(bytes);
}

void paintElement(QPainter &painter, const drawing::Stroke &e, const Prepared &p, bool dark, const QJsonObject &files,
                  PictureCache &pictures, bool backdrop)
{
    const QColor color = dark ? p.dark : p.light;
    const QColor fill = dark ? p.fillDark : p.fillLight;
    if (e.type == drawing::ElementType::Freedraw) {
        if (fill.isValid() && !p.area.isEmpty())
            painter.fillPath(p.area, fill);
        painter.fillPath(p.ink, color);
        return;
    }
    painter.save();
    painter.setOpacity(painter.opacity() * std::clamp(e.opacity, 0, 100) / 100.0);
    if (fill.isValid() && !p.area.isEmpty())
        painter.fillPath(p.area, fill);
    if (fill.isValid() && !p.hatch.isEmpty()) {
        QPen pen(fill, std::max(0.05, p.hatchWidth));
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLines(p.hatch);
    }
    QPen pen(color, std::max(0.05, e.width));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    const QList<double> dashes = drawing::dashPattern(e);
    if (!dashes.isEmpty()) {
        QList<qreal> pattern;
        for (const double d : dashes)
            pattern.append(d / std::max(0.05, e.width)); // (QPen counts in pen widths)
        pen.setDashPattern(pattern);
    }
    if (!p.outline.isEmpty())
        painter.strokePath(p.outline, pen);
    pen.setStyle(Qt::SolidLine);
    for (qsizetype i = 0; i < p.heads.size(); ++i) {
        if (p.headFilled.value(i))
            painter.fillPath(p.heads.at(i), color);
        painter.strokePath(p.heads.at(i), pen);
    }
    const bool rotated = e.angle != 0 && std::isfinite(e.angle);
    if (rotated && (e.type == drawing::ElementType::Text || e.type == drawing::ElementType::Image)) {
        const QPointF c = e.box.center();
        painter.translate(c);
        painter.rotate(e.angle * 180 / std::numbers::pi);
        painter.translate(-c);
    }
    if (e.type == drawing::ElementType::Text) {
        const QFont font = textFont(e.fontFamily, e.fontSize);
        const QFontMetricsF metrics(font);
        painter.setFont(font);
        painter.setPen(color);
        for (const drawing::TextLine &line : drawing::textLines(e)) {
            if (line.text.isEmpty())
                continue;
            const double w = metrics.horizontalAdvance(line.text);
            const double x = e.textAlign == drawing::TextAlign::Center  ? line.baseline.x() - w / 2
                             : e.textAlign == drawing::TextAlign::Right ? line.baseline.x() - w
                                                                        : line.baseline.x();
            painter.drawText(QPointF(x, line.baseline.y()), line.text);
        }
    } else if (e.type == drawing::ElementType::Image) {
        auto it = pictures.find(e.fileId);
        if (it == pictures.end())
            it = pictures.insert(e.fileId, decodePicture(files, e.fileId));
        if (backdrop && dark && !it->isNull()) {
            // "Document dark": the slide inverted as Excalidraw's dark filter, the ink stays readable.
            const QString key = darkPictureKey(e.fileId);
            auto darkIt = pictures.find(key);
            if (darkIt == pictures.end())
                darkIt = pictures.insert(key, darkImage(*it));
            it = darkIt;
        }
        if (!it->isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(e.box, *it);
        } else {
            // No picture: a light grey box with a picture's outline.
            const QColor grey = shownColor(drawing::ImagePlaceholderFill.toString(), dark, QString());
            const QColor edge = shownColor(drawing::ImagePlaceholderEdge.toString(), dark, QString());
            painter.fillRect(e.box, grey);
            QPen glyph(edge, std::max(0.5, std::min(e.box.width(), e.box.height()) / 40));
            glyph.setJoinStyle(Qt::RoundJoin);
            painter.setPen(glyph);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(e.box);
            const QRectF r = e.box.adjusted(e.box.width() * 0.25, e.box.height() * 0.3, -e.box.width() * 0.25,
                                            -e.box.height() * 0.3);
            painter.drawPolyline(QPolygonF{QPointF(r.left(), r.bottom()), QPointF(r.left() + r.width() * 0.4, r.top()),
                                           QPointF(r.left() + r.width() * 0.65, r.center().y()),
                                           QPointF(r.left() + r.width() * 0.8, r.top() + r.height() * 0.3),
                                           QPointF(r.right(), r.bottom()), QPointF(r.left(), r.bottom())});
        }
    }
    painter.restore();
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
    PictureCache pictures;
    const qsizetype backdrop = drawing::backdropIndex(drawing);
    if (backdrop >= 0) {
        // The backdrop decoded at the size this picture shows it (twice that, for a sharp scale).
        const drawing::Stroke &b = drawing.strokes.at(backdrop);
        const QSize shown = (b.box.size() * k * 2).toSize().expandedTo(QSize(16, 16));
        pictures.insert(b.fileId, decodePicture(drawing.files, b.fileId, shown));
    }
    qsizetype hatchBudget = drawing::DrawingHatchLines;
    for (qsizetype i = 0; i < drawing.strokes.size(); ++i) {
        const drawing::Stroke &s = drawing.strokes.at(i);
        const Prepared prepared = prepare(s, std::min(drawing::MaxHatchLines, hatchBudget));
        hatchBudget -= prepared.hatch.size();
        if (!prepared.bounds.intersects(visible))
            continue;
        paintElement(p, s, prepared, dark, drawing.files, pictures, i == backdrop);
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
