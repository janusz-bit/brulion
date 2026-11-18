#include "filesync.h"

#include "vault/filesync.h"

#include <QDir>
#include <QFileDevice>
#include <QFileInfo>
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

/// A path for the wide Win32 calls as Qt's own file calls give them (QFile made the temporary
/// file): native separators, absolute, with the extended-length prefix — so a note deeper than
/// MAX_PATH (260) characters is renamed like QSaveFile renames it, without the system's
/// LongPathsEnabled. ("\\?\" also turns off the system's clean-up of the path: cleanPath() does it.)
std::wstring win32Path(const QString &path)
{
    QString native = QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    if (native.startsWith(u"\\\\?\\"_s) || native.startsWith(u"\\\\.\\"_s))
        return native.toStdWString();
    if (native.startsWith(u"\\\\"_s)) // \\server\share\… -> \\?\UNC\server\share\…
        return (u"\\\\?\\UNC\\"_s + native.sliced(2)).toStdWString();
    return (u"\\\\?\\"_s + native).toStdWString();
}

vaultutil::Replaced replaceFile(const QString &from, const QString &to, QString *error)
{
    // What QSaveFile's commit() does too (QFileSystemEngine::renameOverwriteFile()): one move that
    // replaces the file. No MOVEFILE_COPY_ALLOWED: both lie in one folder, and a copy would not be
    // atomic.
    if (MoveFileExW(win32Path(from).c_str(), win32Path(to).c_str(), MOVEFILE_REPLACE_EXISTING))
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
