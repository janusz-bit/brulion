#include "externalfiles.h"

#include <QStringDecoder>

#include <array>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

enum class Check { Pdf, Ooxml, OpenDocument, Epub, Ole2, Rtf, Mp3, IsoMedia, Ogg, Wav, Flac, Matroska, Text };

struct Entry {
    const char *extension;
    const char *mimeType;
    const char *kind;
    Check check;
    /// Ooxml: the folder of its main part ("word/"); OpenDocument: its own MIME type.
    const char *detail = nullptr;
};

// clang-format off
constexpr std::array entries{
    Entry{"pdf", "application/pdf", "pdf", Check::Pdf},
    Entry{"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document", "document", Check::Ooxml, "word/"},
    Entry{"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "spreadsheet", Check::Ooxml, "xl/"},
    Entry{"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation", "presentation", Check::Ooxml, "ppt/"},
    Entry{"odt", "application/vnd.oasis.opendocument.text", "document", Check::OpenDocument, "application/vnd.oasis.opendocument.text"},
    Entry{"ods", "application/vnd.oasis.opendocument.spreadsheet", "spreadsheet", Check::OpenDocument, "application/vnd.oasis.opendocument.spreadsheet"},
    Entry{"odp", "application/vnd.oasis.opendocument.presentation", "presentation", Check::OpenDocument, "application/vnd.oasis.opendocument.presentation"},
    Entry{"epub", "application/epub+zip", "ebook", Check::Epub},
    Entry{"doc", "application/msword", "document", Check::Ole2},
    Entry{"xls", "application/vnd.ms-excel", "spreadsheet", Check::Ole2},
    Entry{"ppt", "application/vnd.ms-powerpoint", "presentation", Check::Ole2},
    Entry{"rtf", "application/rtf", "document", Check::Rtf},
    Entry{"mp3", "audio/mpeg", "audio", Check::Mp3},
    Entry{"m4a", "audio/mp4", "audio", Check::IsoMedia},
    Entry{"ogg", "audio/ogg", "audio", Check::Ogg},
    Entry{"oga", "audio/ogg", "audio", Check::Ogg},
    Entry{"opus", "audio/ogg", "audio", Check::Ogg},
    Entry{"wav", "audio/wav", "audio", Check::Wav},
    Entry{"flac", "audio/flac", "audio", Check::Flac},
    Entry{"mp4", "video/mp4", "video", Check::IsoMedia},
    Entry{"m4v", "video/mp4", "video", Check::IsoMedia},
    Entry{"mov", "video/quicktime", "video", Check::IsoMedia},
    Entry{"webm", "video/webm", "video", Check::Matroska},
    Entry{"mkv", "video/x-matroska", "video", Check::Matroska},
    Entry{"txt", "text/plain", "text", Check::Text},
    Entry{"csv", "text/csv", "spreadsheet", Check::Text},
    Entry{"tsv", "text/tab-separated-values", "spreadsheet", Check::Text},
};
// clang-format on

bool startsWith(QByteArrayView head, QByteArrayView prefix, qsizetype at = 0)
{
    return head.size() >= at + prefix.size() && head.sliced(at, prefix.size()) == prefix;
}

constexpr QByteArrayView ZipLocal("PK\x03\x04", 4);

/// A zip whose first entry is "mimetype", stored, holding `mimeType` (OpenDocument, EPUB).
bool zipNamesItself(QByteArrayView head, QByteArrayView mimeType)
{
    return startsWith(head, ZipLocal) && startsWith(head, "mimetype", 30) && startsWith(head, mimeType, 38);
}

bool matches(const Entry &entry, QByteArrayView head)
{
    switch (entry.check) {
    case Check::Pdf: return head.first(std::min<qsizetype>(head.size(), 1024)).indexOf("%PDF-") >= 0;
    case Check::Ooxml:
        // A zip with its content types and its main part's folder among its first entries.
        return startsWith(head, ZipLocal) && head.indexOf("[Content_Types].xml") >= 0
               && head.indexOf(QByteArrayView(entry.detail)) >= 0;
    case Check::OpenDocument: return zipNamesItself(head, QByteArrayView(entry.detail));
    case Check::Epub: return zipNamesItself(head, "application/epub+zip");
    case Check::Ole2: return startsWith(head, QByteArrayView("\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1", 8));
    case Check::Rtf: return startsWith(head, "{\\rtf");
    case Check::Mp3:
        // An ID3 tag, or an MPEG audio frame's sync (11 bits set).
        return startsWith(head, "ID3")
               || (head.size() >= 2 && quint8(head.at(0)) == 0xff && (quint8(head.at(1)) & 0xe0) == 0xe0);
    case Check::IsoMedia: return startsWith(head, "ftyp", 4);
    case Check::Ogg: return startsWith(head, "OggS");
    case Check::Wav: return startsWith(head, "RIFF") && startsWith(head, "WAVE", 8);
    case Check::Flac: return startsWith(head, "fLaC");
    case Check::Matroska: return startsWith(head, QByteArrayView("\x1a\x45\xdf\xa3", 4));
    case Check::Text: {
        if (startsWith(head, QByteArrayView("\xff\xfe", 2)) || startsWith(head, QByteArrayView("\xfe\xff", 2)))
            return true; // UTF-16 with its byte order mark (NUL bytes in every ASCII character)
        if (head.contains('\0'))
            return false;
        // Valid UTF-8 (a stateful decoder: a character cut by the head's end is no error).
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString decoded = decoder.decode(head);
        Q_UNUSED(decoded);
        return !decoder.hasError();
    }
    }
    return false;
}

} // namespace

ExternalFileType externalFileType(QStringView fileName, QByteArrayView head)
{
    const qsizetype dot = fileName.lastIndexOf(u'.');
    if (dot <= 0 || dot + 1 >= fileName.size())
        return {};
    const QString extension = fileName.sliced(dot + 1).toString().toLower();
    head = head.first(std::min(head.size(), ExternalFileHeadBytes));
    for (const Entry &entry : entries) {
        if (extension != QLatin1StringView(entry.extension))
            continue;
        if (!matches(entry, head))
            return {};
        return {QString::fromLatin1(entry.mimeType), QString::fromLatin1(entry.kind)};
    }
    return {};
}

} // namespace brulion::platform
