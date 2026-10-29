// DrawingImageProvider — the previews of drawings (the QML image provider "drawing"; docs/UI.md
// "Drawing editor"): the embeds of drawings in notes (EmbedView), the picture view
// (ContentPictureView) and the drawing editor's notice show a drawing through it, in the light or
// the dark theme, so a drawing looks as on the drawing editor's paper and follows the app's theme
// at once (a new URL: Qt's pixmap cache keeps both).
//
//   image://drawing/<absolute file path, percent-encoded>?v=<mtime>-<size>&dark=0|1
//   (Workspace::drawingUrl builds it; `v` only keeps Qt's cache honest when the file changes;
//   `dark`: the app is dark — an "auto" drawing follows it, a "light" or "dark" one does not.)
//
// The files: Brulion's SVG (and Excalidraw's "Embed scene" SVG), a `.excalidraw` scene and an
// Obsidian Excalidraw plugin file (`*.excalidraw.md`, any `.md` with the plugin's frontmatter key;
// other notes give no picture) — core drawing/drawingfile.h.
// A drawing Brulion draws (drawing::editable) is painted by drawingrender.h (renderDrawing) — the
// whole picture, in the frame of the size the file's SVG root names (what the embeds, the picture
// view and the notice are sized by, editor::pictureSize: Brulion writes its pictureRect there, a
// file Excalidraw wrote has its own margins; the elements centred in it at one unit a pixel, smaller
// only when they do not fit — shapes, text and pictures too), fitted into the requested size;
// anything else in such a file (an Excalidraw drawing with frames or embedded web pages, a scene
// that cannot be read) is the file's own picture
// through Qt's SVG renderer, in the dark theme with its colours mapped one by one as Excalidraw's
// dark mode does (drawing::darkSvg: pictures and emoji in it keep theirs; the per-pixel filter
// darkImage when it is not well-formed) — unless that picture is dark already (its edge, the paper,
// darker than mid grey: a "dark" drawing whose scene cannot be read). A `.excalidraw` scene or a
// plugin file Brulion does not draw has no picture of its own: the plugin's export beside it
// (editor::drawingExportOf: `Szkic.excalidraw.svg` / `.png`), else its elements with a dashed grey
// box for each element Brulion does not draw (drawing::previewDrawing) — the size
// editor::localDrawingSize gives the embeds. Stateless: Qt Quick calls it
// in its image reader thread (the browser build: on the GUI thread); it only reads the file.
#pragma once

#include <QQuickImageProvider>
#include <QUrl>

namespace brulion::drawingview {

class DrawingImageProvider : public QQuickImageProvider {
public:
    DrawingImageProvider();

    /// Largest file read (a drawing is far smaller; pictures pasted into an Excalidraw scene are a
    /// few MB each).
    static constexpr qint64 MaxFileBytes = 64 * 1024 * 1024;
    /// Largest side of a preview (px); a larger requested size is scaled down, aspect kept.
    static constexpr int MaxSide = 4096;

    /// The preview URL of the drawing file at `absolutePath` (see above).
    static QUrl url(const QString &absolutePath, const QString &version, bool dark);
    /// The picture of a drawing file's bytes as the app's theme (`appDark`) shows it, fitted into
    /// `requested` (an empty side: from the other one; none: the natural size); `natural` gets
    /// the picture's own size. Null when the bytes are no picture.
    static QImage picture(const QByteArray &bytes, bool appDark, const QSize &requested, QSize *natural = nullptr);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

} // namespace brulion::drawingview
