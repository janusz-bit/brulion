// A clip's pictures downloaded for the notebook (docs/CLIPPER.md "Pictures"): webpicture's fetcher
// (http(s) only, no cookies, redirects checked, the bytes must be a picture by their signature)
// within limits of their own — how many, how large each and all together, how long — and pictures
// of this device or its private network only when the clipped page is there itself (a page on the
// internet cannot make the computer send requests into its home network). data: pictures are
// decoded, never fetched. What is not downloaded stays a picture from the internet in the note.
// Part of the clipper module — see clipper/clipper.h.
#pragma once

#include "clipper/article.h"
#include "webpicture/webpicture.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;

namespace brulion::webpicture {
class PictureFetcher;
}

namespace brulion::clipper {

struct PictureLimits {
    int maxCount = 60;                       ///< pictures saved at most; the rest stay links
    qint64 maxBytes = 15 * 1024 * 1024;      ///< one picture
    qint64 maxTotalBytes = 60 * 1024 * 1024; ///< all of them together
    int timeoutMs = 90 * 1000;               ///< the whole download
    int stallMs = webpicture::TimeoutMs;     ///< one request without progress
    bool privateHosts = false;               ///< also this device's and its network's pictures
};

/// What became of one picture.
struct PictureResult {
    QByteArray data; ///< the picture (empty unless `error` is "")
    QString format;  ///< webpicture::sniffFormat()
    /// "" (downloaded), or why not: "too_many", "total_size", "private_host", "too_large",
    /// "not_picture", "bad_data", "network", "http", "timeout", "redirect", "cancelled".
    QString error;
};

/// Download the pictures of a clip. start() once; progress() while it runs, finished() at the end
/// (also when nothing had to be fetched — from the event loop, never inside start()).
class PictureBatch : public QObject {
    Q_OBJECT
public:
    /// Uses `manager` when given (not owned), else an access manager of its own.
    explicit PictureBatch(QNetworkAccessManager *manager = nullptr, QObject *parent = nullptr);
    ~PictureBatch() override;

    /// Which hosts are private (tests treat 127.0.0.1 as the internet); isPrivateHost() by default.
    void setHostCheck(webpicture::HostCheck check);
    void start(const QList<ClipPicture> &pictures, const PictureLimits &limits);
    /// Stop: what is still running fails with "cancelled"; finished() follows.
    void cancel();

    bool isFinished() const { return m_finished; }
    /// One result per picture given to start(), in its order.
    const QList<PictureResult> &results() const { return m_results; }
    int downloaded() const;

Q_SIGNALS:
    void progress(int done, int total);
    void finished();

private:
    void settle(int index, PictureResult result);
    void finishIfDone();
    void stopAll(const QString &error);

    webpicture::PictureFetcher *m_fetcher = nullptr;
    webpicture::HostCheck m_isPrivate = webpicture::isPrivateHost;
    QList<PictureResult> m_results;
    QList<QUrl> m_urls; ///< per picture: the URL fetched (invalid when not fetched)
    QList<bool> m_done;
    PictureLimits m_limits;
    qint64 m_totalBytes = 0;
    int m_saved = 0;
    int m_pending = 0;
    int m_total = 0;
    bool m_started = false;
    bool m_finished = false;
    QTimer m_deadline;
};

/// The bytes of a data: picture URL (base64 or percent-encoded), or empty when it is malformed or
/// larger than `maxBytes`.
QByteArray decodeDataPicture(QStringView url, qint64 maxBytes);

/// The file name a saved picture gets: webpicture::attachmentName() for an address, "<stem> <n>.<ext>"
/// for a data: picture ("Artykuł 3.png").
QString pictureFileName(const ClipPicture &picture, const QString &format, const QString &stem, int number);

} // namespace brulion::clipper
