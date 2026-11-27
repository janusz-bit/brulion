// PictureCache — a note's pictures decoded for Atrament (docs/ATRAMENT.md M2 step 5, "Pictures"):
// what EmbedView's QML Image does for the editor drawn by Atrament. A picture is read from its
// URL — a local file (file:, the vault's attachments on the desktop and on Android, a picture
// from the internet in the device's cache) or a data: URL (the browser's attachments) — and
// decoded on a worker thread by Qt's QImageReader at the size its place shows it, never at the
// file's: the longer side of the frame in device pixels in PictureDecode's steps (64 px up to
// 512, 128 above, at most 2048; a raster picture never enlarged, an SVG rendered at that size),
// as premultiplied RGBA — Atrament's ImageData, which both of its renderers draw.
//
//   auto cache = PictureCache::shared();
//   const PictureCache::Result r = cache->picture(path, url, PictureCache::sideFor(w, h, scale), header);
//   if (r.image) …  // else a placeholder of the frame's size: `changed` callbacks say when it came
//
// Memory: a byte budget (setBudget(), 48 MB by default) over the decoded pictures, the least
// recently asked for going first — never one somebody still holds (a view keeps the pictures of
// its last frames: they are on screen), so a screenful larger than the budget is kept whole and
// trimmed once it scrolls away. One picture per URL: decoded again for another side (a wider
// window) it replaces the old one once the new one is there (the old one shown meanwhile).
// Notes come from anywhere: a file larger than MaxPixels in its header is not decoded, nor one
// QImageReader refuses (its allocation limit); a failure is remembered (no new try every frame)
// until the file's header size the model reports changes.
//
// An animated picture (a GIF, an animated WebP where Qt reads one) is decoded whole, every frame
// at the side asked for (the decoder composes each one), with how long each shows: `animation`
// in the Result, its first frame `image` — at most MaxAnimationBytes of pixels (a larger one is
// decoded smaller, its frames fitted into that), at most MaxFrames frames. The view decides which
// frame shows when (the page drawn by Atrament: mediarows.cpp).
//
// Pictures an image provider makes (`image://<id>/…`: a drawing's preview — the app's "drawing"
// provider, DrawingImageProvider) come from a provider registered for the process
// (setImageProvider()), called on the cache's worker threads as Qt Quick calls a provider in its
// reader thread: it must be thread-safe. The side asked for is the box it is fitted into.
#pragma once

#include "scene/scene.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QThreadPool>
#include <QUrl>

#include <functional>
#include <map>
#include <memory>

namespace brulion::editor::atr {

class PictureCache : public QObject {
    Q_OBJECT
public:
    // No picture whose header says more pixels than this is decoded (≈ 50 MP: 200 MB as RGBA).
    static constexpr qint64 MaxPixels = 50'000'000;
    // PictureDecode's sides (src/app/ui/picturedecode.h).
    static constexpr int MaxSide = 2048;
    static constexpr int MinSide = 64;
    // An animation's frames together, at most (32 MB: 64 frames of 512 × 256) and how many.
    static constexpr qint64 MaxAnimationBytes = 32LL * 1024 * 1024;
    static constexpr int MaxFrames = 1000;

    explicit PictureCache(QObject *parent = nullptr);
    ~PictureCache() override;

    // The process's cache (every page drawn by Atrament shares it).
    static std::shared_ptr<PictureCache> shared();

    // A URL the cache reads: a local file, a data: URL, image://<id>/… of a provider registered
    // (setImageProvider). (qrc:, http: are not its.)
    static bool readable(const QUrl &url);
    // The picture of `id` (an image:// URL's path and query, as Qt Quick gives a provider), `size`
    // set to its own size, fitted into `requested`: QQuickImageProvider::requestImage's contract.
    using ImageProvider = std::function<QImage(const QString &id, QSize *size, const QSize &requested)>;
    // Registers the provider of image://<providerId>/… for the process (null: none).
    static void setImageProvider(const QString &providerId, ImageProvider provider);
    // Whether QImageReader has a decoder for the picture format of `fileName` (by its suffix:
    // "svg" needs Qt Svg's plugin). Unknown suffixes: true (the reader looks at the bytes).
    static bool decodableName(const QString &fileName);
    // The side a place of `width` × `height` logical px at `scale` decodes at (PictureDecode::sideFor).
    static int sideFor(float width, float height, float scale);

