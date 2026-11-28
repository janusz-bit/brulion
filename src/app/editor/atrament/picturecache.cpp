#include "picturecache.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Qt::StringLiterals;

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

// The bytes of a data: URL ("data:image/png;base64,…"); empty when it is none.
QByteArray dataUrlBytes(const QUrl &url)
{
    const QByteArray encoded = url.toEncoded();
    const qsizetype comma = encoded.indexOf(',');
    if (!encoded.startsWith("data:") || comma < 0)
        return {};
    const QByteArray header = encoded.mid(5, comma - 5);
    const QByteArray payload = QByteArray::fromPercentEncoding(encoded.mid(comma + 1));
    return header.endsWith(";base64") ? QByteArray::fromBase64(payload) : payload;
}

// A data: URL's format by its media type ("image/svg+xml" → "svg"), a file's by its suffix.
QByteArray formatHint(const QUrl &url)
{
    if (url.isLocalFile())
        return QFileInfo(url.toLocalFile()).suffix().toLower().toLatin1();
    const QString s = url.toString(QUrl::RemoveQuery).left(64);
    const qsizetype slash = s.indexOf(u'/');
    if (!s.startsWith(u"data:image/"_s) || slash < 0)
        return {};
    QString type = s.mid(slash + 1);
    type = type.left(type.indexOf(QRegularExpression(u"[;,+]"_s)));
    return type.toLower().toLatin1();
}

struct Decoded {
    std::shared_ptr<at::ImageData> image;
    std::shared_ptr<PictureCache::Animation> animation;
    QSize natural;
    bool full = false;
};

// The image providers of image:// URLs (setImageProvider), for every cache of the process.
struct Providers {
    QMutex mutex;
    QHash<QString, std::shared_ptr<const PictureCache::ImageProvider>> byId;
};

Providers &providers()
{
    static Providers p;
    return p;
}

std::shared_ptr<const PictureCache::ImageProvider> providerOf(const QString &id)
{
    Providers &p = providers();
    QMutexLocker lock(&p.mutex);
    return p.byId.value(id);
}


// An image provider's picture (image://<id>/…), fitted into side × side.
Decoded provided(const QUrl &url, int side)
{
    Decoded out;
    const std::shared_ptr<const PictureCache::ImageProvider> provider = providerOf(url.host());
    if (!provider || !*provider)
        return out;
    // (The id as Qt Quick gives it to a provider: the path after the provider's name, the query.)
    const QString id = url.toString(QUrl::RemoveScheme | QUrl::RemoveAuthority).mid(1);
    QSize natural;
    QImage picture = (*provider)(id, &natural, QSize(side, side));
    if (picture.isNull())
        return out;
    if (qint64(picture.width()) * qint64(picture.height()) > PictureCache::MaxPixels)
        return {};
    if (picture.width() > side || picture.height() > side)
        picture = picture.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    out.natural = natural.isValid() && !natural.isEmpty() ? natural : picture.size();
    out.image = PictureCache::imageData(std::move(picture));
    return out;
}

// Every frame of an animated picture at `target` (none when it has one frame: a still picture).
void decodeFrames(QImageReader &reader, QSize target, Decoded &out)
{
    const int count = reader.imageCount(); // (0: not known before reading)
    if (target.isValid() && !target.isEmpty() && count > 1) {
        // More pixels than an animation may hold: every frame smaller, fitted into that.
        const qint64 frame = qint64(target.width()) * target.height() * 4;
        const qint64 total = frame * std::min(count, PictureCache::MaxFrames);
        if (total > PictureCache::MaxAnimationBytes) {
            const double f = std::sqrt(double(PictureCache::MaxAnimationBytes) / double(total));
            target = QSize(std::max(1, int(target.width() * f)), std::max(1, int(target.height() * f)));
            reader.setScaledSize(target);
            out.full = false;
        }
    }
    auto animation = std::make_shared<PictureCache::Animation>();
    qint64 bytes = 0;
    while (int(animation->frames.size()) < PictureCache::MaxFrames) {
        QImage picture = reader.read();
        if (picture.isNull())
            break;
        bytes += qint64(picture.width()) * picture.height() * 4;
        if (bytes > PictureCache::MaxAnimationBytes && !animation->frames.empty())
            break;
        int delay = reader.nextImageDelay();
        if (delay < 20)
            delay = 100;
        animation->frames.push_back(PictureCache::imageData(std::move(picture)));
        animation->delays.push_back(delay);
        animation->duration += delay;
    }
    if (animation->frames.empty())
        return;
    out.image = std::const_pointer_cast<at::ImageData>(animation->frames.front());
    if (animation->frames.size() > 1)
        out.animation = std::move(animation);
}

