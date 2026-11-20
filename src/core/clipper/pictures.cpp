#include "clipper/pictures.h"

#include "util/paths.h"
#include "webpicture/picturefetcher.h"

#include <QMetaObject>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::clipper {

namespace {

QString errorCode(webpicture::FetchError error)
{
    switch (error) {
    case webpicture::FetchError::Network: return u"network"_s;
    case webpicture::FetchError::Redirect: return u"redirect"_s;
    case webpicture::FetchError::Timeout: return u"timeout"_s;
    case webpicture::FetchError::Http: return u"http"_s;
    case webpicture::FetchError::TooLarge: return u"too_large"_s;
    case webpicture::FetchError::NotPicture: return u"not_picture"_s;
    }
    return u"network"_s;
}

QString extensionOf(const QString &format)
{
    return format == u"jpg" ? u".jpg"_s : u'.' + format;
}

} // namespace

QByteArray decodeDataPicture(QStringView url, qint64 maxBytes)
{
    if (!url.startsWith(u"data:", Qt::CaseInsensitive))
        return {};
    const qsizetype comma = url.indexOf(u',');
    if (comma < 0)
        return {};
    const QStringView header = url.sliced(5, comma - 5);
    const QStringView payload = url.sliced(comma + 1);
    const bool base64 = header.endsWith(u";base64", Qt::CaseInsensitive);
    // A base64 payload of n characters holds at most 3n/4 bytes: refuse before decoding.
    if ((base64 ? payload.size() / 4 * 3 : payload.size()) > maxBytes + 3)
        return {};
    QByteArray bytes;
    if (base64) {
        QByteArray ascii = payload.toLatin1();
        ascii.removeIf([](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; });
        const auto decoded = QByteArray::fromBase64Encoding(ascii, QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
            return {};
        bytes = *decoded;
    } else {
        bytes = QByteArray::fromPercentEncoding(payload.toUtf8());
    }
    return bytes.size() > maxBytes ? QByteArray() : bytes;
}

QString pictureFileName(const ClipPicture &picture, const QString &format, const QString &stem, int number)
{
    if (!picture.source.startsWith(u"data:"_s, Qt::CaseInsensitive))
        return webpicture::attachmentName(QUrl(picture.source), format);
    const QString base = paths::sanitizeFileName(stem.left(80));
    return base + u' ' + QString::number(number) + extensionOf(format);
}

PictureBatch::PictureBatch(QNetworkAccessManager *manager, QObject *parent)
    : QObject(parent), m_fetcher(new webpicture::PictureFetcher(manager, this))
{
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] { stopAll(u"timeout"_s); });
    connect(m_fetcher, &webpicture::PictureFetcher::fetched, this,
            [this](const QUrl &url, const QByteArray &data, const QString &format) {
                for (qsizetype i = 0; i < m_urls.size(); ++i) {
                    if (m_done.at(i) || m_urls.at(i) != url)
                        continue;
                    if (m_saved >= m_limits.maxCount)
                        settle(int(i), PictureResult{{}, {}, u"too_many"_s});
                    else if (m_totalBytes + data.size() > m_limits.maxTotalBytes)
                        settle(int(i), PictureResult{{}, {}, u"total_size"_s});
                    else
                        settle(int(i), PictureResult{data, format, QString()});
                }
                finishIfDone();
            });
    connect(m_fetcher, &webpicture::PictureFetcher::failed, this,
            [this](const QUrl &url, webpicture::FetchError error, const QString &) {
                for (qsizetype i = 0; i < m_urls.size(); ++i) {
                    if (!m_done.at(i) && m_urls.at(i) == url)
                        settle(int(i), PictureResult{{}, {}, errorCode(error)});
                }
                finishIfDone();
            });
}

PictureBatch::~PictureBatch() = default;

void PictureBatch::setHostCheck(webpicture::HostCheck check)
{
    m_isPrivate = check;
    m_fetcher->setPrivateHostCheck(std::move(check));
}

int PictureBatch::downloaded() const
{
    return int(std::ranges::count_if(m_results, [](const PictureResult &r) { return r.error.isEmpty(); }));
}

void PictureBatch::settle(int index, PictureResult result)
{
    if (m_done.at(index))
        return;
    m_done[index] = true;
    if (result.error.isEmpty()) {
        ++m_saved;
        m_totalBytes += result.data.size();
    }
    m_results[index] = std::move(result);
    --m_pending;
    Q_EMIT progress(m_total - m_pending, m_total);
}

void PictureBatch::finishIfDone()
{
    if (m_finished || m_pending > 0 || !m_started)
        return;
    m_finished = true;
    m_deadline.stop();
    Q_EMIT finished();
}

void PictureBatch::stopAll(const QString &error)
{
    if (m_finished)
        return;
    m_fetcher->cancelAll();
    for (qsizetype i = 0; i < m_done.size(); ++i) {
        if (!m_done.at(i))
            settle(int(i), PictureResult{{}, {}, error});
    }
    finishIfDone();
}

void PictureBatch::cancel()
{
    stopAll(u"cancelled"_s);
}

void PictureBatch::start(const QList<ClipPicture> &pictures, const PictureLimits &limits)
{
    if (m_started)
        return;
    m_started = true;
    m_limits = limits;
    m_fetcher->setMaxBytes(limits.maxBytes);
    m_fetcher->setTimeout(limits.stallMs);
    m_results = QList<PictureResult>(pictures.size());
    m_urls = QList<QUrl>(pictures.size());
    m_done = QList<bool>(pictures.size(), false);
    m_total = int(pictures.size());
    m_pending = m_total;
    QList<QUrl> fetch;
    int planned = 0; // pictures that may still be saved (the count limit is checked on arrival too)
    for (qsizetype i = 0; i < pictures.size(); ++i) {
        const ClipPicture &picture = pictures.at(i);
        if (picture.source.startsWith(u"data:"_s, Qt::CaseInsensitive)) {
            const QByteArray data = decodeDataPicture(picture.source, limits.maxBytes);
            const QString format = webpicture::sniffFormat(data);
            if (data.isEmpty())
                settle(int(i), PictureResult{{}, {}, u"bad_data"_s});
            else if (format.isEmpty())
                settle(int(i), PictureResult{{}, {}, u"not_picture"_s});
            else if (planned >= limits.maxCount)
                settle(int(i), PictureResult{{}, {}, u"too_many"_s});
            else if (m_totalBytes + data.size() > limits.maxTotalBytes)
                settle(int(i), PictureResult{{}, {}, u"total_size"_s});
            else {
                ++planned;
                settle(int(i), PictureResult{data, format, QString()});
            }
            continue;
        }
        const QUrl url = webpicture::webUrl(picture.source);
        if (!url.isValid()) {
            settle(int(i), PictureResult{{}, {}, u"network"_s});
            continue;
        }
        if (!limits.privateHosts && m_isPrivate(url.host())) {
            settle(int(i), PictureResult{{}, {}, u"private_host"_s});
            continue;
        }
        if (planned >= limits.maxCount) {
            settle(int(i), PictureResult{{}, {}, u"too_many"_s});
            continue;
        }
        ++planned;
        m_urls[i] = url;
        if (!fetch.contains(url))
            fetch.append(url);
    }
    if (fetch.isEmpty()) {
        // finished() from the event loop, as after a download.
        QMetaObject::invokeMethod(this, [this] { finishIfDone(); }, Qt::QueuedConnection);
        return;
    }
    m_deadline.start(limits.timeoutMs);
    for (const QUrl &url : std::as_const(fetch))
        m_fetcher->fetch(url);
}

} // namespace brulion::clipper
