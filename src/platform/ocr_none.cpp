// No OCR in this build (ocr.h): Android and the browser. The search reads the PDFs' own text only
// (Android: PdfRenderer's text on API 35+); Settings says OCR is not available here.
#include "ocr.h"

using namespace Qt::StringLiterals;

namespace brulion::platform {

QString ocrBackend()
{
    return u"none"_s;
}

bool ocrAvailable()
{
    return false;
}

QString ocrUnavailableReason()
{
    return u"unsupported"_s;
}

QString ocrEngineName()
{
    return {};
}

QStringList ocrLanguages()
{
    return {};
}

QString ocrDataDir()
{
    return {};
}

QString ocrUserDataDir()
{
    return {};
}

std::unique_ptr<OcrEngine> OcrEngine::create(const QString &, QString *error)
{
    if (error)
        *error = u"unsupported"_s;
    return nullptr;
}

} // namespace brulion::platform