// Reads and decodes on a worker thread (QImageReader is reentrant).
Decoded decode(const QUrl &url, int side, bool scalable)
{
    Decoded out;
    if (url.scheme() == u"image"_s)
        return provided(url, side);
    QByteArray bytes;
    std::unique_ptr<QIODevice> device;
    if (url.isLocalFile()) {
        auto file = std::make_unique<QFile>(url.toLocalFile());
        if (!file->open(QIODevice::ReadOnly))
            return out;
        device = std::move(file);
    } else {
        bytes = dataUrlBytes(url);
        if (bytes.isEmpty())
            return out;
        auto buffer = std::make_unique<QBuffer>(&bytes);
        buffer->open(QIODevice::ReadOnly);
        device = std::move(buffer);
    }
    // The suffix's format first, the bytes' if it is not that (a .jpg that is a PNG).
    QImageReader reader(device.get(), formatHint(url));
    // Shown upright by its EXIF orientation, as QML's Image (autoTransform) and the header's size
    // (editor::pictureSize): a quarter turn transposes the size; Qt scales the file's pixels, then
    // turns them.
    reader.setAutoTransform(true);
    const bool turned = reader.transformation() & QImageIOHandler::TransformationRotate90;
    const QSize size = turned ? reader.size().transposed() : reader.size();
    if (size.isValid() && qint64(size.width()) * qint64(size.height()) > PictureCache::MaxPixels)
        return out;
    out.natural = size;
    QSize target = size;
    if (size.isValid() && !size.isEmpty()) {
        const double s = std::min(double(side) / size.width(), double(side) / size.height());
        if (scalable || s < 1)
            target = QSize(std::max(1, int(std::lround(size.width() * s))), std::max(1, int(std::lround(size.height() * s))));
        out.full = !scalable && s >= 1;
        if (target != size)
            reader.setScaledSize(turned ? target.transposed() : target);
    }
    if (reader.supportsAnimation() && reader.imageCount() != 1 && size.isValid() && !size.isEmpty()) {
        decodeFrames(reader, target, out);
        return out;
    }
    QImage picture = reader.read();
    if (picture.isNull())
        return out;
    if (!size.isValid() || size.isEmpty()) {
        // No size in the header: decoded whole (within the reader's allocation limit), fitted after.
        out.natural = picture.size();
        if (qint64(picture.width()) * qint64(picture.height()) > PictureCache::MaxPixels)
            return {};
        const double s = std::min(double(side) / picture.width(), double(side) / picture.height());
        out.full = !scalable && s >= 1;
        if (s < 1 || scalable)
            picture = picture.scaled(std::max(1, int(std::lround(picture.width() * s))),
                                     std::max(1, int(std::lround(picture.height() * s))), Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
    }
    out.image = PictureCache::imageData(std::move(picture));
    return out;
}

} // namespace

PictureCache::PictureCache(QObject *parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(2);
    m_pool.setObjectName(u"PictureCache"_s);
}

PictureCache::~PictureCache()
{
    // No decode outlives the cache: what they post back goes with it.
    m_pool.clear();
    m_pool.waitForDone();
}

std::shared_ptr<PictureCache> PictureCache::shared()
{
    // Made on first use (on the GUI thread) and kept for the process, as Atrament's App.
    static auto *cache = new std::shared_ptr<PictureCache>(std::make_shared<PictureCache>());
    return *cache;
}

