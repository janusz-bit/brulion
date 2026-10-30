// A drawing's backdrop prepared from its source file (docs/UI.md "Drawing editor"; core
// drawing/backdrop.h holds the element): a picture file or a PDF page made the raster stored in
// the drawing — at most drawing::BackdropMaxPixels a side and drawing::MaxBackdropBytes encoded.
//
//   PreparedBackdrop slide = prepareBackdropPdfPage(absolute, u"Załączniki/wykład 3.pdf"_s, 4);
//   if (slide.error.isEmpty()) drawing::setBackdrop(d, slide.image, slide.source, stamp);
//
// Thread-safe (no shared state): DrawingBackdrops runs them on a thread pool.
#pragma once

#include "drawing/backdrop.h"

#include <QImage>
#include <QString>
#include <QVariantMap>

namespace brulion::drawingview {

/// What went wrong while preparing (PreparedBackdrop::error).
namespace backdroperror {
/// The file cannot be read (missing, unreadable, larger than MaxSourceBytes).
inline constexpr QStringView Unreadable = u"unreadable";
/// No picture this device can decode (HEIC without a plugin, random bytes, an empty file).
inline constexpr QStringView Format = u"format";
/// A PDF protected with a password.
inline constexpr QStringView Locked = u"locked";
/// A PDF that cannot be read, or the page is not in it.
inline constexpr QStringView Pdf = u"pdf";
/// This build has no PDF renderer (the browser).
inline constexpr QStringView NoPdf = u"noPdf";
} // namespace backdroperror

/// A source file larger than this is not read (as attachments: 200 MB).
inline constexpr qint64 MaxSourceBytes = 200LL * 1024 * 1024;

struct PreparedBackdrop {
    drawing::BackdropImage image;
    drawing::BackdropSource source;
    /// The source file's name without its extension ("Fizyka – wykład 3"): the drawing's name.
    QString name;
    /// "" when prepared, else one of backdroperror.
    QString error;
};

/// A picture file → the raster stored in the drawing: its bytes kept as they are when it is a PNG
/// or a JPEG within MaxBackdropBytes and BackdropMaxPixels a side that needs no turning (EXIF);
/// else decoded (QImageReader: turned by its EXIF orientation, at most 256 MB decoded, the first
/// frame of an animation; an SVG — a drawing too — drawn BackdropMaxPixels on its long side) and
/// encoded (encodeBackdrop()).
PreparedBackdrop prepareBackdropPicture(const QString &absoluteFile, const QString &vaultPath);
/// A PDF page drawn BackdropMaxPixels on its long side (platform/pdfpages.h), encoded.
PreparedBackdrop prepareBackdropPdfPage(const QString &absoluteFile, const QString &vaultPath, int page);
/// A raster as a backdrop stores it: PNG and JPEG (quality 85) both made, the smaller kept — PNG
/// when the picture is see-through somewhere —, shrunk by 0.75 until it is within
/// MaxBackdropBytes (never below BackdropMinPixels a side; a see-through one that is still too
/// large then goes on white as a JPEG). An empty image for a null one.
drawing::BackdropImage encodeBackdrop(const QImage &image);

/// The prepared backdrop for QML: { bytes, mimeType, width, height, path, page, name, error }.
QVariantMap toVariant(const PreparedBackdrop &prepared);
/// …and back (missing or malformed entries: empty).
PreparedBackdrop fromVariant(const QVariantMap &map);

} // namespace brulion::drawingview
