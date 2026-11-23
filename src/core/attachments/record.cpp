#include "attachments/record.h"

#include "attachments/codec_p.h"

#include <QIODevice>

#include <algorithm>
#include <cmath>

namespace brulion::attachments {

using namespace codec;

namespace {

constexpr char Magic[4] = {'B', 'R', 'A', 'T'};
/// magic, format, extractor, kind/flags/reserved, pageCount, storedPages.
constexpr qsizetype FixedHeader = 4 + 4 + 4 + 4 + 4 + 4;
constexpr qsizetype TableEntry = 4 + 4 + 4;
constexpr quint8 FlagLocked = 1;
constexpr quint8 FlagUnreadable = 2;
/// A page's bytes at most: its text in UTF-8 (≤ 3 bytes a unit), its words, the rest.
constexpr qint64 MaxPageBytes = MaxPageText * 3 + MaxPageWords * 16 + 1024;

quint16 toFraction(qreal v)
{
    if (!std::isfinite(v))
        return 0;
    return quint16(std::lround(std::clamp(v, 0.0, 1.0) * 65535.0));
}

qreal fromFraction(quint16 v)
{
    return qreal(v) / 65535.0;
}

bool knownSource(quint8 s)
{
    return s <= quint8(PageSource::OcrFailed);
}

QByteArray encodePage(const Page &page)
{
    QByteArray out;
    const QString text = page.text.left(MaxPageText);
    const QByteArray languages = page.ocrLanguages.left(64).toUtf8();
    const QByteArray utf8 = text.toUtf8();
    putU16(out, quint16(languages.size()));
    out.append(languages);
    putU32(out, quint32(utf8.size()));
    out.append(utf8);
    QList<Word> words;
    words.reserve(std::min(page.words.size(), MaxPageWords));
    for (const Word &w : page.words) {
        if (words.size() >= MaxPageWords)
            break;
        // A word inside the text (cut words left out).
        if (w.start < 0 || w.length <= 0 || qsizetype(w.start) + w.length > text.size())
            continue;
        words.append(w);
    }
    putU32(out, quint32(words.size()));
    for (const Word &w : std::as_const(words)) {
        putU32(out, quint32(w.start));
        putU32(out, quint32(w.length));
        putU16(out, toFraction(w.box.left()));
        putU16(out, toFraction(w.box.top()));
        putU16(out, toFraction(w.box.right()));
        putU16(out, toFraction(w.box.bottom()));
    }
    putU32(out, fnv1a32(out));
    return out;
}

std::optional<Page> decodePage(QByteArrayView bytes, PageSource source)
{
    if (bytes.size() < 4)
        return std::nullopt;
    const QByteArrayView body = bytes.first(bytes.size() - 4);
    Reader check(bytes.sliced(bytes.size() - 4));
    if (check.u32() != fnv1a32(body))
        return std::nullopt;
    Reader r(body);
    Page page;
    page.source = source;
    const quint16 languages = r.u16();
    page.ocrLanguages = QString::fromUtf8(r.bytes(languages));
    const quint32 textBytes = r.u32();
    if (!r.ok() || textBytes > quint32(MaxPageText * 3))
        return std::nullopt;
    page.text = QString::fromUtf8(r.bytes(textBytes));
    const quint32 words = r.u32();
    if (!r.ok() || words > quint32(MaxPageWords) || qint64(words) * 16 > r.remaining())
        return std::nullopt;
    page.words.reserve(words);
    for (quint32 i = 0; i < words; ++i) {
        Word w;
        w.start = qint32(r.u32());
        w.length = qint32(r.u32());
        const qreal left = fromFraction(r.u16());
        const qreal top = fromFraction(r.u16());
        const qreal right = fromFraction(r.u16());
        const qreal bottom = fromFraction(r.u16());
        if (!r.ok() || w.start < 0 || w.length <= 0 || qsizetype(w.start) + w.length > page.text.size())
            return std::nullopt;
        w.box = QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
        page.words.append(w);
    }
    if (!r.ok() || r.remaining() != 0)
        return std::nullopt;
    return page;
}

struct Header {
    RecordInfo info;
    struct Entry {
        quint32 offset = 0;
        quint32 size = 0;
    };
    QList<Entry> entries;
    qsizetype size = 0; ///< the header's bytes (with its checksum)
};

/// The fixed part's fields; nullopt when they cannot be a record's.
std::optional<Header> decodeFixed(QByteArrayView bytes)
{
    if (bytes.size() < FixedHeader || !std::equal(Magic, Magic + 4, bytes.data()))
        return std::nullopt;
    Reader r(bytes.sliced(4));
    if (r.u32() != RecordFormatVersion)
        return std::nullopt;
    Header h;
    h.info.extractor = r.u32();
    const quint8 kind = r.u8();
    const quint8 flags = r.u8();
    r.u16();
    const quint32 pageCount = r.u32();
    const quint32 stored = r.u32();
    if (!r.ok() || (kind < quint8(Kind::Pdf) || kind > quint8(Kind::Epub)) || stored > quint32(MaxPages)
        || pageCount > 100'000'000u || stored > pageCount)
        return std::nullopt;
    h.info.kind = Kind(kind);
    h.info.locked = flags & FlagLocked;
    h.info.unreadable = flags & FlagUnreadable;
    h.info.pageCount = int(pageCount);
    h.entries.resize(stored);
    h.info.sources.resize(stored);
    h.size = FixedHeader + qsizetype(stored) * TableEntry + 4;
    return h;
}

/// The page table and the checksum (`bytes`: at least the whole header).
bool decodeTable(QByteArrayView bytes, Header &h, qint64 fileSize)
{
    if (bytes.size() < h.size)
        return false;
    Reader check(bytes.sliced(h.size - 4, 4));
    if (check.u32() != fnv1a32(bytes.first(h.size - 4)))
        return false;
    Reader r(bytes.sliced(FixedHeader, h.size - 4 - FixedHeader));
    for (qsizetype i = 0; i < h.entries.size(); ++i) {
        Header::Entry &e = h.entries[i];
        e.offset = r.u32();
        e.size = r.u32();
        const quint8 source = r.u8();
        r.u8();
        r.u16();
        if (!r.ok() || !knownSource(source) || e.offset < quint32(h.size) || e.size > MaxPageBytes
            || qint64(e.offset) + e.size > fileSize)
            return false;
        h.info.sources[i] = PageSource(source);
    }
    return r.ok();
}

std::optional<Header> decodeHeader(QByteArrayView bytes)
{
    std::optional<Header> h = decodeFixed(bytes);
    if (!h || !decodeTable(bytes, *h, bytes.size()))
        return std::nullopt;
    return h;
}

/// Read the header from a device (its start).
std::optional<Header> readHeader(QIODevice &device)
{
    if (!device.isReadable() || !device.seek(0))
        return std::nullopt;
    const QByteArray fixed = device.read(FixedHeader);
    std::optional<Header> h = decodeFixed(fixed);
    if (!h)
        return std::nullopt;
    QByteArray all = fixed;
    all.append(device.read(h->size - FixedHeader));
    if (!decodeTable(all, *h, device.size()))
        return std::nullopt;
    return h;
}

} // namespace

QByteArray encodeRecord(const Record &record)
{
    const qsizetype stored = std::min<qsizetype>(record.pages.size(), MaxPages);
    QList<QByteArray> pages;
    QList<bool> cut;
    pages.reserve(stored);
    cut.reserve(stored);
    qint64 total = 0;
    for (qsizetype i = 0; i < stored; ++i) {
        // Past MaxRecordBytes (offsets are 32-bit) a page keeps no text: read as never read.
        QByteArray page = encodePage(record.pages.at(i));
        const bool over = total + page.size() > MaxRecordBytes;
        if (over)
            page = encodePage(Page());
        total += page.size();
        pages.append(std::move(page));
        cut.append(over);
    }
    QByteArray out;
    out.append(Magic, 4);
    putU32(out, RecordFormatVersion);
    putU32(out, record.extractor);
    putU8(out, quint8(record.kind));
    putU8(out, quint8((record.locked ? FlagLocked : 0) | (record.unreadable ? FlagUnreadable : 0)));
    putU16(out, 0);
    putU32(out, quint32(std::max<qsizetype>(record.pageCount, stored)));
    putU32(out, quint32(stored));
    const qsizetype headerSize = FixedHeader + stored * TableEntry + 4;
    qint64 offset = headerSize;
    for (qsizetype i = 0; i < stored; ++i) {
        putU32(out, quint32(offset));
        putU32(out, quint32(pages.at(i).size()));
        putU8(out, quint8(cut.at(i) ? PageSource::Unread : record.pages.at(i).source));
        putU8(out, 0);
        putU16(out, 0);
        offset += pages.at(i).size();
    }
    putU32(out, fnv1a32(out));
    for (const QByteArray &page : std::as_const(pages))
        out.append(page);
    return out;
}

std::optional<Record> decodeRecord(QByteArrayView bytes)
{
    const std::optional<Header> h = decodeHeader(bytes);
    if (!h)
        return std::nullopt;
    Record record;
    record.kind = h->info.kind;
    record.extractor = h->info.extractor;
    record.pageCount = h->info.pageCount;
    record.locked = h->info.locked;
    record.unreadable = h->info.unreadable;
    record.pages.reserve(h->entries.size());
    for (qsizetype i = 0; i < h->entries.size(); ++i) {
        const Header::Entry &e = h->entries.at(i);
        std::optional<Page> page = decodePage(bytes.sliced(e.offset, e.size), h->info.sources.at(i));
        if (!page)
            return std::nullopt;
        record.pages.append(std::move(*page));
    }
    return record;
}

std::optional<Page> decodeRecordPage(QByteArrayView bytes, int page)
{
    const std::optional<Header> h = decodeHeader(bytes);
    if (!h || page < 1 || page > h->entries.size())
        return std::nullopt;
    const Header::Entry &e = h->entries.at(page - 1);
    return decodePage(bytes.sliced(e.offset, e.size), h->info.sources.at(page - 1));
}

std::optional<Page> readRecordPage(QIODevice &device, int page)
{
    const std::optional<Header> h = readHeader(device);
    if (!h || page < 1 || page > h->entries.size())
        return std::nullopt;
    const Header::Entry &e = h->entries.at(page - 1);
    if (!device.seek(e.offset))
        return std::nullopt;
    const QByteArray bytes = device.read(e.size);
    if (bytes.size() != qsizetype(e.size))
        return std::nullopt;
    return decodePage(bytes, h->info.sources.at(page - 1));
}

std::optional<RecordInfo> readRecordInfo(QIODevice &device)
{
    std::optional<Header> h = readHeader(device);
    return h ? std::optional<RecordInfo>(std::move(h->info)) : std::nullopt;
}

std::optional<RecordInfo> decodeRecordInfo(QByteArrayView bytes)
{
    std::optional<Header> h = decodeHeader(bytes);
    return h ? std::optional<RecordInfo>(std::move(h->info)) : std::nullopt;
}

QList<QRectF> ocrCharBoxes(const Page &page)
{
    QList<QRectF> out;
    if (page.words.isEmpty())
        return out;
    out.resize(page.text.size());
    for (const Word &w : page.words) {
        if (w.start < 0 || w.length <= 0 || qsizetype(w.start) + w.length > page.text.size())
            continue;
        const qreal step = w.box.width() / w.length;
        for (qint32 i = 0; i < w.length; ++i)
            out[w.start + i] = QRectF(w.box.left() + step * i, w.box.top(), step, w.box.height());
    }
    return out;
}

QString joinOcrWords(const QStringList &texts, const QList<int> &lines, const QList<int> &blocks, QList<Word> &words)
{
    QString out;
    const qsizetype n = std::min({texts.size(), lines.size(), blocks.size(), words.size()});
    words.resize(n);
    for (qsizetype i = 0; i < n; ++i) {
        if (i > 0) {
            if (blocks.at(i) != blocks.at(i - 1))
                out.append(u"\n\n");
            else if (lines.at(i) != lines.at(i - 1))
                out.append(u'\n');
            else
                out.append(u' ');
        }
        words[i].start = qint32(out.size());
        words[i].length = qint32(texts.at(i).size());
        out.append(texts.at(i));
    }
    // Empty words (OCR gives some) take no room: dropped.
    words.removeIf([](const Word &w) { return w.length <= 0; });
    return out;
}

} // namespace brulion::attachments
