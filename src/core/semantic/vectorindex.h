// VectorIndex — chunk embeddings of the notebook, searched by cosine similarity. Part of the
// semantic module (semantic/semantic.h).
//
// Stores the model's raw unit vectors (computing them is the expensive part) and a calibrated
// copy (Calibration::apply) that searches use; setCalibration() recomputes the copy. Brute force:
// 10k chunks x 384 dimensions are about 4M multiply-adds per query, well under a millisecond to a
// few milliseconds — no approximate index needed at notebook scale. Not thread-safe.
#pragma once

#include "semantic/calibration.h"
#include "semantic/chunker.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <vector>

namespace brulion::semantic {

class VectorIndex {
public:
    struct Hit {
        QString id;
        float score = 0; ///< cosine similarity of the best chunk
        int line = 0;    ///< where that chunk starts in the note
    };

    explicit VectorIndex(int dimension = 0) : m_dim(dimension) {}

    int dimension() const { return m_dim; }
    /// Add or replace the note `id`: its chunks and their raw unit embeddings (same order and
    /// count; vectors of another dimension are ignored). The first note fixes the dimension.
    void set(const QString &id, const QList<Chunk> &chunks, const QList<Vector> &vectors);
    void remove(const QString &id);
    void clear();
    bool contains(const QString &id) const { return m_docs.contains(id); }
    qsizetype size() const { return m_docs.size(); }
    qsizetype chunkCount() const { return qsizetype(m_owner.size()); }

    void setCalibration(const Calibration &c);
    const Calibration &calibration() const { return m_calibration; }
    /// Calibration::estimate() over the stored chunks; their language is looksEnglish() of the
    /// chunk text, remembered by set().
    Calibration estimateCalibration(const Vector &queryMean) const;

    /// Best `limit` notes for the raw unit embedding of a query, each scored by its best chunk;
    /// ties by id. `calibrated`: compare the calibrated query with the calibrated chunks (pass
    /// calibration().appliesTo(text)), else raw with raw.
    QList<Hit> search(const Vector &query, int limit, bool calibrated = true) const;

private:
    void rebuildCalibrated();

    int m_dim = 0;
    Calibration m_calibration;
    // Chunks, contiguous: chunk k has raw vector m_raw[k*dim…], calibrated m_cal[k*dim…].
    std::vector<float> m_raw, m_cal;
    std::vector<int> m_owner; ///< note slot of chunk k
    std::vector<int> m_line;
    std::vector<bool> m_english;
    QStringList m_slotIds;      ///< note id of a slot ("" = free)
    QHash<QString, int> m_docs; ///< id -> slot
};

} // namespace brulion::semantic
