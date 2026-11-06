// brulion::stats — word and character counts and the reading time of a note or a selection
// (docs/UI.md "Statistics and focus mode"; the page's footer and its More menu). GUI-free and
// pure: text in, numbers out, any thread.
//
// What is counted is the text a reader sees (readableText()):
//   * Markdown syntax goes: heading marks, list markers, task checkboxes and Logseq task keywords,
//     quote marks and a callout's `[!type]`, emphasis, code span backticks, table pipes and their
//     separator rows, rules (`---`), escapes; a link counts as its label (`[[Strona|etykieta]]`
//     -> "etykieta", `[[Strona]]` -> "Strona", `[tekst](https://…)` -> "tekst" — the target of a
//     labelled link is not read); a bare address (`https://…`, `<https://…>`) is one word;
//   * left out: the YAML frontmatter and the `key:: value` page properties at the top (whole
//     pages only), block properties (`key:: value` lines anywhere), block ids (`^abc123`, `id::`),
//     task and reminder tokens with their dates (📅 2026-10-15, ⏰ …, 🔁 every week, priorities),
//     embeds and pictures (`![[…]]`, `![alt](…)`), block references `((id))`, footnote marks
//     `[^1]`, comments (`%%…%%`, `<!-- … -->`, also over several lines), HTML tags, and whole
//     fenced code blocks (code, ```query, ```calc, Mermaid…) and display formulas (`$$` lines) —
//     not prose, not read like it.
// A word is a run of characters between whitespace that holds at least one letter or digit
// (Unicode: Polish diacritics, also decomposed ones with combining marks): "biało-czerwony",
// "m.in.", "3,14", "10:30", "e-mail", "jan@example.pl" and an address are one word each; a dash
// standing alone ("—", "–"), an emoji or punctuation is none. A no-break space separates words
// ("w domu" typeset with one after "w" is two words) except between digits, where it groups
// thousands ("1 000 000" is one number). Characters are user-perceived characters (grapheme
// clusters: an emoji with a skin tone and "ą" written as a + ogonek are one each) of that text without its line
// breaks, spaces and tabs in a row counting as one (as the note shows them); `charactersNoSpaces`
// leaves whitespace out too.
#pragma once

#include <QString>
#include <QStringView>

namespace brulion::stats {

/// Reading speed for readingMinutes(): words per minute (silent reading of Polish or English
/// prose is about 180–250).
inline constexpr int WordsPerMinute = 200;

struct TextStats {
    int words = 0;
    int characters = 0;
    int charactersNoSpaces = 0;
    /// readingMinutes(words).
    int readingMinutes = 0;

    bool operator==(const TextStats &) const = default;
};

struct CountOptions {
    /// A whole note: its frontmatter and the page properties at its top are left out. Off for a
    /// selection or a block's text (a `---` line there is a rule, not frontmatter).
    bool page = true;
};

/// Minutes to read `words` at WordsPerMinute, rounded to the nearest minute but at least 1 for
/// any text (0 for none).
int readingMinutes(int words);

/// Words of plain text (the rule above; no Markdown handling).
int countWords(QStringView text);

/// Counts of plain text as it is (no Markdown handling; line breaks are not characters).
TextStats countPlainText(QStringView text);

/// The text of Markdown `markdown` that is counted: one line per line with visible text (empty
/// lines dropped), "\n" between them. Exposed for tests and diagnostics.
QString readableText(QStringView markdown, const CountOptions &options = {});

/// countPlainText(readableText(markdown, options)).
TextStats countMarkdown(QStringView markdown, const CountOptions &options = {});

} // namespace brulion::stats