    // An animated picture's frames in order and how long each shows (ms: at least 20 — a GIF's 0
    // or 10 shows for 100, as browsers do), one loop's length.
    struct Animation {
        std::vector<std::shared_ptr<const atrament::ImageData>> frames;
        std::vector<int> delays;
        int duration = 0;
    };
    struct Result {
        // The decoded picture (null: not yet, or it failed); maybe of another side than asked
        // for while that one is decoded. An animation's first frame.
        std::shared_ptr<const atrament::ImageData> image;
        // An animated picture's frames (null for a still one).
        std::shared_ptr<const Animation> animation;
        // The file's own pixel size (an SVG's default size), once read; invalid before.
        QSize natural;
        bool failed = false;  // the file could not be read or decoded
        bool loading = false; // a decode for this request is running
    };
    // The picture at `url`, known by `key` (the attachment's vault path, a web picture's address:
    // short, where a data: URL is the whole file), fitted into a `side` × `side` square
    // (`scalable`: an SVG, rendered at that size, also larger than its own; else never enlarged).
    // Asks for it when it is not there at that side (`changed` callbacks when it is). `header`:
    // the size the file's header gave the caller (BlockListModel's embeds) — a key's file replaced
    // by one of another size, or another URL for the key, is decoded again.
    Result picture(const QString &key, const QUrl &url, int side, QSize header = {}, bool scalable = false);
    // What the cache knows of `key` now (its picture, its size, a failure), asking for nothing.
    Result peek(const QString &key) const;

    void setBudget(qint64 bytes);
    qint64 budget() const { return m_budget; }
    // Bytes of decoded pixels the cache keeps, pictures kept, decodes running.
    qint64 bytes() const { return m_bytes; }
    int count() const;
    int pending() const { return m_pending; }
    // Whether the cache has `key` decoded (any side).
    bool contains(const QString &key) const;
    void clear();

    // Called (on this object's thread) whenever a decode finished or failed: a view draws again.
    // Returns an id for unsubscribe().
    int subscribe(std::function<void()> changed);
    void unsubscribe(int id);

Q_SIGNALS:
    void decoded(const QString &key);

private:
    struct Entry {
        QUrl url;
        std::shared_ptr<const atrament::ImageData> image;
        std::shared_ptr<const Animation> animation;
        int side = 0;         // decoded for this side (0: none yet)
        bool full = false;    // at the file's own size: fits any larger side too
        bool scalable = false;
        QSize natural;
        QSize header;
        bool failed = false;
        int loadingSide = 0;  // a decode running for this side (0: none)
        quint64 used = 0;     // last asked for (the cache's clock)
        qint64 bytes = 0;
    };
    void start(const QString &key, Entry &e, int side, bool scalable);
    void finished(const QString &key, const QUrl &url, int side, std::shared_ptr<const atrament::ImageData> image,
                  std::shared_ptr<const Animation> animation, QSize natural, bool full);
    void trim(const QString &keep);

    QHash<QString, Entry> m_entries;
    qint64 m_budget = 48LL * 1024 * 1024;
    qint64 m_bytes = 0;
    quint64 m_clock = 0;
    int m_pending = 0;
    int m_nextId = 1;
    std::map<int, std::function<void()>> m_listeners;
    // Its own threads: the destructor waits for them (no decode outlives the cache).
    QThreadPool m_pool;
};

} // namespace brulion::editor::atr
