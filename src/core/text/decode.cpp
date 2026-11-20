#include "text/decode.h"

#include <QStringDecoder>

#include <algorithm>
#include <array>
#include <optional>

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

/// ISO-8859-2 bytes 0xA0–0xFF (0x80–0x9F are C1 control characters).
constexpr std::array<char16_t, 96> Iso8859_2High = {
        0x00A0, 0x0104, 0x02D8, 0x0141, 0x00A4, 0x013D, 0x015A, 0x00A7, 0x00A8, 0x0160, 0x015E, 0x0164, 0x0179, 0x00AD,
        0x017D, 0x017B, 0x00B0, 0x0105, 0x02DB, 0x0142, 0x00B4, 0x013E, 0x015B, 0x02C7, 0x00B8, 0x0161, 0x015F, 0x0165,
        0x017A, 0x02DD, 0x017E, 0x017C, 0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, 0x010C, 0x00C9,
        0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E, 0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
        0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF, 0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A,
        0x0107, 0x00E7, 0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F, 0x0111, 0x0144, 0x0148, 0x00F3,
        0x00F4, 0x0151, 0x00F6, 0x00F7, 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9};

enum class Legacy { Cp1250, Cp1252, Iso8859_2 };

QString decodeLegacyAs(const QByteArray &data, Legacy kind)
{
    QString out(data.size(), Qt::Uninitialized);
    QChar *dst = out.data();
    for (const char ch : data) {
        const auto b = static_cast<unsigned char>(ch);
        char16_t c = b;
        if (b >= 0x80) {
            switch (kind) {
            case Legacy::Cp1250: c = Cp1250High[b - 0x80]; break;
            case Legacy::Cp1252: c = b < 0xA0 ? Cp1252C1[b - 0x80] : char16_t(b); break;
            case Legacy::Iso8859_2: c = b < 0xA0 ? char16_t(b) : Iso8859_2High[b - 0xA0]; break;
            }
        }
        *dst++ = QChar(c);
    }
    return out;
}

/// 8-bit text that is not UTF-8: windows-1250 when it looks Polish (see looksLikeCp1250()),
/// else windows-1252.
QString decodeLegacy(const QByteArray &data)
{
    return decodeLegacyAs(data, looksLikeCp1250(data) ? Legacy::Cp1250 : Legacy::Cp1252);
}

/// Bytes looked at by detectEncoding().
constexpr qsizetype DetectBytes = 64 * 1024;

