// Fusion of full-text and semantic hits. Part of the semantic module (semantic/semantic.h).
#pragma once

#include "semantic/vectorindex.h"

#include <QList>
#include <QString>

#include <functional>

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

/// The same where only some results can be found by meaning: a result `known` says nothing of —
/// an attachment (a PDF, a picture: no vectors), a note not embedded yet while the index is built
/// — scores by its scaled lexical score alone, as if its meaning matched as well as its words.
QList<ScoredId> fuseConvex(const QList<ScoredId> &lexical, const QList<ScoredId> &semantic,
                           const std::function<bool(const QString &id)> &known, double alpha = DefaultSemanticWeight,
                           int limit = 50);

/// Notes found by meaning alone (no word of the query in them) shown at most: a query of words no
/// note has (a name, a typo of one) lists a few notes close to it, not a page of them.
inline constexpr int MeaningAloneHits = 20;

/// A result of hybridSearch().
struct HybridHit {
    QString id;
    double score = 0;      ///< the fused score (0–1)
    bool lexical = false;  ///< found by its words (one of `lexical`)
    bool semantic = false; ///< among the notes closest in meaning
    int line = -1;         ///< where its closest chunk starts (semantic); -1: not found by meaning
};

/// Search by meaning and by words together — what the app's search and `brulion-cli search
/// --semantic` list: fuseConvex() of `lexical` (full-text hits, best first: ids and scores — notes
/// and attachments) and the `semanticLimit` notes of `index` closest to `query` (a raw unit query
/// vector; `calibrated`: Calibration::appliesTo() of its text), each scaled to 0–1. An id `index`
/// does not have scores by its words alone (fuseConvex with `known`). Every lexical hit stays; of
/// the notes found by meaning alone the best `meaningAlone`. Best first, ties by id.
QList<HybridHit> hybridSearch(const QList<ScoredId> &lexical, const VectorIndex &index, const Vector &query,
                              bool calibrated, int semanticLimit, int meaningAlone = MeaningAloneHits,
                              double alpha = DefaultSemanticWeight);

} // namespace brulion::semantic
