#include "text/decode.h"

#include <QStringDecoder>

#include <array>

namespace brulion::text {

namespace {

/// windows-1252 bytes 0x80–0x9F (0xA0–0xFF are Latin-1); undefined bytes keep their C1 code.
constexpr std::array<char16_t, 32> Cp1252C1 = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                               0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
                                               0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                               0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

/// windows-1250 bytes 0x80–0xFF (Polish, Czech, Hungarian, … Windows "ANSI").
constexpr std::array<char16_t, 128> Cp1250High = {
        0x20AC, 0x0081, 0x201A, 0x0083, 0x201E, 0x2026, 0x2020, 0x2021, 0x0088, 0x2030, 0x0160, 0x2039, 0x015A,
        0x0164, 0x017D, 0x0179, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0098, 0x2122,
        0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A, 0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6,
        0x00A7, 0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B, 0x00B0, 0x00B1, 0x02DB, 0x0142,
        0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C, 0x0154,
        0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, 0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD,
        0x00CE, 0x010E, 0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, 0x0158, 0x016E, 0x00DA,
        0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF, 0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
        0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F, 0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4,
        0x0151, 0x00F6, 0x00F7, 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9};

/// Does legacy 8-bit text look like windows-1250 rather than windows-1252? Bytes of Polish
/// letters that are rare symbols in windows-1252 (ą ł ś ź Ś Ź Ą Ż = ¹ ³ œ Ÿ Œ – ¥ ¯) are weighed
/// against Western letters that windows-1250 lacks (à è ì ò ù å ã õ ø …).
bool looksLikeCp1250(const QByteArray &data)
{
    int polish = 0;
    int western = 0;
    for (const char ch : data) {
        switch (static_cast<unsigned char>(ch)) {
        case 0xB9:
        case 0xB3:
        case 0x9C:
        case 0x9F:
        case 0x8C:
        case 0x8F:
        case 0xA5:
        case 0xAF: ++polish; break;
        case 0xC0:
        case 0xC3:
        case 0xC5:
        case 0xC8:
        case 0xCC:
        case 0xD2:
        case 0xD5:
        case 0xD8:
        case 0xD9:
        case 0xE0:
        case 0xE3:
        case 0xE5:
        case 0xE8:
        case 0xEC:
        case 0xF2:
        case 0xF5:
        case 0xF8:
        case 0xF9: ++western; break;
        default: break;
        }
    }
    return polish > western;
}

/// 8-bit text that is not UTF-8: windows-1250 when it looks Polish (see looksLikeCp1250()),
/// else windows-1252.
QString decodeLegacy(const QByteArray &data)
{
    const bool cp1250 = looksLikeCp1250(data);
    QString out(data.size(), Qt::Uninitialized);
    QChar *dst = out.data();
    for (const char ch : data) {
        const auto b = static_cast<unsigned char>(ch);
        char16_t c = b;
        if (b >= 0x80)
            c = cp1250 ? Cp1250High[b - 0x80] : b < 0xA0 ? Cp1252C1[b - 0x80] : char16_t(b);
        *dst++ = QChar(c);
    }
    return out;
}

} // namespace

QString decodeText(const QByteArray &data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(data.sliced(3));
    if (data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF")) {
        QStringDecoder decoder(data.startsWith("\xFF\xFE") ? QStringConverter::Utf16LE : QStringConverter::Utf16BE);
        return decoder(data.sliced(2));
    }
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = utf8(data);
    if (!utf8.hasError())
        return text;
    return decodeLegacy(data);
}

} // namespace brulion::text
