#include "io/tar.h"

#include "io/messages_p.h"
#include "io/zip.h"

#include "io/staging_p.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>

#include <algorithm>
#include <optional>

namespace brulion::io {

using namespace Qt::StringLiterals;
using detail::IoMessages;

namespace {

constexpr qsizetype BlockSize = 512;

/// Header field offsets / sizes (POSIX ustar).
constexpr qsizetype NameOffset = 0, NameSize = 100;
constexpr qsizetype SizeOffset = 124, SizeSize = 12;
constexpr qsizetype ChecksumOffset = 148, ChecksumSize = 8;
constexpr qsizetype TypeOffset = 156;
constexpr qsizetype MagicOffset = 257;
constexpr qsizetype PrefixOffset = 345, PrefixSize = 155;

/// A NUL-terminated field.
QByteArrayView field(QByteArrayView header, qsizetype offset, qsizetype size)
{
    const QByteArrayView f = header.sliced(offset, size);
    const qsizetype nul = f.indexOf('\0');
    return nul < 0 ? f : f.first(nul);
}

/// Octal number field (spaces / NULs around it allowed) or GNU base-256 (high bit set).
std::optional<qint64> number(QByteArrayView header, qsizetype offset, qsizetype size)
{
    const QByteArrayView f = header.sliced(offset, size);
    if (static_cast<unsigned char>(f[0]) & 0x80) {
        qint64 value = 0;
        for (qsizetype i = 1; i < f.size(); ++i) {
            if (value > (qint64(1) << 47))
                return std::nullopt;
            value = (value << 8) | static_cast<unsigned char>(f[i]);
        }
        return value;
    }
    qint64 value = 0;
    bool digits = false;
    for (const char c : f) {
        if (c >= '0' && c <= '7') {
            if (value > (qint64(1) << 55))
                return std::nullopt;
            value = value * 8 + (c - '0');
            digits = true;
        } else if (c == ' ' || c == '\0') {
            if (digits)
                break;
        } else {
            return std::nullopt;
        }
    }
    return value;
}

bool checksumOk(QByteArrayView header)
{
    const auto stored = number(header, ChecksumOffset, ChecksumSize);
    if (!stored)
        return false;
    qint64 unsignedSum = 0;
    qint64 signedSum = 0;
    for (qsizetype i = 0; i < BlockSize; ++i) {
        const bool inField = i >= ChecksumOffset && i < ChecksumOffset + ChecksumSize;
        const char c = inField ? ' ' : header[i];
        unsignedSum += static_cast<unsigned char>(c);
        signedSum += static_cast<signed char>(c);
    }
    return *stored == unsignedSum || *stored == signedSum;
}

bool isZeroBlock(QByteArrayView block)
{
    return std::ranges::all_of(block, [](char c) { return c == '\0'; });
}

/// The "path" record of a PAX extended header ("<len> path=<value>\n" records).
QString paxPath(QByteArrayView data)
{
    qsizetype pos = 0;
    QString path;
    while (pos < data.size()) {
        const qsizetype space = data.indexOf(' ', pos);
        if (space < 0)
            break;
        bool ok = false;
        const qsizetype length = data.sliced(pos, space - pos).toLongLong(&ok);
        if (!ok || length <= space - pos || pos + length > data.size())
            break;
        const QByteArrayView record = data.sliced(space + 1, length - (space - pos) - 1);
        const QByteArrayView body = record.endsWith('\n') ? record.chopped(1) : record;
        if (body.startsWith("path="))
            path = QString::fromUtf8(body.sliced(5));
        pos += length;
    }
    return path;
}

QString headerName(QByteArrayView header)
{
    QByteArray name = field(header, NameOffset, NameSize).toByteArray();
    if (header.sliced(MagicOffset, 5) == "ustar") {
        if (const QByteArrayView prefix = field(header, PrefixOffset, PrefixSize); !prefix.isEmpty())
            name.prepend('/').prepend(prefix);
    }
    return QString::fromUtf8(name);
}

} // namespace

bool isTar(QByteArrayView data)
{
    if (data.size() < BlockSize)
        return false;
    const QByteArrayView header = data.first(BlockSize);
    if (isZeroBlock(header) || !checksumOk(header))
        return false;
    return header.sliced(MagicOffset, 5) == "ustar" || !field(header, NameOffset, NameSize).isEmpty();
}

namespace {

/// The tar reader over a device (an archive in memory: a QBuffer; on disk: a QFile), header by
/// header — an entry's data is read only when it is kept, into memory or a staged file.
class TarReader {
public:
    TarReader(QIODevice &in, QStringList *warnings, const ZipReadOptions &limits)
        : m_in(in), m_warnings(warnings), m_limits(limits)
    {
    }

    QList<FileEntry> read()
    {
        QString longName;
        while (m_in.size() - m_in.pos() >= BlockSize) {
            const QByteArray header = m_in.read(BlockSize);
            if (header.size() != BlockSize || isZeroBlock(header))
                break;
            const std::optional<qint64> size = sizeOf(header);
            if (!size)
                break;
            const qint64 next = m_in.pos() + ((*size + BlockSize - 1) / BlockSize) * BlockSize;
            if (!entry(header, *size, longName) || !m_in.seek(next))
                break;
        }
        return std::move(m_out);
    }

private:
    void warn(const QString &message)
    {
        if (m_warnings)
            m_warnings->append(message);
    }

