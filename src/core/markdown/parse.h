// parseNote — turns a note into the ParsedNote model of types.h (docs/SYNTAX.md §1-§7, §11):
// block tree with text, properties, tasks, reminders, tags and ids, plus page-level properties,
// aliases, tags, links and headings. Part of the markdown module — see markdown/markdown.h.
#pragma once

#include "types.h"

#include <QStringView>

namespace brulion::markdown {

/// Parse a note. `path` gives the title (file base name) and, with `config`, the journal date;
/// `config.defaultReminderTime` completes date-only reminders. Never fails for any input and
/// runs in linear time.
///
/// Blocks are listed in document pre-order (ParsedNote::blocks), the tree is given by
/// Block::parent / Block::children (indices). Block::text has the list marker, checkbox, block
/// id, property / planning / drawer lines, metadata tokens and `%%comments%%` removed; inline
/// Markdown is kept. Code and HTML blocks are never interpreted (their text is the source).
/// Headings lists the top-level headings only (not those inside callouts or quotes). Property
/// keys are lower-cased; a callout block also gets "callouttype" (and "calloutfold": "+"/"-").
ParsedNote parseNote(const VaultPath &path, QStringView content, const VaultConfig &config = VaultConfig());

/// Index (into note.blocks) of the innermost block whose line range contains `line`, or -1.
int blockAtLine(const ParsedNote &note, int line);

} // namespace brulion::markdown
