// VectorIndex — chunk embeddings of the notebook, searched by cosine similarity. Part of the
// semantic module (semantic/semantic.h).
//
// Stores the model's raw unit vectors (computing them is the expensive part) and, per chunk, the
// length its calibrated vector had before it was scaled to unit length: a calibrated search
// (Calibration::apply) compares the calibrated query q' with every chunk as
// (q'·v − q'·passageMean) / that length — the same as calibrating the chunk, without a second copy
// of the vectors. setCalibration() computes the lengths again. Brute force: 10k chunks x 384
// dimensions are about 4M multiply-adds per query, a millisecond or a few — no approximate index
// is needed at notebook scale.
//
// Incremental: set() replaces a note (its chunks, their vectors and the note's key: semantic/notes.h
// noteKey()), remove() forgets one — its chunks are marked gone and the arrays compacted once
// enough are. save() / load() keep it on the device (half-precision vectors, ~770 bytes a chunk),
// checked against the model and the chunker's version. Not thread-safe: one thread at a time.
#pragma once

#include "semantic/calibration.h"
#include "semantic/chunker.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace brulion::semantic {

class VectorIndex {
public:
    struct Hit {
        QString id;
        float score = 0; ///< cosine similarity of the best chunk
        int line = 0;    ///< where that chunk starts in the note
    };

    /// Bump when the format of save() changes (load() refuses other versions).
    static constexpr quint32 FormatVersion = 1;

    explicit VectorIndex(int dimension = 0) : m_dim(dimension) {}

    int dimension() const { return m_dim; }
    /// Add or replace the note `id`: its chunks and their raw unit embeddings (same order and
    /// count; vectors of another dimension are ignored), and the key of the text they were made
    /// from (noteKey(); 0: none). The first note fixes the dimension.
    void set(const QString &id, const QList<Chunk> &chunks, const QList<Vector> &vectors, quint64 key = 0);
    void remove(const QString &id);
    void clear();
    bool contains(const QString &id) const { return m_docs.contains(id); }
    /// The key set() was given for `id`; 0 for an unknown note.
    quint64 key(const QString &id) const;
    /// The notes, sorted.
    QStringList ids() const;
    qsizetype size() const { return m_docs.size(); }
    qsizetype chunkCount() const { return qsizetype(m_owner.size()) - m_dead; }

    void setCalibration(const Calibration &c);
    const Calibration &calibration() const { return m_calibration; }
    /// Calibration::estimate() over the stored chunks; their language is looksEnglish() of the
    /// chunk text, remembered by set().
    Calibration estimateCalibration(const Vector &queryMean) const;

    /// Best `limit` notes for the raw unit embedding of a query, each scored by its best chunk;
    /// ties by id. `calibrated`: compare the calibrated query with the calibrated chunks (pass
    /// calibration().appliesTo(text)), else raw with raw.
    QList<Hit> search(const Vector &query, int limit, bool calibrated = true) const;
    /// The same score for each of `ids` the index has (any other is left out), in their order.
    QList<Hit> score(const QStringList &ids, const Vector &query, bool calibrated = true) const;

    /// The index as bytes for the device's cache, for the model `model` (its id).
    QByteArray save(const QString &model) const;
    /// An index save() wrote for `model` with this FormatVersion and ChunkerVersion; nullopt for
    /// anything else — another model, another version, a file cut short or damaged. Without a
    /// calibration: estimate one again.
    static std::optional<VectorIndex> load(QByteArrayView data, const QString &model);

private:
    struct Slot {
        QString id;
        quint64 key = 0;
        qsizetype first = 0; ///< its chunks: [first, first + count) — set() appends them together
        qsizetype count = 0;
    };
    struct Query {
        Vector q;      ///< the query as compared (calibrated or raw)
        float c = 0;   ///< q'·passageMean (calibrated)
        bool calibrated = false;
    };
    Query prepare(const Vector &query, bool calibrated) const;
    float chunkScore(const Query &q, qsizetype k) const;
    void computeNorms(qsizetype from);
    void compact();

    int m_dim = 0;
    Calibration m_calibration;
    // Chunks, contiguous: chunk k has raw vector m_raw[k*dim…].
    std::vector<float> m_raw;
    std::vector<float> m_norm;  ///< length of chunk k's calibrated vector before scaling (calibrated search)
    std::vector<int> m_owner;   ///< slot of chunk k; -1: gone (removed, not compacted yet)
    std::vector<int> m_line;
    std::vector<bool> m_english;
    qsizetype m_dead = 0;       ///< chunks gone
    QList<Slot> m_slots;
    QList<int> m_free;          ///< slots free again
    QHash<QString, int> m_docs; ///< id -> slot
};

} // namespace brulion::semantic
