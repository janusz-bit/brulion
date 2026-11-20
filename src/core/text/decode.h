// Decoding text files of unknown origin (imports, files shared from other apps): UTF-8 with or
// without a BOM, UTF-16 with a BOM, else 8-bit Windows text — windows-1250 when Polish letters
// dominate (old Notepad files on Polish Windows), otherwise windows-1252. Never fails: every byte
// sequence gives some text.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::text {

/// Text of a file: UTF-8 (BOM skipped), UTF-16 LE/BE with BOM (BOM skipped), else 8-bit Windows
/// text (windows-1250 when it looks Polish, else windows-1252). Line endings are kept as they are.
QString decodeText(const QByteArray &data);

/// The encodings a user can choose for an imported text (the CSV import's mapping).
enum class TextEncoding {
    Utf8,
    Utf16Le,
    Utf16Be,
    Windows1250, ///< Polish (Central European) Windows, old Excel exports
    Windows1252, ///< Western Windows
    Iso8859_2,   ///< Latin-2 (old Unix and web exports)
};

/// What detectEncoding() found.
struct DetectedEncoding {
    TextEncoding encoding = TextEncoding::Utf8;
    bool bom = false; ///< a byte order mark starts the data
};

/// The encoding of `data` (looks at the first 64 KiB): a BOM decides; UTF-16 without a BOM by its
/// zero bytes (mostly ASCII text: every second byte zero); valid UTF-8; else 8-bit text —
/// ISO-8859-2 when its Polish letters (ą ś ź: 0xB1 0xB6 0xBC) outnumber windows-1250's (0xB9 0x9C
/// 0x9F) and no byte 0x80–0x9F (control characters in ISO-8859-2) occurs, windows-1250 when it
/// looks Polish, else windows-1252.
DetectedEncoding detectEncoding(const QByteArray &data);

/// `data` decoded as `encoding` (a BOM of that encoding skipped). Invalid UTF-8 / UTF-16 gives
/// U+FFFD for the bad bytes; never fails.
QString decodeTextAs(const QByteArray &data, TextEncoding encoding);

/// "utf-8", "utf-16le", "utf-16be", "windows-1250", "windows-1252", "iso-8859-2".
QString encodingName(TextEncoding encoding);
/// The encoding a name means (case-insensitive, also "utf8", "cp1250", "latin2", "iso8859-2");
/// nullopt when unknown.
std::optional<TextEncoding> encodingFromName(QStringView name);

} // namespace brulion::text
