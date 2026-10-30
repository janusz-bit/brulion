// PDF pages as pictures (docs/UI.md "Drawing editor": a PDF page as a drawing's backdrop; docs/
// ARCHITECTURE.md "Platform layer"). One backend per build, chosen by src/platform/CMakeLists.txt:
//
//   Android           the system's android.graphics.pdf.PdfRenderer through JNI
//                     (pdfpages_android.cpp → android/…/PdfPages.kt): nothing added to the APK
//   Qt PDF            QPdfDocument (pdfium; pdfpages_qtpdf.cpp) where Qt has the module: Windows,
//                     the AppImage, CI's Linux build (aqt's `qtpdf`)
//   poppler           poppler-qt6 (pdfpages_poppler.cpp): the Nix build — nixpkgs has Qt PDF only
//                     inside QtWebEngine (+615 MiB), poppler costs +26 MiB
//   none              pdfAvailable() false (pdfpages_none.cpp): the browser — Qt has no Qt PDF for
//                     WebAssembly — and builds without any of them; pictures still work there
//
// Thread-safe: every call opens the file itself (the picker's thumbnails come from Qt Quick's image
// reader thread, the backdrops are prepared on a thread pool). A page is drawn on white.
#pragma once

#include <QImage>
#include <QList>
#include <QSizeF>
#include <QString>

namespace brulion::platform {

/// A PDF renderer is built in.
bool pdfAvailable();
/// Its name for the event log and the About box ("Qt PDF", "poppler", "Android PdfRenderer",
/// "none").
QString pdfBackendName();

/// What pdfInfo() found.
struct PdfInfo {
    /// The number of pages; -1: the file cannot be read as a PDF (`error` says why).
    int pages = -1;
    /// Each page's size in points (1/72 inch), rotation applied (empty when not asked for).
    QList<QSizeF> pagePoints;
    QString error;
    /// The file asks for a password (pages −1).
    bool locked = false;
};

/// The file's pages and — `pageSizes` — their sizes (every page opened: slower for a long PDF).
PdfInfo pdfInfo(const QString &file, bool pageSizes = true);
/// Page `page` (1-based) of the file on white, its long side `maxSide` pixels (1…8192; the aspect
/// from the page's size, rotation applied). Null (and `error`) when the file or the page cannot be
/// read.
QImage renderPdfPage(const QString &file, int page, int maxSide, QString *error = nullptr);

/// The largest maxSide renderPdfPage() takes.
inline constexpr int MaxPdfPageSide = 8192;

/// The pixel size of a page of `points` with its long side `maxSide` (at least 1 × 1; empty for an
/// empty page size). Shared by the backends.
QSize pdfPagePixels(const QSizeF &points, int maxSide);

} // namespace brulion::platform