/// UTF-16 without a BOM: in mostly ASCII text every second byte is zero. Returns the byte order
/// or nullopt.
std::optional<TextEncoding> utf16WithoutBom(QByteArrayView head)
{
    const qsizetype pairs = head.size() / 2;
    if (pairs < 2)
        return std::nullopt;
    qsizetype evenZeros = 0, oddZeros = 0;
    for (qsizetype i = 0; i + 1 < head.size(); i += 2) {
        evenZeros += head[i] == 0;
        oddZeros += head[i + 1] == 0;
    }
    // Text in UTF-8 or an 8-bit code page has no zero bytes at all.
    if (oddZeros * 10 >= pairs * 4 && evenZeros * 20 < pairs)
        return TextEncoding::Utf16Le;
    if (evenZeros * 10 >= pairs * 4 && oddZeros * 20 < pairs)
        return TextEncoding::Utf16Be;
    return std::nullopt;
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

DetectedEncoding detectEncoding(const QByteArray &data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        return {TextEncoding::Utf8, true};
    if (data.startsWith("\xFF\xFE"))
        return {TextEncoding::Utf16Le, true};
    if (data.startsWith("\xFE\xFF"))
        return {TextEncoding::Utf16Be, true};
    const QByteArrayView head = QByteArrayView(data).first(std::min(data.size(), DetectBytes));
    if (const auto utf16 = utf16WithoutBom(head))
        return {*utf16, false};
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    // A cut in the middle of a character at the end of the sample is no error.
    QByteArrayView sample = head;
    if (head.size() < data.size()) {
        qsizetype end = head.size();
        for (int back = 0; back < 3 && end > 0 && (static_cast<unsigned char>(head[end - 1]) & 0xC0) == 0x80; ++back)
            --end;
        if (end > 0 && static_cast<unsigned char>(head[end - 1]) >= 0xC0)
            --end;
        sample = head.first(end);
    }
    const QString decoded = utf8(sample);
    if (!utf8.hasError())
        return {TextEncoding::Utf8, false};
    // ISO-8859-2 against windows-1250: their Polish letters ą ś ź (Ą Ś Ź) sit at different bytes,
    // and ISO-8859-2 has control characters where windows-1250 has ś, ź, Ś, Ź, „ ” – …
    qsizetype iso = 0, cp = 0;
    bool c1 = false;
    for (const char ch : sample) {
        const auto b = static_cast<unsigned char>(ch);
        if (b >= 0x80 && b < 0xA0)
            c1 = true;
        if (b == 0xB1 || b == 0xB6 || b == 0xBC || b == 0xA1 || b == 0xA6 || b == 0xAC)
            ++iso;
        else if (b == 0xB9 || b == 0x9C || b == 0x9F || b == 0xA5 || b == 0x8C || b == 0x8F)
            ++cp;
    }
    if (!c1 && iso > cp)
        return {TextEncoding::Iso8859_2, false};
    return {looksLikeCp1250(sample.toByteArray()) ? TextEncoding::Windows1250 : TextEncoding::Windows1252, false};
}

QString decodeTextAs(const QByteArray &data, TextEncoding encoding)
{
    switch (encoding) {
    case TextEncoding::Utf8: {
        QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
        return decoder(data.startsWith("\xEF\xBB\xBF") ? QByteArrayView(data).sliced(3) : QByteArrayView(data));
    }
    case TextEncoding::Utf16Le:
    case TextEncoding::Utf16Be: {
        const bool le = encoding == TextEncoding::Utf16Le;
        QStringDecoder decoder(le ? QStringConverter::Utf16LE : QStringConverter::Utf16BE,
                               QStringConverter::Flag::Stateless);
        const bool bom = data.startsWith(le ? "\xFF\xFE" : "\xFE\xFF");
        return decoder(bom ? QByteArrayView(data).sliced(2) : QByteArrayView(data));
    }
    case TextEncoding::Windows1250: return decodeLegacyAs(data, Legacy::Cp1250);
    case TextEncoding::Windows1252: return decodeLegacyAs(data, Legacy::Cp1252);
    case TextEncoding::Iso8859_2: return decodeLegacyAs(data, Legacy::Iso8859_2);
    }
    return decodeText(data);
}

QString encodingName(TextEncoding encoding)
{
    switch (encoding) {
    case TextEncoding::Utf8: return QStringLiteral("utf-8");
    case TextEncoding::Utf16Le: return QStringLiteral("utf-16le");
    case TextEncoding::Utf16Be: return QStringLiteral("utf-16be");
    case TextEncoding::Windows1250: return QStringLiteral("windows-1250");
    case TextEncoding::Windows1252: return QStringLiteral("windows-1252");
    case TextEncoding::Iso8859_2: return QStringLiteral("iso-8859-2");
    }
    return QStringLiteral("utf-8");
}

std::optional<TextEncoding> encodingFromName(QStringView name)
{
    QString n = name.trimmed().toString().toLower();
    n.remove(u'-').remove(u'_').remove(u' ');
    if (n == u"utf8")
        return TextEncoding::Utf8;
    if (n == u"utf16le" || n == u"utf16" || n == u"unicode")
        return TextEncoding::Utf16Le;
    if (n == u"utf16be")
        return TextEncoding::Utf16Be;
    if (n == u"windows1250" || n == u"cp1250" || n == u"win1250")
        return TextEncoding::Windows1250;
    if (n == u"windows1252" || n == u"cp1252" || n == u"win1252" || n == u"latin1" || n == u"iso88591")
        return TextEncoding::Windows1252;
    if (n == u"iso88592" || n == u"latin2" || n == u"l2")
        return TextEncoding::Iso8859_2;
    return std::nullopt;
}

} // namespace brulion::text
