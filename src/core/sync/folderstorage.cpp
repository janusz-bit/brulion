#include "sync/folderstorage.h"

#include "sync/http.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTimeZone>

using namespace Qt::StringLiterals;

namespace brulion::sync {

QString folderErrorName(FolderErrorCode code)
{
    switch (code) {
    case FolderErrorCode::None: return {};
    case FolderErrorCode::NotFound: return u"not-found"_s;
    case FolderErrorCode::Exists: return u"exists"_s;
    case FolderErrorCode::NotAllowed: return u"not-allowed"_s;
    case FolderErrorCode::Unavailable: return u"unavailable"_s;
    case FolderErrorCode::Unsupported: return u"unsupported"_s;
    case FolderErrorCode::Failed: return u"failed"_s;
    }
    return {};
}

FolderError FolderStorage::move(const QString &from, const QString &to, bool overwrite)
{
    Q_UNUSED(from);
    Q_UNUSED(to);
    Q_UNUSED(overwrite);
    return FolderError::make(FolderErrorCode::Unsupported, u"This folder cannot move files"_s);
}

namespace {

FolderEntry entryOf(const QFileInfo &info)
{
    FolderEntry entry;
    entry.name = info.fileName();
    entry.isDir = info.isDir();
    entry.size = entry.isDir ? 0 : info.size();
    entry.mtime = info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
    return entry;
}

FolderError failed(const QString &what, const QString &path, const QString &why)
{
    return FolderError::make(FolderErrorCode::Failed, u"%1 %2: %3"_s.arg(what, path, why));
}

/// `f` (a QFile or a QSaveFile) opened on `file`, or why it cannot be.
template<typename File>
FolderError openAs(File &f, const QString &file, QIODevice::OpenMode mode, const QString &what)
{
    f.setFileName(file);
    return f.open(mode) ? FolderError() : failed(what, file, f.errorString());
}

} // namespace

LocalFolderStorage::LocalFolderStorage(const QString &root)
    : m_root(QDir::cleanPath(root))
{
}

FolderError FolderStorage::readInto(const QString &path, qint64 maxBytes, const PieceSink &sink)
{
    const FolderReadResult read = this->read(path, maxBytes);
    if (read.error.isError())
        return read.error;
    const QByteArrayView data(read.data);
    for (qsizetype at = 0; at < data.size(); at += HttpRequest::StreamPieceBytes) {
        if (!sink(data.sliced(at, std::min<qsizetype>(HttpRequest::StreamPieceBytes, data.size() - at))))
            return FolderError::make(FolderErrorCode::Failed, u"The piece read from %1 was not kept"_s.arg(path));
    }
    return {};
}

FolderStatResult FolderStorage::writeFrom(const QString &path, const PieceSource &source)
{
    QByteArray data;
    for (;;) {
        const std::optional<QByteArray> piece = source();
        if (!piece)
            return {std::nullopt, FolderError::make(FolderErrorCode::Failed, u"The body for %1 could not be read"_s.arg(path))};
        if (piece->isEmpty())
            break;
        data.append(*piece);
    }
    return write(path, data);
}

QString LocalFolderStorage::absolute(const QString &path) const
{
    return path.isEmpty() ? m_root : m_root + u'/' + path;
}

FolderStatResult LocalFolderStorage::stat(const QString &path)
{
    const QFileInfo info(absolute(path));
    if (!info.exists())
        return {};
    FolderEntry entry = entryOf(info);
    if (path.isEmpty())
        entry.name.clear();
    return {entry, {}};
}

FolderListResult LocalFolderStorage::list(const QString &folder)
{
    const QString dir = absolute(folder);
    const QFileInfo info(dir);
    if (!info.isDir())
        return {{}, FolderError::make(FolderErrorCode::NotFound, u"No folder %1"_s.arg(dir))};
    if (!info.isReadable())
        return {{}, FolderError::make(FolderErrorCode::NotAllowed, u"Folder %1 cannot be read"_s.arg(dir))};
    FolderListResult result;
    const QFileInfoList children =
            QDir(dir).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &child : children) {
        // A link names something elsewhere: not part of this folder's notes.
        if (child.isSymLink())
            continue;
        if (child.isFile() || child.isDir())
            result.entries.append(entryOf(child));
    }
    return result;
}

FolderError LocalFolderStorage::openToRead(const QString &path, qint64 maxBytes, QFile &f) const
{
    const QString file = absolute(path);
    const QFileInfo info(file);
    if (!info.exists())
        return FolderError::make(FolderErrorCode::NotFound, u"No file %1"_s.arg(file));
    if (info.isDir())
        return failed(u"Read"_s, file, u"a folder"_s);
    if (info.size() > maxBytes)
        return failed(u"Read"_s, file, u"%1 bytes, more than %2"_s.arg(info.size()).arg(maxBytes));
    return openAs(f, file, QIODevice::ReadOnly, u"Read"_s);
}

