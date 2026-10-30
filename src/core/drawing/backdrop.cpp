#include "drawing/backdrop.h"

#include "drawing/excalidraw.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>
#include <climits>
#include <cmath>
#include <iterator>
#include <set>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

namespace {

const QStringList &backdropMimeTypes()
{
    static const QStringList types = {u"image/png"_s, u"image/jpeg"_s, u"image/webp"_s, u"image/gif"_s};
    return types;
}

/// The marker of an element (an empty object when it has none or it is no object).
QJsonObject markerOf(const Stroke &e, bool *present = nullptr)
{
    const QJsonValue v = e.element.value(u"customData"_s).toObject().value(BackdropKey);
    if (present)
        *present = v.isObject();
    return v.toObject();
}

/// The path as a relative vault path, or "".
QString safePath(const QString &path)
{
    if (path.isEmpty() || path.size() > 4096 || path.startsWith(u'/') || path.startsWith(u'\\'))
        return {};
    if (path.size() >= 2 && path.at(1) == u':')
        return {}; // "C:\…", "c:/…"
    for (const QChar c : path) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            return {};
    }
    const QString slashes = QString(path).replace(u'\\', u'/');
    for (const QStringView segment : QStringView(slashes).split(u'/')) {
        if (segment == u"..")
            return {};
    }
    return path;
}

int safePage(const QJsonValue &v)
{
    if (!v.isDouble())
        return 0;
    const double page = v.toDouble();
    if (!std::isfinite(page) || page != std::floor(page) || page < 1 || page > MaxBackdropPage)
        return 0;
    return int(page);
}

QJsonObject markerJson(const BackdropSource &source)
{
    return QJsonObject{{u"path"_s, safePath(source.path)},
                       {u"page"_s, source.page >= 1 && source.page <= MaxBackdropPage ? source.page : 0}};
}

double round2(double v)
{
    return std::round(v * 100) / 100;
}

int randomOf(const EditStamp &s)
{
    const quint32 r = s.random ? s.random() : 1;
    return int(std::max<quint32>(1, r % 2147483647u));
}

} // namespace

qsizetype backdropIndex(const Drawing &d)
{
    if (d.strokes.isEmpty())
        return -1;
    const Stroke &e = d.strokes.first();
    if (e.type != ElementType::Image || !e.locked)
        return -1;
    bool present = false;
    markerOf(e, &present);
    return present ? 0 : -1;
}

std::optional<BackdropSource> backdropSource(const Drawing &d)
{
    if (backdropIndex(d) < 0)
        return std::nullopt;
    const QJsonObject marker = markerOf(d.strokes.first());
    return BackdropSource{safePath(marker.value(u"path"_s).toString()), safePage(marker.value(u"page"_s))};
}

QSizeF backdropSceneSize(QSize pixels)
{
    if (pixels.width() <= 0 || pixels.height() <= 0)
        return {};
    const double k = BackdropLongSide / std::max(pixels.width(), pixels.height());
    return QSizeF(round2(pixels.width() * k), round2(pixels.height() * k));
}

QString fileIdOf(QByteArrayView bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}

QString dataUrl(const BackdropImage &image)
{
    return u"data:"_s + image.mimeType + u";base64,"_s + QString::fromLatin1(image.bytes.toBase64());
}

bool validBackdropImage(const BackdropImage &image)
{
    return !image.bytes.isEmpty() && image.bytes.size() <= MaxBackdropBytes
           && backdropMimeTypes().contains(image.mimeType) && image.pixels.width() > 0 && image.pixels.height() > 0;
}

