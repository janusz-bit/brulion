// Workspace — pictures from the internet in notes (EditorHost::webPicture, docs/SYNTAX.md §2).
//
// The QML engine loads nothing from the network (src/app/ui/localnetwork.h): the Workspace
// fetches a picture itself (webpicture::PictureFetcher — http(s), size and time limits, no
// cookies, the bytes must be a picture) when the user allowed it — "Pokaż" on that picture, the
// device's setting webPicturesShown, or "Pobierz do notatnika" — into the device's cache
// (webpicture::PictureCache), and the editor shows the cached file. A failure is remembered for
// the session (no retries on every repaint) until the user asks again.
//
// Without asking, a picture is fetched only from a host of the internet (a NAS or router at
// 192.168.x.x or localhost waits for "Pokaż": a synced note must not make the device send
// requests into its network), and only once a session (one the cache dropped since waits for
// "Pokaż" too, not fetched again on every repaint); turning the setting off cancels the fetches
// it started. In the browser the cache is in the tab's memory (not IndexedDB): smaller, and gone
// with the tab; the requests are ones a page may send to another site, the browser follows
// redirects itself (no redirected() to check them), and a failed connection is told as what a
// browser forbids (CORS, http:// on a https:// page).
#include "workspace.h"

#include "picturesize.h"
#include "webpicture/picturecache.h"
#include "webpicture/picturefetcher.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QSet>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {
/// The browser's cache size: the files and their data: URLs live in the tab's memory.
constexpr qint64 BrowserCacheBytes = 50 * 1024 * 1024;
} // namespace

class Workspace::WebPictures : public QObject {
public:
    explicit WebPictures(QObject *parent) : QObject(parent) { }
    webpicture::PictureCache cache;
    webpicture::PictureFetcher *fetcher = nullptr;
    /// The URLs as written (the editor's keys) waiting for each fetched URL.
    QHash<QUrl, QSet<QString>> waiting;
    /// URL as written → the sentence of its failure (this session).
    QHash<QString, QString> errors;
    /// Fetches started only because pictures are shown without asking (cancelled when that is
    /// turned off; an explicit loadWebPicture() takes the URL out), and the URLs fetched so this
    /// session (not fetched again without asking once the cache dropped them).
    QSet<QUrl> autoQueued;
    QSet<QUrl> autoFetched;
    /// The browser: cached pictures as data: URLs, with their sizes.
    QHash<QString, QUrl> dataUrls;
    QHash<QString, QSize> dataSizes;
};

