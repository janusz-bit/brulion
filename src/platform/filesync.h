// The platform's file sync for the vault's own atomic writes (core: vault/filesync.h).
#pragma once

namespace brulion::platform {

/// Sets vaultutil::setFileSync() to this platform's call (fsync(), Windows' _commit()). Once, at
/// start-up, before a vault opens.
void installFileSync();

} // namespace brulion::platform