bool setBackdrop(Drawing &d, const BackdropImage &image, const BackdropSource &source, const EditStamp &stamp)
{
    if (!validBackdropImage(image))
        return false;
    const QSizeF size = backdropSceneSize(image.pixels);
    const QString fileId = fileIdOf(image.bytes);
    d.files.insert(fileId, QJsonObject{{u"mimeType"_s, image.mimeType},
                                       {u"id"_s, fileId},
                                       {u"dataURL"_s, dataUrl(image)},
                                       {u"created"_s, double(stamp.now)},
                                       {u"lastRetrieved"_s, double(stamp.now)}});
    if (backdropIndex(d) == 0) {
        // The backdrop itself changed: merges see it changed here, undo diffs it.
        Stroke &e = d.strokes.first();
        e.box = QRectF(e.box.topLeft(), size);
        e.angle = 0;
        e.opacity = 100;
        e.fileId = fileId;
        e.version = e.version < INT_MAX ? e.version + 1 : 2;
        e.versionNonce = randomOf(stamp);
        e.updated = stamp.now;
        QJsonObject custom = e.element.value(u"customData"_s).toObject();
        custom.insert(BackdropKey.toString(), markerJson(source));
        e.element.insert(u"customData"_s, custom);
        e.element.insert(u"crop"_s, QJsonValue::Null);
        e.element.insert(u"scale"_s, QJsonArray{1, 1});
        e.element.insert(u"status"_s, u"saved"_s);
        return true;
    }
    Stroke e;
    e.type = ElementType::Image;
    e.id = stamp.newId ? stamp.newId() : u"backdrop"_s;
    e.box = QRectF(QPointF(0, 0), size);
    e.color = u"transparent"_s;
    e.fill = u"transparent"_s;
    e.width = 1;
    e.roughness = 0;
    e.locked = true;
    e.fileId = fileId;
    e.seed = randomOf(stamp);
    e.version = 1;
    e.versionNonce = randomOf(stamp);
    e.updated = stamp.now;
    // Written from its read form (with the marker) while unchanged: excalidraw.h elementJson().
    QJsonObject json = elementJson(e);
    json.insert(u"crop"_s, QJsonValue::Null);
    json.insert(u"customData"_s, QJsonObject{{BackdropKey.toString(), markerJson(source)}});
    e.element = json;
    d.strokes.prepend(std::move(e));
    return true;
}

bool removeBackdrop(Drawing &d)
{
    if (backdropIndex(d) != 0)
        return false;
    d.strokes.removeFirst();
    return true;
}

std::optional<Drawing> newDrawingOnBackdrop(const BackdropImage &image, const BackdropSource &source,
                                            const EditStamp &stamp)
{
    Drawing d;
    if (!setBackdrop(d, image, source, stamp))
        return std::nullopt;
    d.theme = source.page > 0 ? PaperTheme::Auto : PaperTheme::Light;
    return d;
}

QList<int> parsePageRanges(QStringView text, int count, int limit)
{
    if (count <= 0 || limit <= 0)
        return {};
    const QString lower = text.trimmed().toString().toLower();
    // The lowest `limit` pages named (a huge range stops as soon as it cannot add any).
    std::set<int> pages;
    const auto add = [&pages, limit](int p) {
        if (int(pages.size()) >= limit && p > *pages.rbegin())
            return false;
        pages.insert(p);
        if (int(pages.size()) > limit)
            pages.erase(std::prev(pages.end()));
        return true;
    };
    const auto addRange = [&](int a, int b) {
        if (a > b)
            std::swap(a, b);
        a = std::max(a, 1);
        b = std::min(b, count);
        for (int p = a; p <= b && add(p); ++p) { }
    };
    if (lower == u"wszystkie" || lower == u"all" || lower == u"*") {
        addRange(1, count);
    } else {
        const QString unified = QString(lower).replace(u';', u',');
        for (const QStringView rawPart : QStringView(unified).split(u',')) {
            const QStringView part = rawPart.trimmed();
            if (part.isEmpty())
                continue;
            // "a-b" with a hyphen, an en dash or an em dash; a sign is no number.
            qsizetype dash = -1;
            for (qsizetype i = 0; i < part.size(); ++i) {
                const char16_t c = part.at(i).unicode();
                if (c == u'-' || c == u'\u2013' || c == u'\u2014') {
                    dash = i;
                    break;
                }
            }
            const auto number = [](QStringView s, int *out) {
                s = s.trimmed();
                if (s.isEmpty())
                    return false;
                for (const QChar c : s) {
                    if (c < u'0' || c > u'9')
                        return false;
                }
                bool ok = false;
                const int v = s.toInt(&ok);
                *out = v;
                return ok;
            };
            int a = 0, b = 0;
            if (dash < 0) {
                if (number(part, &a) && a >= 1 && a <= count)
                    add(a);
            } else if (number(part.first(dash), &a) && number(part.sliced(dash + 1), &b)) {
                addRange(a, b);
            }
        }
    }
    return QList<int>(pages.cbegin(), pages.cend());
}

QString formatPageRanges(const QList<int> &pages)
{
    QList<int> sorted;
    for (const int p : pages) {
        if (p >= 1)
            sorted.append(p);
    }
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    QStringList parts;
    for (qsizetype i = 0; i < sorted.size();) {
        qsizetype j = i;
        while (j + 1 < sorted.size() && sorted.at(j + 1) == sorted.at(j) + 1)
            ++j;
        if (j - i >= 2) {
            parts.append(QString::number(sorted.at(i)) + u'-' + QString::number(sorted.at(j)));
        } else {
            for (qsizetype k = i; k <= j; ++k)
                parts.append(QString::number(sorted.at(k)));
        }
        i = j + 1;
    }
    return parts.join(u", "_s);
}

} // namespace brulion::drawing
