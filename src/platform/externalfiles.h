// Which attachments of the notebook may be handed to another app (docs/UI.md "PDF viewer" → "Other
// attachments": "Open in another app"; openexternal.h hands them over). Pure, so tested on every
// platform (tests/platform/tst_externalfiles.cpp).
//
// An allow-list: a file goes out only when its extension names a document, a spreadsheet, a
// presentation, an e-book, audio, video or plain text AND its first bytes look like that format —
// so a program, a script, a shortcut or an HTML page renamed to `.docx` is never given to
// xdg-open / ShellExecute (which may pick an app by the content or run it). Pictures, PDFs and notes
// open in Brulion itself; PDFs are on the list too (the PDF viewer's own button).
#pragma once

#include <QByteArrayView>
#include <QString>
#include <QStringView>

namespace brulion::platform {

/// How many of a file's first bytes externalFileType() wants (an office file's zip entries).
inline constexpr qsizetype ExternalFileHeadBytes = 64 * 1024;

/// What externalFileType() found.
struct ExternalFileType {
    /// The MIME type the file goes out as ("" — not on the list, or its bytes do not match).
    QString mimeType;
    /// "pdf", "document", "spreadsheet", "presentation", "ebook", "audio", "video", "text" (the
    /// messages' wording); "" when refused.
    QString kind;
    bool allowed() const { return !mimeType.isEmpty(); }
};

/// The type `fileName` (its extension, any case) goes out as, checked against `head` — the file's
/// first bytes (at most ExternalFileHeadBytes are looked at): PDF ("%PDF-" in the first 1024
/// bytes), Office Open XML (a zip with its parts), OpenDocument and EPUB (a zip whose first entry
/// names its own MIME type), the old Office formats (OLE2), RTF, MP3, M4A / MP4 / MOV, Ogg, WAV,
/// FLAC, WebM / MKV, and text (TXT, CSV, TSV: no NUL byte, valid UTF-8 or a byte order mark).
ExternalFileType externalFileType(QStringView fileName, QByteArrayView head);

} // namespace brulion::platform
