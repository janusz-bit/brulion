// Calibration of embedding vectors — a gradient-free edit of the model's output. Part of the
// semantic module (semantic/semantic.h).
#pragma once

#include <QJsonObject>
#include <QList>
#include <QStringView>

namespace brulion::semantic {

using Vector = QList<float>;

/// Scale `v` to unit length in place (a zero vector stays zero).
void normalize(Vector &v);
/// Dot product; vectors of different sizes give 0.
float dot(const Vector &a, const Vector &b);

/// calibrated(v) = unit( P (v - mean) ), P = I - d dᵀ: the mean of queries (or of passages) is
/// subtracted — the common component every e5 vector shares — and the language direction d is
/// projected out, so that an English query is not pulled towards English notes just for their
/// language. Empty members are skipped (an empty Calibration is the identity on unit vectors).
struct Calibration {
    Vector queryMean;   ///< mean embedding of typical queries (shipped with the model)
    Vector passageMean; ///< mean embedding of the notebook's chunks
    Vector direction;   ///< unit vector: mean(Polish chunks) - mean(English chunks)

    /// Calibrate only queries of at most this many words: the query mean comes from short,
    /// note-search-style queries, and on question-style queries (7–11 words in the public Polish
    /// retrieval sets) calibrating cost 0.02 MRR while short queries gained 0.04–0.06.
    int maxQueryWords = 5;

    bool isEmpty() const { return queryMean.isEmpty() && passageMean.isEmpty() && direction.isEmpty(); }
    /// Whether a query of this text is searched in the calibrated space (maxQueryWords).
    bool appliesTo(QStringView query) const;
    /// The calibrated, unit-length version of the unit embedding `v`.
    Vector apply(const Vector &v, bool query) const;

    /// From the notebook itself — no queries and no labels needed: `passages` are the unit
    /// embeddings of its chunks, `english` tells which chunks are English (looksEnglish()). The
    /// direction is left empty when either language has fewer than `minPerLanguage` chunks.
    static Calibration estimate(const QList<Vector> &passages, const QList<bool> &english,
                                const Vector &queryMean, int minPerLanguage = 5);

    /// {"queryMean": [...], "passageMean": [...], "direction": [...]}; missing keys stay empty.
    QJsonObject toJson() const;
    static Calibration fromJson(const QJsonObject &o);
};

} // namespace brulion::semantic
