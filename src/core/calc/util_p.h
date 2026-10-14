// Internal helpers of the calc module (not part of its public API).
#pragma once

#include <QChar>
#include <QString>
#include <QStringView>

namespace brulion::calc::detail {

/// NFC form of `s`. Text without characters from U+0300 up is already NFC (fast path).
inline QString toNfc(QStringView s)
{
    for (const QChar c : s) {
        if (c.unicode() >= 0x0300)
            return s.toString().normalized(QString::NormalizationForm_C);
    }
    return s.toString();
}

inline bool isAsciiDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

/// First character of an identifier: a Unicode letter or '_'.
inline bool isIdentStart(char32_t c)
{
    return c == U'_' || QChar::isLetter(c);
}

/// Following characters: letters, combining marks, decimal digits, '_'.
inline bool isIdentPart(char32_t c)
{
    return isIdentStart(c) || QChar::isMark(c) || QChar::isDigit(c);
}

/// Code point at `i` (combining a surrogate pair) and its length in UTF-16 units.
inline char32_t codePointAt(QStringView s, qsizetype i, qsizetype *length)
{
    const QChar c = s.at(i);
    if (c.isHighSurrogate() && i + 1 < s.size() && s.at(i + 1).isLowSurrogate()) {
        *length = 2;
        return QChar::surrogateToUcs4(c, s.at(i + 1));
    }
    *length = 1;
    return c.unicode();
}

/// A valid identifier: `[\p{L}_][\p{L}\p{M}\p{Nd}_]*` (after NFC normalisation).
bool isIdentifier(QStringView name);

/// Turn an arbitrary key ("due-date", "cena netto", "2026") into an identifier ("due_date",
/// "cena_netto", "_2026"); empty when nothing usable remains.
QString sanitizeIdentifier(QStringView key);

} // namespace brulion::calc::detail
