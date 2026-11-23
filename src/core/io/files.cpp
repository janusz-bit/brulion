#include "io/files.h"

#include <QFile>

namespace brulion::io {

QByteArray bytesOf(const FileEntry &entry)
{
    if (!entry.onDisk())
        return entry.data;
    QFile file(entry.file);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool sameContent(const FileEntry &a, const FileEntry &b)
{
    if (a.size() != b.size())
        return false;
    if (!a.onDisk() && !b.onDisk())
        return a.data == b.data;
    if (a.onDisk() && b.onDisk() && a.file == b.file)
        return true;
    // In pieces: an entry on disk is a large one.
    constexpr qint64 Piece = 256 * 1024;
    QFile fa(a.file);
    QFile fb(b.file);
    if ((a.onDisk() && !fa.open(QIODevice::ReadOnly)) || (b.onDisk() && !fb.open(QIODevice::ReadOnly)))
        return false;
    for (qint64 at = 0; at < a.size(); at += Piece) {
        const QByteArray pa = a.onDisk() ? fa.read(Piece) : a.data.sliced(at, std::min(Piece, a.size() - at));
        const QByteArray pb = b.onDisk() ? fb.read(Piece) : b.data.sliced(at, std::min(Piece, b.size() - at));
        if (pa.isEmpty() || pa != pb)
            return false;
    }
    return true;
}

} // namespace brulion::io