Workspace::WebPictures *Workspace::webPictures() const
{
    if (m_webPictures)
        return m_webPictures;
    auto *self = const_cast<Workspace *>(this);
    m_webPictures = new WebPictures(self);
    QString dir = m_webPictureCacheDir;
    if (dir.isEmpty()) {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        dir = base.isEmpty() ? QString() : QDir(base).filePath(u"web-pictures"_s);
    }
    m_webPictures->cache.setDir(dir);
    m_webPictures->fetcher = new webpicture::PictureFetcher(nullptr, m_webPictures);
    if (m_webPictureHostCheck)
        m_webPictures->fetcher->setPrivateHostCheck(m_webPictureHostCheck);
    m_webPictures->fetcher->setSimpleRequests(!m_webPageScheme.isEmpty());
    WebPictures *web = m_webPictures;
    connect(web->fetcher, &webpicture::PictureFetcher::fetched, self,
            [self, web](const QUrl &url, const QByteArray &data, const QString &format) {
                const QString path = web->cache.store(url, data, format);
                if (path.isEmpty()) {
                    for (const QString &written : web->waiting.value(url))
                        web->errors.insert(written, tr("The picture could not be saved on this device."));
                } else {
                    if (web->autoQueued.contains(url))
                        web->autoFetched.insert(url);
                    const qint64 cap = self->m_webPictureCacheBytes > 0 ? self->m_webPictureCacheBytes
                                       : self->m_dataUrlAssets          ? BrowserCacheBytes
                                                                        : webpicture::DefaultCacheBytes;
                    web->cache.prune(cap);
                    // The data: URLs of the files dropped go with them.
                    for (auto it = web->dataUrls.begin(); it != web->dataUrls.end();) {
                        if (!web->cache.contains(webpicture::webUrl(it.key()))) {
                            web->dataSizes.remove(it.key());
                            it = web->dataUrls.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
                web->autoQueued.remove(url);
                const QSet<QString> written = web->waiting.take(url);
                for (const QString &w : written)
                    Q_EMIT self->webPictureChanged(w);
            });
    connect(web->fetcher, &webpicture::PictureFetcher::failed, self,
            [self, web](const QUrl &url, webpicture::FetchError error, const QString &detail) {
                QString message;
                const QString host = url.host();
                switch (error) {
                case webpicture::FetchError::Network:
                    // A browser tells a page no reason: what it forbids is the likely one.
                    if (self->m_webPageScheme.isEmpty())
                        message = tr("No connection to %1.").arg(host);
                    else if (self->m_webPageScheme == u"https" && url.scheme() == u"http")
                        message = tr("The browser loads no pictures over http:// into a page opened over https://. "
                                     "Save the picture in the notebook in the app on a computer or phone.");
                    else
                        message = tr("%1 does not let other websites load its pictures (CORS), or there is no "
                                     "connection. Save the picture in the notebook in the app on a computer or "
                                     "phone.")
                                          .arg(host);
                    break;
                case webpicture::FetchError::Redirect:
                    message = tr("%1 redirected to an address Brulion does not open.").arg(host);
                    break;
                case webpicture::FetchError::Timeout: message = tr("%1 did not answer in time.").arg(host); break;
                case webpicture::FetchError::Http: message = tr("%1 answered with error %2.").arg(host, detail); break;
                case webpicture::FetchError::TooLarge:
                    message = tr("The picture is larger than %1 MB.").arg(webpicture::MaxBytes / (1024 * 1024));
                    break;
                case webpicture::FetchError::NotPicture:
                    message = tr("%1 sent no picture from this address.").arg(host);
                    break;
                }
                qInfo().noquote() << u"Picture from %1 not loaded: %2 (%3)"_s.arg(host, webpicture::errorName(error),
                                                                                  detail);
                web->autoQueued.remove(url);
                const QSet<QString> written = web->waiting.take(url);
                for (const QString &w : written) {
                    web->errors.insert(w, message);
                    Q_EMIT self->webPictureChanged(w);
                }
            });
    return m_webPictures;
}

void Workspace::setWebPictureCacheDir(const QString &dir)
{
    m_webPictureCacheDir = dir;
    if (m_webPictures) {
        m_webPictures->cache.setDir(dir);
        m_webPictures->dataUrls.clear();
        m_webPictures->dataSizes.clear();
    }
}

void Workspace::setWebPageScheme(const QString &scheme)
{
    m_webPageScheme = scheme.toLower();
    if (m_webPictures)
        m_webPictures->fetcher->setSimpleRequests(!m_webPageScheme.isEmpty());
}

bool Workspace::webPicturesShown() const
{
    return m_webPicturesShown;
}

void Workspace::setWebPictureHostCheck(std::function<bool(const QString &)> isPrivate)
{
    m_webPictureHostCheck = std::move(isPrivate);
    if (m_webPictures)
        m_webPictures->fetcher->setPrivateHostCheck(m_webPictureHostCheck ? m_webPictureHostCheck
                                                                          : webpicture::isPrivateHost);
}

void Workspace::setWebPicturesShown(bool shown)
{
    if (m_webPicturesShown == shown)
        return;
    m_webPicturesShown = shown;
    if (!shown && m_webPictures) {
        // Consent withdrawn: what was fetched only for it stops; "Pokaż" and "Zapisz w notatniku"
        // go on. The rows ask again.
        WebPictures *web = m_webPictures;
        // All at once: aborting one must not start another of them from the queue.
        web->fetcher->cancel(web->autoQueued.values());
        for (const QUrl &url : std::as_const(web->autoQueued))
            web->waiting.remove(url);
        web->autoQueued.clear();
    }
    Q_EMIT webPicturesShownChanged();
    Q_EMIT webPictureChanged(QString());
}

QVariantMap Workspace::webPicture(const QString &url) const
{
    const QUrl web = webpicture::webUrl(url);
    if (!web.isValid())
        return {{u"state"_s, u"failed"_s}, {u"error"_s, tr("This is no address of a picture.")}};
    WebPictures *pictures = webPictures();
    const QString file = pictures->cache.lookup(web);
    if (!file.isEmpty()) {
        // The format from the cache's file name (sniffed when it was stored): a data: URL has
        // none, and the web URL's ending need not name it.
        QVariantMap out{{u"state"_s, u"ready"_s}, {u"format"_s, QFileInfo(file).suffix()}};
        if (!m_dataUrlAssets) {
            out.insert(u"url"_s, QUrl::fromLocalFile(file));
            return out;
        }
        // The browser shows attachments as data: URLs (assetUrl()); the cached pictures too.
        auto cached = pictures->dataUrls.constFind(url);
        if (cached == pictures->dataUrls.constEnd()) {
            const QByteArray bytes = pictures->cache.read(web);
            const QString format = QFileInfo(file).suffix();
            const QString mime = format == u"svg"   ? u"image/svg+xml"_s
                                 : format == u"jpg" ? u"image/jpeg"_s
                                 : format == u"ico" ? u"image/x-icon"_s
                                                    : u"image/"_s + format;
            QBuffer buffer;
            buffer.setData(bytes);
            buffer.open(QIODevice::ReadOnly);
            pictures->dataSizes.insert(url, editor::pictureSize(&buffer, file));
            cached = pictures->dataUrls.insert(
                    url, QUrl(u"data:"_s + mime + u";base64,"_s + QString::fromLatin1(bytes.toBase64())));
        }
        out.insert(u"url"_s, *cached);
        const QSize size = pictures->dataSizes.value(url);
        if (size.isValid()) {
            out.insert(u"width"_s, size.width());
            out.insert(u"height"_s, size.height());
        }
        return out;
    }
    if (m_dataUrlAssets) { // the file is gone: its data: URL too (a new fetch may differ)
        pictures->dataUrls.remove(url);
        pictures->dataSizes.remove(url);
    }
    if (pictures->fetcher->isFetching(web)) {
        // Maybe for another spelling of the same URL ("HTTPS://Example.com/…"): this one is told
        // when it ends too.
        pictures->waiting[web].insert(url);
        return {{u"state"_s, u"loading"_s}};
    }
    if (const auto error = pictures->errors.constFind(url); error != pictures->errors.constEnd())
        return {{u"state"_s, u"failed"_s}, {u"error"_s, *error}};
    const bool isPrivate =
            m_webPictureHostCheck ? m_webPictureHostCheck(web.host()) : webpicture::isPrivateHost(web.host());
    if (m_webPicturesShown && !isPrivate && !pictures->autoFetched.contains(web)) {
        // Shown without asking: fetched now (the answer comes from the event loop, so asking
        // while the editor builds its rows changes nothing under it).
        pictures->waiting[web].insert(url);
        pictures->autoQueued.insert(web);
        pictures->fetcher->fetch(web);
        return {{u"state"_s, u"loading"_s}};
    }
    return {{u"state"_s, u"ask"_s}};
}

void Workspace::loadWebPicture(const QString &url, bool forDownload)
{
    Q_UNUSED(forDownload); // the user asked for this picture either way
    const QUrl web = webpicture::webUrl(url);
    if (!web.isValid())
        return;
    WebPictures *pictures = webPictures();
    pictures->errors.remove(url); // asked again: a new attempt
    pictures->autoQueued.remove(web); // asked for: goes on when the setting is turned off
    if (!pictures->cache.lookup(web).isEmpty()) {
        QMetaObject::invokeMethod(this, [this, url] { Q_EMIT webPictureChanged(url); }, Qt::QueuedConnection);
        return;
    }
    pictures->waiting[web].insert(url);
    if (pictures->fetcher->isFetching(web))
        return;
    pictures->fetcher->fetch(web);
    Q_EMIT webPictureChanged(url); // loading
}

QByteArray Workspace::webPictureData(const QString &url, QString *format) const
{
    const QUrl web = webpicture::webUrl(url);
    if (!web.isValid())
        return {};
    WebPictures *pictures = webPictures();
    const QString file = pictures->cache.lookup(web);
    if (file.isEmpty())
        return {};
    const QByteArray data = pictures->cache.read(web);
    if (format)
        *format = webpicture::sniffFormat(data);
    return data;
}

} // namespace brulion::shell
