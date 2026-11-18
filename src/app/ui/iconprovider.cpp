#include "iconprovider.h"

#include <QImageReader>
#include <QMutexLocker>
#include <QPainter>
#include <QQmlEngine>
#include <QRgba64>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

/// At most this many rasters are kept (a few hundred icons in a session; the colours are the
/// pixmap cache's).
constexpr qsizetype MaxRasters = 512;

/// The colour in full precision (QColor keeps 16 bits a channel: a colour made with alpha(0.5)
/// must fill exactly as IconImage filled it), 16 hex digits: rrrrggggbbbbaaaa.
QString colorKey(const QColor &color)
{
    const QRgba64 c = color.rgba64();
    const quint64 v = (quint64(c.red()) << 48) | (quint64(c.green()) << 32) | (quint64(c.blue()) << 16) | c.alpha();
    return u"%1"_s.arg(v, 16, 16, QLatin1Char('0'));
}

bool parseColorKey(QStringView key, QColor *color)
{
    if (key.size() != 16)
        return false;
    bool ok = false;
    const quint64 v = key.toULongLong(&ok, 16);
    if (!ok)
        return false;
    *color = QColor::fromRgba64(quint16(v >> 48), quint16(v >> 32), quint16(v >> 16), quint16(v));
    return true;
}

} // namespace

QString IconImageProvider::providerId()
{
    return u"bicon"_s;
}

IconImageProvider::IconImageProvider(const QString &folder)
    : QQuickImageProvider(QQuickImageProvider::Image), m_folder(folder)
{ }

bool IconImageProvider::validName(const QString &name)
{
    return !name.isEmpty() && !name.startsWith(u'.') && !name.contains(u'/') && !name.contains(u'\\')
           && !name.contains(QChar(0));
}

int IconImageProvider::rasterCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_rasterCount;
}

QImage IconImageProvider::raster(const QString &name, const QSize &pixelSize)
{
    if (!validName(name))
        return {};
    const QString key = name + u'@' + QString::number(pixelSize.width()) + u'x' + QString::number(pixelSize.height());
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_rasters.constFind(key);
        if (it != m_rasters.constEnd())
            return it.value();
    }
    // As QQuickPixmap reads a file (readImage): the reader's own size when none is asked for,
    // else the requested size (an SVG scales to it: QQuickImageProviderWithOptions::loadSize).
    QImageReader reader(m_folder + u'/' + name + u".svg"_s);
    if (pixelSize.width() > 0 && pixelSize.height() > 0)
        reader.setScaledSize(pixelSize);
    QImage image;
    if (!reader.read(&image))
        return {};
    // What the pixmap cache's texture factory keeps (QQuickDefaultTextureFactory), and the image
    // IconImage recoloured.
    if (image.format() != QImage::Format_ARGB32_Premultiplied && image.format() != QImage::Format_RGB32)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QMutexLocker lock(&m_mutex);
    if (m_rasters.size() >= MaxRasters)
        m_rasters.clear();
    m_rasters.insert(key, image);
    ++m_rasterCount;
    return image;
}

QImage IconImageProvider::recoloured(const QImage &image, const QColor &color)
{
    if (image.isNull() || color.alpha() <= 0)
        return image;
    QImage out = image;
    QPainter painter(&out);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(out.rect(), color);
    painter.end();
    return out;
}

QImage IconImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const qsizetype slash = id.indexOf(u'/');
    QColor color;
    if (slash < 0 || !parseColorKey(QStringView(id).first(slash), &color))
        return {};
    const QString name = QUrl::fromPercentEncoding(id.sliced(slash + 1).toUtf8());
    const QImage image = recoloured(raster(name, requestedSize), color);
    if (size)
        *size = image.size();
    return image;
}

Icons::Icons(QObject *parent) : QObject(parent) { }

Icons *Icons::forEngine(QQmlEngine *engine)
{
    if (engine && !engine->imageProvider(IconImageProvider::providerId()))
        engine->addImageProvider(IconImageProvider::providerId(), new IconImageProvider);
    return new Icons;
}

QUrl Icons::source(const QString &name, const QColor &color) const
{
    if (!IconImageProvider::validName(name))
        return {};
    return QUrl(u"image://"_s + IconImageProvider::providerId() + u'/' + colorKey(color) + u'/'
                + QString::fromLatin1(QUrl::toPercentEncoding(name)));
}

} // namespace brulion::ui
