// What one attachment (a PDF, a picture, a document) says, as the device's cache keeps it — one file per content
// hash (manifest.h), so a renamed or copied file is never read again. Part of the attachments
// module — see attachments/attachments.h.
//
// The file's format (all integers little-endian):
//
//   "BRAT"  u32 FormatVersion  u32 extractor  u8 kind  u8 flags (1 locked, 2 unreadable)  u16 0
//   u32 pageCount (the file's pages)  u32 storedPages (≤ MaxPages)
//   storedPages × { u32 offset (from the file's start)  u32 size  u8 source  u8 0  u16 0 }
//   u32 FNV-1a 32 of everything above
//   then each page:  u16 languages' length + UTF-8   u32 text's length + UTF-8
//                    u32 words  × { u32 start  u32 length (UTF-16 units of the text)
//                                   u16 left top right bottom (fractions of the page × 65535) }
//                    u32 FNV-1a 32 of the page's bytes before it
//
// A page is read alone (readRecordPage: the header, then its bytes); anything malformed — a wrong
// magic or checksum, a range outside the file, a word outside its text, sizes over the caps — makes
// no record (std::nullopt), never a crash or a huge allocation.
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QRectF>
#include <QString>

#include <optional>

QT_BEGIN_NAMESPACE
class QIODevice;
QT_END_NAMESPACE

namespace brulion::attachments {

/// Bump when the format above changes (an older file is then no record: read again).
inline constexpr quint32 RecordFormatVersion = 1;
/// The most pages a record keeps (a PDF's pages beyond are not searched).
inline constexpr int MaxPages = 20000;
/// The most UTF-16 units of text a page keeps (a hostile PDF's page; the rest is cut).
inline constexpr qsizetype MaxPageText = 200'000;
/// The most OCR words a page keeps.
inline constexpr qsizetype MaxPageWords = 50'000;
/// The largest record written: pages beyond it keep no text (Unread).
inline constexpr qint64 MaxRecordBytes = 1'000'000'000;

/// What a file is: a PDF, a picture, or a document read from its XML (documenttext.h: DOCX and PPTX —
/// Office Open XML —, ODT, EPUB).
enum class Kind : quint8 { Pdf = 1, Picture = 2, Docx = 3, Pptx = 4, Odt = 5, Epub = 6 };

/// Where a page's text came from.
enum class PageSource : quint8 {
    /// Not read: the page could not be opened, or the record is new.
    Unread = 0,
    /// The file's own text: a PDF's text layer, a document's paragraphs (documenttext.h).
    TextLayer = 1,
    /// Too little text of its own to be anything but a scan or a picture: OCR would read it (OCR
    /// is off, not available, or has not come to it yet). `text` keeps what little there is.
    NeedsOcr = 2,
    /// Recognised by OCR in `ocrLanguages`: `text` and its `words` with their boxes.
    Ocr = 3,
    /// OCR was tried and failed (the page could not be drawn, the engine failed): not tried again
    /// until the OCR languages change.
    OcrFailed = 4,
};

/// A word OCR recognised: a range of the page's text and its box in fractions of the page (0…1,
/// top-left origin; the page as shown — a picture as its EXIF orientation shows it).
struct Word {
    qint32 start = 0;
    qint32 length = 0;
    QRectF box;
    friend bool operator==(const Word &, const Word &) = default;
};

struct Page {
    PageSource source = PageSource::Unread;
    QString text;
    /// The OCR languages ("pol+eng") of an Ocr / OcrFailed page; "" otherwise.
    QString ocrLanguages;
    /// An Ocr page's words, in reading order (empty otherwise).
    QList<Word> words;
    friend bool operator==(const Page &, const Page &) = default;
};

struct Record {
    Kind kind = Kind::Pdf;
    /// The extracting code's version (the app's AttachmentIndexer: a newer one reads files again).
    quint32 extractor = 0;
    /// The file's pages (a picture: 1; a document: its slides, files or parts; a locked or unreadable
    /// PDF or document: 0).
    int pageCount = 0;
    /// A PDF or a document protected with a password: nothing read.
    bool locked = false;
    /// The file could not be read as what its name says.
    bool unreadable = false;
    /// The pages read (at most MaxPages; page N is pages[N - 1]).
    QList<Page> pages;
    friend bool operator==(const Record &, const Record &) = default;
};

/// What a record says without its texts (readRecordInfo).
struct RecordInfo {
    Kind kind = Kind::Pdf;
    quint32 extractor = 0;
    int pageCount = 0;
    bool locked = false;
    bool unreadable = false;
    QList<PageSource> sources;
};

QByteArray encodeRecord(const Record &record);
std::optional<Record> decodeRecord(QByteArrayView bytes);
/// Page `page` (1-based) of an encoded record.
std::optional<Page> decodeRecordPage(QByteArrayView bytes, int page);
/// The same from a file (or any random-access device) opened for reading: only the header and
/// that page are read.
std::optional<Page> readRecordPage(QIODevice &device, int page);
/// The header alone.
std::optional<RecordInfo> readRecordInfo(QIODevice &device);
std::optional<RecordInfo> decodeRecordInfo(QByteArrayView bytes);

/// A box for every UTF-16 unit of an Ocr page's text (fractions of the page): a word's box split
/// across its characters by their share of its length (OCR gives words, not letters); units between
/// words (spaces, line breaks) get an empty box — platform/pdfsearch.h's pdfRangeRects() skips them.
/// Empty for a page without words.
QList<QRectF> ocrCharBoxes(const Page &page);

/// The page's text from OCR words in reading order: words of a line joined by a space, lines by
/// "\n", blocks by "\n\n"; `words` get their ranges in it. `lines` gives each word's line and
/// `blocks` its block (as many as `texts`).
QString joinOcrWords(const QStringList &texts, const QList<int> &lines, const QList<int> &blocks, QList<Word> &words);

} // namespace brulion::attachments
