#include "io/zip.h"

#include "io/messages_p.h"
#include "util/unicode.h"

#include <QDir>
#include <QFile>
#include <QMutex>
#include <QScopeGuard>
#include <QSet>
#include <QStringDecoder>
#include <QStringTokenizer>
#include <QTemporaryFile>

#include <miniz.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string_view>

namespace brulion::io {

using namespace Qt::StringLiterals;
using detail::IoMessages;

namespace {

/// Code page 437 (the ZIP default for names without the UTF-8 flag), bytes 0x80–0xFF.
constexpr std::u16string_view Cp437High =
        u"ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀"
        u"αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ";
static_assert(Cp437High.size() == 128);

constexpr mz_uint16 Utf8NameFlag = 1u << 11;
/// More than any deflate stream can expand (about 1032:1), with room for headers.
constexpr quint64 MaxDeflateRatio = 1100;

QString decodeCp437(const QByteArray &raw)
{
    QString out;
    out.reserve(raw.size());
    for (const char c : raw) {
        const auto b = static_cast<unsigned char>(c);
        out += b < 0x80 ? char16_t(b) : Cp437High[b - 0x80];
    }
    return out;
}

QString decodeName(const QByteArray &raw, bool utf8Flag)
{
    if (utf8Flag)
        return QString::fromUtf8(raw);
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = decoder(raw);
    return decoder.hasError() ? decodeCp437(raw) : text;
}

QString entryName(mz_zip_archive *archive, mz_uint index, const mz_zip_archive_file_stat &stat)
{
    const mz_uint size = mz_zip_reader_get_filename(archive, index, nullptr, 0);
    if (size <= 1)
        return {};
    QByteArray raw(qsizetype(size), '\0');
    mz_zip_reader_get_filename(archive, index, raw.data(), size);
    raw.truncate(qsizetype(size) - 1);
    return decodeName(raw, (stat.m_bit_flag & Utf8NameFlag) != 0);
}

QString parentFolder(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return slash < 0 ? QString() : path.first(slash);
}

struct ReadState {
    qint64 total = 0;
    int count = 0;
    bool stopped = false;
};

void warn(QStringList *warnings, const QString &message)
{
    if (warnings)
        warnings->append(message);
}

/// Where an archive is read from: memory, or a file read piece by piece (never loaded whole).
struct ZipSource {
    QByteArrayView bytes;
    QString file; ///< used when not empty
};

/// A ZIP archive open for reading from a ZipSource.
class OpenZip {
public:
    explicit OpenZip(const ZipSource &source)
    {
        if (source.file.isEmpty()) {
            m_ok = !source.bytes.isEmpty()
                   && mz_zip_reader_init_mem(&archive, source.bytes.data(), size_t(source.bytes.size()), 0);
            return;
        }
        m_file.setFileName(source.file);
        if (!m_file.open(QIODevice::ReadOnly) || m_file.size() <= 0)
            return;
        archive.m_pRead = &OpenZip::readAt;
        archive.m_pIO_opaque = this;
        m_ok = mz_zip_reader_init(&archive, mz_uint64(m_file.size()), 0);
    }
    ~OpenZip()
    {
        if (m_ok)
            mz_zip_reader_end(&archive);
    }
    Q_DISABLE_COPY_MOVE(OpenZip)
    bool ok() const { return m_ok; }

    mz_zip_archive archive{};

private:
    static size_t readAt(void *opaque, mz_uint64 offset, void *buffer, size_t n)
    {
        auto *self = static_cast<OpenZip *>(opaque);
        if (qint64(offset) != self->m_file.pos() && !self->m_file.seek(qint64(offset)))
            return 0;
        const qint64 got = self->m_file.read(static_cast<char *>(buffer), qint64(n));
        return got < 0 ? 0 : size_t(got);
    }

