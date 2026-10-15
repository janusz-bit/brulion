#include "io/tar.h"

#include "io/messages_p.h"
#include "io/zip.h"

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

/// A NUL-terminated header field.
QByteArray field(QByteArrayView header, qsizetype offset, qsizetype size)
{
    const QByteArrayView f = header.sliced(offset, size);
    const qsizetype nul = f.indexOf('\0');
    return (nul < 0 ? f : f.first(nul)).toByteArray();
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
    for (const char c : block) {
        if (c != '\0')
            return false;
    }
    return true;
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
    QByteArray name = field(header, NameOffset, NameSize);
    if (header.sliced(MagicOffset, 5) == "ustar") {
        const QByteArray prefix = field(header, PrefixOffset, PrefixSize);
        if (!prefix.isEmpty())
            name = prefix + '/' + name;
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

QList<FileEntry> readTar(QByteArrayView tar, QStringList *warnings)
{
    QList<FileEntry> out;
    const auto warn = [&](const QString &message) {
        if (warnings)
            warnings->append(message);
    };
    QString longName;
    qsizetype pos = 0;
    while (pos + BlockSize <= tar.size()) {
        const QByteArrayView header = tar.sliced(pos, BlockSize);
        if (isZeroBlock(header))
            break;
        if (!checksumOk(header)) {
            warn(IoMessages::tr("The tar archive is damaged; only a part of it was read."));
            return out;
        }
        const auto size = number(header, SizeOffset, SizeSize);
        const qsizetype dataStart = pos + BlockSize;
        if (!size || *size < 0 || *size > tar.size() - dataStart) {
            warn(IoMessages::tr("The tar archive is truncated; only a part of it was read."));
            return out;
        }
        const QByteArrayView data = tar.sliced(dataStart, qsizetype(*size));
        pos = dataStart + ((qsizetype(*size) + BlockSize - 1) / BlockSize) * BlockSize;

        const char type = header[TypeOffset];
        if (type == 'L') { // GNU long name of the next entry
            const qsizetype nul = data.indexOf('\0');
            longName = QString::fromUtf8(nul < 0 ? data : data.first(nul));
            continue;
        }
        if (type == 'x') { // PAX header of the next entry
            const QString path = paxPath(data);
            if (!path.isEmpty())
                longName = path;
            continue;
        }
        const QString name = longName.isEmpty() ? headerName(header) : longName;
        longName.clear();
        if (type != '0' && type != '\0' && type != '7')
            continue; // directories, links, devices, global PAX headers
        if (name.endsWith(u'/'))
            continue;
        const QString path = sanitizeArchivePath(name);
        if (!path.isEmpty())
            out.append(FileEntry{path, data.toByteArray()});
    }
    return out;
}

} // namespace brulion::io
