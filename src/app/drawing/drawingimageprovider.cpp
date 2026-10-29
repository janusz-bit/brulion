#include "drawingimageprovider.h"

#include "drawingrender.h"

#include "picturesize.h"

#include "drawing/drawing.h"
#include "drawing/drawingfile.h"
#include "drawing/excalidraw.h"
#include "drawing/palette.h"
#include "drawing/svgwrite.h"

#include <QBuffer>
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
    // (A `.md` file is read only when it is an Obsidian Excalidraw plugin drawing:
    // drawing::readDrawingFile refuses any other.)
    if (!path.endsWith(u".svg"_s, Qt::CaseInsensitive) && !path.endsWith(u".excalidraw"_s, Qt::CaseInsensitive)
        && !path.endsWith(u".md"_s, Qt::CaseInsensitive))
        return {};
    return path;
}

/// The size an SVG drawing file's root names (what the editor's embeds, the picture view and the
/// drawing editor's notice were told: editor::pictureSize); invalid when it names none or the
/// bytes are no SVG drawing (an .excalidraw scene).
QSize ownSize(const QByteArray &bytes)
{
    if (!drawing::looksLikeExcalidrawSvg(bytes.left(drawing::HeadBytes)))
        return {};
    QByteArray copy = bytes; // (shared, not copied)
    QBuffer buffer(&copy);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};
    return editor::pictureSize(&buffer, u"drawing.svg"_s);
}

/// The part of the scene a preview shows: the file's own frame `own` around the picture
/// `picture` (drawing::pictureRect), centred on it, the strokes at one unit a pixel — made
/// smaller only when they do not fit.
QRectF frameAround(const QRectF &picture, const QSize &own)
{
    const double k = std::min({1.0, own.width() / picture.width(), own.height() / picture.height()});
    const QSizeF scene(own.width() / k, own.height() / k);
    return QRectF(picture.center() - QPointF(scene.width() / 2, scene.height() / 2), scene);
}

/// The picture is dark already (a "dark" drawing's file whose scene cannot be read): its edge —
/// the paper — over white is darker than mid grey.
bool looksDark(const QImage &picture)
{
    const QImage image = picture.convertToFormat(QImage::Format_ARGB32);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0)
        return false;
    double sum = 0;
    int n = 0;
    const auto add = [&](int x, int y) {
        const QRgb p = image.pixel(x, y);
        const drawing::Rgba seen =
                drawing::over({qRed(p), qGreen(p), qBlue(p), qAlpha(p)}, 100, drawing::Rgba{255, 255, 255, 255});
        sum += drawing::luminance(seen);
        ++n;
    };
    for (int x = 0; x < w; ++x) {
        add(x, 0);
        add(x, h - 1);
    }
    for (int y = 1; y < h - 1; ++y) {
        add(0, y);
        add(w - 1, y);
    }
    return n > 0 && sum / n < 0.18;
}

} // namespace

DrawingImageProvider::DrawingImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) { }

QUrl DrawingImageProvider::url(const QString &absolutePath, const QString &version, bool dark)
{
    return QUrl(u"image://drawing/"_s + QString::fromLatin1(QUrl::toPercentEncoding(absolutePath)) + u"?v="_s
                        + QString::fromLatin1(QUrl::toPercentEncoding(version)) + u"&dark="_s + (dark ? u'1' : u'0'),
                QUrl::StrictMode);
}

