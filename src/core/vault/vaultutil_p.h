// Internal helpers of the vault module: path arithmetic used by the Vault (pure functions,
// unit-tested in tst_vault_paths) and the module's logging category. Not part of the public API.
#pragma once

#include "log/categories.h"
#include "types.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QStringList>

#include <functional>

namespace brulion {

/// "brulion.vault" (the log module's category).
using logging::lcVault;

namespace vaultutil {

/// ".brulion" — the folder of the shared vault settings (ignored except for config.json).
QString configFolder();

/// Ancestor folders, outermost first: "a/b/c.md" -> {"a", "a/b"}; "c.md" -> {}.
QStringList ancestors(const VaultPath &path);

/// True if `path` equals `dir` or lies inside it. The root ("") contains everything.
bool isWithin(const VaultPath &path, const VaultPath &dir);

/// True if `path` lies strictly inside `dir`.
bool isStrictlyWithin(const VaultPath &path, const VaultPath &dir);

/// Re-root `path` from `from` to `to`: rebase("a/x/y.md", "a", "b") == "b/x/y.md".
/// `path` must be within `from`.
VaultPath rebase(const VaultPath &path, const VaultPath &from, const VaultPath &to);

/// Insert `suffix` before the extension of the last segment: ("a/b.md", " 2") -> "a/b 2.md".
/// Folders (`isDir`), names without an extension and dot files (".env") get it appended.
VaultPath insertBeforeExtension(const VaultPath &path, const QString &suffix, bool isDir = false);

/// `base`, or the first of "Name 2.ext", "Name 3.ext", ... for which `isTaken` is false.
/// Unlike paths::uniquePath() the extension keeps its case ("Raport.PDF" -> "Raport 2.PDF").
VaultPath uniqueName(const VaultPath &base, const std::function<bool(const VaultPath &)> &isTaken, bool isDir = false);

/// Local wall-clock trash stamp "yyyyMMdd-HHmmss".
QString trashStamp(const QDateTime &time);

/// Remove a " (yyyyMMdd-HHmmss)" or " (yyyyMMdd-HHmmss) n" trash suffix from one path segment
/// ("a (20261014-101112) 2.md" -> "a.md").
QString stripTrashStamp(const QString &segment);

/// Files Brulion handles as UTF-8 text (hashed, cached and snapshotted into History);
/// everything else is binary and compared by size and modification time only.
bool isTextPath(const VaultPath &path);

/// Paths the vault tracks: everything paths::isIgnored() lets through, plus the vault config.
bool isTracked(const VaultPath &path);

/// "\xEF\xBB\xBF" — the UTF-8 byte order mark.
QByteArray utf8Bom();

/// Writes `bytes` to `absolutePath` atomically (QSaveFile: a temporary file renamed over it). A
/// rename refused for a moment — on Windows while another program has the file open for a look
/// after each write (a virus scanner, an indexer) — is tried again a few times within about a
/// tenth of a second, as Syncthing and editors do; any other failure is final. `error`: why not.
bool saveAtomically(const QString &absolutePath, QByteArrayView bytes, QString *error);

/// UTF-8 bytes to text with a leading byte order mark removed.
QString decodeUtf8(const QByteArray &bytes);

} // namespace vaultutil
} // namespace brulion
