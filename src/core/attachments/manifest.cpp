#include "attachments/manifest.h"

#include "attachments/codec_p.h"

#include <QCryptographicHash>
#include <QIODevice>
#include <QStringList>

#include <algorithm>

namespace brulion::attachments {

using namespace codec;
using namespace Qt::StringLiterals;

namespace {

constexpr char Magic[4] = {'B', 'R', 'A', 'M'};
constexpr qsizetype HashBytes = 32;
constexpr quint32 MaxPathBytes = 16 * 1024;

} // namespace

QByteArray encodeManifest(const Manifest &manifest)
{
    QByteArray out;
    out.append(Magic, 4);
    putU32(out, ManifestFormatVersion);
    // In path order: the same manifest gives the same bytes.
    QStringList paths = manifest.keys();
    std::ranges::sort(paths);
    qsizetype written = 0;
    QByteArray entries;
    for (const QString &path : std::as_const(paths)) {
        const ManifestEntry &e = manifest.value(path);
        const QByteArray utf8 = path.toUtf8();
        if (e.hash.size() != HashBytes || utf8.isEmpty() || utf8.size() > qsizetype(MaxPathBytes))
            continue;
        putU32(entries, quint32(utf8.size()));
        entries.append(utf8);
        putU64(entries, quint64(e.size));
        putU64(entries, quint64(e.mtime));
        entries.append(e.hash);
        ++written;
    }
    putU32(out, quint32(written));
    out.append(entries);
    putU32(out, fnv1a32(out));
    return out;
}

std::optional<Manifest> decodeManifest(QByteArrayView bytes)
{
    if (bytes.size() < 4 + 4 + 4 + 4 || !std::equal(Magic, Magic + 4, bytes.data()))
        return std::nullopt;
    const QByteArrayView body = bytes.first(bytes.size() - 4);
    Reader check(bytes.sliced(bytes.size() - 4));
    if (check.u32() != fnv1a32(body))
        return std::nullopt;
    Reader r(body.sliced(4));
    if (r.u32() != ManifestFormatVersion)
        return std::nullopt;
    const quint32 count = r.u32();
    // Each entry takes at least 4 + 1 + 8 + 8 + 32 bytes.
    if (!r.ok() || count > MaxManifestEntries || qint64(count) * 53 > r.remaining())
        return std::nullopt;
    Manifest out;
    out.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        const quint32 length = r.u32();
        if (!r.ok() || length == 0 || length > MaxPathBytes)
            return std::nullopt;
        const QString path = QString::fromUtf8(r.bytes(length));
        ManifestEntry e;
        e.size = qint64(r.u64());
        e.mtime = qint64(r.u64());
        e.hash = r.bytes(HashBytes).toByteArray();
        if (!r.ok() || e.size < 0)
            return std::nullopt;
        out.insert(path, std::move(e));
    }
    if (r.remaining() != 0)
        return std::nullopt;
    return out;
}

std::optional<QByteArray> hashFile(QIODevice &device, const std::function<bool()> &cancelled)
{
    if (!device.isReadable())
        return std::nullopt;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray chunk(1 << 20, Qt::Uninitialized);
    for (;;) {
        if (cancelled && cancelled())
            return std::nullopt;
        const qint64 n = device.read(chunk.data(), chunk.size());
        if (n < 0)
            return std::nullopt;
        if (n == 0)
            break;
        hash.addData(QByteArrayView(chunk.constData(), n));
    }
    return hash.result();
}

QString recordFileName(const QByteArray &hash)
{
    return QString::fromLatin1(hash.toHex()) + u".bat"_s;
}

QString manifestFileName(const QString &vaultId)
{
    const QByteArray key = QCryptographicHash::hash(vaultId.toUtf8(), QCryptographicHash::Sha1).toHex().left(20);
    return u"notebooks/"_s + QString::fromLatin1(key) + u".manifest"_s;
}

} // namespace brulion::attachments
