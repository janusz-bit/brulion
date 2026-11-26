// The fuzzy matcher of a project's files (docs/CODE-FILES.md "Fuzzy matching"): the file picker
// (Ctrl+P) and a thread's @-mentions rank the paths of a FileIndex with it. Part of the codefiles
// module.
//
//   const FuzzyQuery q = prepareQuery(u"mainqml");
//   const FuzzyMatch m = matchPath(q, u"src/app/qml/Main.qml", 12);   // 12: where "Main.qml" starts
//   m.score > 0; m.positions — the matched UTF-16 offsets, to mark them
//
// The query's characters must appear in the path in order (a subsequence), case and Latin marks
// ignored (one UTF-16 unit for one: "zolw" finds "Żółw.md", positions stay the path's own); spaces
// in the query are dropped. Among the ways the characters can be matched the best one is scored
// (dynamic programming, as fzf does): each matched character, more after a path separator or at
// the start, after "_", "-", "." or a space, at a camelCase hump or where letters turn to digits,
// more again for a run of consecutive characters, less for each skipped one; an upper-case
// character typed and matched exactly scores a little more. A query that matches inside the base
// name (the part after the last "/") is scored there, with a bonus — the base name's hits come
// first —, more when the name starts with the query or is it (the extension aside); a query with a
// "/" in it is matched against the whole path. Shorter paths win ties. Queries longer than 64
// characters and paths longer than 512 match nothing. Pure functions, no allocation in the scoring
// pass but a per-thread scratch buffer.
#pragma once

#include <QList>
#include <QString>
#include <QStringView>

namespace brulion::codefiles {

/// One UTF-16 unit folded for matching: lower case, a Latin letter without its marks (ż → z,
/// Ł → l, É → e); one unit for one, so positions in the folded text are the original's.
char16_t foldChar(char16_t c);

/// The longest query that matches anything, and the longest path matched.
inline constexpr int MaxQueryLength = 64;
inline constexpr int MaxPathLength = 512;

/// A query ready for matching.
struct FuzzyQuery {
    /// Folded (foldChar), without spaces.
    QString folded;
    /// The same characters as typed (the case bonus).
    QString typed;
    /// It has a "/" (matched against the whole path).
    bool hasSlash = false;
    bool isEmpty() const { return folded.isEmpty(); }
};
FuzzyQuery prepareQuery(QStringView query);

struct FuzzyMatch {
    /// 0: no match; higher is better.
    int score = 0;
    /// The matched UTF-16 offsets in the path, ascending.
    QList<int> positions;
    /// Every character matched in the base name.
    bool inName = false;
};

/// `query` against a relative path whose base name starts at `nameStart` (0 for a path without a
/// "/"). An empty query matches everything with score 1 and no positions.
FuzzyMatch matchPath(const FuzzyQuery &query, QStringView path, qsizetype nameStart);
/// matchPath()'s score alone (0: none) — the fast pass over a whole index.
int scorePath(const FuzzyQuery &query, QStringView path, qsizetype nameStart);

} // namespace brulion::codefiles
