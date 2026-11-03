// The words of a line of Markdown worth spell-checking (docs/UI.md "Spell checking"). Part of the
// core module "spell" — see spell/spell.h.
//
// What a note writes besides prose is never checked: inline code and comments, formulas (`$…$`,
// `$$…$$`), wiki links, embeds and block references whole, a Markdown link's destination (its
// text is prose and is checked), bare URLs, `<…>` autolinks and HTML tags, HTML entities, tags,
// the task and date tokens (📅 ⏳ 🛫 ➕ ✅ ❌ ⏰ 🔁 and its rule, priorities, task ids, Logseq
// `SCHEDULED:`), block ids, Logseq macros `{{…}}`, footnote markers `[^1]`; at the start of a line
// a `key::` property name, a task keyword (TODO, DONE, …) and a callout's type (`[!note]`).
//
// A word is a run of letters (any script, with combining marks) in which an apostrophe (' ’ ʼ) or
// a hyphen (- ‐ ‑) may join letters: "don't", "O’Brien", "biało-czerwony". Not checked: runs with
// a digit or '_' ("COVID-19", "mp3", "snake_case"), with a '.' or '@' between letters (file
// names, domains, e-mail addresses, "e.g"), next to a '/' or '\' (paths, LaTeX commands), single
// letters, words in capitals (acronyms: "NASA", "PDF") and words with a capital after a small
// letter ("iPhone", "QtQuick"). A word directly followed by '.' says so (`dot`): "np." is checked
// with its dot when the word alone is not known (SpellChecker).
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace brulion::spell {

/// A word to check: the UTF-16 range [from, to) of the scanned text.
struct WordSpan {
    qsizetype from = 0;
    qsizetype to = 0;
    /// The word is directly followed by '.' (an abbreviation or the end of a sentence).
    bool dot = false;

    qsizetype length() const { return to - from; }
    friend bool operator==(const WordSpan &, const WordSpan &) = default;
};

/// The words of `text` worth checking, in order. `text` is one line of a note (or one inline
/// paragraph: line breaks separate words); `lineStart`: it begins a line, so a property name, a
/// task keyword, a callout type or a list marker at its start is syntax. Linear in the text
/// (the markdown module's scanners), never fails.
QList<WordSpan> findWords(QStringView text, bool lineStart = true);

/// The word of findWords(text) that contains `position` or ends right at it (the caret after a
/// word), else nothing.
std::optional<WordSpan> wordAt(QStringView text, qsizetype position, bool lineStart = true);

/// The text a backend is asked about for `span` of `text`: the word, NFC, typographic
/// apostrophes as ASCII ', soft hyphens dropped; "np." with its dot when `span.dot`.
QString checkedText(QStringView text, const WordSpan &span);

/// The word as backends get it: NFC, ’ and ʼ as ', ‐ and ‑ as -, soft hyphens (U+00AD) dropped.
QString normalizedWord(QStringView word);

/// The parts of a hyphenated word ("biało-czerwony" → {"biało", "czerwony"}; also ‐ and ‑);
/// empty for a word without a hyphen.
QStringList hyphenParts(QStringView word);

} // namespace brulion::spell
