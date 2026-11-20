#include "canvasimageprovider.h"

#include "canvas/canvas.h"
#include "canvas/svg.h"

#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSvgRenderer>
#include <QUrlQuery>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::canvasview {

CanvasImageProvider::CanvasImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) { }

QUrl CanvasImageProvider::url(const QString &absolutePath, bool dark)
{
    const QFileInfo info(absolutePath);
    QUrl url;
    url.setScheme(u"image"_s);
    url.setHost(u"canvas"_s);
    url.setPath(u'/' + QString::fromLatin1(QUrl::toPercentEncoding(absolutePath)));
    QUrlQuery q;
    q.addQueryItem(u"v"_s,
                   QString::number(info.lastModified().toMSecsSinceEpoch()) + u'-' + QString::number(info.size()));
    q.addQueryItem(u"dark"_s, dark ? u"1"_s : u"0"_s);
    url.setQuery(q);
    return url;
}

QImage CanvasImageProvider::picture(const QString &text, bool dark, QSize requested, QSize *natural)
{
    const canvas::ReadResult r = canvas::readCanvas(text);
    if (!r.canvas)
        return {};
    canvas::SvgOptions options;
    options.dark = dark;
    options.maxPixels = MaxSide;
    const QSizeF own = canvas::svgSize(*r.canvas, options);
    if (natural)
        *natural = own.toSize();
    QSize size = own.toSize();
    if (requested.width() > 0 || requested.height() > 0) {
        const double sx = requested.width() > 0 ? requested.width() / own.width() : HUGE_VAL;
        const double sy = requested.height() > 0 ? requested.height() / own.height() : HUGE_VAL;
        const double s = std::min(sx, sy);
        size = QSize(std::max(1, int(own.width() * s)), std::max(1, int(own.height() * s)));
    }
    size = size.boundedTo(QSize(MaxSide, MaxSide));
    QSvgRenderer renderer(canvas::toSvg(*r.canvas, options).toUtf8());
    if (!renderer.isValid())
        return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
    return image;
}

QImage CanvasImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    if (size)
        *size = QSize();
    const qsizetype q = id.indexOf(u'?');
    const QString path = QUrl::fromPercentEncoding((q < 0 ? id : id.left(q)).toUtf8());
    const QUrlQuery query(q < 0 ? QString() : id.mid(q + 1));
    if (!canvas::isCanvasPath(path))
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > canvas::MaxCanvasChars * 4)
        return {};
    const QImage image =
            picture(QString::fromUtf8(file.readAll()), query.queryItemValue(u"dark"_s) == u"1", requestedSize, size);
    return image;
}

} // namespace brulion::canvasview
