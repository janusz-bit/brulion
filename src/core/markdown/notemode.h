// Note mode (docs/SYNTAX.md §4 "Tryb notatki / Note mode"): whether a note is shown as a
// Logseq-like outline (a bullet per block) or as an Obsidian-like continuous document. Part of
// the markdown module — see markdown/markdown.h for the module overview.
//
// An explicit choice is stored with the note, so it is portable and synced: the YAML frontmatter
// key `brulion: document` / `brulion: outline` (the Obsidian plugin convention, like Kanban's
// `kanban-plugin: board`). Resolution order (resolveNoteMode):
//   1. frontmatter `brulion` with a valid value (case-insensitive)      -> Frontmatter
//   2. journal pages                       -> VaultConfig::journalMode   -> JournalDefault
//   3. other pages: the top-level blocks without frontmatter / page properties; with at least two
//      of them, >= 70 % list items -> outline, <= 30 % -> document          -> Heuristic
//   4. otherwise (empty, one block, mixed) -> VaultConfig::pageMode      -> PageDefault
// Imported Logseq pages (all bullets) therefore open as outlines and Obsidian notes (headings and
// paragraphs) as documents without any key being written.
//
// The key is written only on an explicit user choice (withNoteModeChoice() for a new note,
// setNoteModeKey() for a toggle). Changing the mode never touches anything but the `brulion`
// entry of the frontmatter.
#pragma once

#include "types.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::markdown {

/// The frontmatter key holding an explicit note mode.
inline constexpr QStringView NoteModeKey = u"brulion";

/// Where a resolved note mode comes from (resolution steps 1-4 above).
enum class NoteModeSource { Frontmatter, JournalDefault, Heuristic, PageDefault };

struct ResolvedNoteMode {
    NoteMode mode = NoteMode::Document;
    NoteModeSource source = NoteModeSource::PageDefault;

    /// True when the note itself names its mode (frontmatter key).
    bool isExplicit() const { return source == NoteModeSource::Frontmatter; }
    friend bool operator==(const ResolvedNoteMode &, const ResolvedNoteMode &) = default;
};

/// "outline" / "document" — the value written to the frontmatter and to config.json.
QString noteModeName(NoteMode mode);

/// Inverse of noteModeName(), case-insensitive and ignoring surrounding whitespace; nullopt for
/// any other text.
std::optional<NoteMode> parseNoteMode(QStringView name);

/// The mode named by the frontmatter key `brulion` (a string value, see parseNoteMode()), or
/// nullopt without frontmatter, without the key or with an invalid value.
std::optional<NoteMode> frontmatterNoteMode(QStringView content);

/// Resolution step 3 alone: the mode suggested by the top-level blocks of `content` (frontmatter
/// and Logseq page properties excluded, headings count as non-list blocks), or nullopt when the
/// content does not decide (fewer than two blocks, or between 30 % and 70 % list items).
std::optional<NoteMode> heuristicNoteMode(QStringView content);

/// The mode of a note (resolution order above). `isJournal`: the note is a daily note
/// (journalDateFromPath() gives a date). Never fails; linear in the size of `content`.
ResolvedNoteMode resolveNoteMode(QStringView content, bool isJournal, const VaultConfig &config);

/// Set (`mode`) or remove (nullopt) the frontmatter key `brulion` — content in, content out.
/// * Setting: an existing entry is replaced in place (its line ending kept; duplicate entries
///   are dropped), a missing key is appended before the closing delimiter, and a note without
///   frontmatter gets `---\nbrulion: <mode>\n---\n` in front (after a BOM, with the document's
///   line ending). Unchanged content when the key already names `mode`.
/// * Removing: every `brulion` entry is removed; when the frontmatter then holds nothing but blank
///   lines, the whole block goes (so set + remove gives back a note that had no frontmatter).
/// Every other byte — other keys, comments, order, line endings, BOM, the body — is preserved, so
/// removing the key again restores the original of a note whose frontmatter had other content.
/// Each edit is verified (the other keys must parse to the same values and the body must be
/// identical); frontmatter that cannot be edited safely (malformed YAML, or a key that is not on
/// a line of its own at column 0, as in an indented root mapping — a second key would make the
/// YAML invalid for other tools) gives the unchanged content — callers can check
/// frontmatterNoteMode(result).
QString setNoteModeKey(QStringView content, std::optional<NoteMode> mode);

/// The initial content of a new note for which the user chose `chosen` (new-page dialog): the
/// key is written only when resolveNoteMode() of `content` would not give `chosen` anyway.
QString withNoteModeChoice(QStringView content, NoteMode chosen, bool isJournal, const VaultConfig &config);

} // namespace brulion::markdown
