// Workspace — pictures from the internet in notes (EditorHost::webPicture, docs/SYNTAX.md §2).
//
// The QML engine loads nothing from the network (src/app/ui/localnetwork.h): the Workspace
// fetches a picture itself (webpicture::PictureFetcher — http(s), size and time limits, no
// cookies, the bytes must be a picture) when the user allowed it — "Pokaż" on that picture, the
// device's setting webPicturesShown, or "Pobierz do notatnika" — into the device's cache
// (webpicture::PictureCache), and the editor shows the cached file. A failure is remembered for
// the session (no retries on every repaint) until the user asks again.
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

class Workspace::WebPictures : public QObject {
public:
    explicit WebPictures(QObject *parent)
        : QObject(parent)
    {
    }
    webpicture::PictureCache cache;
    webpicture::PictureFetcher *fetcher = nullptr;
    /// The URLs as written (the editor's keys) waiting for each fetched URL.
    QHash<QUrl, QSet<QString>> waiting;
    /// URL as written → the sentence of its failure (this session).
    QHash<QString, QString> errors;
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
    WebPictures *web = m_webPictures;
    connect(web->fetcher, &webpicture::PictureFetcher::fetched, self,
            [self, web](const QUrl &url, const QByteArray &data, const QString &format) {
                const QString path = web->cache.store(url, data, format);
                if (path.isEmpty()) {
                    for (const QString &written : web->waiting.value(url))
                        web->errors.insert(written, tr("The picture could not be saved on this device."));
                } else {
                    web->cache.prune();
                }
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
                    message = tr("No connection to %1.").arg(host);
                    break;
                case webpicture::FetchError::Timeout:
                    message = tr("%1 did not answer in time.").arg(host);
                    break;
                case webpicture::FetchError::Http:
                    message = tr("%1 answered with error %2.").arg(host, detail);
                    break;
                case webpicture::FetchError::TooLarge:
                    message = tr("The picture is larger than %1 MB.").arg(webpicture::MaxBytes / (1024 * 1024));
                    break;
                case webpicture::FetchError::NotPicture:
                    message = tr("%1 sent no picture from this address.").arg(host);
                    break;
                }
                qInfo().noquote() << u"Picture from %1 not loaded: %2 (%3)"_s.arg(host, webpicture::errorName(error), detail);
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

bool Workspace::webPicturesShown() const
{
    return m_webPicturesShown;
}

void Workspace::setWebPicturesShown(bool shown)
{
    if (m_webPicturesShown == shown)
        return;
    m_webPicturesShown = shown;
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
        QVariantMap out{{u"state"_s, u"ready"_s}};
        if (!m_dataUrlAssets) {
            out.insert(u"url"_s, QUrl::fromLocalFile(file));
            return out;
        }
        // The browser shows attachments as data: URLs (assetUrl()); the cached pictures too.
        auto cached = pictures->dataUrls.constFind(url);
        if (cached == pictures->dataUrls.constEnd()) {
            const QByteArray bytes = pictures->cache.read(web);
            const QString format = QFileInfo(file).suffix();
            const QString mime = format == u"svg" ? u"image/svg+xml"_s
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
    if (pictures->fetcher->isFetching(web))
        return {{u"state"_s, u"loading"_s}};
    if (const auto error = pictures->errors.constFind(url); error != pictures->errors.constEnd())
        return {{u"state"_s, u"failed"_s}, {u"error"_s, *error}};
    if (m_webPicturesShown) {
        // Shown without asking: fetched now (the answer comes from the event loop, so asking
        // while the editor builds its rows changes nothing under it).
        pictures->waiting[web].insert(url);
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
