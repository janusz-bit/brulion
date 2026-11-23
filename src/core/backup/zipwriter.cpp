#include "backup/zipwriter_p.h"

#include <QDateTime>

#include <miniz.h>

#include <algorithm>
#include <cstring>

namespace brulion::backup::detail {

namespace {

constexpr quint32 LocalHeaderSignature = 0x04034b50;
constexpr quint32 CentralHeaderSignature = 0x02014b50;
constexpr quint32 EndSignature = 0x06054b50;
constexpr quint32 Zip64EndSignature = 0x06064b50;
constexpr quint32 Zip64LocatorSignature = 0x07064b50;
constexpr quint16 Zip64ExtraId = 0x0001;
constexpr quint16 Utf8NamesFlag = 0x0800;
constexpr quint16 VersionNeeded = 20;
constexpr quint16 VersionNeededZip64 = 45;
constexpr quint16 VersionMadeBy = 45; // (MS-DOS as the host: the attributes below are DOS's)
constexpr quint32 DosFolderAttribute = 0x10;
constexpr quint32 Max32 = 0xFFFFFFFFu;
constexpr quint16 Max16 = 0xFFFFu;
/// A size that needs the ZIP64 records, with room for what deflate adds to bytes it cannot shrink
/// (5 bytes per 65 535 stored in a block).
constexpr qint64 Zip64Size = qint64(Max32) - (1 << 20);
constexpr qint64 MaxEntries = 0x7FFFFFFF;

void put16(QByteArray &to, quint16 value)
{
    to.append(char(value & 0xff));
    to.append(char(value >> 8));
}

void put32(QByteArray &to, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        to.append(char((value >> (8 * i)) & 0xff));
}

void put64(QByteArray &to, quint64 value)
{
    for (int i = 0; i < 8; ++i)
        to.append(char((value >> (8 * i)) & 0xff));
}

/// DOS time and date (local time, as ZIP records them) of `mtimeMs`: 1980 … 2107 only.
void dosTime(qint64 mtimeMs, quint16 &time, quint16 &date)
{
    static const qint64 earliest = QDateTime(QDate(1980, 1, 2), QTime(0, 0)).toSecsSinceEpoch();
    static const qint64 latest = QDateTime(QDate(2107, 12, 30), QTime(0, 0)).toSecsSinceEpoch();
    const QDateTime local = QDateTime::fromSecsSinceEpoch(std::clamp(mtimeMs / 1000, earliest, latest));
    time = quint16((local.time().hour() << 11) | (local.time().minute() << 5) | (local.time().second() / 2));
    date = quint16(((local.date().year() - 1980) << 9) | (local.date().month() << 5) | local.date().day());
}

} // namespace

struct ZipWriter::Entry {
    QByteArray name;
    quint16 time = 0;
    quint16 date = 0;
    bool folder = false;
    bool deflate = false;
    bool zip64 = false; ///< its sizes are in a ZIP64 record
    qint64 size = 0;    ///< the uncompressed size: promised by beginFile()
    qint64 headerOffset = 0;
    qint64 written = 0; ///< uncompressed bytes handed over
    qint64 compressed = 0;
    quint32 crc = 0; // (MZ_CRC32_INIT)
    std::unique_ptr<tdefl_compressor> compressor;
};

/// Where the compressor's output goes.
struct DeflateSink {
    static mz_bool put(const void *buffer, int length, void *user)
    {
        auto *writer = static_cast<ZipWriter *>(user);
        writer->m_entry->compressed += length;
        return writer->put(static_cast<const char *>(buffer), length);
    }
};

ZipWriter::ZipWriter(QIODevice &out, const Options &options) : m_out(out), m_options(options) { }

ZipWriter::~ZipWriter() = default;

bool ZipWriter::fail(Error error)
{
    if (m_error == Error::None)
        m_error = error;
    return false;
}

bool ZipWriter::put(const char *data, qsizetype n)
{
    if (m_error != Error::None)
        return false;
    if (m_options.maxBytes > 0 && m_offset + n > m_options.maxBytes)
        return fail(Error::TooLarge);
    if (m_out.pos() != m_offset && !m_out.seek(m_offset))
        return fail(Error::WriteFailed);
    if (m_out.write(data, n) != n)
        return fail(Error::WriteFailed);
    m_offset += n;
    return true;
}

bool ZipWriter::putAt(qint64 offset, const QByteArray &data)
{
    if (m_error != Error::None)
        return false;
    if (!m_out.seek(offset) || m_out.write(data) != data.size())
        return fail(Error::WriteFailed);
    return true;
}

QByteArray ZipWriter::localHeader(bool final) const
{
    const Entry &e = *m_entry;
    // The first version of a deflated entry's header cannot hold its sizes yet; a stored entry's
    // sizes are known, its checksum is not. The final version is written when the entry ends.
    const quint64 uncompressed = e.deflate && !final ? 0 : quint64(e.size);
    const quint64 compressed = e.deflate ? (final ? quint64(e.compressed) : 0) : quint64(e.size);
    QByteArray extra;
    if (e.zip64) {
        put16(extra, Zip64ExtraId);
        put16(extra, 16);
        put64(extra, uncompressed);
        put64(extra, compressed);
    }
    QByteArray header;
    put32(header, LocalHeaderSignature);
    put16(header, e.zip64 ? VersionNeededZip64 : VersionNeeded);
    put16(header, Utf8NamesFlag);
    put16(header, e.deflate ? MZ_DEFLATED : 0);
    put16(header, e.time);
    put16(header, e.date);
    put32(header, final ? e.crc : 0);
    put32(header, e.zip64 ? Max32 : quint32(compressed));
    put32(header, e.zip64 ? Max32 : quint32(uncompressed));
    put16(header, quint16(e.name.size()));
    put16(header, quint16(extra.size()));
    header += e.name;
    header += extra;
    return header;
}

bool ZipWriter::begin(const QByteArray &name, qint64 mtimeMs, qint64 size, int level, bool folder)
{
    if (m_error != Error::None)
        return false;
    if (m_open || m_finished || name.isEmpty() || name.size() > Max16 || size < 0 || m_entries >= MaxEntries
        || level < 0 || level > MZ_UBER_COMPRESSION)
        return fail(Error::Misuse);
    m_entry = std::make_unique<Entry>();
    Entry &e = *m_entry;
    e.name = name;
    e.folder = folder;
    e.size = size;
    e.headerOffset = m_offset;
    e.deflate = !folder && level > 0 && size > 3;
    e.zip64 = m_options.forceZip64 || size >= Zip64Size;
    dosTime(mtimeMs, e.time, e.date);
    if (e.deflate) {
        e.compressor.reset(new tdefl_compressor);
        if (tdefl_init(e.compressor.get(), &DeflateSink::put, this,
                       tdefl_create_comp_flags_from_zip_params(level, -15, MZ_DEFAULT_STRATEGY))
            != TDEFL_STATUS_OKAY)
            return fail(Error::Compression);
    }
    const QByteArray header = localHeader(false);
    m_open = true;
    return put(header.constData(), header.size());
}

bool ZipWriter::addFolder(const QByteArray &utf8Name, qint64 mtimeMs)
{
    QByteArray name = utf8Name;
    if (!name.endsWith('/'))
        name.append('/');
    if (!begin(name, mtimeMs, 0, 0, true))
        return false;
    return endFile();
}

bool ZipWriter::beginFile(const QByteArray &utf8Name, qint64 mtimeMs, qint64 size, int level)
{
    return begin(utf8Name, mtimeMs, size, level, false);
}

bool ZipWriter::write(const char *data, qsizetype n)
{
    if (!m_open || m_error != Error::None)
        return false;
    Entry &e = *m_entry;
    if (n < 0 || e.written + n > e.size)
        return fail(Error::Misuse);
    if (n == 0)
        return true;
    e.crc = quint32(mz_crc32(e.crc, reinterpret_cast<const unsigned char *>(data), size_t(n)));
    e.written += n;
    if (!e.deflate) {
        e.compressed += n;
        return put(data, n);
    }
    const tdefl_status status = tdefl_compress_buffer(e.compressor.get(), data, size_t(n), TDEFL_NO_FLUSH);
    if (status != TDEFL_STATUS_OKAY)
        return m_error != Error::None ? false : fail(Error::Compression);
    return true;
}

bool ZipWriter::endFile()
{
    if (!m_open || m_error != Error::None)
        return false;
    Entry &e = *m_entry;
    if (e.written != e.size)
        return fail(Error::Misuse);
    if (e.deflate) {
        static const char none[1] = {0};
        if (tdefl_compress_buffer(e.compressor.get(), none, 0, TDEFL_FINISH) != TDEFL_STATUS_DONE)
            return m_error != Error::None ? false : fail(Error::Compression);
        e.compressor.reset();
        // (A compressed size above 4 GiB has its ZIP64 record from the start: see begin().)
        if (!e.zip64 && e.compressed > qint64(Max32))
            return fail(Error::TooLarge);
    }
    if (e.size > 0 && !putAt(e.headerOffset, localHeader(true)))
        return false;
    recordEntry();
    m_open = false;
    m_entry.reset();
    return true;
}

void ZipWriter::abandonFile()
{
    if (!m_open)
        return;
    m_offset = m_entry->headerOffset;
    m_open = false;
    m_entry.reset();
}

void ZipWriter::recordEntry()
{
    const Entry &e = *m_entry;
    const bool bigOffset = e.headerOffset >= qint64(Max32);
    QByteArray extra;
    if (e.zip64 || bigOffset) {
        QByteArray fields;
        if (e.zip64) {
            put64(fields, quint64(e.size));
            put64(fields, quint64(e.compressed));
        }
        if (bigOffset)
            put64(fields, quint64(e.headerOffset));
        put16(extra, Zip64ExtraId);
        put16(extra, quint16(fields.size()));
        extra += fields;
    }
    QByteArray &d = m_directory;
    put32(d, CentralHeaderSignature);
    put16(d, VersionMadeBy);
    put16(d, (e.zip64 || bigOffset) ? VersionNeededZip64 : VersionNeeded);
    put16(d, Utf8NamesFlag);
    put16(d, e.deflate ? MZ_DEFLATED : 0);
    put16(d, e.time);
    put16(d, e.date);
    put32(d, e.crc);
    put32(d, e.zip64 ? Max32 : quint32(e.compressed));
    put32(d, e.zip64 ? Max32 : quint32(e.size));
    put16(d, quint16(e.name.size()));
    put16(d, quint16(extra.size()));
    put16(d, 0); // no comment
    put16(d, 0); // the disk
    put16(d, 0); // internal attributes
    put32(d, e.folder ? DosFolderAttribute : 0);
    put32(d, bigOffset ? Max32 : quint32(e.headerOffset));
    d += e.name;
    d += extra;
    ++m_entries;
}

bool ZipWriter::finish(const QByteArray &comment)
{
    if (m_open || m_finished || m_error != Error::None || comment.size() > Max16)
        return fail(Error::Misuse);
    const qint64 directoryOffset = m_offset;
    const qint64 directorySize = m_directory.size();
    if (!put(m_directory.constData(), m_directory.size()))
        return false;
    m_directory.clear();
    const bool zip64 = m_options.forceZip64 || m_entries >= Max16 || directoryOffset >= qint64(Max32)
            || directorySize >= qint64(Max32);
    if (zip64) {
        const qint64 recordOffset = m_offset;
        QByteArray record;
        put32(record, Zip64EndSignature);
        put64(record, 44); // the rest of this record
        put16(record, VersionMadeBy);
        put16(record, VersionNeededZip64);
        put32(record, 0);
        put32(record, 0);
        put64(record, quint64(m_entries));
        put64(record, quint64(m_entries));
        put64(record, quint64(directorySize));
        put64(record, quint64(directoryOffset));
        put32(record, Zip64LocatorSignature);
        put32(record, 0);
        put64(record, quint64(recordOffset));
        put32(record, 1);
        if (!put(record.constData(), record.size()))
            return false;
    }
    QByteArray end;
    put32(end, EndSignature);
    put16(end, 0);
    put16(end, 0);
    put16(end, zip64 ? Max16 : quint16(m_entries));
    put16(end, zip64 ? Max16 : quint16(m_entries));
    put32(end, zip64 ? Max32 : quint32(directorySize));
    put32(end, zip64 ? Max32 : quint32(directoryOffset));
    put16(end, quint16(comment.size()));
    end += comment;
    if (!put(end.constData(), end.size()))
        return false;
    m_finished = true;
    return true;
}

} // namespace brulion::backup::detail
