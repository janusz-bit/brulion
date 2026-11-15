// Unicode normalisation that stays linear in the length of its text.
#pragma once

#include <QString>

namespace brulion::unicode {

/// A letter with more combining marks than this — no writing system makes one (Unicode's
/// stream-safe limit is 30) — is not normalised by nfc(): it stays as written.
inline constexpr qsizetype MaxMarks = 64;

/// NFC of `text`, as QString::normalized() gives it, in time linear in the text's length.
///
/// Qt's own is quadratic twice over: it puts a run of combining marks in order by swapping
/// neighbours, and it decomposes in place, moving the rest of the text once for every character
/// it decomposes. 16 000 marks out of order after one letter took 4 s to normalise, and so did
/// 160 000 letters after a single decomposed one. A tag, a link's target, a heading or a file name
/// from a server can be exactly that, and each of them is normalised when a note is parsed —
/// at every start and at every keystroke near it.
///
/// Here the text is normalised a few hundred characters at a time, in pieces that end where no
/// composition reaches across: before a character that is neither a mark nor a Hangul vowel or
/// final consonant. The result is QString's for every text whose letters carry at most MaxMarks
/// marks; a letter with more keeps them as they are written.
QString nfc(QStringView text);
/// The same for a QString: returned as it is (shared) when it is ASCII.
QString nfc(const QString &text);

} // namespace brulion::unicode
