// PictureDecode — the size a picture is decoded at: what its place shows, not what its file holds
// (docs/PERFORMANCE.md "Pictures and memory").
//
//   Image {
//       readonly property int side: PictureDecode.side(frame.width, frame.height, Screen.devicePixelRatio)
//       sourceSize: Qt.size(side, side)       // a raster file: device pixels (Qt scales nothing up)
//   }
//
// A phone's 12 MP photo shown 560 × 420 in a note was decoded at 2048 × 1536: 12.6 MB of pixels
// and a texture of 16.8 MB with its mipmaps for 0.9 MB on screen. The side is the longer side of
// the place in device pixels, rounded up to a step — 64 px up to 512, 128 px above — so that a
// window being resized decodes a picture again only now and then (the picture shown stays until
// the next one is there: Image.retainWhileLoading), and at most a quarter wider than shown (from
// 256 px up): sharp without mipmaps. One side for both of Image.sourceSize's: Qt fits the file into that square
// with its aspect ratio, which also holds for a photo turned by its EXIF orientation (the
// reader's size is the sensor's, the other way round).
#pragma once

#include <QObject>

namespace brulion::ui {

class PictureDecode : public QObject {
    Q_OBJECT
    /// No picture of a note or the picture view is decoded larger than this a side.
    Q_PROPERTY(int maxSide READ maxSide CONSTANT FINAL)

public:
    using QObject::QObject;

    static constexpr int MaxSide = 2048;
    static constexpr int MinSide = 64;

    int maxSide() const { return MaxSide; }

    /// The side for a place of `shownWidth` × `shownHeight` logical pixels on a screen of
    /// `devicePixelRatio` (≤ 0: 1): MinSide … MaxSide, see above.
    Q_INVOKABLE int side(qreal shownWidth, qreal shownHeight, qreal devicePixelRatio) const
    {
        return sideFor(shownWidth, shownHeight, devicePixelRatio);
    }
    /// … and at most `limit` (the picture view: its fitted picture, never beyond the cap).
    Q_INVOKABLE int sideWithin(qreal shownWidth, qreal shownHeight, qreal devicePixelRatio, int limit) const
    {
        return qBound(MinSide, sideFor(shownWidth, shownHeight, devicePixelRatio), qMax(MinSide, limit));
    }

    static int sideFor(qreal shownWidth, qreal shownHeight, qreal devicePixelRatio);
};

} // namespace brulion::ui
