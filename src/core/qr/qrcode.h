// brulion::qr — QR codes (docs/UI.md "QR code"): a note shown as codes another phone's camera
// reads, without a network or an account. This file makes one code: Nayuki's QR Code generator
// (3rdparty/qrcodegen, MIT) with the text's UTF-8 in byte mode, which phone cameras and scanner
// apps read as text — Polish letters and emoji included. No ECI header: scanners take UTF-8
// without one, and some show the header as stray characters. GUI-free and pure: any thread.
// qrtext.h has the text a note's codes carry and its cutting into several codes.
#pragma once

#include <QByteArrayView>
#include <QList>
#include <QString>

namespace brulion::qr {

/// The most bytes one code holds: version 40 (177 × 177 modules), byte mode, error correction L.
inline constexpr qsizetype MaxBytes = 2953;

/// Light modules a scanner needs around a code (the quiet zone): 4 by the standard. Not part of
/// Code; whoever draws a code leaves them.
inline constexpr int QuietZone = 4;

/// Error correction (ISO/IEC 18004): how much of a code may be unreadable — L about 7 % of its
/// codewords, M 15 %, Q 25 %, H 30 %.
enum class Ecc { Low, Medium, Quartile, High };

/// One QR code (Model 2) without its quiet zone: size × size modules, `true` dark.
struct Code {
    int version = 0;     ///< 1 … 40; 0 for no code
    int size = 0;        ///< modules per side, version × 4 + 17 (21 … 177); 0 for no code
    Ecc ecc = Ecc::Low;  ///< the error correction it has
    int mask = -1;       ///< the mask pattern it has, 0 … 7
    QList<bool> modules; ///< size × size, row by row from the top left corner

    bool isNull() const { return size == 0; }
    /// The module at column `x`, row `y`; light (false) outside the code.
    bool dark(int x, int y) const;
};

/// `bytes` as one code in byte mode: the smallest version that holds them with error correction
/// L, the correction then raised as far as that version allows (a short text gets H, which a
/// scratched screen or glare spoils less), the mask with the lowest penalty of the standard.
/// Empty bytes are a version 1 code. A null Code when they are more than MaxBytes. Never throws.
Code encodeBytes(QByteArrayView bytes);
/// `text` as one code of its UTF-8 bytes (encodeBytes()); a lone surrogate — broken text — is
/// written as U+FFFD.
Code encode(const QString &text);

} // namespace brulion::qr
