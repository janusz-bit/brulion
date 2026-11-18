#include "filesync.h"

#include "vault/filesync.h"

#include <QFileDevice>

#include <cerrno>

#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

namespace brulion::platform {

namespace {

bool syncFile(QFileDevice &file)
{
    // (QFile::handle() is a C library descriptor everywhere, on Windows one Qt opens for it.)
    const int fd = file.handle();
    if (fd < 0) {
        errno = EBADF; // (the caller's message reads it)
        return false;
    }
#ifdef Q_OS_WIN
    return _commit(fd) == 0;
#else
    return ::fsync(fd) == 0;
#endif
}

} // namespace

void installFileSync()
{
    vaultutil::setFileSync(&syncFile);
}

} // namespace brulion::platform
