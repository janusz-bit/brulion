#include "semantic/fusion.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace brulion::semantic {

namespace {

/// The scores of `list` scaled to [0, 1] (all equal: 1).
QHash<QString, double> scaled(const QList<ScoredId> &list)
{
    QHash<QString, double> out;
    if (list.isEmpty())
        return out;
    const double lo = std::ranges::min(list, {}, &ScoredId::score).score;
    const double hi = std::ranges::max(list, {}, &ScoredId::score).score;
    for (const ScoredId &s : list)
        out.insert(s.id, hi > lo ? (s.score - lo) / (hi - lo) : 1.0);
    return out;
}

} // namespace

QList<ScoredId> fuseConvex(const QList<ScoredId> &lexical, const QList<ScoredId> &semantic, double alpha,
                           int limit)
{
    return fuseConvex(lexical, semantic, {}, alpha, limit);
}

QList<ScoredId> fuseConvex(const QList<ScoredId> &lexical, const QList<ScoredId> &semantic,
                           const std::function<bool(const QString &id)> &known, double alpha, int limit)
{
    alpha = std::clamp(alpha, 0.0, 1.0);
    const QHash<QString, double> lex = scaled(lexical);
    const QHash<QString, double> sem = scaled(semantic);
    QHash<QString, double> scores;
    for (auto it = lex.cbegin(); it != lex.cend(); ++it)
        scores.insert(it.key(), (known && !known(it.key()) ? 1.0 : 1.0 - alpha) * it.value());
    for (auto it = sem.cbegin(); it != sem.cend(); ++it)
        scores[it.key()] += alpha * it.value();
    QList<ScoredId> out;
    out.reserve(scores.size());
    for (auto it = scores.cbegin(); it != scores.cend(); ++it)
        out.append({it.key(), it.value()});
    const auto better = [](const ScoredId &a, const ScoredId &b) {
        return a.score != b.score ? a.score > b.score : a.id < b.id;
    };
    if (limit >= 0 && out.size() > limit) {
        std::partial_sort(out.begin(), out.begin() + limit, out.end(), better);
        out.resize(limit);
    } else {
        std::ranges::sort(out, better);
    }
    return out;
}

QList<HybridHit> hybridSearch(const QList<ScoredId> &lexical, const VectorIndex &index, const Vector &query,
                              bool calibrated, int semanticLimit, int meaningAlone, double alpha)
{
    QList<ScoredId> semantic;
    QHash<QString, int> lines;
    for (const VectorIndex::Hit &h : index.search(query, semanticLimit, calibrated)) {
        semantic.append({h.id, double(h.score)});
        lines.insert(h.id, h.line);
    }
    QSet<QString> words;
    for (const ScoredId &s : lexical)
        words.insert(s.id);
    // (No note close in meaning — an empty index, a query vector of another model: the words alone.)
    const QList<ScoredId> fused = fuseConvex(
            lexical, semantic,
            [&index, &semantic](const QString &id) { return !semantic.isEmpty() && index.contains(id); }, alpha, -1);
    QList<HybridHit> out;
    out.reserve(fused.size());
    int alone = 0;
    for (const ScoredId &s : fused) {
        HybridHit hit;
        hit.id = s.id;
        hit.score = s.score;
        hit.lexical = words.contains(s.id);
        const auto line = lines.constFind(s.id);
        hit.semantic = line != lines.cend();
        hit.line = hit.semantic ? line.value() : -1;
        if (!hit.lexical && ++alone > meaningAlone)
            continue;
        out.append(std::move(hit));
    }
    return out;
}

} // namespace brulion::semantic
