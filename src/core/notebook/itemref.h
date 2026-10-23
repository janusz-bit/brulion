// ItemRef — a stable address of a line or a block of a note, for tools that read a note now and
// change it later (brulion-cli: `task done "Dom.md#L12@3f9a01bc"`). Part of the notebook module —
// see notebook/notebook.h.
//
//   "Projekty/Dom.md#L12@3f9a01bc"   line 12 (1-based, as editors count) whose text hashes to
//                                    3f9a01bc (lineHash(): 8 hex digits of FNV-1a over its UTF-8)
//   "Projekty/Dom.md#L12"            line 12, whatever it holds now
//   "Dom.md#^abc123", "Dom#^abc123"  the block with id abc123 in that page
//   "^abc123", "((abc123))"          the block with that id anywhere in the notebook
//
// resolve() finds the line again after other lines were inserted or removed: the hash first at the
// line itself, then at the nearest line within ±50 lines holding the same text (Moved); the line
// is Stale when its text changed (or it is too far away), so a tool never edits the wrong task.
#pragma once

#include "types.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::notebook {

struct ItemRef {
    QString page;     ///< the page part as written (a path or a page name; "" for "^id" / "((id))")
    int line = -1;    ///< 0-based line (written 1-based after "#L"), -1 when none
    QString lineHash; ///< 8 lower-case hex digits, "" when none
    QString blockId;  ///< without '^', "" when none

    bool hasLine() const { return line >= 0; }
    bool hasBlock() const { return !blockId.isEmpty(); }
    friend bool operator==(const ItemRef &, const ItemRef &) = default;
};

/// 8 hex digits of FNV-1a 64 over the UTF-8 of `lineText` (the line without its terminator).
QString lineHash(QStringView lineText);

/// "path#L13@hash" for 0-based `line` of `path` holding `lineText`.
QString formatLineRef(const VaultPath &path, int line, QStringView lineText);
/// "path#^id".
QString formatBlockRef(const VaultPath &path, const QString &blockId);
/// The ref as written by the formatters above ("^id" without a page).
QString formatRef(const ItemRef &ref);

/// Does `text` look like an item reference (it has "#L<n>", "#^id", or is "^id" / "((id))")?
bool looksLikeItemRef(QStringView text);
/// Parse a reference; nullopt for malformed input ("#L", "#L0", "#L-1", "@zz", a NUL character,
/// an id with characters ids never have, more than 4096 characters). A plain page name without
/// "#L" / "#^" parses as {page} only.
std::optional<ItemRef> parseItemRef(QStringView text);

struct ResolvedLine {
    enum Status {
        Ok,       ///< the line is where the ref says (and holds the text it hashed)
        Moved,    ///< the text is at another line now (`line`)
        Stale,    ///< the line's text changed: nothing safe to edit
        NotFound, ///< no such line or block
    };
    Status status = NotFound;
    int line = -1; ///< 0-based
};

/// Find `ref`'s line in `content`: by block id (the block's first line: `^id` or Logseq `id::`),
/// else by line and hash (see the file comment); a ref without a hash takes the line as it is.
ResolvedLine resolve(const QString &content, const ItemRef &ref, const VaultConfig &config = VaultConfig());

} // namespace brulion::notebook