    QFile m_file;
    bool m_ok = false;
};

/// Entries unpacked to a file of the staging folder (ZipReadOptions): pictures, audio, video, PDFs
/// and archives — what importers pass on or unpack (prepareImportFiles()) without reading it.
bool isStagedKind(const QString &path)
{
    static const QSet<QString> extensions = {
            u"png"_s,  u"jpg"_s, u"jpeg"_s, u"gif"_s, u"webp"_s, u"heic"_s, u"heif"_s, u"avif"_s, u"bmp"_s,  u"tif"_s,
            u"tiff"_s, u"ico"_s, u"mp3"_s,  u"m4a"_s, u"aac"_s,  u"ogg"_s,  u"opus"_s, u"wav"_s,  u"flac"_s, u"mp4"_s,
            u"m4v"_s,  u"mov"_s, u"webm"_s, u"mkv"_s, u"avi"_s,  u"3gp"_s,  u"pdf"_s,  u"zip"_s,
    };
    const qsizetype dot = path.lastIndexOf(u'.');
    return dot >= 0 && extensions.contains(path.sliced(dot + 1).toLower());
}

/// Unpack entry `index` (`size` bytes) into a new file of the staging folder, piece by piece:
/// its absolute path, or "" when it is damaged or cannot be written (nothing left behind).
QString stageEntry(mz_zip_archive *archive, mz_uint index, qint64 size, const ZipReadOptions &options,
                   bool *writeFailed)
{
    QTemporaryFile file(QDir(options.stagingFolder).filePath(u"entry-XXXXXX"_s));
    file.setAutoRemove(true);
    if (!file.open()) {
        *writeFailed = true;
        return {};
    }
    struct Sink {
        QFile *file;
        qint64 left;
        const std::function<void(qint64)> *onPiece;
        bool failed = false;
    } sink{&file, size, &options.onStagedPiece};
    const auto write = [](void *opaque, mz_uint64, const void *data, size_t n) -> size_t {
        auto *s = static_cast<Sink *>(opaque);
        if (qint64(n) > s->left)
            return 0;
        // (miniz hands over what it inflated or read: a dictionary's or a read buffer's worth.)
        if (s->file->write(static_cast<const char *>(data), qint64(n)) != qint64(n)) {
            s->failed = true;
            return 0;
        }
        if (*s->onPiece)
            (*s->onPiece)(qint64(n));
        s->left -= qint64(n);
        return n;
    };
    const bool ok = mz_zip_reader_extract_to_callback(archive, index, write, &sink, 0) && sink.left == 0;
    *writeFailed = sink.failed;
    if (!ok || !file.flush())
        return {};
    file.setAutoRemove(false);
    return QFileInfo(file.fileName()).absoluteFilePath();
}

/// Read the archive into `out` (paths prefixed with `prefix`). False when it is not a ZIP archive.
bool readInto(const ZipSource &source, const QString &prefix, int depth, const ZipReadOptions &options,
              ReadState &state, QList<FileEntry> &out, QStringList *warnings)
{
    OpenZip zip(source);
    if (!zip.ok())
        return false;
    mz_zip_archive &archive = zip.archive;

    const mz_uint count = mz_zip_reader_get_num_files(&archive);
    for (mz_uint i = 0; i < count && !state.stopped; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&archive, i, &stat)) {
            warn(warnings, IoMessages::tr("A damaged ZIP entry was skipped."));
            continue;
        }
        const QString name = entryName(&archive, i, stat);
        if (stat.m_is_directory || name.endsWith(u'/') || name.endsWith(u'\\'))
            continue;
        const QString relative = sanitizeArchivePath(name);
        if (relative.isEmpty())
            continue;
        const QString path = prefix.isEmpty() ? relative : prefix + u'/' + relative;
        if (stat.m_is_encrypted) {
            warn(warnings, IoMessages::tr("“%1” is encrypted and was skipped.").arg(path));
            continue;
        }
        if (!stat.m_is_supported) {
            warn(warnings, IoMessages::tr("“%1” uses an unsupported compression method and was skipped.").arg(path));
            continue;
        }
        if (stat.m_uncomp_size > quint64(options.maxEntrySize)) {
            warn(warnings, IoMessages::tr("“%1” is too large and was skipped.").arg(path));
            continue;
        }
        // A size its compressed bytes cannot give (a forged header: a stored entry is as long as
        // it is, deflate has its limit): no memory is set aside and cleared for it.
        if (stat.m_method == 0 ? stat.m_uncomp_size != stat.m_comp_size
                               : stat.m_uncomp_size > (stat.m_comp_size + 1) * MaxDeflateRatio) {
            warn(warnings, IoMessages::tr("“%1” is damaged and was skipped.").arg(path));
            continue;
        }
        if (state.total + qint64(stat.m_uncomp_size) > options.maxTotalSize || state.count >= options.maxEntries) {
            warn(warnings, IoMessages::tr("The archive is too large; only a part of it was read."));
            state.stopped = true;
            break;
        }
        const qint64 size = qint64(stat.m_uncomp_size);
        FileEntry entry{path, {}, {}, 0};
        if (!options.stagingFolder.isEmpty() && size > options.stageAbove && isStagedKind(path)) {
            bool writeFailed = false;
            entry.file = stageEntry(&archive, i, size, options, &writeFailed);
            if (entry.file.isEmpty()) {
                warn(warnings, writeFailed
                                       ? IoMessages::tr("“%1” could not be unpacked: the disk may be full.").arg(path)
                                       : IoMessages::tr("“%1” is damaged and was skipped.").arg(path));
                continue;
            }
            entry.fileSize = size;
        } else if (size > 0) {
            entry.data.resize(qsizetype(size));
            if (!mz_zip_reader_extract_to_mem(&archive, i, entry.data.data(), size_t(entry.data.size()), 0)) {
                warn(warnings, IoMessages::tr("“%1” is damaged and was skipped.").arg(path));
                continue;
            }
        }
        state.total += size;
        ++state.count;
        if (options.expandNested && depth < options.maxNestingDepth && path.endsWith(u".zip"_s, Qt::CaseInsensitive)
            && isZip(entry.onDisk() ? fileHead(entry.file) : entry.data)) {
            if (readInto(ZipSource{entry.data, entry.file}, parentFolder(path), depth + 1, options, state, out,
                         warnings)) {
                if (entry.onDisk())
                    QFile::remove(entry.file); // (its files are what is left of it)
                continue;
            }
            warn(warnings, IoMessages::tr("“%1” is not a valid ZIP archive.").arg(path));
        }
        out.append(std::move(entry));
    }
    return true;
}

