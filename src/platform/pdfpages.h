// PDF pages as pictures (docs/UI.md "Drawing editor": a PDF page as a drawing's backdrop; "PDF
// viewer": the thumbnails of embedded PDFs and the in-app viewer; docs/ARCHITECTURE.md "Platform
// layer"). One backend per build, chosen by src/platform/CMakeLists.txt:
//
//   Android           the system's android.graphics.pdf.PdfRenderer through JNI
//                     (pdfpages_android.cpp → android/…/PdfPages.kt sessions): nothing added to the
//                     APK; text (search) on Android 15+ (API 35) only
//   Qt PDF            QPdfDocument (pdfium; pdfpages_qtpdf.cpp) where Qt has the module: Windows,
//                     the AppImage, CI's Linux build (aqt's `qtpdf`)
//   poppler           poppler-qt6 (pdfpages_poppler.cpp): the Nix build — nixpkgs has Qt PDF only
//                     inside QtWebEngine (+615 MiB), poppler costs +26 MiB
//   none              pdfAvailable() false (pdfpages_none.cpp): the browser — Qt has no Qt PDF for
//                     WebAssembly — and builds without any of them; pictures still work there
//
// PdfReader is an open PDF (parsed once) that draws whole pages or parts of them and finds text; a
// reader is used by one thread at a time (the app's PdfReaderPool leases them). The free functions
// pdfInfo() and renderPdfPage() open a reader for each call (pdfpages.cpp), so they are thread-safe
// (the picker's thumbnails come from Qt Quick's image reader thread, the backdrops are prepared on
// a thread pool). A page is always drawn on white.
#pragma once

#include <QImage>
#include <QList>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringView>

#include <memory>

namespace brulion::platform {

/// A PDF renderer is built in.
bool pdfAvailable();
/// Its name for the event log and the About box ("Qt PDF", "poppler", "Android PdfRenderer",
/// "none").
QString pdfBackendName();
/// This build's renderer extracts a page's text: Qt PDF and poppler yes; Android on API 35+
/// (PdfRenderer.Page's text APIs, probed once); none no. The PDF viewer hides its search otherwise.
bool pdfTextAvailable();

/// What pdfInfo() found.
struct PdfInfo {
    /// The number of pages; -1: the file cannot be read as a PDF (`error` says why).
    int pages = -1;
    /// Each page's size in points (1/72 inch), rotation applied — only the first page's when
    /// pdfInfo() was not asked for all.
    QList<QSizeF> pagePoints;
    QString error;
    /// The file asks for a password (pages −1).
    bool locked = false;
};

/// The file's pages and their sizes: every page's (`pageSizes`; every page opened — slower for a
/// long PDF) or the first page's only (the page picker's thumbnails take its shape).
PdfInfo pdfInfo(const QString &file, bool pageSizes = true);
/// Page `page` (1-based) of the file on white, its long side `maxSide` pixels (1…8192; the aspect
/// from the page's size, rotation applied). Null (and `error`) when the file or the page cannot be
/// read, or the picture would have more than MaxPdfClipPixels.
QImage renderPdfPage(const QString &file, int page, int maxSide, QString *error = nullptr);

/// The largest maxSide renderPdfPage() takes.
inline constexpr int MaxPdfPageSide = 8192;
/// A page drawn larger than this (its long side, in pixels) is not drawn: PdfReader::render()'s
/// `full` (the viewer at 800 % of a large page asks for parts of a page this big).
inline constexpr int MaxPdfFullSide = 32768;
/// The largest picture one PdfReader::render() returns (16 MP, 64 MB): the viewer asks for at most
/// a screenful.
inline constexpr qint64 MaxPdfClipPixels = 16'000'000;
/// The most places PdfReader::find() returns for one page.
inline constexpr int MaxPdfHitsPerPage = 500;

/// A place of a search on a page: the range of the page's text (PdfReader::pageText()) and its
/// rectangles in points (top-left origin, the page as shown: rotation applied).
struct PdfTextHit {
    int start = 0;
    int length = 0;
    QList<QRectF> rects;
};

/// An open PDF, used by ONE thread at a time (the app's PdfReaderPool leases it). Opening parses
/// the file once; pages are opened per call.
class PdfReader {
public:
    virtual ~PdfReader() = default;
    PdfReader(const PdfReader &) = delete;
    PdfReader &operator=(const PdfReader &) = delete;

    /// The file opened, or null with `info` saying why (pages −1, error, locked). On success `info`
    /// (optional) has the page count and the FIRST page's size only: opening never walks all
    /// pages. Defined by the backend.
    static std::unique_ptr<PdfReader> open(const QString &file, PdfInfo *info = nullptr);

    virtual int pageCount() const = 0;
    /// Page `page` (1-based) in points, rotation applied; invalid when the page cannot be read.
    virtual QSizeF pageSize(int page) = 0;
    /// The page scaled to `full` pixels, of it the part `clip` (empty: all of it; else clipped to
    /// the page), on white: Format_RGB32, or Format_RGBA8888_Premultiplied that is opaque (Android:
    /// drawn into it by PdfRenderer). Null when the page cannot be drawn,
    /// `full`'s long side exceeds MaxPdfFullSide, the result would have more than
    /// MaxPdfClipPixels, or `clip` lies outside the page.
    virtual QImage render(int page, const QSize &full, const QRect &clip = {}) = 0;
    /// The page's text in reading order ("" when it has none or the backend cannot extract it).
    virtual QString pageText(int page) = 0;
    /// The places of `query` on the page (pdfsearch.h: case- and diacritics-insensitive, runs of
    /// white space equal), at most `maxHits`, left to right; none without a text API.
    virtual QList<PdfTextHit> find(int page, QStringView query, int maxHits = MaxPdfHitsPerPage) = 0;
    /// Called by the lease on the thread that is about to use / has stopped using the reader (Qt
    /// PDF: its QPdfDocument is pulled to that thread and given back to no thread — QObject allows
    /// pulling an object without thread affinity). Default: nothing.
    virtual void attachToCurrentThread() { }
    virtual void detachFromThread() { }
    /// The reader cannot be used any more (Android: its PdfRenderer session was closed under it);
    /// the pool drops it instead of keeping it idle. Default: never.
    virtual bool lost() const { return false; }

protected:
    PdfReader() = default;
    /// render()'s checks shared by the backends: `clip` (empty: the whole page) within `full`, the
    /// sizes in bounds. The rectangle to draw; empty when nothing may be drawn.
    static QRect clipOf(const QSize &full, const QRect &clip);
    /// `rendered` (any format) painted on white into a picture of `size`, scaled when it differs.
    static QImage onWhite(const QImage &rendered, const QSize &size);
};

/// The pixel size of a page of `points` with its long side `maxSide` (at least 1 × 1; empty for an
/// empty page size). Shared by the backends.
QSize pdfPagePixels(const QSizeF &points, int maxSide);
/// The resolution (dots per inch) that draws a page of `points` at `pixels` (pdfPagePixels()): each
/// side at least as many pixels, the drawn picture's long side at most MaxPdfPageSide — so memory
/// follows the size asked for, never the page's own (a hostile 20000-pt page drawn for a 230 px
/// thumbnail). 0 for an empty page or size. The poppler backend renders at it.
double pdfRenderDpi(const QSizeF &points, const QSize &pixels);

} // namespace brulion::platform
