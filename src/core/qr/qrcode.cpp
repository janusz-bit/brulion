#include "qr/qrcode.h"

#include <qrcodegen.hpp>

#include <cstdint>
#include <exception>
#include <vector>

namespace brulion::qr {

namespace {

Ecc eccOf(qrcodegen::QrCode::Ecc ecc)
{
    switch (ecc) {
    case qrcodegen::QrCode::Ecc::LOW: return Ecc::Low;
    case qrcodegen::QrCode::Ecc::MEDIUM: return Ecc::Medium;
    case qrcodegen::QrCode::Ecc::QUARTILE: return Ecc::Quartile;
    case qrcodegen::QrCode::Ecc::HIGH: return Ecc::High;
    }
    return Ecc::Low;
}

} // namespace

bool Code::dark(int x, int y) const
{
    if (x < 0 || y < 0 || x >= size || y >= size)
        return false;
    return modules.at(qsizetype(y) * size + x);
}

Code encodeBytes(QByteArrayView bytes)
{
    // The generator throws for data that fits no version (the browser's Qt never catches it):
    // what one code cannot hold is refused before it is asked.
    if (bytes.size() > MaxBytes)
        return {};
    std::vector<std::uint8_t> data;
    data.reserve(std::size_t(bytes.size()));
    for (const char byte : bytes)
        data.push_back(static_cast<std::uint8_t>(byte));
    try {
        const std::vector<qrcodegen::QrSegment> segments{qrcodegen::QrSegment::makeBytes(data)};
        // The smallest version for L, then as much correction as it allows (boostEcl).
        const qrcodegen::QrCode qr =
                qrcodegen::QrCode::encodeSegments(segments, qrcodegen::QrCode::Ecc::LOW, qrcodegen::QrCode::MIN_VERSION,
                                                  qrcodegen::QrCode::MAX_VERSION, -1, true);
        Code code;
        code.version = qr.getVersion();
        code.size = qr.getSize();
        code.ecc = eccOf(qr.getErrorCorrectionLevel());
        code.mask = qr.getMask();
        code.modules.resize(qsizetype(code.size) * code.size);
        for (int y = 0; y < code.size; ++y) {
            for (int x = 0; x < code.size; ++x)
                code.modules[qsizetype(y) * code.size + x] = qr.getModule(x, y);
        }
        return code;
    } catch (const std::exception &) {
        // (Unreachable: the length was checked. A safety net where exceptions are caught.)
        return {};
    }
}

Code encode(const QString &text)
{
    if (text.size() > MaxBytes) // a UTF-16 unit takes at least a byte
        return {};
    // A lone surrogate (broken text) as U+FFFD — three bytes, as qr::utf8Size() counts it — rather
    // than left out as QString::toUtf8() does: the reader sees that a character was broken.
    QString whole;
    whole.reserve(text.size());
    bool broken = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
            whole += c;
            whole += text[++i];
        } else if (c.isSurrogate()) {
            whole += QChar::ReplacementCharacter;
            broken = true;
        } else {
            whole += c;
        }
    }
    return encodeBytes((broken ? whole : text).toUtf8());
}

} // namespace brulion::qr
