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

/// Votes of the bytes for windows-1250 against windows-1252: +1 for Polish letters that are rare
/// symbols in windows-1252 (ą ł ś ź Ś Ź Ą Ż = ¹ ³ œ Ÿ Œ – ¥ ¯), -1 for Western letters that
/// windows-1250 lacks (à è ì ò ù å ã õ ø …).
constexpr std::array<signed char, 256> Cp1250Votes = [] {
    std::array<signed char, 256> votes{};
    for (const int b : {0xB9, 0xB3, 0x9C, 0x9F, 0x8C, 0x8F, 0xA5, 0xAF})
        votes[b] = 1;
    for (const int b :
         {0xC0, 0xC3, 0xC5, 0xC8, 0xCC, 0xD2, 0xD5, 0xD8, 0xD9, 0xE0, 0xE3, 0xE5, 0xE8, 0xEC, 0xF2, 0xF5, 0xF8, 0xF9})
        votes[b] = -1;
    return votes;
}();

/// Does legacy 8-bit text look like windows-1250 rather than windows-1252 (more votes for it)?
bool looksLikeCp1250(const QByteArray &data)
{
    qsizetype score = 0;
    for (const char ch : data)
        score += Cp1250Votes[static_cast<unsigned char>(ch)];
    return score > 0;
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
