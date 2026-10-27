// Decoding text files of unknown origin (imports, files shared from other apps): UTF-8 with or
// without a BOM, UTF-16 with a BOM, else 8-bit Windows text — windows-1250 when Polish letters
// dominate (old Notepad files on Polish Windows), otherwise windows-1252. Never fails: every byte
// sequence gives some text.
#pragma once

#include <QByteArray>
#include <QString>

namespace brulion::text {

/// Text of a file: UTF-8 (BOM skipped), UTF-16 LE/BE with BOM (BOM skipped), else 8-bit Windows
/// text (windows-1250 when it looks Polish, else windows-1252). Line endings are kept as they are.
QString decodeText(const QByteArray &data);

} // namespace brulion::text
