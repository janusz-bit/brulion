#include "filesync.h"

#include "vault/filesync.h"

#include <QDir>
#include <QFileDevice>
#include <QString>

#include <cerrno>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>

#include <string>
#else
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;

namespace brulion::platform {

QString extendedLengthPath(const QString &absolute)
{
    QString native = absolute;
    native.replace(u'/', u'\\');
    if (native.startsWith(u"\\\\?\\"_s) || native.startsWith(u"\\\\.\\"_s))
        return native;                // (already one, or a device)
    if (native.startsWith(u"\\\\"_s)) // \\server\share\… -> \\?\UNC\server\share\…
        return u"\\\\?\\UNC\\"_s + native.sliced(2);
    if (native.size() >= 3 && native.at(0).isLetter() && native.at(1) == u':' && native.at(2) == u'\\')
        return u"\\\\?\\"_s + native;
    return native; // not absolute: the prefix would not resolve it
}

namespace {

#ifdef Q_OS_WIN

bool syncFile(QFileDevice &file, QString *error)
{
    // QFileDevice::handle() is a C library descriptor that Qt makes for its native handle
    // (_open_osfhandle(), text mode — irrelevant here: nothing is written through it). _commit()
    // would flush it too, but reports every failure as EBADF (and asserts in the debug runtime):
    // FlushFileBuffers() on the native handle says what happened, as QSaveFile's commit() does.
    const int fd = file.handle();
    const HANDLE handle = fd < 0 ? INVALID_HANDLE_VALUE : reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (handle == INVALID_HANDLE_VALUE) {
        if (error)
            *error = qt_error_string(ERROR_INVALID_HANDLE);
        return false;
    }
    if (FlushFileBuffers(handle))
        return true;
    if (error)
        *error = qt_error_string(int(GetLastError()));
    return false;
}

/// The system's clean-up of a path, as Qt's own long-path form does first
/// (QFSFileEnginePrivate::longFileName()): the "\\?\" prefix turns it off, so without it a
/// doubled separator stays an empty name and a trailing dot or space stays part of one ("Dr./a.md"
/// is in the folder "Dr" that QDir made). As it was when the system cannot say.
QString fullPathName(const QString &path)
{
    const std::wstring in = QDir::toNativeSeparators(path).toStdWString();
    DWORD size = GetFullPathNameW(in.c_str(), 0, nullptr, nullptr);
    if (size == 0)
        return path;
    std::wstring out(size, L'\0');
    size = GetFullPathNameW(in.c_str(), size, out.data(), nullptr);
    if (size == 0 || size >= out.size())
        return path;
    out.resize(size);
    return QString::fromStdWString(out);
}

vaultutil::Replaced replaceFile(const QString &from, const QString &to, QString *error)
{
    // What QSaveFile's commit() does too (QFileSystemEngine::renameOverwriteFile()): one move that
    // replaces the file. No MOVEFILE_COPY_ALLOWED: both lie in one folder, and a copy would not be
    // atomic.
    const std::wstring source = extendedLengthPath(fullPathName(from)).toStdWString();
    const std::wstring target = extendedLengthPath(fullPathName(to)).toStdWString();
    if (MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
        return vaultutil::Replaced::Done;
    const DWORD code = GetLastError();
    if (error)
        *error = qt_error_string(int(code));
    switch (code) {
    // Another program has one of the files open without FILE_SHARE_DELETE: a virus scanner or an
    // indexer looking at the temporary file just closed (then the source is "in use"), a sync client
    // or a viewer reading the note (ERROR_ACCESS_DENIED for the target, also given for a read-only
    // one — refused before, saveAtomicallyStamped()), a byte-range lock. Gone after a moment.
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION: return vaultutil::Replaced::Busy;
    default: return vaultutil::Replaced::Failed;
    }
}

#else

bool syncFile(QFileDevice &file, QString *error)
{
    const int fd = file.handle();
    if (fd < 0) {
        if (error)
            *error = qt_error_string(EBADF);
        return false;
    }
    // As QSaveFile's commit() (QFSFileEngine::syncToDisk()): the data and what reading it back
    // needs (the size), not the file's times — the rename after it is what must not come first.
    int result = 0;
    do {
#if defined(_POSIX_SYNCHRONIZED_IO) && _POSIX_SYNCHRONIZED_IO > 0
        result = ::fdatasync(fd);
#else
        result = ::fsync(fd);
#endif
    } while (result != 0 && errno == EINTR);
    if (result == 0)
        return true;
    if (error)
        *error = qt_error_string(errno);
    return false;
}

#endif

} // namespace

void installFileSync()
{
    vaultutil::setFileSync(&syncFile);
#ifdef Q_OS_WIN
    vaultutil::setFileReplace(&replaceFile);
#endif
}

} // namespace brulion::platform