bool isCompressedFormat(const QString &path)
{
    static const QSet<QString> extensions = {
            u"png"_s, u"jpg"_s, u"jpeg"_s, u"gif"_s,  u"webp"_s,  u"heic"_s, u"avif"_s, u"zip"_s,  u"gz"_s,
            u"bz2"_s, u"xz"_s,  u"7z"_s,   u"rar"_s,  u"mp3"_s,   u"m4a"_s,  u"aac"_s,  u"ogg"_s,  u"opus"_s,
            u"mp4"_s, u"m4v"_s, u"mov"_s,  u"webm"_s, u"mkv"_s,   u"pdf"_s,  u"docx"_s, u"xlsx"_s, u"pptx"_s,
            u"odt"_s, u"ods"_s, u"epub"_s, u"woff"_s, u"woff2"_s, u"3gp"_s,
    };
    const qsizetype dot = path.lastIndexOf(u'.');
    return dot >= 0 && extensions.contains(path.sliced(dot + 1).toLower());
}

} // namespace

bool isZip(QByteArrayView data)
{
    return data.size() >= 4 && data[0] == 'P' && data[1] == 'K'
           && ((data[2] == '\x03' && data[3] == '\x04') || (data[2] == '\x05' && data[3] == '\x06'));
}

QList<FileEntry> readZip(QByteArrayView zip, QStringList *warnings, const ZipReadOptions &options)
{
    QList<FileEntry> out;
    ReadState state;
    if (!readInto(ZipSource{zip, {}}, QString(), 0, options, state, out, warnings))
        warn(warnings, IoMessages::tr("The file is not a valid ZIP archive."));
    return out;
}

QList<FileEntry> readZipFile(const QString &file, QStringList *warnings, const ZipReadOptions &options)
{
    QList<FileEntry> out;
    ReadState state;
    if (!readInto(ZipSource{{}, file}, QString(), 0, options, state, out, warnings))
        warn(warnings, IoMessages::tr("The file is not a valid ZIP archive."));
    return out;
}

QByteArray fileHead(const QString &file, qint64 bytes)
{
    QFile in(file);
    return in.open(QIODevice::ReadOnly) ? in.read(bytes) : QByteArray();
}

