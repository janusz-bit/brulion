// The icons of BIcon (qml/components/BIcon.qml) as images of Qt Quick's pixmap cache: Lucide's
// monochrome SVGs (assets/icons, ":/icons/<name>.svg") rasterised at the size asked for and
// recoloured on the CPU.
//
//   BIcon is an Item with one Image { source: Icons.source(name, color); sourceSize: … }. It used to
//   hold a Loader of Qt's IconImage (with its component and context five objects per icon, every
//   icon recoloured and uploaded as a texture of its own); the pixmap cache now shares one image and
//   one texture among all the icons of a name, size and colour.
//
// IconImageProvider ("image://bicon/<colour>/<name>", the colour as 16 hex digits rrrrggggbbbbaaaa:
// a QColor's full precision, which the fill can show) draws exactly what Qt's IconImage drew: the
// SVG read through QImageReader at the requested size (the source size × the device pixel ratio,
// as QQuickImageBase asks), then — unless the colour is fully transparent — filled with the colour
// in CompositionMode_SourceIn (QQuickIconImage::pixmapChange). tests/app/icons compares the two
// pixel by pixel. Icons is the QML singleton that builds those URLs; creating it gives its
// engine the provider (any engine that loads BIcon: the app's, tests', tools'). Library
// brulion_ui_icons (Qt Quick's public API); registered by qmlforeign.h.
#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QUrl>

class QQmlEngine;

namespace brulion::ui {

class IconImageProvider : public QQuickImageProvider {
public:
    /// The provider's name in "image://<name>/…".
    static QString providerId();

    /// `folder`: where the SVGs are (":/icons" in the app; a test's own folder).
    explicit IconImageProvider(const QString &folder = QStringLiteral(":/icons"));

    /// `id` = "<colour>/<name>" (Icons::source); `requestedSize` the size in device pixels. A
    /// null image (an unknown name, a malformed id): the Image reports an error, as a missing
    /// file did.
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    /// The SVG of `name` rasterised at `pixelSize` (unscaled when invalid), uncoloured — cached:
    /// a colour that changes (an animated hover) only recolours.
    QImage raster(const QString &name, const QSize &pixelSize);
    /// `image` in `color` (SourceIn), as IconImage recoloured it; unchanged when `color` is
    /// fully transparent (IconImage left such icons alone).
    static QImage recoloured(const QImage &image, const QColor &color);
    /// A file name may not leave the folder (names can come from notes: a page's icon).
    static bool validName(const QString &name);

    /// SVGs rasterised so far (tests: the cache works).
    int rasterCount() const;

private:
    QString m_folder;
    mutable QMutex m_mutex; // Image { asynchronous: true } asks from Qt's loader thread
    QHash<QString, QImage> m_rasters;
    int m_rasterCount = 0;
};

class Icons : public QObject {
    Q_OBJECT

public:
    explicit Icons(QObject *parent = nullptr);

    /// The singleton for `engine`, which gets the provider if it has none yet.
    static Icons *forEngine(QQmlEngine *engine);

    /// "image://bicon/<colour>/<name>", the name percent-encoded ("" for an empty or invalid
    /// name: no image, no lookup).
    Q_INVOKABLE QUrl source(const QString &name, const QColor &color) const;
};

} // namespace brulion::ui
