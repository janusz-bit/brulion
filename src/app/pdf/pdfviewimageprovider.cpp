#include "pdfviewimageprovider.h"

#include "pdfrenderpool.h"

#include "pdfpages.h"

#include <QDir>
#include <QUrlQuery>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::pdfview {

namespace {

class Response : public QQuickImageResponse {
    Q_OBJECT

public:
    Response() = default;
    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }
    void cancel() override { m_cancelled->store(true); }
    std::shared_ptr<std::atomic_bool> cancelled() const { return m_cancelled; }

    void deliver(const QImage &image)
    {
        m_image = image;
        Q_EMIT finished();
    }

private:
    QImage m_image;
    std::shared_ptr<std::atomic_bool> m_cancelled = std::make_shared<std::atomic_bool>(false);
};

/// What a page that cannot be drawn shows (no QML warning, the view model says why).
QImage failedPicture()
{
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(QColor(0xe8, 0xea, 0xed));
    return image;
}

bool finite(qreal v)
{
    return std::isfinite(v);
}

} // namespace

PdfViewImageProvider::Request PdfViewImageProvider::parse(const QString &id)
{
    Request out;
    const qsizetype q = id.indexOf(u'?');
    // Qt hands the id over as its URL shows it (most escapes decoded, "%2F" and "%3F" not).
    out.file = QUrl::fromPercentEncoding((q < 0 ? id : id.left(q)).toUtf8());
    if (out.file.isEmpty() || !QDir::isAbsolutePath(out.file) || out.file.contains(QChar(0))
        || !out.file.endsWith(u".pdf"_s, Qt::CaseInsensitive))
        return out;
    const QString slashes = QDir::fromNativeSeparators(out.file);
    for (const QStringView segment : QStringView(slashes).split(u'/')) {
        if (segment == u"..")
            return out;
    }
    const QUrlQuery query(q < 0 ? QString() : id.mid(q + 1));
    out.page = query.queryItemValue(u"page"_s).toInt();
    out.version = query.queryItemValue(u"v"_s, QUrl::FullyDecoded);
    bool okW = false;
    bool okH = false;
    out.full = QSizeF(query.queryItemValue(u"w"_s).toDouble(&okW), query.queryItemValue(u"h"_s).toDouble(&okH));
    if (out.page < 1 || !okW || !okH || !finite(out.full.width()) || !finite(out.full.height())
        || !(out.full.width() > 0) || !(out.full.height() > 0))
        return out;
    const QString clip = query.queryItemValue(u"clip"_s);
    if (!clip.isEmpty()) {
        const QStringList parts = clip.split(u',');
        if (parts.size() != 4)
            return out;
        qreal v[4];
        for (int i = 0; i < 4; ++i) {
            bool ok = false;
            v[i] = parts.at(i).toDouble(&ok);
            if (!ok || !finite(v[i]))
                return out;
        }
        out.clip = QRectF(v[0], v[1], v[2], v[3]);
        if (!(out.clip.width() > 0) || !(out.clip.height() > 0))
            return out;
    }
    out.valid = true;
    return out;
}

bool PdfViewImageProvider::pixels(const Request &request, const QSize &requestedSize, QSize *full, QRect *clip)
{
    if (!request.valid)
        return false;
    const QSizeF logical = request.clip.isEmpty() ? request.full : request.clip.size();
    // The ratio Qt asks for (the screen's): the requested size over the logical one; none asked: 1.
    qreal ratio = 1;
    if (requestedSize.width() > 0)
        ratio = requestedSize.width() / logical.width();
    else if (requestedSize.height() > 0)
        ratio = requestedSize.height() / logical.height();
    if (!finite(ratio) || !(ratio > 0))
        return false;
    QSizeF pageSize = request.full * ratio;
    // A page drawn larger than MaxPdfFullSide is drawn at that size (its detail pictures too).
    const qreal longSide = std::max(pageSize.width(), pageSize.height());
    qreal shrink = 1;
    if (longSide > platform::MaxPdfFullSide) {
        shrink = platform::MaxPdfFullSide / longSide;
        pageSize *= shrink;
    }
    *full = QSize(std::max(1, int(std::lround(pageSize.width()))), std::max(1, int(std::lround(pageSize.height()))));
    if (request.clip.isEmpty()) {
        *clip = QRect();
    } else {
        const qreal k = ratio * shrink;
        const QRectF c(request.clip.x() * k, request.clip.y() * k, request.clip.width() * k, request.clip.height() * k);
        *clip = c.toAlignedRect().intersected(QRect(QPoint(0, 0), *full));
        if (clip->isEmpty())
            return false;
    }
    const QSize result = clip->isEmpty() ? *full : clip->size();
    return qint64(result.width()) * result.height() <= platform::MaxPdfClipPixels;
}

QQuickImageResponse *PdfViewImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    auto *response = new Response;
    const Request request = parse(id);
    QSize full;
    QRect clip;
    if (!platform::pdfAvailable() || !pixels(request, requestedSize, &full, &clip)) {
        // (Queued: Qt connects to finished() after this returns.)
        QMetaObject::invokeMethod(response, [response] { response->deliver(QImage()); }, Qt::QueuedConnection);
        return response;
    }
    const std::shared_ptr<std::atomic_bool> cancelled = response->cancelled();
    // (Alive until it has finished: Qt deletes a response only after finished(), cancelled or not.)
    Response *const target = response;
    PdfRenderPool::start(
            [request, full, clip, cancelled, target] {
                QImage image;
                bool failed = false;
                if (!cancelled->load()) {
                    // (A protected PDF the viewer opened: with its password, kept in memory only.)
                    PdfReaderPool &pool = PdfReaderPool::instance();
                    PdfReaderPool::Lease lease =
                            pool.acquire(request.file, request.version, nullptr, pool.password(request.file));
                    if (lease && request.page <= lease->pageCount() && !cancelled->load()) {
                        image = lease->render(request.page, full, clip);
                        failed = image.isNull();
                    } else if (!cancelled->load()) {
                        failed = true;
                    }
                }
                if (failed) {
                    PdfReaderPool::instance().reportFailure(request.file, request.version, request.page);
                    image = failedPicture();
                }
                // finished() must come also for a cancelled response (Qt waits for it).
                QMetaObject::invokeMethod(target, [target, image] { target->deliver(image); }, Qt::QueuedConnection);
            },
            clip.isEmpty() ? PdfRenderPool::Page : PdfRenderPool::Detail);
    return response;
}

} // namespace brulion::pdfview

#include "pdfviewimageprovider.moc"
