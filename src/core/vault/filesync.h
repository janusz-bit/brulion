// The file system calls of the vault's own atomic writes that Qt has no portable call for. The
// vault recognises the echo of its own write from the folder's stamps only when it syncs the
// written data itself, before its rename (vaultutil::saveAtomicallyStamped()), so the platform layer
// sets the sync (src/platform/filesync.cpp: fdatasync()/fsync(), Windows' FlushFileBuffers()) and,
// where the C++ library's rename falls short, the rename over the file (Windows: MoveFileExW() with
// long paths, and which refusals are a moment's). Unset — brulion-cli, a platform without them —
// the writes are QSaveFile's (synced inside commit()) and the echo lists the folder.
#pragma once

class QFileDevice;
class QString;

namespace brulion::vaultutil {

/// Syncs an open file's data to its storage (true: done; `error`: why not).
using FileSync = bool (*)(QFileDevice &file, QString *error);

/// Sets the sync (once, at start-up, before a vault opens; null: none).
void setFileSync(FileSync sync);
FileSync fileSync();

/// What a FileReplace did.
enum class Replaced {
    Done,
    /// Refused for a moment: another program holds one of the files (on Windows a virus scanner,
    /// an indexer or a sync client that opened it without sharing its deletion) — tried again.
    Busy,
    Failed, ///< any other failure: final
};

/// Renames the file `from` over the file `to` (absolute paths in one folder; `to` may exist and is
/// replaced in one step). `error`: why not.
using FileReplace = Replaced (*)(const QString &from, const QString &to, QString *error);

/// Sets the rename (once, at start-up; null: std::filesystem::rename(), whose refusals for lack of
/// permission count as Busy).
void setFileReplace(FileReplace replace);
FileReplace fileReplace();

} // namespace brulion::vaultutil
