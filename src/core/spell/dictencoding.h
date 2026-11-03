// The character encodings of Hunspell dictionaries (the `SET` line of a .aff file). Part of the
// core module "spell" — see spell/spell.h.
//
// Hunspell compares bytes in the dictionary's own encoding: a word goes in and suggestions come
// out in it. The Polish dictionary (pl_PL of LibreOffice, sjp.pl) is ISO8859-2, the English ones
// UTF-8 or ISO8859-1. Qt's QStringConverter knows only the Unicode encodings and Latin-1 (others
// only with ICU, which the AppImage, Windows and Android builds lack), so the 8-bit ones Polish
// and English dictionaries use are tables here.
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::spell {

/// True for the encodings below: UTF-8, ISO8859-1, ISO8859-2, ISO8859-15 — written as in .aff
/// files and in any letter case, with or without '-' and '_' ("ISO-8859-2", "iso8859_2", "utf8").
bool isKnownDictionaryEncoding(QByteArrayView encoding);

/// `text` in `encoding`; nothing when a character cannot be written in it (a Polish dictionary
/// cannot know "voilà": the word is not in it) or the encoding is unknown.
std::optional<QByteArray> encodeDictionaryText(QStringView text, QByteArrayView encoding);

/// `bytes` read in `encoding` (invalid UTF-8 as U+FFFD; an unknown encoding as UTF-8).
QString decodeDictionaryText(QByteArrayView bytes, QByteArrayView encoding);

} // namespace brulion::spell
