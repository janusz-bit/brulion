// PdfViewImageProvider — the PDF viewer's pages and their detail pictures (the QML image provider
// "pdfview"; PdfViewModel builds the URLs, qml/content/ContentPdfView.qml shows them):
//
//   image://pdfview/<absolute file, percent-encoded>?page=N&v=<mtime-size>&w=<full logical width>
//                  &h=<full logical height>[&clip=x,y,cw,ch]
//
// The page drawn at w × h logical pixels × the ratio Qt asks for (requestedSize / the clip's or the
// page's logical size: Qt multiplies an Image's sourceSize by the screen's ratio for providers), or
// only its part `clip` (page-local logical pixels). Asynchronous and cancellable: each picture is a
// task on PdfRenderPool (a page before a detail picture), with a reader leased from PdfReaderPool;
// a cancelled one (scrolled away) is not drawn. Only absolute `.pdf` paths without ".." segments
// (as PdfPageImageProvider), finite numbers; the page's long side at most MaxPdfFullSide (larger:
// drawn at that size), the picture at most MaxPdfClipPixels. A page that cannot be drawn answers
// with a 16 × 16 light grey picture (QML would warn about an error) and tells the view model
// (PdfReaderPool::reportFailure → the page's "cannot be shown").
#pragma once

#include <QQuickAsyncImageProvider>
#include <QRectF>
#include <QSizeF>
#include <QString>

namespace brulion::pdfview {

class PdfViewImageProvider : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

    /// What an id asks for (tests).
    struct Request {
        bool valid = false;
        QString file;
        int page = 0;
        QString version;
        QSizeF full; ///< logical
        QRectF clip; ///< logical, page-local; empty: the whole page
    };
    static Request parse(const QString &id);
    /// The pixel sizes for `request` and Qt's `requestedSize`: the page drawn at `full`, its part
    /// `clip` (empty: all). False when nothing may be drawn.
    static bool pixels(const Request &request, const QSize &requestedSize, QSize *full, QRect *clip);
};

} // namespace brulion::pdfview
