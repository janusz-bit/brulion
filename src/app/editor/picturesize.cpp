#include "picturesize.h"

#include "drawing/drawingfile.h"
#include "drawing/excalidraw.h"
#include "drawing/svgwrite.h"

#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QXmlStreamReader>

#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

bool hasSuffix(const QString &name, QLatin1StringView suffix)
{
    return name.endsWith(suffix, Qt::CaseInsensitive);
}

/// A document of the root <svg> element's width, height and viewBox only (nullopt: the file does
/// not start with an <svg> element, or is not well-formed up to it).
std::optional<QByteArray> svgRootOnly(QIODevice *device)
{
    QXmlStreamReader xml(device);
    while (!xml.atEnd()) {
        switch (xml.readNext()) {
        case QXmlStreamReader::StartElement: {
            if (xml.name() != u"svg")
                return std::nullopt;
            const QXmlStreamAttributes attributes = xml.attributes();
            QString doc = u"<svg xmlns=\"http://www.w3.org/2000/svg\""_s;
            for (const QString &name : {u"width"_s, u"height"_s, u"viewBox"_s}) {
                if (attributes.hasAttribute(name))
                    doc += u' ' + name + u"=\""_s + attributes.value(name).toString().toHtmlEscaped() + u'"';
            }
            doc += u"/>"_s;
            return doc.toUtf8();
        }
        case QXmlStreamReader::Invalid: return std::nullopt;
        default: break;
        }
    }
    return std::nullopt;
}

QSize readerSize(QImageReader &reader)
{
    const QSize size = reader.size();
    return size.isValid() && !size.isEmpty() ? size : QSize();
}

QSize svgSize(QIODevice *device)
{
    const qint64 start = device->pos();
    if (std::optional<QByteArray> root = svgRootOnly(device)) {
        QBuffer buffer(&*root);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "svg");
        const QSize size = readerSize(reader);
        // Empty: the size comes from the drawing's bounds (no viewBox, no absolute width and
        // height) — only the whole document has it.
        if (size.isValid())
            return size;
    }
    if (device->size() - start > MaxFullSvgBytes || !device->seek(start))
        return {};
    QImageReader reader(device, "svg");
    return readerSize(reader);
}

} // namespace

QSize pictureSize(QIODevice *device, const QString &fileName)
{
    if (!device || !device->isReadable())
        return {};
    if (hasSuffix(fileName, ".svg"_L1))
        return svgSize(device);
    if (hasSuffix(fileName, ".svgz"_L1)) {
        if (device->size() - device->pos() > MaxFullSvgBytes)
            return {};
        QImageReader reader(device, "svgz");
        return readerSize(reader);
    }
    // No format: the reader recognises the content (with a QFile, its name's suffix as a hint).
    QImageReader reader(device);
    return readerSize(reader);
}

QSize localPictureSize(const QString &file)
{
    const QFileInfo info(file);
    if (!info.isFile())
        return {};
    struct Entry {
        QDateTime modified;
        qint64 bytes = -1;
        QSize size;
    };
    // The editor's thread only in the app; guarded anyway (tests, a future indexer thread).
    static QMutex mutex;
    static QHash<QString, Entry> cache;
    const QDateTime modified = info.lastModified();
    {
        const QMutexLocker lock(&mutex);
        const auto it = cache.constFind(file);
        if (it != cache.constEnd() && it->modified == modified && it->bytes == info.size())
            return it->size;
    }
    QFile f(file);
    const QSize size = f.open(QIODevice::ReadOnly) ? pictureSize(&f, file) : QSize();
    const QMutexLocker lock(&mutex);
    if (cache.size() > 2000)
        cache.clear();
    cache.insert(file, Entry{modified, info.size(), size});
    return size;
}

