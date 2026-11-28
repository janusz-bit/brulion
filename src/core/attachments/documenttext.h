// The text of documents for the whole-notebook search (docs/UI.md "Searching attachments"): DOCX
// and PPTX (Office Open XML), ODT (OpenDocument) and EPUB are ZIP files of XML / XHTML; this reads
// their paragraphs in reading order, never anything else — no macros, no embedded objects, no
// styles, no pictures. Part of the attachments module — see attachments/attachments.h.
//
//   DOCX  word/document.xml (the part `_rels/.rels` names as the office document) — the paragraphs
//         w:p with w:t, tabs and line breaks, text boxes, then the footnotes and endnotes; deleted
//         text of tracked changes and the fallback copies of mc:AlternateContent are left out;
//         cut into parts of about `partChars` characters at paragraph ends
//   PPTX  ppt/slides/slideN.xml in the order of ppt/presentation.xml's slide list (the slide files'
//         numbers when there is none) — one page per slide, its a:p / a:t paragraphs, then (after a
//         blank line) its speaker notes (the slide's notesSlide relationship)
//   ODT   content.xml — text:p and text:h with spaces, tabs, line breaks, footnotes and notes in
//         the margin (not the tracked changes' deleted text, not the authors and dates of
//         comments); cut into parts like DOCX
//   ODS, ODP  content.xml as ODT's: a spreadsheet's cells and a presentation's text boxes are
//         text:p paragraphs (cut into parts like DOCX; not per sheet or slide yet)
//   XLSX  xl/workbook.xml's sheets in their order (xl/worksheets/sheetN.xml by number when the list
//         is unreadable) — one page per sheet: each row a line, its cells by tabs — shared strings,
//         inline strings, numbers and formulas' cached values (not the formulas)
//   EPUB  META-INF/container.xml → the package's spine → the XHTML files in that order — one page
//         per file (one over `epubUnitChars` is cut at paragraph ends), the body's text with
//         paragraphs at block elements, without scripts, styles, the head and SVG
//
// Defensive, as notes come from anywhere: the whole file is never read into memory (the archive is
// read through the device, only the parts needed are unpacked); the archive's entries, the
// central directory, every part and the sum of all parts unpacked are capped (DocumentLimits), a
// size a part's compressed bytes cannot give (a "zip bomb": stored parts as long as they are,
// deflate at most ~1000:1) is refused before any memory is set aside, an entry that is encrypted or
// uses another compression than deflate is skipped, a password-protected ODT is locked, a file that
// is no ZIP (an encrypted DOCX is an OLE2 file) is unreadable. The XML is read with
// QXmlStreamReader: no external entity is ever read, a DOCTYPE's own entities make the part
// unreadable (no "billion laughs"). A damaged or cut part keeps the text read before the damage.
// Never throws, never crashes on malformed input.
#pragma once

#include "attachments/record.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <functional>
#include <optional>

QT_BEGIN_NAMESPACE
class QIODevice;
QT_END_NAMESPACE

namespace brulion::attachments {

enum class DocumentFormat : quint8 { Docx, Pptx, Odt, Epub, Xlsx, Ods, Odp };

/// The format a file's name says (".docx", ".pptx", ".odt", ".epub", ".xlsx", ".ods", ".odp", any
/// case); nullopt for any other name.
std::optional<DocumentFormat> documentFormatOf(QStringView path);
/// The record kind of a format's files, and back.
Kind kindOf(DocumentFormat format);
std::optional<DocumentFormat> documentFormatOf(Kind kind);
/// The kinds of the documents above (not a PDF, not a picture).
bool isDocumentKind(Kind kind);
/// The name the search results give a kind ("pdf", "picture", "presentation" — a PPTX, its pages are
/// slides —, "document": a DOCX, an ODT, an EPUB).
QString kindName(Kind kind);

struct DocumentLimits {
    /// A larger file is not read (extractDocumentText: Status::TooLarge).
    qint64 maxFileBytes = qint64(256) << 20;
    /// The archive's entries (its central directory is read whole: a hostile file naming millions
    /// of entries would take their bytes in memory).
    int maxEntries = 10'000;
    /// One part, unpacked.
    qint64 maxPartBytes = qint64(32) << 20;
    /// All the parts unpacked together.
    qint64 maxTotalBytes = qint64(256) << 20;
    /// All the text kept (UTF-16 units); the rest is cut (DocumentText::truncated).
    qsizetype maxTextChars = 16'000'000;
    /// DOCX / ODT: a part (page) of the document ends at the first paragraph end past this many
    /// characters.
    qsizetype partChars = 4000;
    /// EPUB: a file's page is cut at the first paragraph end past this many characters.
    qsizetype epubUnitChars = 30'000;
};

struct DocumentText {
    enum class Status : quint8 {
        /// Read (the pages may be none: a document without text).
        Ok,
        /// No ZIP, no readable main part, a damaged archive.
        Unreadable,
        /// Protected with a password (an ODT's encrypted parts, ZIP-encrypted entries).
        Locked,
        /// Over DocumentLimits::maxFileBytes.
        TooLarge,
    };
    Status status = Status::Unreadable;
    /// Page by page (a slide, a file of an EPUB, a part of a DOCX or an ODT); a page without text
    /// is "".
    QStringList pages;
    /// Part of the text was cut: a limit was reached.
    bool truncated = false;
};

/// The text of the file read from `device` (random access, open for reading). `cancelled` is asked
/// now and then: nullopt when it says true.
std::optional<DocumentText> extractDocumentText(QIODevice &device, DocumentFormat format,
                                                const DocumentLimits &limits = {},
                                                const std::function<bool()> &cancelled = {});

/// What a record keeps of `text`: the kind of `format`, `extractor`, a page per page (their text
/// "the file's own": PageSource::TextLayer, at most MaxPageText and MaxPages of them); locked /
/// unreadable from the status.
Record documentRecord(DocumentFormat format, const DocumentText &text, quint32 extractor);

} // namespace brulion::attachments
