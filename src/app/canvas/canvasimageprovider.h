// CanvasImageProvider — pictures of canvases (the QML image provider "canvas"; docs/UI.md "Canvas
// view"): the sync center's two versions of a canvas in conflict, a canvas shown as a card on
// another canvas, the view's export as PNG. The canvas's own SVG (core canvas/svg.h: cards, groups,
// edges and labels, escaped; no pictures from other files) drawn by Qt SVG into the requested size.
//
//   image://canvas/<absolute file path, percent-encoded>?v=<mtime>-<size>&dark=0|1
//   (`v` keeps Qt's cache honest when the file changes.)
//
// Stateless: Qt Quick calls it in its image reader thread; it only reads the file.
#pragma once

#include <QQuickImageProvider>
#include <QUrl>

namespace brulion::canvasview {

class CanvasImageProvider : public QQuickImageProvider {
public:
    CanvasImageProvider();

    /// Largest side of a picture (px).
    static constexpr int MaxSide = 4096;

    /// The picture URL of the canvas file at `absolutePath`.
    static QUrl url(const QString &absolutePath, bool dark);
    /// A canvas file's text as a picture fitted into `requested` (an empty side: from the other;
    /// none: its own size, at most MaxSide); null for a file that is no canvas.
    static QImage picture(const QString &text, bool dark, QSize requested, QSize *natural = nullptr);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

} // namespace brulion::canvasview
