// The file sync of the vault's own atomic writes. The vault recognises the echo of its own write
// from the folder's stamps only when it syncs the written data itself, before its rename
// (vaultutil::saveAtomicallyStamped()); Qt has no portable call for it, so the platform layer sets
// one (src/platform/filesync.cpp: fsync(), _commit()). Unset — brulion-cli, a platform without
// one — the writes are QSaveFile's (synced inside commit()) and the echo lists the folder.
#pragma once

class QFileDevice;

namespace brulion::vaultutil {

/// Syncs an open file's data to its storage (true: done).
using FileSync = bool (*)(QFileDevice &file);

/// Sets the sync (once, at start-up, before a vault opens; null: none).
void setFileSync(FileSync sync);
FileSync fileSync();

} // namespace brulion::vaultutil
