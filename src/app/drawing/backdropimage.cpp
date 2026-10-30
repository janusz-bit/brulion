#include "backdropimage.h"

#include "editorhost.h"
#include "pdfpages.h"

#include "drawing/svgwrite.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QSvgRenderer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::drawingview {

namespace {

QByteArray encoded(const QImage &image, const char *format, int quality = -1)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, format, quality))
        return {};
    return bytes;
}

/// Some pixel is not fully opaque.
bool seeThrough(const QImage &image)
{
    if (!image.hasAlphaChannel())
        return false;
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(line[x]) != 255)
                return true;
        }
    }
    return false;
}

/// The smaller of PNG and JPEG (PNG only when see-through).
drawing::BackdropImage smaller(const QImage &image, bool alpha)
{
    drawing::BackdropImage out{encoded(image, "PNG"), u"image/png"_s, image.size()};
    if (!alpha) {
        const QByteArray jpeg = encoded(image.convertToFormat(QImage::Format_RGB32), "JPEG", 85);
        if (!jpeg.isEmpty() && (out.bytes.isEmpty() || jpeg.size() < out.bytes.size())) {
            out.bytes = jpeg;
            out.mimeType = u"image/jpeg"_s;
        }
    }
    return out;
}

QString nameOf(const QString &vaultPath)
{
    const QString name = QFileInfo(vaultPath).completeBaseName().trimmed();
    return name.isEmpty() ? QFileInfo(vaultPath).fileName() : name;
}

PreparedBackdrop failed(const QString &vaultPath, int page, QStringView error)
{
    PreparedBackdrop out;
    out.source = drawing::BackdropSource{vaultPath, page};
    out.name = nameOf(vaultPath);
    out.error = error.toString();
    return out;
}

/// The size of `size` with its long side at most `side`.
QSize within(QSize size, int side)
{
    if (size.width() <= side && size.height() <= side)
        return size;
    return size.scaled(side, side, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
}

} // namespace

