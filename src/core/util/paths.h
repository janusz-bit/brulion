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
/// Is `ext` (as returned by extension(), with the dot) a real file extension: 1-10 ASCII
/// letters/digits with at least one letter, not Markdown? ".png", ".pdf", ".mp3" are; "",
/// ". smith" ("Dr. Smith"), ".10" ("Spotkanie 14.10"), ".2" ("v1.2") are not. A link target with
/// one names an attachment (link resolution, the path parts of a link: docs/SYNTAX.md §2).
bool isAttachmentExtension(QStringView ext);
/// An attachment picture (png, jpg, jpeg, gif, webp, svg, bmp, avif, ico): opened as a picture,
/// not as text (the file's bytes are not a note), embedded as a picture, exported as <img>.
bool isPicturePath(const VaultPath &path);
/// The extensions isPicturePath() accepts (".png", ".jpg", …: lower-case, with the dot), for
/// lists that name them (the picture file dialog's filter).
const QStringList &pictureExtensions();
/// An attachment PDF by its name (".pdf", any case; also a hidden file named ".pdf"): opened in the
/// PDF viewer, embedded as its first page's thumbnail (docs/UI.md "PDF viewer").
bool isPdfPath(const VaultPath &path);
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
