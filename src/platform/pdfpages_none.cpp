// PDF pages without a renderer (pdfpages.h): the browser build — Qt has no Qt PDF for WebAssembly
// — and builds that found neither Qt PDF nor poppler-qt6. Pictures still become backdrops there,
// and drawings with PDF pages made elsewhere show as they are (their pixels are in the file).
#include "pdfpages.h"

using namespace Qt::StringLiterals;

namespace brulion::platform {

bool pdfAvailable()
{
    return false;
}

QString pdfBackendName()
{
    return u"none"_s;
}

PdfInfo pdfInfo(const QString &file, bool pageSizes)
{
    Q_UNUSED(file);
    Q_UNUSED(pageSizes);
    PdfInfo info;
    info.error = u"This build has no PDF renderer"_s;
    return info;
}

QImage renderPdfPage(const QString &file, int page, int maxSide, QString *error)
{
    Q_UNUSED(file);
    Q_UNUSED(page);
    Q_UNUSED(maxSide);
    if (error)
        *error = u"This build has no PDF renderer"_s;
    return {};
}

} // namespace brulion::platform
