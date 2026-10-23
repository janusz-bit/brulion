// brulion::markdown — the Brulion Markdown dialect (docs/SYNTAX.md is the source of truth):
// parsing notes into the ParsedNote model of types.h, inline links/tags, metadata tokens, YAML
// frontmatter, pure content edits, journals, templates, note modes, the block editor model
// (BlockDocument) and the rich-text renderer (InlineRenderer). Include this header to get the
// whole module.
//
// API overview (namespace brulion::markdown; never reads the clock — callers pass `today`/`now`):
//
//   parse.h        parseNote(path, content, config) -> ParsedNote   (linear; never fails)
//                    blocks in pre-order with parent/children indices, links, tags, tasks
//                    (checkboxes + Logseq markers, emoji tokens, SCHEDULED/DEADLINE),
//                    reminders (⏰, ⏰!, (@…)), block/page properties, frontmatter, headings,
//                    block ids (^id, id::), journal date
//                  blockAtLine(note, line) -> block index or -1
//                  findNoteTags(content) -> NoteTag{name, from, to, line, form: Hash | Bracketed
//                  | Property | Frontmatter} (every tag occurrence of a note with its range:
//                  inline, `tags::` items, the frontmatter's `tags:` items)
//   inline.h       extractInline(text, baseOffset, line) -> {links, tags}; findTags(text) ->
//                  TagSpan{name, from, to} (every occurrence, for renderers/exporters);
//                  tagSyntax(name) -> "#name" | "#[[name with spaces]]" | "" (the tag naming a
//                  page, SYNTAX §3); maskInlineCode(text)
//   tokens.h       tokens::Due "📅", Scheduled "⏳", Start "🛫", Created "➕", Done "✅",
//                  Cancelled "❌", Recurrence "🔁", Reminder "⏰", Alarm "⏰!", priorityEmoji(p)
//                  findTokens(masked) -> QList<Token>, stripTokens(text),
//                  formatReminderToken(DateSpec, alarm), blockIdAtEnd(line),
//                  parseLogseqTimestamp(inner), recurrenceOf(token)
//   tasks.h        statusFromCheckbox / statusFromKeyword / statusChar / keywordFor,
//                  matchTaskMarker(text, start)
//   properties.h   parsePropertyValue(key, raw), matchPropertyLine(line),
//                  frontmatterProperties(data), namesFrom(value, splitWhitespace)
//   frontmatter.h  findFrontmatter / parseFrontmatter(content), parseYaml(yaml) (subset, no
//                  deps), stringifyYaml / stringifyFrontmatter(data, keyOrder),
//                  setFrontmatterValue / removeFrontmatterValue(content, key[, value])
//   edit.h         pure edits (content in -> content out, EOLs and other bytes preserved):
//                  setTaskStatus(content, line, status, today) (✅/❌ dates, recurring tasks get
//                  their next occurrence inserted above, like Obsidian Tasks),
//                  toggleTask(content, line, today), removeTaskMarker(content, line) (no status),
//                  ensureBlockId(content, line) -> {content, id},
//                  generateBlockId(), listItemRange(content, line) -> {from, to}
//   rename.h       renameLinks(content, oldName, newName[, oldPath, newPath]),
//                  renameTags(...[, naming]) (the tags naming the page: `#old`, `#[[old]]`,
//                  `tags::` and frontmatter items; bracket form kept, sub-tags untouched; `naming`
//                  keeps those of other pages with that name), tagsToRename(...)
//   linkformat.h   links to pages, headings and blocks (SYNTAX §2 "Copied links"):
//                  headingFragment(text) -> "Plan praca" (markup, tag hashes and characters that
//                  break wiki links removed), headingKey(text), headingMatches(fragment, text),
//                  findHeading(headings, fragment) (the one matcher of [[Page#Heading]] and
//                  Obsidian heading paths [[Page#A#B]]: navigation, embeds, copying),
//                  uniqueHeadingFragment(headings, i) (the heading's name, else a path, else ""),
//                  formatReference(kind, page, fragment, id) -> "[[Page#^id]]", "((id))", …
//   journal.h      journalPathForDate(date, config) / journalDateFromPath(path, config)
//                  (Qt QDate format strings from VaultConfig::journalFileFormat)
//   templates.h    applyTemplate(template, {now, title, locale}) -> {text, cursor}
//                  ({{date}}, {{date:FORMAT}}, {{time}}, {{title}}, {{weekday}}, {{cursor}})
//   notemode.h     NoteMode outline / document: resolveNoteMode(content, isJournal, config) ->
//                  ResolvedNoteMode{mode, source} (frontmatter `brulion:` key, journal default,
//                  heuristicNoteMode(content), page default); frontmatterNoteMode(content),
//                  setNoteModeKey(content, mode | nullopt) (pure, only the `brulion` entry
//                  changes), withNoteModeChoice(content, chosen, isJournal, config) (new notes),
//                  noteModeName / parseNoteMode ("outline" / "document")
//   blockdocument.h  BlockDocument — editable list of blocks with lossless round trip:
//                  at(i) -> BlockEntry{uid, kind, prefix, body, depth, parent, headingLevel,
//                  lang, taskStatus, blockId, trailing, line}; toString();
//                  setBody, split, insertAfter, mergeWithPrevious, remove, indent, outdent,
//                  moveUp, moveDown, toggleTask, setTaskStatus, ensureBlockId(i, isTaken),
//                  convertToList, convertToParagraph (leave a list: the document mode) ->
//                  EditResult{changes (Removed/Moved/Inserted/Changed rows),
//                  focus, cursor}; subtreeRange, parent, hasChildren, indexAtLine. Frontmatter /
//                  page-property blocks at the top are pinned (never moved, nothing goes above).
//   inlinerenderer.h  InlineRenderer(RenderContext{resolvePage, blockText, linkedBlockText,
//                  inlineCalc, today, locale, palette, taskCheckbox}).render(body) /
//                  renderBlock(body, kind) ->
//                  escaped Qt rich text with page:/tag:/block:/ext:/task:/reminder: links and
//                  coloured chips for dates, reminders and recurrence (reminder:FROM-TO is the
//                  token's UTF-16 range in the text given to render() / the body given to
//                  renderBlock())
#pragma once

#include "markdown/blockdocument.h"
#include "markdown/edit.h"
#include "markdown/frontmatter.h"
#include "markdown/inline.h"
#include "markdown/inlinerenderer.h"
#include "markdown/journal.h"
#include "markdown/linkformat.h"
#include "markdown/notemode.h"
#include "markdown/parse.h"
#include "markdown/properties.h"
#include "markdown/rename.h"
#include "markdown/tasks.h"
#include "markdown/templates.h"
#include "markdown/tokens.h"
