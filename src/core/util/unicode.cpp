#include "util/unicode.h"

#include <algorithm>

namespace brulion::unicode {

namespace {

/// Characters normalised by one call of QString::normalized(): its quadratic terms stay small.
constexpr qsizetype ChunkSize = 256;

bool isAscii(QStringView text)
{
    return std::ranges::all_of(text, [](QChar c) { return c.unicode() < 128; });
}

/// Can a piece start with `c`: no composition, decomposition or reordering reaches from the
/// text before it across it. Marks combine with what stands before them (also the vowel signs
/// with combining class 0 that some scripts compose), and so do Hangul's vowels and finals.
bool startsPiece(char32_t c)
{
    if (c < 0x300)
        return true; // (no mark below the combining diacritics)
    if ((c >= 0x1161 && c <= 0x1175) || (c >= 0x11A8 && c <= 0x11C2))
        return false;
    return QChar::combiningClass(c) == 0 && !QChar::isMark(c);
}

QString normalized(QStringView chunk)
{
    return isAscii(chunk) ? chunk.toString() : chunk.toString().normalized(QString::NormalizationForm_C);
}

QString nfcInPieces(QStringView text)
{
    QString out;
    out.reserve(text.size());
    qsizetype chunkStart = 0; // the pieces not normalised yet begin here
    qsizetype pieceStart = 0;
    // The piece [pieceStart, end) is whole: on to the chunk, or out as written when it is longer
    // than any letter with its marks.
    const auto closePiece = [&](qsizetype end) {
        if (end - pieceStart > MaxMarks) {
            out += normalized(text.sliced(chunkStart, pieceStart - chunkStart));
            out += text.sliced(pieceStart, end - pieceStart);
            chunkStart = end;
        } else if (end - chunkStart >= ChunkSize) {
            out += normalized(text.sliced(chunkStart, end - chunkStart));
            chunkStart = end;
        }
        pieceStart = end;
    };
    for (qsizetype i = 0; i < text.size();) {
        char32_t c = text[i].unicode();
        qsizetype length = 1;
        if (text[i].isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
            c = QChar::surrogateToUcs4(text[i], text[i + 1]);
            length = 2;
        }
        if (i > pieceStart && startsPiece(c))
            closePiece(i);
        i += length;
    }
    closePiece(text.size());
    out += normalized(text.sliced(chunkStart));
    return out;
}

} // namespace

QString nfc(QStringView text)
{
    if (isAscii(text))
        return text.toString();
    // Short enough for any piece of it: Qt's own, at once (every name, tag and key but the odd one).
    if (text.size() <= MaxMarks)
        return text.toString().normalized(QString::NormalizationForm_C);
    return nfcInPieces(text);
}

QString nfc(const QString &text)
{
    if (isAscii(text))
        return text;
    if (text.size() <= MaxMarks)
        return text.normalized(QString::NormalizationForm_C);
    return nfcInPieces(text);
}

} // namespace brulion::unicode
