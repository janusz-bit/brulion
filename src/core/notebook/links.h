// The links Brulion copies to a page or a block (docs/SYNTAX.md §2 "Copied links"), computed from a
// note's text so that the editor's "Copy link" and `brulion-cli block link` give the same
// text. Part of the notebook module — see notebook/notebook.h.
#pragma once

#include "markdown/inline.h"
#include "types.h"

#include <QString>

#include <functional>
#include <optional>

namespace brulion {
class VaultIndex;
}

namespace brulion::notebook {

/// The page part of every link Brulion copies to `path` (docs/SYNTAX.md §2 "Copied links") — the
/// "Copy link" of the page menu, the folder tree and the editor, and `brulion-cli block link`
/// alike: a daily note by its file name ([[2026-10-22]] reads as the
/// date, also before the note exists); any other page by the shortest [[target]] that reaches it
/// from anywhere, VaultIndex::linkTarget() — the title, or the shortest folder path suffix
/// ("Projekty/Dom") when the title names another page too —, which `shortestTarget` gives ("" when
/// the index has no such page). A page the index does not have yet (a new one) by its title, or by
/// its path without ".md" when the title leads to another page (`resolveTitle`: title -> path).
QString pageLinkTarget(const VaultPath &path, const VaultConfig &config,
                       const std::function<QString(const VaultPath &path)> &shortestTarget,
                       const std::function<QString(const QString &title)> &resolveTitle);
/// pageLinkTarget() with the index's linkTarget() and resolveLink() (no index: the title).
QString pageLinkTarget(const VaultPath &path, const VaultConfig &config, const VaultIndex *index);

/// What a link menu's "Copy link" copies for a link of rendered text (`url`: an InlineRenderer
/// URL) written on `fromPath`: `#tag` / `#[[tag with spaces]]` for tag:, `((id))` for block:, the
/// address for ext:, and for page: `[[target#fragment]]` with the page by pageLinkTarget() (a
/// `[[#Heading]]` names `fromPath`) — as written when no page has that name (`path` "" then).
/// For a path part of a link (part:, docs/SYNTAX.md §2 "Path parts"; `resolvePart(target, part,
/// from)`: VaultIndex::resolveLinkPart()): a page by `[[pageLinkTarget()]]`, a folder by its path
/// with a trailing '/' as a `path:` query filter takes it (`Klienci/Firma/`; quoted when it is not
/// one token: `"Projekty/Zażółć gęślą/"`), a new page by `[[prefix]]` as written; empty for a
/// malformed part: URL or without `resolvePart`.
/// `resolveLink(target, from)`: a link target written on `from` -> the page's path, "" when none.
/// Empty `text` for other URLs.
struct CopiedLink {
    QString text;
    QString path;   ///< page: the linked page's path ("" when it does not exist); part: the page's or the folder's
    QString folder; ///< part: naming a folder — its vault path (then `text` is "<folder>/", maybe quoted)
};
CopiedLink
copiedLink(const QString &url, const QString &fromPath, const VaultConfig &config,
           const std::function<QString(const QString &target, const QString &from)> &resolveLink,
           const std::function<QString(const VaultPath &path)> &shortestTarget,
           const std::function<std::optional<markdown::LinkPartTarget>(const QString &target, int part,
                                                                       const QString &from)> &resolvePart = {});

struct BlockLinks {
    QString id;          ///< the block id (without '^')
    bool added = false;  ///< the id was written now: `content` carries it
    QString content;     ///< the note with the id
    int line = -1;       ///< the block's first line (0-based)
    QString ref;         ///< "((id))" — the Logseq reference, resolved in the whole notebook
    QString wikiLink;    ///< "[[Page#^id]]"
    QString embed;       ///< "!((id))"; a heading: "![[Page#Heading]]" (a unique name), else "![[Page#^id]]"
    QString headingLink; ///< "[[Page#Heading]]" for a heading with a unique name, else ""
};

/// The links to the block containing `line` of `content` (any line of the block, also a blank line
/// after it), adding a block id (markdown::BlockDocument::ensureBlockId, avoiding ids `isTaken`
/// knows) when it has none. nullopt for a line in no block or a block that cannot carry an id
/// (frontmatter, an unclosed code fence).
std::optional<BlockLinks> blockLinks(const QString &content, int line, const QString &pageTarget,
                                     const std::function<bool(const QString &id)> &isTaken = {});

} // namespace brulion::notebook
