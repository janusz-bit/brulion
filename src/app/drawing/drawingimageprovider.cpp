#include "drawingimageprovider.h"

#include "drawingrender.h"

#include "drawing/drawing.h"
#include "drawing/palette.h"
#include "drawing/svgwrite.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSvgRenderer>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::drawingview {

namespace {

/// `natural` fitted into `requested` (both sides: within the box; one side: that side; none: as
/// it is), at most MaxSide a side, at least one pixel.
QSize fitted(const QSizeF &natural, const QSize &requested)
{
    if (natural.isEmpty() || !std::isfinite(natural.width()) || !std::isfinite(natural.height()))
        return {};
    double k = 1;
    if (requested.width() > 0 && requested.height() > 0)
        k = std::min(requested.width() / natural.width(), requested.height() / natural.height());
    else if (requested.width() > 0)
        k = requested.width() / natural.width();
    else if (requested.height() > 0)
        k = requested.height() / natural.height();
    const double side = DrawingImageProvider::MaxSide;
    k = std::min({k, side / natural.width(), side / natural.height()});
    return QSize(std::clamp(int(std::lround(natural.width() * k)), 1, DrawingImageProvider::MaxSide),
                 std::clamp(int(std::lround(natural.height() * k)), 1, DrawingImageProvider::MaxSide));
}

/// The file of a preview URL's id (the part after "image://drawing/"), refused when it is no
/// drawing file: not absolute, a ".." segment, another kind of file.
QString fileOf(const QString &id, bool *dark)
{
    const qsizetype q = id.indexOf(u'?');
    // Qt hands the id over as its URL shows it (most escapes decoded, "%2F" and "%3F" not).
    const QString path = QUrl::fromPercentEncoding((q < 0 ? id : id.left(q)).toUtf8());
    *dark = QUrlQuery(q < 0 ? QString() : id.mid(q + 1)).queryItemValue(u"dark"_s) == u"1";
    if (path.isEmpty() || !QDir::isAbsolutePath(path) || path.contains(QChar(0)))
        return {};
    const QString slashes = QDir::fromNativeSeparators(path);
    const QList<QStringView> segments = QStringView(slashes).split(u'/');
    if (std::any_of(segments.cbegin(), segments.cend(), [](QStringView segment) { return segment == u".."; }))
        return {};
    if (!path.endsWith(u".svg"_s, Qt::CaseInsensitive) && !path.endsWith(u".excalidraw"_s, Qt::CaseInsensitive))
        return {};
    return path;
}

} // namespace

DrawingImageProvider::DrawingImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) { }

QUrl DrawingImageProvider::url(const QString &absolutePath, const QString &version, bool dark)
{
    return QUrl(u"image://drawing/"_s + QString::fromLatin1(QUrl::toPercentEncoding(absolutePath)) + u"?v="_s
                        + QString::fromLatin1(QUrl::toPercentEncoding(version)) + u"&dark="_s + (dark ? u'1' : u'0'),
                QUrl::StrictMode);
}

QImage DrawingImageProvider::picture(const QByteArray &bytes, bool appDark, const QSize &requested, QSize *natural)
{
    if (natural)
        *natural = QSize();
    const std::optional<drawing::Drawing> d = drawing::readDrawing(bytes);
    if (d && drawing::editable(*d)) {
        // Brulion's own renderer: what the drawing editor's paper shows.
        const QRectF rect = drawing::pictureRect(*d);
        if (natural)
            *natural = QSize(int(rect.width()), int(rect.height()));
        return renderDrawing(*d, fitted(rect.size(), requested), drawing::rendersDark(d->theme, appDark));
    }
    // Shapes, text, pictures (an Excalidraw drawing), a scene that cannot be read: the file's own
    // picture, in the dark through Excalidraw's dark filter.
    QSvgRenderer svg(bytes);
    if (!svg.isValid())
        return {};
    const QSize own = svg.defaultSize();
    const QSize size = fitted(QSizeF(own), requested);
    if (size.isEmpty())
        return {};
    if (natural)
        *natural = own;
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return {};
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        svg.render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
    }
    return d ? (drawing::rendersDark(d->theme, appDark) ? darkImage(image) : image)
             : (appDark ? darkImage(image) : image);
}

QImage DrawingImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    if (size)
        *size = QSize();
    bool dark = false;
    const QString path = fileOf(id, &dark);
    if (path.isEmpty())
        return {};
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly))
        return {};
    if (file.size() <= 0 || file.size() > MaxFileBytes)
        return {};
    const QByteArray bytes = file.read(MaxFileBytes + 1);
    if (bytes.isEmpty() || bytes.size() > MaxFileBytes)
        return {};
    return picture(bytes, dark, requestedSize, size);
}

} // namespace brulion::drawingview
