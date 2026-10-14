// Link rewriting when a page is renamed or moved (docs/SYNTAX.md §2). Part of the markdown
// module — see markdown/markdown.h for the module overview.
#pragma once

#include "types.h"

#include <QString>

namespace brulion::markdown {

/// Rewrite links to a renamed page (content in → content out). The links rewritten are exactly
/// those parseNote() reports (never inside code, comments or HTML blocks): wiki links and embeds
/// (`[[Old]]`, `[[Old|alias]]`, `[[Old#Heading]]`, `![[Old#^id]]`, `[[folder/Old]]`, also in
/// frontmatter values) and relative Markdown links (`[x](Old.md)`,
/// `[x](folder/Old%20Name.md#Heading)`). `oldName` / `newName` are page names (file base names
/// without .md) matched case-insensitively (NFC). With `oldPath` (and `newPath`) path-qualified
/// links are rewritten too: a rename in place keeps the written folder prefix (`../b/Old`), a
/// move writes the full new vault path. Alias, heading and block parts are kept; everything
/// else is preserved byte for byte. Empty names never produce empty link targets.
QString renameLinks(const QString &content, const QString &oldName, const QString &newName,
                    const VaultPath &oldPath = {}, const VaultPath &newPath = {});

} // namespace brulion::markdown