FolderReadResult LocalFolderStorage::read(const QString &path, qint64 maxBytes)
{
    QByteArray data;
    const FolderError error = readInto(path, maxBytes, [&data](QByteArrayView piece) {
        data.append(piece);
        return true;
    });
    if (error.isError())
        return {{}, error};
    return {std::move(data), {}};
}

FolderError LocalFolderStorage::readInto(const QString &path, qint64 maxBytes, const PieceSink &sink)
{
    QFile f;
    if (const FolderError error = openToRead(path, maxBytes, f); error.isError())
        return error;
    QByteArray piece(HttpRequest::StreamPieceBytes, Qt::Uninitialized);
    for (qint64 total = 0;;) {
        const qint64 n = f.read(piece.data(), piece.size());
        if (n <= 0)
            return n < 0 ? failed(u"Read"_s, f.fileName(), f.errorString()) : FolderError();
        total += n;
        if (total > maxBytes)
            return failed(u"Read"_s, f.fileName(), u"it grew past %1 bytes"_s.arg(maxBytes));
        if (!sink(QByteArrayView(piece.constData(), n)))
            return failed(u"Read"_s, f.fileName(), u"the piece read was not kept"_s);
    }
}

FolderStatResult LocalFolderStorage::write(const QString &path, const QByteArray &data)
{
    bool given = false;
    return writeFrom(path, [&data, &given]() -> std::optional<QByteArray> {
        if (given)
            return QByteArray();
        given = true;
        return data; // (an empty file: the end at once)
    });
}

FolderError LocalFolderStorage::openToWrite(const QString &path, QSaveFile &f) const
{
    const QString file = absolute(path);
    const QFileInfo info(file);
    if (!info.absoluteDir().exists())
        return FolderError::make(FolderErrorCode::NotFound, u"No folder for %1"_s.arg(file));
    if (info.isDir())
        return failed(u"Write"_s, file, u"a folder"_s);
    // (QSaveFile: the file is replaced only when all of it was written — or stays as it was.)
    return openAs(f, file, QIODevice::WriteOnly, u"Write"_s);
}

FolderStatResult LocalFolderStorage::writeFrom(const QString &path, const PieceSource &source)
{
    QSaveFile f;
    if (const FolderError error = openToWrite(path, f); error.isError())
        return {std::nullopt, error};
    for (std::optional<QByteArray> piece = source(); !piece || !piece->isEmpty(); piece = source()) {
        if (!piece) {
            f.cancelWriting();
            return {std::nullopt, failed(u"Write"_s, f.fileName(), u"its body could not be read"_s)};
        }
        if (f.write(*piece) != piece->size())
            return {std::nullopt, failed(u"Write"_s, f.fileName(), f.errorString())};
    }
    if (!f.commit())
        return {std::nullopt, failed(u"Write"_s, f.fileName(), f.errorString())};
    FolderStatResult after = stat(path);
    if (!after.error.isError() && !after.entry)
        after.error = failed(u"Write"_s, f.fileName(), u"gone right after it was written"_s);
    return after;
}

FolderError LocalFolderStorage::makeFolder(const QString &path)
{
    const QString dir = absolute(path);
    if (QFileInfo::exists(dir))
        return FolderError::make(FolderErrorCode::Exists, u"%1 exists"_s.arg(dir));
    if (path.isEmpty())
        return QDir().mkpath(dir) ? FolderError() : failed(u"Create"_s, dir, u"mkpath failed"_s);
    if (!QFileInfo(dir).absoluteDir().exists())
        return FolderError::make(FolderErrorCode::NotFound, u"No folder for %1"_s.arg(dir));
    return QDir().mkdir(dir) ? FolderError() : failed(u"Create"_s, dir, u"mkdir failed"_s);
}

FolderError LocalFolderStorage::remove(const QString &path)
{
    const QString target = absolute(path);
    const QFileInfo info(target);
    if (!info.exists() && !info.isSymLink())
        return FolderError::make(FolderErrorCode::NotFound, u"No %1"_s.arg(target));
    if (info.isDir() && !info.isSymLink())
        return QDir(target).removeRecursively() ? FolderError() : failed(u"Delete"_s, target, u"not everything deleted"_s);
    QFile f(target);
    return f.remove() ? FolderError() : failed(u"Delete"_s, target, f.errorString());
}

FolderError LocalFolderStorage::move(const QString &from, const QString &to, bool overwrite)
{
    const QString source = absolute(from);
    const QString target = absolute(to);
    if (!QFileInfo::exists(source))
        return FolderError::make(FolderErrorCode::NotFound, u"No %1"_s.arg(source));
    if (!QFileInfo(target).absoluteDir().exists())
        return FolderError::make(FolderErrorCode::NotFound, u"No folder for %1"_s.arg(target));
    if (QFileInfo::exists(target)) {
        if (!overwrite)
            return FolderError::make(FolderErrorCode::Exists, u"%1 exists"_s.arg(target));
        if (const FolderError error = remove(to); error.isError())
            return error;
    }
    return QDir().rename(source, target) ? FolderError() : failed(u"Move"_s, source, u"rename to %1 failed"_s.arg(target));
}

} // namespace brulion::sync
