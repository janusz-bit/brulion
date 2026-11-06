#include "semantic/fusion.h"

#include <QHash>

#include <algorithm>

namespace brulion::semantic {

namespace {

void addScaled(const QList<ScoredId> &list, double weight, QHash<QString, double> *out)
{
    if (list.isEmpty())
        return;
    double lo = list.first().score, hi = lo;
    for (const ScoredId &s : list) {
        lo = std::min(lo, s.score);
        hi = std::max(hi, s.score);
    }
    for (const ScoredId &s : list) {
        const double scaled = hi > lo ? (s.score - lo) / (hi - lo) : 1.0;
        (*out)[s.id] += weight * scaled;
    }
}

} // namespace

QList<ScoredId> fuseConvex(const QList<ScoredId> &lexical, const QList<ScoredId> &semantic, double alpha,
                           int limit)
{
    alpha = std::clamp(alpha, 0.0, 1.0);
    QHash<QString, double> scores;
    addScaled(lexical, 1.0 - alpha, &scores);
    addScaled(semantic, alpha, &scores);
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
        std::sort(out.begin(), out.end(), better);
    }
    return out;
}

} // namespace brulion::semantic
