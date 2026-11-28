#include "iconprovider.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMutexLocker>
#include <QPainter>
#include <QQmlComponent>
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

void Icons::addProvider(QQmlEngine *engine)
{
    if (engine && !engine->imageProvider(IconImageProvider::providerId()))
        engine->addImageProvider(IconImageProvider::providerId(), new IconImageProvider);
}

Icons *Icons::forEngine(QQmlEngine *engine)
{
    addProvider(engine);
    return new Icons;
}

QUrl Icons::source(const QString &name, const QColor &color) const
{
    return sourceUrl(name, color);
}

QStringList Icons::names(const QString &folder) const
{
    QStringList out;
    const QFileInfoList files = QDir(folder).entryInfoList({u"*.svg"_s}, QDir::Files, QDir::NoSort);
    for (const QFileInfo &file : files)
        out.append(file.completeBaseName());
    out.sort(); // (by code points: QDir's order depends on the locale)
    return out;
}

QUrl Icons::sourceUrl(const QString &name, const QColor &color)
{
    if (!IconImageProvider::validName(name))
        return {};
    return QUrl(u"image://"_s + IconImageProvider::providerId() + u'/' + colorKey(color) + u'/'
                + QString::fromLatin1(QUrl::toPercentEncoding(name)));
}

} // namespace brulion::ui

// ---- BIcon ----------------------------------------------------------------------------------------

namespace {

/// What every icon of an engine shares: the component of Qt Quick's Image, the theme singleton
/// (its iconSize and fg: the defaults).
struct IconEngineData {
    QQmlComponent *image = nullptr;
    QPointer<QObject> theme;
    bool themeLooked = false;
};

IconEngineData &engineData(QQmlEngine *engine)
{
    static QHash<QQmlEngine *, IconEngineData> data;
    auto it = data.find(engine);
    if (it != data.end())
        return *it;
    QObject::connect(engine, &QObject::destroyed, [key = engine] { data.remove(key); });
    IconEngineData d;
    brulion::ui::Icons::addProvider(engine);
    d.image = new QQmlComponent(engine, engine);
    d.image->setData("import QtQuick\nImage {}\n", QUrl(u"qrc:/brulion/BIconImage.qml"_s));
    if (d.image->isError())
        qWarning("BIcon: %s", qPrintable(d.image->errorString()));
    return *data.insert(engine, d);
}

QObject *themeOf(QQmlEngine *engine)
{
    IconEngineData &d = engineData(engine);
    if (!d.themeLooked) {
        d.themeLooked = true;
        d.theme = engine->singletonInstance<QObject *>("Brulion", "Theme");
    }
    return d.theme;
}

} // namespace

using brulion::ui::Icons;

BIcon::BIcon(QQuickItem *parent) : QQuickItem(parent) { }

void BIcon::setName(const QString &name)
{
    if (name == m_name)
        return;
    m_name = name;
    if (isComponentComplete()) {
        updateImplicitSize();
        updateImage();
    }
    Q_EMIT nameChanged();
}

void BIcon::setIconSize(qreal size)
{
    m_sizeSet = true;
    if (qFuzzyCompare(size, m_size))
        return;
    m_size = size;
    if (isComponentComplete()) {
        updateImplicitSize();
        updateImage();
    }
    Q_EMIT sizeChanged();
}

void BIcon::setColor(const QColor &color)
{
    m_colorSet = true;
    if (color == m_color)
        return;
    m_color = color;
    if (isComponentComplete())
        updateImage();
    Q_EMIT colorChanged();
}

void BIcon::setCollapseWhenEmpty(bool collapse)
{
    if (collapse == m_collapseWhenEmpty)
        return;
    m_collapseWhenEmpty = collapse;
    if (isComponentComplete())
        updateImplicitSize();
    Q_EMIT collapseWhenEmptyChanged();
}

void BIcon::componentComplete()
{
    QQuickItem::componentComplete();
    if (!m_sizeSet || !m_colorSet) {
        if (QQmlEngine *engine = qmlEngine(this))
            m_theme = themeOf(engine);
        if (m_theme) {
            const QMetaObject *mo = m_theme->metaObject();
            const int slot = staticMetaObject.indexOfSlot("followTheme()");
            for (const char *name : {"iconSize", "fg"}) {
                if ((name[0] == 'i' && m_sizeSet) || (name[0] == 'f' && m_colorSet))
                    continue;
                const QMetaProperty p = mo->property(mo->indexOfProperty(name));
                if (p.hasNotifySignal())
                    QMetaObject::connect(m_theme, p.notifySignalIndex(), this, slot);
            }
            const bool sizeSet = m_sizeSet, colorSet = m_colorSet;
            followTheme();
            m_sizeSet = sizeSet; // (followTheme() sets them through the setters)
            m_colorSet = colorSet;
        }
    }
    updateImplicitSize();
    updateImage();
}

void BIcon::followTheme()
{
    if (!m_theme)
        return;
    const bool sizeSet = m_sizeSet, colorSet = m_colorSet;
    if (!sizeSet)
        setIconSize(m_theme->property("iconSize").toReal());
    if (!colorSet)
        setColor(m_theme->property("fg").value<QColor>());
    m_sizeSet = sizeSet;
    m_colorSet = colorSet;
}

void BIcon::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size())
        return;
    // An explicit width or height: the implicit size follows it (BIcon.qml's `implicitWidth: width`).
    // Without one the size is the implicit size already.
    setImplicitSize(newGeometry.width(), newGeometry.height());
    if (m_image) {
        m_image->setSize(newGeometry.size());
        updateFillMode();
    }
}

void BIcon::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged && value.boolValue && !m_image && isComponentComplete())
        updateImage();
}

void BIcon::updateImplicitSize()
{
    const qreal side = m_name.isEmpty() && m_collapseWhenEmpty ? 0 : m_size;
    // (A width that does not follow is an explicit one, which the implicit size keeps following.)
    setImplicitSize(side, side);
    if (width() != side || height() != side)
        setImplicitSize(width(), height());
}

void BIcon::updateImage()
{
    if (!m_image) {
        QQmlEngine *engine = qmlEngine(this);
        if (m_name.isEmpty() || !engine || !isVisible())
            return;
        QQmlComponent *component = engineData(engine).image;
        QObject *object = component->beginCreate(qmlContext(this) ? qmlContext(this) : engine->rootContext());
        m_image = qobject_cast<QQuickItem *>(object);
        if (!m_image) {
            delete object;
            return;
        }
        m_image->setParent(this); // (deleted with the icon: a parent item does not delete its children)
        m_image->setParentItem(this);
        m_image->setSize(QQuickItem::size());
        m_image->setProperty("sourceSize", QSizeF(m_size, m_size).toSize());
        updateFillMode();
        m_image->setProperty("source", Icons::sourceUrl(m_name, m_color));
        component->completeCreate();
        return;
    }
    m_image->setProperty("sourceSize", QSizeF(m_size, m_size).toSize());
    updateFillMode();
    m_image->setProperty("source", Icons::sourceUrl(m_name, m_color));
}

void BIcon::updateFillMode()
{
    // (Image.PreserveAspectFit 1, Image.Pad 6: QQuickImage::FillMode.)
    constexpr int PreserveAspectFit = 1;
    constexpr int Pad = 6;
    if (!m_image)
        return;
    const int side = qRound(m_size);
    m_image->setProperty("fillMode", width() < side || height() < side ? PreserveAspectFit : Pad);
}
