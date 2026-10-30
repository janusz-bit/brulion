#include "pdfpageimageprovider.h"

#include "pdfpages.h"

#include <QDir>
#include <QUrlQuery>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::drawingview {

PdfPageImageProvider::PdfPageImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) { }

QUrl PdfPageImageProvider::url(const QString &absolutePath, int page, const QString &version)
{
    return QUrl(u"image://pdfpage/"_s + QString::fromLatin1(QUrl::toPercentEncoding(absolutePath)) + u"?page="_s
                        + QString::number(page) + u"&v="_s + QString::fromLatin1(QUrl::toPercentEncoding(version)),
                QUrl::StrictMode);
}

QImage PdfPageImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    if (size)
        *size = QSize();
    const qsizetype q = id.indexOf(u'?');
    // Qt hands the id over as its URL shows it (most escapes decoded, "%2F" and "%3F" not).
    const QString path = QUrl::fromPercentEncoding((q < 0 ? id : id.left(q)).toUtf8());
    const int page = QUrlQuery(q < 0 ? QString() : id.mid(q + 1)).queryItemValue(u"page"_s).toInt();
    if (path.isEmpty() || !QDir::isAbsolutePath(path) || path.contains(QChar(0)) || page < 1
        || !path.endsWith(u".pdf"_s, Qt::CaseInsensitive))
        return {};
    const QString slashes = QDir::fromNativeSeparators(path);
    for (const QStringView segment : QStringView(slashes).split(u'/')) {
        if (segment == u"..")
            return {};
    }
    const int asked = std::max(requestedSize.width(), requestedSize.height());
    const QImage image = platform::renderPdfPage(path, page, std::clamp(asked > 0 ? asked : DefaultSide, 16, MaxSide));
    if (size)
        *size = image.size();
    return image;
}

} // namespace brulion::drawingview
