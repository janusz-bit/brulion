// PDF pages without a renderer (pdfpages.h): the browser build — Qt has no Qt PDF for WebAssembly
// — and builds that found neither Qt PDF nor poppler-qt6. Pictures still become backdrops there,
// drawings with PDF pages made elsewhere show as they are (their pixels are in the file), and the
// PDF viewer hands PDFs to the browser's own viewer (a new tab) or the system's app instead.
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

bool pdfTextAvailable()
{
    return false;
}

bool pdfSelectionAvailable()
{
    return false;
}

bool pdfPasswordAvailable()
{
    return false;
}

std::unique_ptr<PdfReader> PdfReader::open(const QString &, PdfInfo *info, const QString &)
{
    if (info) {
        *info = PdfInfo();
        info->error = u"This build has no PDF renderer"_s;
    }
    return nullptr;
}

} // namespace brulion::platform
