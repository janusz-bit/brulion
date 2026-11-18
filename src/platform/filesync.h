// The platform's file calls for the vault's own atomic writes (core: vault/filesync.h).
#pragma once

namespace brulion::platform {

/// Sets vaultutil::setFileSync() to this platform's call (fdatasync()/fsync(), Windows'
/// FlushFileBuffers()) and, on Windows, vaultutil::setFileReplace() to MoveFileExW() (long paths,
/// the refusals of a virus scanner or an indexer tried again). Once, at start-up, before a vault
/// opens.
void installFileSync();

} // namespace brulion::platform
