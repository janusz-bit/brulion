// Pure content edits (content in → content out). Only the affected lines change; everything
// else, including "\r\n" / "\n" line endings, is preserved byte for byte. Part of the markdown
// module — see markdown/markdown.h for the module overview.
#pragma once

#include "types.h"

#include <QDate>
#include <QString>

#include <functional>
#include <optional>

namespace brulion::markdown {

/// Set the status of the task at `line` (any line of a list item targets the item's first
/// line). Checkboxes get the status character, Logseq keywords the matching keyword (same
/// workflow: TODO/DOING or LATER/NOW). Becoming done appends `✅ today`, becoming cancelled
/// `❌ today` (before a trailing block id); leaving those states removes them; a Polish 🔁 rule
/// on the line is rewritten in canonical English. Completing a recurring task (`🔁` or a Logseq
/// repeater on a SCHEDULED/DEADLINE line) inserts its next occurrence directly above it — same
/// indentation, status todo, dates shifted by the rule from the due (else scheduled, start,
/// reminder) date or from `today` for "when done" rules, ➕ set to today, no ✅/❌/block id — like
/// Obsidian Tasks. A non-task line becomes a task (`[ ] ` after its list marker, or a `- [ ] `
/// prefix). Lines in code, frontmatter, tables, headings, rules, callout headers, hidden
/// comments and out-of-range lines leave the content unchanged.
QString setTaskStatus(const QString &content, int line, TaskStatus status, QDate today);

/// Toggle the task at `line`: todo / doing / deferred → done; done / cancelled → todo. A line
/// that is not a task becomes a todo task.
QString toggleTask(const QString &content, int line, QDate today);

/// Remove the task marker of the task at `line` ("no status"; any line of a list item targets the
/// item's first line): the checkbox or Logseq keyword and the space after it go, so do the ✅ / ❌
/// dates; the list item and the rest of its text stay ("- [x] a ✅ 2026-10-14" → "- a",
/// "- TODO a" → "- a"). Lines that are not tasks (and every line setTaskStatus leaves alone) give
/// the content unchanged.
QString removeTaskMarker(const QString &content, int line);

/// Random 6-character block id `[a-z0-9]` (QRandomGenerator::global(), unbiased).
QString generateBlockId();

struct EnsuredBlockId {
    QString content;
    QString id; ///< empty when the block cannot carry an id
};

/// The id of the block at `line`, adding one when missing (SYNTAX §4): ` ^abc123` at the end of
/// a list item's / heading's / callout's first line or of a paragraph's / quote's last line; a
/// separate `^abc123` line after code blocks, tables, HTML, rules and quotes with nested blocks.
/// Every placement is verified by re-parsing (the other end of the block is used when the
/// preferred one does not work, e.g. inside a `%%comment%%`). Existing `^id` and Logseq `id::`
/// ids are returned unchanged. Blank lines, frontmatter and blocks that cannot carry an id (an
/// unclosed code fence) give the unchanged content and an empty id. `isTaken` lets callers
/// avoid ids used elsewhere (e.g. vault-wide); ids of this content are always avoided.
EnsuredBlockId ensureBlockId(const QString &content, int line,
                             const std::function<bool(const QString &)> &isTaken = {});

/// Character range [from, to) of the list item at `line` together with all its children: from
/// the start of the item's first line to the end of its last line (terminator excluded).
/// nullopt when `line` is not inside a list item.
struct TextRange {
    qsizetype from = 0;
    qsizetype to = 0;
    friend bool operator==(const TextRange &, const TextRange &) = default;
};
std::optional<TextRange> listItemRange(const QString &content, int line);

} // namespace brulion::markdown
