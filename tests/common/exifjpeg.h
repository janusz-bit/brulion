// A JPEG carrying an EXIF orientation tag, as a phone stores a photo taken sideways: the pixels as
// the sensor read them, and the tag saying how to turn them. QImageWriter cannot make one — its
// setTransformation() turns the pixels themselves and writes no tag.
#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QImageWriter>

namespace brulion::test {

/// `image` stored as a JPEG (quality 100) with an APP1 EXIF segment whose orientation tag (0x0112)
/// is `orientation` (1…8, EXIF's numbering: 6 is a quarter turn clockwise, 8 the other way).
inline QByteArray exifOrientedJpeg(const QImage &image, quint16 orientation)
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(100);
    writer.write(image);
    const QByteArray jpeg = buffer.data();
    // TIFF, little-endian: the header, one IFD with one SHORT entry, no next IFD.
    QByteArray tiff("II*\0\x08\0\0\0", 8);
    tiff += QByteArray("\x01\0", 2);                                    // one entry
    tiff += QByteArray("\x12\x01\x03\0\x01\0\0\0", 8);                  // 0x0112, SHORT, count 1
    tiff += char(orientation & 0xff);
    tiff += char(orientation >> 8);
    tiff += QByteArray("\0\0", 2);                                      // (the value's padding)
    tiff += QByteArray("\0\0\0\0", 4);                                  // no next IFD
    const QByteArray payload = QByteArray("Exif\0\0", 6) + tiff;
    const int length = int(payload.size()) + 2;
    QByteArray app1("\xff\xe1", 2);
    app1 += char(length >> 8);
    app1 += char(length & 0xff);
    app1 += payload;
    // Right after SOI (FF D8), before the JFIF segment Qt writes.
    return jpeg.left(2) + app1 + jpeg.mid(2);
}

} // namespace brulion::test
