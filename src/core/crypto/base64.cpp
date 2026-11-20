#include "crypto/base64.h"

namespace brulion::crypto {

namespace {

constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// The value of a Base64 character, -1 for any other.
int valueOf(char16_t c)
{
    if (c >= u'A' && c <= u'Z')
        return c - u'A';
    if (c >= u'a' && c <= u'z')
        return c - u'a' + 26;
    if (c >= u'0' && c <= u'9')
        return c - u'0' + 52;
    if (c == u'+')
        return 62;
    if (c == u'/')
        return 63;
    return -1;
}

bool isAsciiWhitespace(char16_t c)
{
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\f' || c == u'\r';
}

} // namespace

QString encodeBase64(QByteArrayView bytes)
{
    QString out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    qsizetype i = 0;
    const auto at = [&](qsizetype k) { return quint32(static_cast<unsigned char>(bytes[k])); };
    for (; i + 3 <= bytes.size(); i += 3) {
        const quint32 v = at(i) << 16 | at(i + 1) << 8 | at(i + 2);
        out.append(QLatin1Char(Alphabet[v >> 18 & 63]));
        out.append(QLatin1Char(Alphabet[v >> 12 & 63]));
        out.append(QLatin1Char(Alphabet[v >> 6 & 63]));
        out.append(QLatin1Char(Alphabet[v & 63]));
    }
    const qsizetype rest = bytes.size() - i;
    if (rest == 1) {
        const quint32 v = at(i) << 16;
        out.append(QLatin1Char(Alphabet[v >> 18 & 63]));
        out.append(QLatin1Char(Alphabet[v >> 12 & 63]));
        out.append(QStringLiteral("=="));
    } else if (rest == 2) {
        const quint32 v = at(i) << 16 | at(i + 1) << 8;
        out.append(QLatin1Char(Alphabet[v >> 18 & 63]));
        out.append(QLatin1Char(Alphabet[v >> 12 & 63]));
        out.append(QLatin1Char(Alphabet[v >> 6 & 63]));
        out.append(u'=');
    }
    return out;
}

std::optional<QByteArray> decodeBase64(QStringView text, Base64Mode mode)
{
    QString cleaned;
    QStringView input = text;
    if (mode == Base64Mode::Forgiving) {
        cleaned.reserve(text.size());
        for (const QChar c : text) {
            if (!isAsciiWhitespace(c.unicode()))
                cleaned.append(c);
        }
        input = cleaned;
        if (input.size() % 4 == 0) {
            if (input.endsWith(u"=="))
                input.chop(2);
            else if (input.endsWith(u'='))
                input.chop(1);
        }
        if (input.size() % 4 == 1)
            return std::nullopt;
    } else {
        if (input.size() % 4 != 0)
            return std::nullopt;
        if (input.endsWith(u"=="))
            input.chop(2);
        else if (input.endsWith(u'='))
            input.chop(1);
    }
    QByteArray out;
    out.reserve(input.size() * 3 / 4 + 3);
    quint32 buffer = 0;
    int bits = 0;
    for (const QChar c : input) {
        const int v = valueOf(c.unicode());
        if (v < 0)
            return std::nullopt;
        buffer = buffer << 6 | quint32(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.append(char(buffer >> bits & 0xFF));
        }
    }
    // Leftover bits of the last character: zero in canonical Base64.
    if (mode == Base64Mode::Strict && bits > 0 && (buffer & ((1u << bits) - 1)) != 0)
        return std::nullopt;
    if (mode == Base64Mode::Strict && input.size() % 4 == 1)
        return std::nullopt;
    return out;
}

} // namespace brulion::crypto
