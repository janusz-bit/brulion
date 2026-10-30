// PDF pages through Qt PDF (pdfpages.h; QPdfDocument, pdfium): Windows, the AppImage, CI's Linux
// build. Qt PDF serialises pdfium's calls itself; each call here has its own document.
#include "pdfpages.h"

#include <QPainter>
#include <QPdfDocument>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

QString loadError(QPdfDocument::Error error)
{
    switch (error) {
    case QPdfDocument::Error::None: return {};
    case QPdfDocument::Error::FileNotFound: return u"File not found"_s;
    case QPdfDocument::Error::InvalidFileFormat: return u"Not a PDF"_s;
    case QPdfDocument::Error::IncorrectPassword: return u"The PDF is protected with a password"_s;
    case QPdfDocument::Error::UnsupportedSecurityScheme: return u"Unsupported security scheme"_s;
    case QPdfDocument::Error::DataNotYetAvailable: return u"Data not available"_s;
    case QPdfDocument::Error::Unknown: break;
    }
    return u"The PDF cannot be read"_s;
}

/// The document of `file` loaded; false and `info` filled when it cannot be.
bool load(QPdfDocument &doc, const QString &file, PdfInfo *info)
{
    const QPdfDocument::Error error = doc.load(file);
    if (error == QPdfDocument::Error::None && doc.pageCount() > 0)
        return true;
    info->pages = -1;
    info->locked = error == QPdfDocument::Error::IncorrectPassword;
    info->error = error == QPdfDocument::Error::None ? u"The PDF has no pages"_s : loadError(error);
    return false;
}

} // namespace

bool pdfAvailable()
{
    return true;
}

QString pdfBackendName()
{
    return u"Qt PDF"_s;
}

PdfInfo pdfInfo(const QString &file)
{
    PdfInfo info;
    QPdfDocument doc;
    if (!load(doc, file, &info))
        return info;
    info.pages = doc.pageCount();
    info.pagePoints.reserve(info.pages);
    for (int i = 0; i < info.pages; ++i)
        info.pagePoints.append(doc.pagePointSize(i));
    return info;
}

QImage renderPdfPage(const QString &file, int page, int maxSide, QString *error)
{
    PdfInfo info;
    QPdfDocument doc;
    if (!load(doc, file, &info)) {
        if (error)
            *error = info.error;
        return {};
    }
    if (page < 1 || page > doc.pageCount()) {
        if (error)
            *error = u"No page %1"_s.arg(page);
        return {};
    }
    const QSize size = pdfPagePixels(doc.pagePointSize(page - 1), maxSide);
    const QImage rendered = size.isEmpty() ? QImage() : doc.render(page - 1, size);
    if (rendered.isNull()) {
        if (error)
            *error = u"Page %1 cannot be rendered"_s.arg(page);
        return {};
    }
    // On white: pdfium leaves what the page does not paint transparent.
    QImage out(rendered.size(), QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter painter(&out);
    painter.drawImage(0, 0, rendered);
    painter.end();
    return out;
}

} // namespace brulion::platform
