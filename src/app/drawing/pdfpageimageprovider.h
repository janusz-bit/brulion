// PdfPageImageProvider — the thumbnails of a PDF's pages in the backdrop's page picker (the QML
// image provider "pdfpage"; qml/drawing/DrawingBackdropDialog.qml, docs/UI.md "Drawing editor"):
//
//   image://pdfpage/<absolute file path, percent-encoded>?page=N&v=<mtime>-<size>
//   (DrawingBackdrops::pageUrl builds it; `v` keeps Qt's cache honest when the file changes.)
//
// The page drawn on white at the requested size (its long side; none: DefaultSide) by
// platform/pdfpages.h. Only absolute paths of `.pdf` files without ".." segments. Stateless: Qt
// Quick calls it in its image reader thread; each call opens the file (fine for a page picker).
#pragma once

#include <QQuickImageProvider>
#include <QUrl>

namespace brulion::drawingview {

class PdfPageImageProvider : public QQuickImageProvider {
public:
    PdfPageImageProvider();

    /// The long side of a thumbnail without a requested size, and the largest one drawn.
    static constexpr int DefaultSide = 320;
    static constexpr int MaxSide = 1600;

    static QUrl url(const QString &absolutePath, int page, const QString &version);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

} // namespace brulion::drawingview
