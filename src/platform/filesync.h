// The platform's file calls for the vault's own atomic writes (core: vault/filesync.h).
#pragma once

#include <QString>

namespace brulion::platform {

/// Sets vaultutil::setFileSync() to this platform's call (fdatasync()/fsync(), Windows'
/// FlushFileBuffers()) and, on Windows, vaultutil::setFileReplace() to MoveFileExW() (long paths,
/// the refusals of a virus scanner or an indexer tried again). Once, at start-up, before a vault
/// opens.
void installFileSync();

/// Windows' extended-length form of an absolute, clean path, as Qt's own file calls use it (so a
/// path deeper than MAX_PATH works without the system's LongPathsEnabled): backslashes and
/// "\\?\" — "\\?\UNC\server\share\…" for a share; one with a prefix or a device path
/// ("\\.\…") as it is; a relative one only with backslashes. The prefix also turns off the
/// system's clean-up of a path (doubled separators, ".", "..", a name's trailing dots and spaces):
/// the rename cleans its paths with GetFullPathNameW() first, as Qt does. Pure, on every platform.
QString extendedLengthPath(const QString &absolute);

} // namespace brulion::platform
