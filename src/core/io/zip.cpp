#include "io/zip.h"

#include "io/messages_p.h"

#include <QMutex>
#include <QScopeGuard>
#include <QSet>
#include <QStringDecoder>

#include <miniz.h>

#include <algorithm>
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

QString decodeCp437(const QByteArray &raw)
{
    QString out;
    out.reserve(raw.size());
    for (const char c : raw) {
        const auto b = static_cast<unsigned char>(c);
        out += b < 0x80 ? QChar(char16_t(b)) : QChar(Cp437High[b - 0x80]);
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

/// Read `zip` into `out` (paths prefixed with `prefix`). False when `zip` is not a ZIP archive.
bool readInto(QByteArrayView zip, const QString &prefix, int depth, const ZipReadOptions &options, ReadState &state,
              QList<FileEntry> &out, QStringList *warnings)
{
    mz_zip_archive archive{};
    if (zip.isEmpty() || !mz_zip_reader_init_mem(&archive, zip.data(), size_t(zip.size()), 0))
        return false;
    const auto cleanup = qScopeGuard([&] { mz_zip_reader_end(&archive); });

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
        if (state.total + qint64(stat.m_uncomp_size) > options.maxTotalSize || state.count >= options.maxEntries) {
            warn(warnings, IoMessages::tr("The archive is too large; only a part of it was read."));
            state.stopped = true;
            break;
        }
        QByteArray data;
        if (stat.m_uncomp_size > 0) {
            data.resize(qsizetype(stat.m_uncomp_size));
            if (!mz_zip_reader_extract_to_mem(&archive, i, data.data(), size_t(data.size()), 0)) {
                warn(warnings, IoMessages::tr("“%1” is damaged and was skipped.").arg(path));
                continue;
            }
        }
        state.total += data.size();
        ++state.count;
        if (options.expandNested && depth < options.maxNestingDepth && path.endsWith(u".zip"_s, Qt::CaseInsensitive)
            && isZip(data)) {
            if (readInto(data, parentFolder(path), depth + 1, options, state, out, warnings))
                continue;
            warn(warnings, IoMessages::tr("“%1” is not a valid ZIP archive.").arg(path));
        }
        out.append(FileEntry{path, std::move(data)});
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
    if (!readInto(zip, QString(), 0, options, state, out, warnings))
        warn(warnings, IoMessages::tr("The file is not a valid ZIP archive."));
    return out;
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
    QByteArray out(static_cast<const char *>(buffer), qsizetype(size));
    mz_free(buffer);
    return out;
}

QString sanitizeArchivePath(QStringView name)
{
    QString s = name.toString().normalized(QString::NormalizationForm_C);
    s.replace(u'\\', u'/');
    if (s.size() >= 2 && s[1] == u':' && s[0].isLetter())
        s.remove(0, 2);
    QStringList parts;
    for (const QString &rawSegment : s.split(u'/')) {
        QString segment;
        segment.reserve(rawSegment.size());
        for (const QChar c : rawSegment) {
            if (c.unicode() >= 0x20 && c.unicode() != 0x7f)
                segment += c;
        }
        if (segment.isEmpty() || segment == u".")
            continue;
        if (segment == u"..") {
            if (!parts.isEmpty())
                parts.removeLast();
            continue;
        }
        parts.append(segment);
    }
    return parts.join(u'/');
}

} // namespace brulion::io
