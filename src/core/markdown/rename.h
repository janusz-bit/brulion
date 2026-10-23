// Link and tag rewriting when a page is renamed or moved (docs/SYNTAX.md §2, §3). Part of the
// markdown module — see markdown/markdown.h for the module overview.
#pragma once

#include "markdown/parse.h"
#include "types.h"

#include <QList>
#include <QString>

#include <functional>

namespace brulion::markdown {

/// Which tags a rename may rewrite, asked with a tag's key (lower-case NFC, no '#'; the keys
/// VaultIndex::tagsNaming() lists for the renamed page). An empty function accepts every tag
/// the names match.
using TagFilter = std::function<bool(const QString &key)>;

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

/// Rewrite the tags that name a renamed page (content in → content out; SYNTAX §3 "Tagi jako
/// linki do stron"), the companion of renameLinks() (run both; a `[[link]]` in a tag list is the
/// links' part). Rewritten are exactly the occurrences of findNoteTags():
///   * `#old` (any letter case) -> tagSyntax(newName): `#Nowa`, or `#[[Nowa nazwa]]` when the
///     name is no simple tag; `#[[old]]` keeps the bracket form (`#[[Nowa]]`);
///   * the items of `tags::` / `tag::` properties (`tags:: old, praca`): the item's text
///     (`[[New, name]]` when a comma or a leading quote / '#' / '[' would change the list);
///   * the items of the frontmatter's `tags:` / `tag:` (flow `[a, b]`, block `- a`, quoted
///     items): the item's text; when the YAML would then read differently, that key's lines are
///     rewritten by setFrontmatterValue() instead.
/// `oldName` matches case-insensitively (NFC, not diacritic-insensitive); an empty `oldName`
/// rewrites path-qualified tags only (a duplicate base name whose tag names another page). With
/// `oldPath` (and `newPath`) path-qualified tags (`#Projekty/Dom`, also a path suffix
/// `#Stare/Dom`) are rewritten too: a rename in place keeps the written folder prefix, a move
/// writes the new vault path without `.md`. A base-name tag is rewritten only when the name
/// changes (a move keeps `#Dom`). Sub-tags (`#old/x`), longer tags (`#oldx`), code, comments and
/// formulas are never touched; line endings and every other byte are preserved. A new name no tag
/// can hold (`[`, `]`, `|`) leaves the tags as they are. With `naming`, only the tags it accepts
/// are rewritten: the names alone cannot tell whether `#dom` or the path suffix `#Stare/Dom`
/// names the renamed page or another one with that name (VaultIndex::tagsNaming() can). The
/// `tag:` filters of ```query blocks are not tags and stay as they are.
QString renameTags(const QString &content, const QString &oldName, const QString &newName,
                   const VaultPath &oldPath = {}, const VaultPath &newPath = {}, const TagFilter &naming = {});

/// The occurrences renameTags() would rewrite with the same arguments, in document order (e.g.
/// to count them before asking). Frontmatter names in a YAML form findNoteTags() cannot place
/// come last, without a range (from = to = -1).
QList<NoteTag> tagsToRename(const QString &content, const QString &oldName, const QString &newName,
                            const VaultPath &oldPath = {}, const VaultPath &newPath = {}, const TagFilter &naming = {});

} // namespace brulion::markdown
