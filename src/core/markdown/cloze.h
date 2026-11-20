// Cloze deletions inside a line (docs/SYNTAX.md "Flashcards"): Anki's and the Obsidian Spaced
// Repetition plugin's `{{c1::answer}}` / `{{c1::answer::hint}}` and Logseq's `{{cloze answer}}`.
// Part of the markdown module — see markdown/markdown.h. The renderer shows the answer tinted as a
// highlight, without the braces, the number and the hint (the flashcards extension, which reads
// cards itself, hides it on the question's side).
//
//   findClozes(u"Stolicą Polski jest {{c1::Warszawa::miasto}}.")
//       == {{20, 44, 26, 34, 1, 36, 42}}
#pragma once

#include <QList>
#include <QStringView>

namespace brulion::markdown {

struct ClozeSpan {
    qsizetype from = 0; ///< the whole `{{…}}`
    qsizetype to = 0;
    qsizetype textFrom = 0; ///< the answer
    qsizetype textTo = 0;
    int number = 0; ///< c1 → 1, c12 → 12; Logseq's `{{cloze …}}`: 0
    qsizetype hintFrom = -1; ///< after `::` in `{{c1::answer::hint}}`, else -1
    qsizetype hintTo = -1;
    friend bool operator==(const ClozeSpan &, const ClozeSpan &) = default;
};

/// The clozes of `text` (one or more lines; code spans and comments are the caller's to blank
/// out), in order: `{{c<digits>::` (1 to 3 digits) or `{{cloze` followed by a space or `:`, an
/// answer of at least one character that is not white space only, no line break and no `{{` or
/// `}}` in it, then `}}`. An unclosed or empty one is text. Linear time.
QList<ClozeSpan> findClozes(QStringView text);

} // namespace brulion::markdown