QByteArray writeZip(const QList<FileEntry> &entries, const ZipWriteOptions &options)
{
    mz_zip_archive archive{};
    if (!mz_zip_writer_init_heap(&archive, 0, 0))
        return {};
    const auto cleanup = qScopeGuard([&] { mz_zip_writer_end(&archive); });

    // DOS timestamps cover 1980-01-01 … 2107-12-31 (local time); others would overflow the fields.
    const QDateTime earliest(QDate(1980, 1, 1), QTime(0, 0));
    const QDateTime latest(QDate(2107, 12, 31), QTime(23, 59, 58));
    const QDateTime stamp = options.modified.isValid() ? std::clamp(options.modified, earliest, latest) : earliest;
    MZ_TIME_T time = MZ_TIME_T(stamp.toSecsSinceEpoch());
    // miniz converts the time with localtime(), which is not reentrant: one writer at a time.
    static QBasicMutex writerMutex;
    const std::lock_guard lock(writerMutex);
    const auto level = mz_uint(std::clamp(options.compressionLevel, 0, 9));
    QSet<QString> seen;
    for (const FileEntry &entry : entries) {
        const QString path = sanitizeArchivePath(entry.path);
        if (path.isEmpty() || seen.contains(path))
            continue;
        seen.insert(path);
        const QByteArray name = path.toUtf8();
        const mz_uint entryLevel = options.storeCompressedFormats && isCompressedFormat(path) ? 0 : level;
        if (!mz_zip_writer_add_mem_ex_v2(&archive, name.constData(), entry.data.constData(), size_t(entry.data.size()),
                                         nullptr, 0, entryLevel, 0, 0, &time, nullptr, 0, nullptr, 0))
            return {};
    }
    void *buffer = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&archive, &buffer, &size))
        return {};
    const std::unique_ptr<void, decltype(&mz_free)> owner(buffer, &mz_free);
    return QByteArray(static_cast<const char *>(buffer), qsizetype(size));
}

std::optional<QByteArray> gunzip(QByteArrayView data, qint64 maxSize)
{
    const auto byte = [&data](qsizetype i) { return static_cast<unsigned char>(data[i]); };
    qsizetype start = 0;
    int flags = TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF;
    if (data.size() >= 18 && byte(0) == 0x1F && byte(1) == 0x8B && byte(2) == 8) {
        // gzip: a 10-byte header, optional extra field, name, comment and header CRC.
        const int flg = byte(3);
        start = 10;
        if (flg & 0x04) {
            if (start + 2 > data.size())
                return std::nullopt;
            start += 2 + (byte(start) | (byte(start + 1) << 8));
        }
        for (const int bit : {0x08, 0x10}) {
            if (!(flg & bit))
                continue;
            while (start < data.size() && data[start] != 0)
                ++start;
            ++start;
        }
        if (flg & 0x02)
            start += 2;
        if (start >= data.size())
            return std::nullopt;
    } else if (data.size() >= 2 && (byte(0) & 0x0F) == 8 && ((byte(0) << 8) | byte(1)) % 31 == 0) {
        flags |= TINFL_FLAG_PARSE_ZLIB_HEADER;
    } else {
        return std::nullopt;
    }
    QByteArray out;
    if (maxSize <= 0)
        return std::nullopt;
    const qsizetype capacity =
            qsizetype(std::min<qint64>(maxSize, std::clamp<qint64>(qint64(data.size()) * 4, 4096, 1 << 20)));
    tinfl_decompressor decompressor;
    tinfl_init(&decompressor);
    size_t inPos = size_t(start);
    out.resize(capacity);
    size_t outPos = 0;
    for (;;) {
        size_t inBytes = size_t(data.size()) - inPos;
        size_t outBytes = size_t(out.size()) - outPos;
        const tinfl_status status = tinfl_decompress(
                &decompressor, reinterpret_cast<const mz_uint8 *>(data.data()) + inPos, &inBytes,
                reinterpret_cast<mz_uint8 *>(out.data()), reinterpret_cast<mz_uint8 *>(out.data()) + outPos, &outBytes,
                flags | (inPos + inBytes < size_t(data.size()) ? TINFL_FLAG_HAS_MORE_INPUT : 0));
        inPos += inBytes;
        outPos += outBytes;
        if (status == TINFL_STATUS_DONE) {
            out.resize(qsizetype(outPos));
            return out;
        }
        if (status == TINFL_STATUS_HAS_MORE_OUTPUT) {
            if (out.size() >= maxSize)
                return std::nullopt;
            out.resize(qsizetype(std::min<qint64>(qint64(out.size()) * 2, maxSize)));
            continue;
        }
        return std::nullopt; // damaged, or the input ended
    }
}

QString sanitizeArchivePath(QStringView name)
{
    QString s = unicode::nfc(name);
    s.replace(u'\\', u'/');
    if (s.size() >= 2 && s[1] == u':' && s[0].isLetter())
        s.remove(0, 2);
    s.removeIf([](QChar c) { return c.unicode() < 0x20 || c.unicode() == 0x7f; });
    QStringList parts;
    for (const QStringView segment : qTokenize(s, u'/', Qt::SkipEmptyParts)) {
        if (segment == u"..") {
            if (!parts.isEmpty())
                parts.removeLast();
        } else if (segment != u".") {
            parts.append(segment.toString());
        }
    }
    return parts.join(u'/');
}

} // namespace brulion::io
