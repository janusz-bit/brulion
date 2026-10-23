// The links Brulion copies to a page or a block (docs/SYNTAX.md §2 "Copied links"), computed from a
// note's text so that the editor's "Kopiuj odnośnik" and `brulion-cli block link` give the same
// text. Part of the notebook module — see notebook/notebook.h.
#pragma once

#include "types.h"

#include <QString>

#include <functional>
#include <optional>

namespace brulion::notebook {

/// The page part of a copied link to `path` (PageController::linkTarget()): a daily note by its
/// title ([[2026-10-22]] reads as the date), else the title — unless the title leads to another
/// page (`resolveTitle` gives that page's path), then the path without ".md". A title that leads
/// nowhere yet leads here once the page exists.
QString pageLinkTarget(const VaultPath &path, const VaultConfig &config,
                       const std::function<QString(const QString &title)> &resolveTitle);

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