std::shared_ptr<at::ImageData> PictureCache::imageData(QImage picture)
{
    picture = picture.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const int w = picture.width();
    const int h = picture.height();
    std::vector<at::Pixel> pixels(std::size_t(w) * std::size_t(h));
    for (int y = 0; y < h; ++y)
        std::memcpy(pixels.data() + std::size_t(y) * std::size_t(w), picture.constScanLine(y), std::size_t(w) * 4);
    return at::ImageData::make(w, h, std::move(pixels));
}

bool PictureCache::readable(const QUrl &url)
{
    if (url.scheme() == u"image"_s)
        return bool(providerOf(url.host()));
    return url.isLocalFile() || url.scheme() == u"data"_s;
}

void PictureCache::setImageProvider(const QString &providerId, ImageProvider provider)
{
    Providers &p = providers();
    QMutexLocker lock(&p.mutex);
    if (provider)
        p.byId.insert(providerId, std::make_shared<const ImageProvider>(std::move(provider)));
    else
        p.byId.remove(providerId);
}

bool PictureCache::decodableName(const QString &fileName)
{
    static const QSet<QByteArray> formats = [] {
        QSet<QByteArray> set;
        for (const QByteArray &f : QImageReader::supportedImageFormats())
            set.insert(f.toLower());
        return set;
    }();
    const QByteArray suffix = QFileInfo(fileName).suffix().toLower().toLatin1();
    if (suffix.isEmpty())
        return true;
    if (suffix == "jpeg" || suffix == "jpg")
        return formats.contains("jpeg") || formats.contains("jpg");
    if (suffix == "svgz")
        return formats.contains("svgz") || formats.contains("svg");
    static const QSet<QByteArray> pictures{"png", "gif", "bmp", "webp", "avif", "svg", "tif", "tiff", "ico", "heic",
                                           "heif", "jxl", "tga", "ppm", "pgm", "pbm", "xpm", "xbm"};
    return !pictures.contains(suffix) || formats.contains(suffix);
}

int PictureCache::sideFor(float width, float height, float scale)
{
    // PictureDecode::sideFor (src/app/ui/picturedecode.cpp): 64 px steps up to 512, 128 above.
    const float ratio = scale > 0 && std::isfinite(scale) ? scale : 1;
    const float longer = std::fmax(width, height) * ratio;
    if (!(longer > float(MinSide)))
        return MinSide;
    if (longer >= float(MaxSide))
        return MaxSide;
    const int pixels = int(std::ceil(longer));
    const int step = pixels <= 512 ? 64 : 128;
    return std::min(MaxSide, (pixels + step - 1) / step * step);
}

PictureCache::Result PictureCache::picture(const QString &key, const QUrl &url, int side, QSize header, bool scalable)
{
    side = std::clamp(side, MinSide, MaxSide);
    auto it = m_entries.find(key);
    if (it == m_entries.end())
        it = m_entries.insert(key, Entry{});
    Entry &e = *it;
    e.used = ++m_clock;
    const bool headerKnown = header.isValid() && !header.isEmpty();
    if (e.url != url || (headerKnown && e.header.isValid() && !e.header.isEmpty() && header != e.header)
        || e.scalable != scalable) {
        // Another file for the key (a replaced picture, a new version): from the start.
        m_bytes -= e.bytes;
        const quint64 used = e.used;
        e = Entry{};
        e.used = used;
        e.url = url;
        e.scalable = scalable;
    }
    if (headerKnown)
        e.header = header;
    Result r{e.image, e.animation, e.natural, e.failed, e.loadingSide != 0};
    if (e.failed || !readable(url))
        return {e.image, e.animation, e.natural, true, false};
    // Good enough: decoded for this side, at the file's own size (a larger side gains nothing),
    // or for a side up to twice as large (sharp; another view showing it larger).
    const bool fits = e.image
                      && (e.side == side || (e.full && side >= e.side) || (e.side > side && e.side <= 2 * side));
    if (!fits && e.loadingSide != side) {
        start(key, e, side, scalable);
        r.loading = true;
    }
    return r;
}

PictureCache::Result PictureCache::peek(const QString &key) const
{
    const auto it = m_entries.constFind(key);
    if (it == m_entries.cend())
        return {};
    return {it->image, it->animation, it->natural, it->failed, it->loadingSide != 0};
}

