// The icons of BIcon (the item below, QML type BIcon) as images of Qt Quick's pixmap cache: Lucide's
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
#include <QPointer>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

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
    /// The same, for C++ (BIcon).
    static QUrl sourceUrl(const QString &name, const QColor &color);
    /// Gives `engine` the provider unless it has one.
    static void addProvider(QQmlEngine *engine);
};

} // namespace brulion::ui

/// BIcon (registered by qmlforeign.h; in the global namespace: the UI tests and the remote
/// control's `find --type` match QML types by class name) — a Lucide icon at any size and colour:
///
///   BIcon { name: "bell"; size: Theme.iconSize; color: Theme.fg }
///
/// What BIcon.qml was until 2026-11 (an Item holding an Image whose picture comes from the
/// provider above), in C++: the Image is made from Qt Quick's own type, only once the icon has a
/// name and is visible (its window shows it: a hidden chip's or view's icon has none), and fed
/// from here — no bindings, no Accessible attached object, no context of its own (a
/// few hundred icons live at once: docs/PERFORMANCE.md). The Image is the item's first and only
/// child, as wide and high as the item; its source Icons::sourceUrl(name, color), its source size
/// size × size (rounded, as Qt.size() into a QSize), its fill mode IconImage's choice: the picture at
/// its own size, centred, while it fits (Image.Pad), else scaled down (PreserveAspectFit). The
/// item's implicit size is size × size (0 × 0 without a name when collapseWhenEmpty). `size` and
/// `color` default to Theme.iconSize and Theme.fg and follow them until set. Icons are decorative
/// for assistive technology (not accessible items): label the control instead.
class BIcon : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged FINAL)
    Q_PROPERTY(qreal size READ iconSize WRITE setIconSize NOTIFY sizeChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged FINAL)
    Q_PROPERTY(bool collapseWhenEmpty READ collapseWhenEmpty WRITE setCollapseWhenEmpty NOTIFY collapseWhenEmptyChanged
                       FINAL)

public:
    explicit BIcon(QQuickItem *parent = nullptr);

    QString name() const { return m_name; }
    void setName(const QString &name);
    qreal iconSize() const { return m_size; }
    void setIconSize(qreal size);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    bool collapseWhenEmpty() const { return m_collapseWhenEmpty; }
    void setCollapseWhenEmpty(bool collapse);

    /// The Image that shows the picture (null until the icon is first visible with a name).
    QQuickItem *image() const { return m_image; }

Q_SIGNALS:
    void nameChanged();
    void sizeChanged();
    void colorChanged();
    void collapseWhenEmptyChanged();

protected:
    void componentComplete() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private Q_SLOTS:
    void followTheme();

private:
    void updateImplicitSize();
    void updateImage();
    void updateFillMode();

    QString m_name;
    qreal m_size = 24;
    QColor m_color = Qt::black;
    bool m_collapseWhenEmpty = true;
    bool m_sizeSet = false;
    bool m_colorSet = false;
    QPointer<QObject> m_theme; // followed while size or colour are the theme's
    QQuickItem *m_image = nullptr;
};
