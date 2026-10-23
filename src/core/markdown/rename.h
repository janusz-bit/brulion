// Link and tag rewriting when a page is renamed or moved (docs/SYNTAX.md §2, §3): by name
// (renameLinks(), renameTags()) or link by link (retargetLinks(), retargetTags() — what
// VaultIndex::planRelocation() writes so every link keeps its target). Part of the markdown
// module — see markdown/markdown.h for the module overview.
#pragma once

#include "markdown/parse.h"
#include "types.h"

#include <QHash>
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

/// One link to point at another target (retargetLinks()).
struct LinkEdit {
    /// The link's document range as parseNote() reports it (Link::from, Link::to).
    qsizetype from = 0;
    qsizetype to = 0;
    /// The target to write: for wiki links and embeds as written between `[[` and `#` / `|`
    /// ("Projekty/Dom", "plan.png"); for Markdown links the destination's path, decoded
    /// ("Projekty/Mój dom.md") — it is percent-encoded here.
    QString newTarget;

    friend bool operator==(const LinkEdit &, const LinkEdit &) = default;
};

/// Point links at other targets (content in → content out). Each edit names a link by its range
/// — a wiki link, an embed or a Markdown link to a file that parseNote() reports at exactly that
/// range (so never in code, comments or HTML blocks) — and only the link's target is replaced:
/// `|alias`, `#Heading`, `#^id`, the `!` of embeds, blanks inside `[[ ]]`, a Markdown link's
/// text, fragment and angle brackets stay. Markdown destinations are percent-encoded ('%' and
/// '#' always; spaces and parentheses in a plain destination, '<' and '>' in an angle one).
/// Edits that match no link, overlap an earlier one or have a target no link can hold (empty, a
/// line break, `[[`, `]]`, '|' or '#' in a wiki target) are ignored; every other byte — line
/// endings, a BOM — is preserved, and content without changes is returned as is.
QString retargetLinks(const QString &content, const QList<LinkEdit> &edits);

/// Rename tags by key (content in → content out): every occurrence of findNoteTags() — `#tag`,
/// `#[[tag]]`, `tags::` items, the frontmatter's `tags:` items — whose key (lower-case NFC,
/// without '#'; trailing '/' ignored) `names` maps is written as the mapped name, in its own form
/// as renameTags() writes a new name (`#Projekty/Dom`, `#[[Projekty/Mój dom]]`, a property or
/// YAML item; a frontmatter value the items cannot express is rewritten as a list). Names no tag
/// can hold leave their tags as they are; everything else is preserved byte for byte.
QString retargetTags(const QString &content, const QHash<QString, QString> &names);

} // namespace brulion::markdown