namespace {

/// A drawing in the frame `rect` of the scene, as large as `size` (its own size), fitted into
/// `requested`.
QImage renderScene(const drawing::Drawing &d, bool appDark, const QSize &size, const QRectF &rect,
                   const QSize &requested, QSize *natural)
{
    if (natural)
        *natural = size;
    return renderDrawing(d, fitted(QSizeF(size), requested), drawing::rendersDark(d.theme, appDark), rect);
}

/// A picture file's own picture (an SVG Brulion does not draw, a PNG the Excalidraw plugin
/// exported), in the dark with Excalidraw's dark colours — unless it is dark already.
QImage ownPicture(const QByteArray &bytes, bool dark, const QSize &requested, QSize *natural)
{
    if (bytes.startsWith("\x89PNG\r\n\x1a\n")) {
        // A raster picture (the plugin's PNG export).
        QImage raster = QImage::fromData(bytes);
        if (raster.isNull())
            return {};
        const QSize size = fitted(QSizeF(raster.size()), requested);
        if (size.isEmpty())
            return {};
        if (natural)
            *natural = raster.size();
        const QImage image = raster.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                     .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        return !dark || looksDark(image) ? image : darkImage(image);
    }
    QSvgRenderer svg(bytes);
    if (!svg.isValid())
        return {};
    const QSize own = svg.defaultSize();
    const QSize size = fitted(QSizeF(own), requested);
    if (size.isEmpty())
        return {};
    if (natural)
        *natural = own;
    const auto render = [&size](QSvgRenderer &renderer) {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull())
            return image;
        image.fill(Qt::transparent);
        {
            QPainter p(&image);
            renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
        }
        return image;
    };
    const QImage image = render(svg);
    // Dark when the drawing (or, its scene unread, the app) says so — unless the file's picture
    // is dark already: a "dark" drawing's file whose scene cannot be read (damaged, too large)
    // has the dark colours in it, which the filter would turn light again.
    if (!dark || image.isNull() || looksDark(image))
        return image;
    // Its colours as Excalidraw's dark mode shows them, one by one: the pictures and emoji in it
    // keep theirs. (Not well-formed for the colour mapping: the dark filter on every pixel.)
    if (const std::optional<QByteArray> mapped = drawing::darkSvg(bytes)) {
        QSvgRenderer mappedSvg(*mapped);
        if (mappedSvg.isValid() && mappedSvg.defaultSize() == own)
            return render(mappedSvg);
    }
    return darkImage(image);
}

/// The picture of a drawing file's bytes (see DrawingImageProvider::picture); `exportFile`: the
/// Excalidraw plugin's export beside a `.md` drawing (picturesize.h drawingExportOf), the preview
/// of one Brulion does not draw.
QImage pictureOf(const QByteArray &bytes, const std::optional<drawing::DrawingFile> &file, bool appDark,
                 const QSize &requested, QSize *natural, const QString &exportFile)
{
    if (natural)
        *natural = QSize();
    const bool svgFile = !file || file->format == drawing::FileFormat::Svg;
    if (file && drawing::editable(file->drawing)) {
        // Brulion's own renderer: what the drawing editor's paper shows, in the frame of the
        // file's own size — what the embeds were sized by. Brulion's SVG has its pictureRect
        // there; one Excalidraw wrote frames the strokes a little differently; a `.excalidraw`
        // scene or a plugin file has no size of its own (editor::localDrawingSize: pictureRect).
        const QRectF rect = drawing::pictureRect(file->drawing);
        const QSize own = svgFile ? ownSize(bytes) : QSize();
        const bool framed = own.isValid() && !own.isEmpty();
        const QSize size = framed ? own : QSize(int(rect.width()), int(rect.height()));
        return renderScene(file->drawing, appDark, size, framed ? frameAround(rect, own) : rect, requested, natural);
    }
    if (!svgFile) {
        // A `.excalidraw` scene or a plugin file with frames or embedded pages: the plugin's
        // export beside it, else its elements with a dashed box for each of the others — never a
        // blank picture.
        if (!exportFile.isEmpty()) {
            QFile f(exportFile);
            if (f.open(QIODevice::ReadOnly) && f.size() <= DrawingImageProvider::MaxFileBytes) {
                const QImage image = ownPicture(f.readAll(), drawing::rendersDark(file->drawing.theme, appDark),
                                                requested, natural);
                if (!image.isNull())
                    return image;
            }
        }
        const drawing::Drawing preview = drawing::previewDrawing(file->drawing);
        const QRectF rect = drawing::pictureRect(preview);
        return renderScene(preview, appDark, QSize(int(rect.width()), int(rect.height())), rect, requested, natural);
    }
    // Frames, embedded pages (an Excalidraw drawing Brulion does not edit), a scene that cannot be
    // read: the file's own picture, in the dark with Excalidraw's dark colours.
    return ownPicture(bytes, file ? drawing::rendersDark(file->drawing.theme, appDark) : appDark, requested,
                      natural);
}

} // namespace

QImage DrawingImageProvider::picture(const QByteArray &bytes, bool appDark, const QSize &requested, QSize *natural)
{
    return pictureOf(bytes, drawing::readDrawingFile(bytes), appDark, requested, natural, QString());
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
    const std::optional<drawing::DrawingFile> read = drawing::readDrawingFile(bytes);
    if (path.endsWith(u".md"_s, Qt::CaseInsensitive) && !read)
        return {}; // a note, not a drawing
    const QString exportFile =
            read && read->format == drawing::FileFormat::ExcalidrawMarkdown && !drawing::editable(read->drawing)
                    ? editor::drawingExportOf(path)
                    : QString();
    return pictureOf(bytes, read, dark, requestedSize, size, exportFile);
}

} // namespace brulion::drawingview