bool localFileIsDrawing(const QString &file)
{
    if (!hasSuffix(file, ".svg"_L1) && !hasSuffix(file, ".excalidraw"_L1) && !hasSuffix(file, ".md"_L1))
        return false;
    const QFileInfo info(file);
    if (!info.isFile())
        return false;
    struct Entry {
        QDateTime modified;
        qint64 bytes = -1;
        bool drawing = false;
    };
    static QMutex mutex;
    static QHash<QString, Entry> cache;
    const QDateTime modified = info.lastModified();
    {
        const QMutexLocker lock(&mutex);
        const auto it = cache.constFind(file);
        if (it != cache.constEnd() && it->modified == modified && it->bytes == info.size())
            return it->drawing;
    }
    QFile f(file);
    const bool drawing = f.open(QIODevice::ReadOnly)
                         && brulion::drawing::looksLikeDrawingFile(file, f.read(brulion::drawing::HeadBytes));
    const QMutexLocker lock(&mutex);
    if (cache.size() > 2000)
        cache.clear();
    cache.insert(file, Entry{modified, info.size(), drawing});
    return drawing;
}

QString drawingExportOf(const QString &file)
{
    if (!hasSuffix(file, ".md"_L1))
        return {};
    // "Szkic.excalidraw.md" → "Szkic.excalidraw.svg", "Szkic.svg" (the plugin's two naming
    // settings); "Plan.md" → "Plan.svg".
    QStringList stems{file.chopped(3)};
    if (hasSuffix(stems.first(), ".excalidraw"_L1))
        stems.append(stems.first().chopped(11));
    for (const QString &stem : std::as_const(stems)) {
        for (const QLatin1StringView ext : {".svg"_L1, ".png"_L1}) {
            const QString candidate = stem + ext;
            if (QFileInfo(candidate).isFile())
                return candidate;
        }
    }
    return {};
}

QSize localDrawingSize(const QString &file)
{
    const QFileInfo info(file);
    if (!info.isFile() || info.size() > brulion::drawing::MaxSceneBytes)
        return {};
    struct Entry {
        QDateTime modified;
        qint64 bytes = -1;
        QSize size;
        bool foreign = false; ///< a drawing Brulion does not draw (its export may stand for it)
    };
    static QMutex mutex;
    static QHash<QString, Entry> cache;
    const QDateTime modified = info.lastModified();
    std::optional<Entry> entry;
    {
        const QMutexLocker lock(&mutex);
        const auto it = cache.constFind(file);
        if (it != cache.constEnd() && it->modified == modified && it->bytes == info.size())
            entry = *it;
    }
    if (!entry) {
        entry = Entry{modified, info.size(), {}, false};
        QFile f(file);
        if (f.open(QIODevice::ReadOnly)) {
            const std::optional<brulion::drawing::DrawingFile> read =
                    brulion::drawing::readDrawingFile(f.read(brulion::drawing::MaxSceneBytes));
            if (read && read->format != brulion::drawing::FileFormat::Svg) {
                entry->foreign = !brulion::drawing::editable(read->drawing);
                const QRectF rect = brulion::drawing::pictureRect(brulion::drawing::previewDrawing(read->drawing));
                entry->size = QSize(int(std::ceil(rect.width())), int(std::ceil(rect.height())));
            }
        }
        const QMutexLocker lock(&mutex);
        if (cache.size() > 2000)
            cache.clear();
        cache.insert(file, *entry);
    }
    if (entry->foreign) {
        // A drawing Brulion does not draw: the plugin's export beside it is its preview (looked up
        // every time: it may come or go without the drawing changing).
        const QString exportFile = drawingExportOf(file);
        if (!exportFile.isEmpty()) {
            if (const QSize own = localPictureSize(exportFile); own.isValid() && !own.isEmpty())
                return own;
        }
    }
    return entry->size;
}

bool canDecodePictureFormat(const QString &fileName)
{
    const QString format = pictureFormatName(fileName).toLower();
    if (format.isEmpty())
        return false;
    // The image plugins are loaded once per process: their formats never change while it runs.
    static const QSet<QString> formats = [] {
        QSet<QString> out;
        const QList<QByteArray> supported = QImageReader::supportedImageFormats();
        for (const QByteArray &f : supported)
            out.insert(QString::fromLatin1(f).toLower());
        return out;
    }();
    return formats.contains(format);
}

QString pictureFormatName(const QString &fileName)
{
    const qsizetype slash = fileName.lastIndexOf(u'/');
    const qsizetype dot = fileName.lastIndexOf(u'.');
    if (dot <= slash + 1 || dot == fileName.size() - 1)
        return {};
    return fileName.mid(dot + 1).toUpper();
}

} // namespace brulion::editor
