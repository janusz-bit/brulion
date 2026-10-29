// parseNote — turns a note into the ParsedNote model of types.h (docs/SYNTAX.md §1-§7, §11):
// block tree with text, properties, tasks, reminders, tags and ids, plus page-level properties,
// aliases, tags, links and headings. Part of the markdown module — see markdown/markdown.h.
#pragma once

#include "types.h"

#include <QList>
#include <QString>
#include <QStringView>

namespace brulion::markdown {

/// The version of parseNote()'s output. Bump it whenever parseNote() returns anything different
/// for any input (another block split, text, property, task field, link offset…): the index
/// cache (index/indexcache.h) keeps parsed notes on the device and must not hand out old parses.
/// tst_markdown_model's golden digest (kParseDigest) fails until you do.
inline constexpr int ParserVersion = 1;

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
/// The same; the blocks' raw texts (Block::raw, BlockSource) share `content` instead of a copy of
/// it (the index keeps the note's text anyway).
ParsedNote parseNote(const VaultPath &path, const QString &content, const VaultConfig &config = VaultConfig());

/// Index (into note.blocks) of the innermost block whose line range contains `line`, or -1.
int blockAtLine(const ParsedNote &note, int line);

/// How a tag occurrence is written (see findNoteTags()).
enum class TagForm {
    Hash,        ///< `#tag` in the text (also in a property value: `tags:: #a`, `typ:: #b`)
    Bracketed,   ///< `#[[tag with spaces]]`
    Property,    ///< an item of a `tags::` / `tag::` value that is no `#tag` (`tags:: a, b`)
    Frontmatter, ///< an item of the frontmatter `tags:` / `tag:` value (`tags: [a, "b"]`)
};

/// One tag occurrence of a note (findNoteTags()).
struct NoteTag {
    QString name;       ///< as written, NFC, without '#' and decoration (key: lower-cased name)
    qsizetype from = 0; ///< document range: Hash / Bracketed from the '#' to the end of the tag
    qsizetype to = 0;   ///< (a trailing '/' excluded, `]]` included); Property / Frontmatter
                        ///< the name only (no quotes, no '#')
    int line = 0;       ///< 0-based line of `from`
    TagForm form = TagForm::Hash;

    friend bool operator==(const NoteTag &, const NoteTag &) = default;
};

/// Every tag occurrence of a whole note, in document order, by exactly the rules parseNote()
/// tags blocks and pages with (SYNTAX §3): inline `#tag` / `#[[…]]` outside code, fences, HTML
/// blocks, comments and formulas, the items of `tags::` / `tag::` properties, and the items of
/// the frontmatter's `tags:` / `tag:` key that can be placed in the YAML source (plain or quoted
/// scalars, flow and block lists; an item that is a `[[link]]` is left to the links). Tags that
/// are only the text of a `[[link]]` (`tags:: [[Dom]]`) are links: see renameLinks().
QList<NoteTag> findNoteTags(QStringView content);

} // namespace brulion::markdown