    /// The data size a header gives, nullopt (with a warning) for a damaged or cut archive.
    std::optional<qint64> sizeOf(QByteArrayView header)
    {
        if (!checksumOk(header)) {
            warn(IoMessages::tr("The tar archive is damaged; only a part of it was read."));
            return std::nullopt;
        }
        const auto size = number(header, SizeOffset, SizeSize);
        if (!size || *size < 0 || *size > m_in.size() - m_in.pos()) {
            warn(IoMessages::tr("The tar archive is truncated; only a part of it was read."));
            return std::nullopt;
        }
        return size;
    }

    /// One member (`size` bytes of data at the device's position); false: stop reading.
    bool entry(QByteArrayView header, qint64 size, QString &longName)
    {
        const char type = header[TypeOffset];
        if (type == 'L' || type == 'x') {
            nameOfNext(type, size, longName);
            return true;
        }
        const QString path = filePath(header, type, longName);
        if (path.isEmpty())
            return true;
        switch (limitFor(path, size)) {
        case Limit::Skip: return true;
        case Limit::Stop: return false;
        case Limit::Within: break;
        }
        return keep(path, size);
    }

    /// The path of a regular file's member ("" for directories, links, devices, global PAX
    /// headers, a name that is no path); the long name of the member is used up.
    static QString filePath(QByteArrayView header, char type, QString &longName)
    {
        const QString name = longName.isEmpty() ? headerName(header) : longName;
        longName.clear();
        const bool file = type == '0' || type == '\0' || type == '7';
        return file && !name.endsWith(u'/') ? sanitizeArchivePath(name) : QString();
    }

    /// A GNU long name ('L') or a PAX header ('x') of the next entry (small: read whole).
    void nameOfNext(char type, qint64 size, QString &longName)
    {
        const QByteArray data = m_in.read(std::min<qint64>(size, 1 << 20));
        const QString name = type == 'L' ? QString::fromUtf8(field(data, 0, data.size())) : paxPath(data);
        if (!name.isEmpty())
            longName = name;
    }

    enum class Limit { Within, Skip, Stop };
    /// The limits as readZip() applies them (and with its messages).
    Limit limitFor(const QString &path, qint64 size)
    {
        if (size > m_limits.maxEntrySize) {
            warn(IoMessages::tr("“%1” is too large and was skipped.").arg(path));
            return Limit::Skip;
        }
        if (m_total + size > m_limits.maxTotalSize || m_out.size() >= m_limits.maxEntries) {
            warn(IoMessages::tr("The archive is too large; only a part of it was read."));
            return Limit::Stop;
        }
        return Limit::Within;
    }

    /// The entry's data kept: staged (a large attachment) or in memory; false: stop reading.
    bool keep(const QString &path, qint64 size)
    {
        FileEntry kept{path, {}, {}, 0};
        if (!m_limits.stagingFolder.isEmpty() && size > m_limits.stageAbove && detail::isStagedKind(path)) {
            kept.file = stage(size);
            if (kept.file.isEmpty()) {
                warn(IoMessages::tr("“%1” could not be unpacked: the disk may be full.").arg(path));
                return false;
            }
            kept.fileSize = size;
        } else {
            kept.data = m_in.read(size);
            if (kept.data.size() != size) {
                warn(IoMessages::tr("The tar archive is truncated; only a part of it was read."));
                return false;
            }
        }
        m_total += size;
        m_out.append(std::move(kept));
        return true;
    }

    /// `size` bytes of the device copied into a new file of the staging folder in pieces: its
    /// absolute path, or "" (nothing left behind) when it cannot be written.
    QString stage(qint64 size)
    {
        QTemporaryFile file(QDir(m_limits.stagingFolder).filePath(u"entry-XXXXXX"_s));
        if (!file.open())
            return {};
        QByteArray piece(PieceBytes, Qt::Uninitialized);
        for (qint64 left = size; left > 0;) {
            const qint64 n = m_in.read(piece.data(), std::min<qint64>(left, PieceBytes));
            if (n <= 0 || file.write(piece.constData(), n) != n)
                return {};
            if (m_limits.onStagedPiece)
                m_limits.onStagedPiece(n);
            left -= n;
        }
        if (!file.flush())
            return {};
        file.setAutoRemove(false);
        return QFileInfo(file.fileName()).absoluteFilePath();
    }

    static constexpr qint64 PieceBytes = 256 * 1024;
    QIODevice &m_in;
    QStringList *m_warnings;
    const ZipReadOptions &m_limits;
    QList<FileEntry> m_out;
    qint64 m_total = 0;
};

} // namespace

QList<FileEntry> readTar(QByteArrayView tar, QStringList *warnings, const ZipReadOptions &limits)
{
    QByteArray data = QByteArray::fromRawData(tar.data(), tar.size());
    QBuffer in(&data);
    in.open(QIODevice::ReadOnly);
    ZipReadOptions inMemory = limits;
    inMemory.stagingFolder.clear(); // (an archive in memory: its entries too, as before)
    return TarReader(in, warnings, inMemory).read();
}

QList<FileEntry> readTarFile(const QString &file, QStringList *warnings, const ZipReadOptions &limits)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        if (warnings)
            warnings->append(IoMessages::tr("The tar archive could not be read."));
        return {};
    }
    return TarReader(in, warnings, limits).read();
}

} // namespace brulion::io
