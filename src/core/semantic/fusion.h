// Fusion of full-text and semantic hits. Part of the semantic module (semantic/semantic.h).
#pragma once

#include <QList>
#include <QString>

namespace brulion::semantic {

struct ScoredId {
    QString id;
    double score = 0;
};

/// Default weight of the semantic score, chosen on the training notes of the search experiments
/// (0.8 for both e5-small and mmlw-e5-small; RRF with equal weights did worse than either alone).
inline constexpr double DefaultSemanticWeight = 0.8;

/// alpha * semantic + (1 - alpha) * lexical, each list's scores min-max scaled to [0, 1] first (a
/// list whose scores are all equal scales to 1); an id missing from a list scores 0 there. Best
/// `limit` first, ties by id.
QList<ScoredId> fuseConvex(const QList<ScoredId> &lexical, const QList<ScoredId> &semantic,
                           double alpha = DefaultSemanticWeight, int limit = 50);

} // namespace brulion::semantic
