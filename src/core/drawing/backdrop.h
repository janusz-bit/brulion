// A drawing's backdrop ("podkład", docs/SYNTAX.md §2 "Drawings", docs/UI.md "Drawing editor"): a
// picture, a slide exported as a picture or a PDF page under the ink and the shapes — lecture notes
// on slides. It is an ordinary Excalidraw image element at index 0, `locked`, marked by
// `customData.brulionBackdrop = {"path": "<vault path>", "page": <1-based; 0 a picture>}`, its
// raster in the scene's `files` — so excalidraw.com shows the slide (locked, at the bottom) and keeps
// the marker, and Brulion's merge, undo and z-order rules treat it as any element:
//
//   {"id":"…","type":"image","x":0,"y":0,"width":1200,"height":675,"locked":true,"fileId":"<sha1>",
//    "status":"saved","scale":[1,1],"crop":null,…,
//    "customData":{"brulionBackdrop":{"path":"Załączniki/Fizyka – wykład 3.pdf","page":3}}}
//   files: {"<sha1>": {"mimeType":"image/png","id":"<sha1>","dataURL":"data:image/png;base64,…",…}}
//
// Only the element at index 0 counts: the same marker anywhere else is an ordinary picture (an
// Excalidraw user who moved it up), an unlocked one is none either (a picture the user unlocked
// in excalidraw.com). The path is the source file's vault path ("/" separators, relative) as the
// app stored it; a path that is absolute or leaves the vault reads as none. The raster travels in
// the file: the drawing looks the same on every device and in every other app, also where no PDF
// renderer exists. Pure functions: the app passes ids, random numbers and the clock (EditStamp)
// and prepares the raster (src/app/drawing/backdropimage.h).
#pragma once

#include "drawing/drawing.h"
#include "drawing/edit.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::drawing {

/// The key of the marker in the element's customData.
inline constexpr QStringView BackdropKey = u"brulionBackdrop";
/// The long side of a new backdrop (scene units): a slide 1200 × 675, an A4 page 849 × 1200.
inline constexpr double BackdropLongSide = 1200;
/// The raster's long side (pixels; 2 per scene unit) and the least a large one is shrunk to.
inline constexpr int BackdropMaxPixels = 2400;
inline constexpr int BackdropMinPixels = 1200;
/// The largest encoded raster a backdrop takes (setBackdrop() refuses more).
inline constexpr qsizetype MaxBackdropBytes = 4 * 1024 * 1024;
/// The most PDF pages inserted at once (one drawing each; parsePageRanges()).
inline constexpr int MaxPagesPerInsert = 60;
/// The largest page number a marker may name (more reads as 0: a picture).
inline constexpr int MaxBackdropPage = 100000;

/// Where a backdrop came from: the file's vault path and, for a PDF, its page (1-based; 0 — a
/// picture).
struct BackdropSource {
    QString path;
    int page = 0;

    friend bool operator==(const BackdropSource &, const BackdropSource &) = default;
};

/// A backdrop's raster as stored: its encoded bytes ("image/png", "image/jpeg", "image/webp" or
/// "image/gif") and its size in pixels.
struct BackdropImage {
    QByteArray bytes;
    QString mimeType;
    QSize pixels;
};

/// The index of the drawing's backdrop: 0 when the first element is a locked image whose
/// customData has the marker (an object), else −1.
qsizetype backdropIndex(const Drawing &d);
/// The backdrop's source (nullopt: no backdrop). A path that is not a relative vault path ("/…",
/// "C:…", a ".." segment, control characters) reads as ""; a page that is not a whole number in
/// 1…MaxBackdropPage as 0.
std::optional<BackdropSource> backdropSource(const Drawing &d);
/// The scene size of a new backdrop of `pixels`: its long side BackdropLongSide, the aspect kept,
/// rounded to two decimals; an empty size for an invalid one.
QSizeF backdropSceneSize(QSize pixels);
/// The file id of a raster: the SHA-1 of its bytes in hex (Excalidraw's generateIdFromFile).
QString fileIdOf(QByteArrayView bytes);
/// "data:<mimeType>;base64,<bytes>".
QString dataUrl(const BackdropImage &image);
/// The raster is one a backdrop takes: bytes within MaxBackdropBytes, a known mime type, a valid
/// size.
bool validBackdropImage(const BackdropImage &image);
/// Put `image` under everything as the backdrop: a new locked image element at index 0 at (0, 0),
/// or — replacing one — the backdrop element itself changed (same id, its top left kept, the new
/// size, file and source; stamped). The other elements stay as they are; the file entry is added
/// (the old one stays until sceneJson() drops files no element uses). False — nothing changed —
/// for an invalid image.
bool setBackdrop(Drawing &d, const BackdropImage &image, const BackdropSource &source, const EditStamp &stamp);
/// Remove the backdrop (its file entry stays: undo brings it back). False when there is none.
bool removeBackdrop(Drawing &d);
/// A new drawing on `image`: the backdrop set (setBackdrop()) and its background — "Jasne" (Light)
/// on a picture, which keeps its colours, "Jak motyw" (Auto) on a PDF page, which turns dark with
/// the app ("document dark"). nullopt for an invalid image.
std::optional<Drawing> newDrawingOnBackdrop(const BackdropImage &image, const BackdropSource &source,
                                            const EditStamp &stamp);
/// The pages a user typed out of `count`: "1-3, 7"; "3–1" (en dash; reversed); "wszystkie",
/// "all" or "*" — every page. Sorted, unique, within 1…count (a range is clipped to it), the first
/// `limit` of them (a caller may ask for one more to tell that the text named too many); parts
/// that are no number or range are ignored.
QList<int> parsePageRanges(QStringView text, int count, int limit = MaxPagesPerInsert);
/// Sorted unique pages as a user would type them: runs of three or more as ranges ("1-3, 7, 9-10"
/// → "1-3, 7, 9, 10"); "" for none. parsePageRanges() reads it back.
QString formatPageRanges(const QList<int> &pages);

} // namespace brulion::drawing
