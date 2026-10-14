// Ranking for page suggestions (`[[` completion, quick switcher) and the bounded edit distance
// shared with the search index. Names and queries are folded (text::foldKey). Internal.
#pragma once

#include <QString>
#include <QStringView>

namespace brulion::index_detail {

/// Match quality of a name against a query (higher is better).
enum class NameRank {
    None = 0,
    Fuzzy = 1,      ///< the query's characters occur in order, spread out
    FuzzyClose = 2, ///< one/two typos in a word's beginning, or in-order characters close together
    Contains = 3,   ///< the query occurs inside a word
    WordPrefix = 4, ///< a word of the name starts with the query (or every query word starts one)
    Prefix = 5,     ///< the name starts with the query
    Exact = 6,
};

/// Optimal-string-alignment distance (insert / delete / substitute / swap of adjacent
/// characters) between `a` and `b`, or `max + 1` as soon as it exceeds `max`.
int boundedEditDistance(QStringView a, QStringView b, int max);

/// Rank of a folded `name` for the folded, non-empty query `q`.
NameRank rankName(QStringView name, QStringView q);

} // namespace brulion::index_detail