void PictureCache::start(const QString &key, Entry &e, int side, bool scalable)
{
    e.loadingSide = side;
    ++m_pending;
    const QUrl url = e.url;
    m_pool.start([this, key, url, side, scalable] {
        Decoded d = decode(url, side, scalable);
        // (The cache waits for its decodes in its destructor: `this` is there.)
        QMetaObject::invokeMethod(
                this,
                [this, key, url, side, image = std::move(d.image), animation = std::move(d.animation),
                 natural = d.natural, full = d.full]() mutable {
                    finished(key, url, side, std::move(image), std::move(animation), natural, full);
                },
                Qt::QueuedConnection);
    });
}

void PictureCache::finished(const QString &key, const QUrl &url, int side, std::shared_ptr<const at::ImageData> image,
                            std::shared_ptr<const Animation> animation, QSize natural, bool full)
{
    --m_pending;
    auto it = m_entries.find(key);
    if (it == m_entries.end() || it->url != url)
        return; // cleared, or the key's file changed meanwhile
    Entry &e = *it;
    if (e.loadingSide == side)
        e.loadingSide = 0;
    else if (e.image)
        return; // a decode for another side was asked for since; the one shown stays
    if (natural.isValid())
        e.natural = natural;
    if (image) {
        m_bytes -= e.bytes;
        e.bytes = 0;
        if (animation)
            for (const auto &frame : animation->frames)
                e.bytes += qint64(frame->width) * qint64(frame->height) * 4;
        else
            e.bytes = qint64(image->width) * qint64(image->height) * 4;
        m_bytes += e.bytes;
        e.image = std::move(image);
        e.animation = std::move(animation);
        e.side = side;
        e.full = full;
        e.failed = false;
    } else if (!e.image) {
        e.failed = true;
    }
    trim(key);
    // (A copy: a listener may unsubscribe.)
    const std::map<int, std::function<void()>> listeners = m_listeners;
    for (const auto &[id, f] : listeners)
        if (f)
            f();
    Q_EMIT decoded(key);
}

void PictureCache::trim(const QString &keep)
{
    while (m_bytes > m_budget) {
        // The least recently asked for picture nobody holds (a view's frames keep theirs).
        auto oldest = m_entries.end();
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            // (An animation's first frame is its `image` too: held when either is held elsewhere.)
            const long own = it->animation ? 2 : 1;
            if (!it->image || it.key() == keep || it->image.use_count() > own
                || (it->animation && it->animation.use_count() > 1))
                continue;
            if (oldest == m_entries.end() || it->used < oldest->used)
                oldest = it;
        }
        if (oldest == m_entries.end())
            break;
        m_bytes -= oldest->bytes;
        // Its size stays known (a frame of the right size when it comes back), the pixels go.
        oldest->image.reset();
        oldest->animation.reset();
        oldest->bytes = 0;
        oldest->side = 0;
        oldest->full = false;
    }
    // Entries without pixels are a few bytes each; a long session's are trimmed now and then.
    if (m_entries.size() > 4096) {
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (!it->image && !it->loadingSide && it.key() != keep)
                it = m_entries.erase(it);
            else
                ++it;
        }
    }
}

void PictureCache::setBudget(qint64 bytes)
{
    m_budget = std::max<qint64>(0, bytes);
    trim(QString());
}

int PictureCache::count() const
{
    return int(std::count_if(m_entries.cbegin(), m_entries.cend(), [](const Entry &e) { return bool(e.image); }));
}

bool PictureCache::contains(const QString &key) const
{
    const auto it = m_entries.constFind(key);
    return it != m_entries.cend() && it->image;
}

void PictureCache::clear()
{
    // (Decodes still running come back to nothing.)
    m_entries.clear();
    m_bytes = 0;
}

int PictureCache::subscribe(std::function<void()> changed)
{
    const int id = m_nextId++;
    m_listeners.emplace(id, std::move(changed));
    return id;
}

void PictureCache::unsubscribe(int id)
{
    m_listeners.erase(id);
}

} // namespace brulion::editor::atr
