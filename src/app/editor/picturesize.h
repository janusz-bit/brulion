// The pixel size of a picture attachment, as Qt's image reader reports it (what the editor's
// QML Image shows once it has loaded), read from as little of the file as possible: the editor
// gives an embedded picture its size from the start, so its block keeps its height when the
// picture has loaded (docs/UI.md "Scrolling performance"). On the GUI thread, while blocks are
// created during scrolling, so it must stay cheap for any file:
//  * raster pictures: their header (QImageReader::size()), tens of microseconds;
//  * SVG: only the root <svg> element — its width, height and viewBox, given to Qt's SVG reader
//    as a document of their own (Qt's own rules for units, percentages and the viewBox). Qt's
//    reader would parse the whole document for its size (a 2.4 MB drawing: 0.6 s on a phone-
//    class CPU). Where the root leaves the size to the content (no viewBox, no absolute width
//    and height), the whole document only up to MaxFullSvgBytes, else unknown;
//  * SVGZ (compressed): the whole file up to MaxFullSvgBytes, else unknown.
// Unknown is an invalid QSize (the picture then takes its size when it has loaded).
#pragma once

#include <QSize>
#include <QString>

class QIODevice;

namespace brulion::editor {

/// Largest SVG / SVGZ file parsed whole for its size.
inline constexpr qint64 MaxFullSvgBytes = 64 * 1024;

/// The size of the picture in `device` (open for reading, at its start); `fileName` tells SVG
/// from the rest (by extension). Invalid when unknown or not a picture.
QSize pictureSize(QIODevice *device, const QString &fileName);

/// The size of a local picture file, cached per file, modification time and file size.
QSize localPictureSize(const QString &file);

/// The local file is a drawing (docs/SYNTAX.md §2 "Drawings"), by its name and first 4 KB
/// (drawing::looksLikeDrawingFile): an SVG naming an embedded Excalidraw scene, a `.excalidraw`
/// scene, an Obsidian Excalidraw plugin file (a `.md` with the key `excalidraw-plugin` in its
/// frontmatter). Cached per file, modification time and size, like localPictureSize(): the editor
/// asks for every embedded SVG it shows.
bool localFileIsDrawing(const QString &file);

/// The picture the Obsidian Excalidraw plugin exported beside its drawing `file` (the plugin's
/// "auto-export": `Szkic.excalidraw.svg` or `.png` of `Szkic.excalidraw.md`, also `Szkic.svg` /
/// `.png`), "" none — the preview of a drawing Brulion does not draw itself (frames, embedded
/// pages). Only for a `.md` file.
QString drawingExportOf(const QString &file);

/// The size of the preview of a local `.excalidraw` or Obsidian Excalidraw plugin drawing (what
/// the drawing image provider renders): the scene's drawing::pictureRect (EmptyWidth × EmptyHeight
/// for one without elements), with dashed boxes for elements Brulion does not draw
/// (drawing::previewDrawing) — or, for such a drawing, the size of the plugin's export beside it.
/// Invalid when the file is no drawing. Cached per file, modification time and size (a scene is
/// read and parsed once per version).
QSize localDrawingSize(const QString &file);

/// This device has a decoder for the picture format of `fileName`, by its extension: one of
/// QImageReader's formats — WebP and AVIF need Qt's image plugins, which Android, the browser and
/// some desktops lack. Whether the file is an attachment picture at all is the caller's question
/// (paths::isPicturePath). The picture view (Workspace::canDecodePicture) and the editor's embeds
/// (EditorHost::canDecodePicture) name a format this says no to instead of failing to load it.
bool canDecodePictureFormat(const QString &fileName);

/// The name of the picture format of `fileName`, its extension in upper case ("assets/łąka.webp"
/// -> "WEBP"; "" without one): how those messages name it.
QString pictureFormatName(const QString &fileName);

} // namespace brulion::editor
