// Vault path helpers. Vault paths are POSIX-style, relative, without leading "./" or "/".
#pragma once

#include "types.h"

#include <functional>
#include <optional>

namespace brulion::paths {

/// Normalise separators, collapse "." and "..", strip leading/trailing slashes, NFC.
VaultPath normalize(const QString &path);
VaultPath join(const QString &a, const QString &b);
/// "a/b/c.md" -> "a/b"; "c.md" -> "".
QString dirname(const VaultPath &path);
/// "a/b/c.md" -> "c.md".
QString basename(const VaultPath &path);
/// "a/b/c.md" -> ".md" (lower-case), "" when none.
QString extension(const VaultPath &path);
bool isMarkdown(const VaultPath &path);
/// "a/b/Page.md" -> "a/b/Page".
QString stripMarkdownExtension(const VaultPath &path);
/// "a/b/Page.md" -> "Page".
QString pageTitle(const VaultPath &path);
/// Paths that must never be indexed: .brulion/, .trash/, .git/, .obsidian/, logseq/, .stfolder,
/// .stversions/, node_modules/, and temp files of sync tools/editors.
bool isIgnored(const VaultPath &path);
bool isTempFile(const QString &fileName);

struct SyncthingConflict {
    VaultPath original;
    QString time;    ///< "YYYY-MM-DDTHH:mm:ss" local
    QString device;  ///< short device id
};
/// Parse "dir/Note.sync-conflict-20261014-101010-ABCDEFG.md".
std::optional<SyncthingConflict> parseSyncthingConflict(const VaultPath &path);
/// Syncthing conflict copy or Brulion's own "Name.conflict-<device>-<stamp>.md".
bool isConflictCopy(const VaultPath &path);

/// File-name-safe page name (keeps Unicode letters, drops separators and reserved chars).
QString sanitizeFileName(const QString &name);
/// Append " 2", " 3", … before the extension until `exists` returns false.
VaultPath uniquePath(const VaultPath &path, const std::function<bool(const VaultPath &)> &exists);

} // namespace brulion::paths