drawing::BackdropImage encodeBackdrop(const QImage &source)
{
    if (source.isNull())
        return {};
    QImage image = source;
    if (image.width() > drawing::BackdropMaxPixels || image.height() > drawing::BackdropMaxPixels)
        image = image.scaled(within(image.size(), drawing::BackdropMaxPixels), Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
    const bool alpha = seeThrough(image);
    if (!alpha)
        image = image.convertToFormat(QImage::Format_RGB32);
    drawing::BackdropImage out = smaller(image, alpha);
    while (out.bytes.size() > drawing::MaxBackdropBytes
           && std::max(image.width(), image.height()) > drawing::BackdropMinPixels) {
        const int side = std::max(drawing::BackdropMinPixels, int(std::max(image.width(), image.height()) * 0.75));
        image = image.scaled(within(image.size(), side), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        out = smaller(image, alpha);
    }
    if (out.bytes.size() > drawing::MaxBackdropBytes && alpha) {
        // Still too large see-through (noise): on white, as a JPEG.
        QImage flat(image.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter p(&flat);
        p.drawImage(0, 0, image);
        p.end();
        out = smaller(flat, false);
    }
    if (out.bytes.isEmpty() || out.bytes.size() > drawing::MaxBackdropBytes)
        return {};
    return out;
}

PreparedBackdrop prepareBackdropPicture(const QString &absoluteFile, const QString &vaultPath)
{
    QFile file(absoluteFile);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxAttachmentBytes)
        return failed(vaultPath, 0, backdroperror::Unreadable);
    const QByteArray bytes = file.readAll();
    file.close();
    if (bytes.isEmpty())
        return failed(vaultPath, 0, backdroperror::Format);

    PreparedBackdrop out;
    out.source = drawing::BackdropSource{vaultPath, 0};
    out.name = nameOf(vaultPath);

    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAllocationLimit(256);
    reader.setAutoTransform(true);
    const QByteArray format = reader.format().toLower();
    const bool svg = format == "svg" || format == "svgz" || absoluteFile.endsWith(u".svg"_s, Qt::CaseInsensitive)
                     || absoluteFile.endsWith(u".svgz"_s, Qt::CaseInsensitive);
    QImage image;
    if (svg) {
        // A vector picture (a drawing's too): drawn BackdropMaxPixels on its long side.
        QSvgRenderer renderer(bytes);
        QSize size = renderer.isValid() ? renderer.defaultSize() : QSize();
        if (size.isEmpty())
            return failed(vaultPath, 0, backdroperror::Format);
        size = size.scaled(drawing::BackdropMaxPixels, drawing::BackdropMaxPixels, Qt::KeepAspectRatio)
                       .expandedTo(QSize(1, 1));
        image = QImage(size, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull())
            return failed(vaultPath, 0, backdroperror::Format);
        image.fill(Qt::transparent);
        QPainter p(&image);
        renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
        p.end();
        out.image = encodeBackdrop(image);
    } else {
        if (format.isEmpty() || !reader.canRead())
            return failed(vaultPath, 0, backdroperror::Format);
        const QSize own = reader.size();
        // A PNG or a JPEG small enough, upright: its bytes as they are.
        const bool keepable = (format == "png" || format == "jpeg" || format == "jpg")
                              && bytes.size() <= drawing::MaxBackdropBytes && own.isValid()
                              && own.width() <= drawing::BackdropMaxPixels && own.height() <= drawing::BackdropMaxPixels
                              && reader.transformation() == QImageIOHandler::TransformationNone;
        if (keepable) {
            out.image = drawing::BackdropImage{bytes, format == "png" ? u"image/png"_s : u"image/jpeg"_s, own};
            return out;
        }
        if (own.isValid() && (own.width() > drawing::BackdropMaxPixels || own.height() > drawing::BackdropMaxPixels))
            reader.setScaledSize(within(own, drawing::BackdropMaxPixels));
        image = reader.read();
        if (image.isNull())
            return failed(vaultPath, 0, backdroperror::Format);
        out.image = encodeBackdrop(image);
    }
    if (out.image.bytes.isEmpty())
        return failed(vaultPath, 0, backdroperror::Format);
    return out;
}

PreparedBackdrop prepareBackdropPdfPage(const QString &absoluteFile, const QString &vaultPath, int page)
{
    if (!platform::pdfAvailable())
        return failed(vaultPath, page, backdroperror::NoPdf);
    if (!QFileInfo(absoluteFile).isFile())
        return failed(vaultPath, page, backdroperror::Unreadable);
    const QImage rendered = platform::renderPdfPage(absoluteFile, page, drawing::BackdropMaxPixels);
    if (rendered.isNull()) // (why: a password, or anything else)
        return failed(vaultPath, page,
                      platform::pdfInfo(absoluteFile, false).locked ? backdroperror::Locked : backdroperror::Pdf);
    PreparedBackdrop out;
    out.source = drawing::BackdropSource{vaultPath, page};
    out.name = nameOf(vaultPath);
    out.image = encodeBackdrop(rendered);
    if (out.image.bytes.isEmpty())
        return failed(vaultPath, page, backdroperror::Pdf);
    return out;
}

QVariantMap toVariant(const PreparedBackdrop &p)
{
    return {{u"bytes"_s, p.image.bytes},
            {u"mimeType"_s, p.image.mimeType},
            {u"width"_s, p.image.pixels.width()},
            {u"height"_s, p.image.pixels.height()},
            {u"path"_s, p.source.path},
            {u"page"_s, p.source.page},
            {u"name"_s, p.name},
            {u"error"_s, p.error}};
}

QVariantMap toDrawingVariant(const PreparedBackdrop &prepared, qint64 now)
{
    QVariantMap out = toVariant(prepared);
    if (!prepared.error.isEmpty())
        return out;
    const std::optional<drawing::Drawing> d =
            drawing::newDrawingOnBackdrop(prepared.image, prepared.source, drawing::randomEditStamp(now));
    if (!d) {
        out.insert(u"error"_s, backdroperror::Format.toString());
        return out;
    }
    out.insert(u"svg"_s, drawing::toSvg(*d));
    out.remove(u"bytes"_s);
    return out;
}

PreparedBackdrop fromVariant(const QVariantMap &map)
{
    PreparedBackdrop p;
    p.image.bytes = map.value(u"bytes"_s).toByteArray();
    p.image.mimeType = map.value(u"mimeType"_s).toString();
    p.image.pixels = QSize(map.value(u"width"_s).toInt(), map.value(u"height"_s).toInt());
    p.source.path = map.value(u"path"_s).toString();
    p.source.page = std::max(0, map.value(u"page"_s).toInt());
    p.name = map.value(u"name"_s).toString();
    p.error = map.value(u"error"_s).toString();
    return p;
}

} // namespace brulion::drawingview
