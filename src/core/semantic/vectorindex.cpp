#include "semantic/vectorindex.h"

#include <algorithm>
#include <limits>

namespace brulion::semantic {

void VectorIndex::set(const QString &id, const QList<Chunk> &chunks, const QList<Vector> &vectors)
{
    remove(id);
    if (m_dim == 0 && !vectors.isEmpty())
        m_dim = int(vectors.first().size());
    int slot = int(m_slotIds.indexOf(QString()));
    if (slot < 0) {
        slot = int(m_slotIds.size());
        m_slotIds.append(id);
    } else {
        m_slotIds[slot] = id;
    }
    m_docs.insert(id, slot);
    const qsizetype n = std::min(chunks.size(), vectors.size());
    for (qsizetype k = 0; k < n; ++k) {
        const Vector &v = vectors.at(k);
        if (v.size() != m_dim)
            continue;
        m_raw.insert(m_raw.end(), v.cbegin(), v.cend());
        const Vector c = m_calibration.isEmpty() ? v : m_calibration.apply(v, false);
        m_cal.insert(m_cal.end(), c.cbegin(), c.cend());
        m_owner.push_back(slot);
        m_line.push_back(chunks.at(k).line);
        m_english.push_back(looksEnglish(chunks.at(k).text));
    }
}

void VectorIndex::remove(const QString &id)
{
    const auto it = m_docs.constFind(id);
    if (it == m_docs.cend())
        return;
    const int slot = it.value();
    m_docs.erase(it);
    m_slotIds[slot].clear();
    // Compact the chunk arrays (O(chunks); fine for a prototype, batched in a real index).
    size_t w = 0;
    const size_t d = size_t(m_dim);
    for (size_t k = 0; k < m_owner.size(); ++k) {
        if (m_owner[k] == slot)
            continue;
        if (w != k) {
            std::copy_n(m_raw.begin() + ptrdiff_t(k * d), d, m_raw.begin() + ptrdiff_t(w * d));
            std::copy_n(m_cal.begin() + ptrdiff_t(k * d), d, m_cal.begin() + ptrdiff_t(w * d));
            m_owner[w] = m_owner[k];
            m_line[w] = m_line[k];
            m_english[w] = m_english[k];
        }
        ++w;
    }
    m_raw.resize(w * d);
    m_cal.resize(w * d);
    m_owner.resize(w);
    m_line.resize(w);
    m_english.resize(w);
}

void VectorIndex::clear()
{
    *this = VectorIndex(m_dim);
}

void VectorIndex::setCalibration(const Calibration &c)
{
    m_calibration = c;
    rebuildCalibrated();
}

void VectorIndex::rebuildCalibrated()
{
    const size_t d = size_t(m_dim);
    m_cal.resize(m_raw.size());
    Vector v(m_dim);
    for (size_t k = 0; k < m_owner.size(); ++k) {
        std::copy_n(m_raw.begin() + ptrdiff_t(k * d), d, v.begin());
        const Vector c = m_calibration.isEmpty() ? v : m_calibration.apply(v, false);
        std::copy_n(c.cbegin(), d, m_cal.begin() + ptrdiff_t(k * d));
    }
}

Calibration VectorIndex::estimateCalibration(const Vector &queryMean) const
{
    QList<Vector> raw;
    QList<bool> english;
    raw.reserve(qsizetype(m_owner.size()));
    const size_t d = size_t(m_dim);
    for (size_t k = 0; k < m_owner.size(); ++k) {
        raw.append(Vector(m_raw.cbegin() + ptrdiff_t(k * d), m_raw.cbegin() + ptrdiff_t((k + 1) * d)));
        english.append(m_english[k]);
    }
    return Calibration::estimate(raw, english, queryMean);
}

QList<VectorIndex::Hit> VectorIndex::search(const Vector &query, int limit, bool calibrated) const
{
    if (query.size() != m_dim || m_owner.empty() || limit <= 0)
        return {};
    calibrated = calibrated && !m_calibration.isEmpty();
    const Vector q = calibrated ? m_calibration.apply(query, true) : query;
    const std::vector<float> &chunks = calibrated ? m_cal : m_raw;
    const size_t d = size_t(m_dim);
    constexpr float none = -std::numeric_limits<float>::infinity();
    std::vector<float> best(size_t(m_slotIds.size()), none);
    std::vector<int> bestLine(best.size(), 0);
    const float *qv = q.constData();
    for (size_t k = 0; k < m_owner.size(); ++k) {
        const float *c = chunks.data() + k * d;
        float s = 0;
        for (size_t i = 0; i < d; ++i)
            s += qv[i] * c[i];
        const size_t slot = size_t(m_owner[k]);
        if (s > best[slot]) {
            best[slot] = s;
            bestLine[slot] = m_line[k];
        }
    }
    QList<Hit> hits;
    for (size_t slot = 0; slot < best.size(); ++slot) {
        if (best[slot] != none)
            hits.append({m_slotIds.at(qsizetype(slot)), best[slot], bestLine[slot]});
    }
    const auto better = [](const Hit &a, const Hit &b) { return a.score != b.score ? a.score > b.score : a.id < b.id; };
    if (hits.size() > limit) {
        std::partial_sort(hits.begin(), hits.begin() + limit, hits.end(), better);
        hits.resize(limit);
    } else {
        std::sort(hits.begin(), hits.end(), better);
    }
    return hits;
}

} // namespace brulion::semantic
