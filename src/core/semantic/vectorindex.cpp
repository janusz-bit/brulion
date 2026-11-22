#include "semantic/vectorindex.h"

#include <QDataStream>
#include <QIODevice>
#include <QSysInfo>
#include <QtEndian>
#include <QtNumeric>
#include <qfloat16.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace brulion::semantic {

namespace {

constexpr char Magic[4] = {'B', 'R', 'S', 'V'};
/// Sanity limits of load(): a damaged file never makes it allocate without end.
constexpr quint32 MaxDimension = 4096;
constexpr quint32 MaxChunksOfNote = 1u << 20;

const auto better = [](const VectorIndex::Hit &a, const VectorIndex::Hit &b) {
    return a.score != b.score ? a.score > b.score : a.id < b.id;
};

} // namespace

void VectorIndex::set(const QString &id, const QList<Chunk> &chunks, const QList<Vector> &vectors, quint64 key)
{
    remove(id);
    if (m_dim == 0 && !vectors.isEmpty())
        m_dim = int(vectors.first().size());
    int slot = 0;
    if (!m_free.isEmpty()) {
        slot = m_free.takeLast();
    } else {
        slot = int(m_slots.size());
        m_slots.append(Slot{});
    }
    Slot &s = m_slots[slot];
    s.id = id;
    s.key = key;
    s.first = qsizetype(m_owner.size());
    s.count = 0;
    m_docs.insert(id, slot);
    const qsizetype n = std::min(chunks.size(), vectors.size());
    for (qsizetype k = 0; k < n; ++k) {
        const Vector &v = vectors.at(k);
        if (v.size() != m_dim)
            continue;
        m_raw.insert(m_raw.end(), v.cbegin(), v.cend());
        m_owner.push_back(slot);
        m_line.push_back(chunks.at(k).line);
        m_english.push_back(looksEnglish(chunks.at(k).text));
        ++s.count;
    }
    computeNorms(s.first);
}

void VectorIndex::remove(const QString &id)
{
    const auto it = m_docs.constFind(id);
    if (it == m_docs.cend())
        return;
    const int slot = it.value();
    m_docs.erase(it);
    Slot &s = m_slots[slot];
    for (qsizetype k = s.first; k < s.first + s.count; ++k)
        m_owner[size_t(k)] = -1;
    m_dead += s.count;
    s = Slot{};
    m_free.append(slot);
    // The arrays are compacted once a good part of them is gone (a note edited again and again
    // moves nothing until then).
    if (m_dead > 1024 && m_dead * 4 > qsizetype(m_owner.size()))
        compact();
}

void VectorIndex::compact()
{
    const size_t d = size_t(m_dim);
    size_t w = 0;
    for (size_t k = 0; k < m_owner.size(); ++k) {
        const int slot = m_owner[k];
        if (slot < 0)
            continue;
        if (w != k) {
            std::copy_n(m_raw.begin() + ptrdiff_t(k * d), d, m_raw.begin() + ptrdiff_t(w * d));
            m_norm[w] = m_norm[k];
            m_owner[w] = slot;
            m_line[w] = m_line[k];
            m_english[w] = m_english[k];
        }
        // (A slot's chunks stay together and in order: its first one moves first.)
        Slot &s = m_slots[slot];
        if (s.first == qsizetype(k))
            s.first = qsizetype(w);
        ++w;
    }
    m_raw.resize(w * d);
    m_norm.resize(w);
    m_owner.resize(w);
    m_line.resize(w);
    m_english.resize(w);
    m_dead = 0;
}

void VectorIndex::clear()
{
    const Calibration c = m_calibration;
    *this = VectorIndex(m_dim);
    m_calibration = c;
}

quint64 VectorIndex::key(const QString &id) const
{
    const auto it = m_docs.constFind(id);
    return it == m_docs.cend() ? 0 : m_slots.at(it.value()).key;
}

QStringList VectorIndex::ids() const
{
    QStringList out = m_docs.keys();
    std::ranges::sort(out);
    return out;
}

void VectorIndex::setCalibration(const Calibration &c)
{
    m_calibration = c;
    computeNorms(0);
}

void VectorIndex::computeNorms(qsizetype from)
{
    // ‖P(v − passageMean)‖ of every chunk from `from` on (P: the language direction projected out).
    const size_t d = size_t(m_dim);
    m_norm.resize(m_owner.size(), 1.0f);
    if (m_calibration.isEmpty())
        return;
    const Vector &mean = m_calibration.passageMean;
    const Vector &dir = m_calibration.direction;
    const bool useMean = mean.size() == m_dim;
    const bool useDir = dir.size() == m_dim;
    std::vector<float> w(d);
    for (size_t k = size_t(from); k < m_owner.size(); ++k) {
        const float *v = m_raw.data() + k * d;
        for (size_t i = 0; i < d; ++i)
            w[i] = useMean ? v[i] - mean.at(qsizetype(i)) : v[i];
        if (useDir) {
            const float p = std::inner_product(w.cbegin(), w.cend(), dir.cbegin(), 0.0f);
            for (size_t i = 0; i < d; ++i)
                w[i] -= p * dir.at(qsizetype(i));
        }
        m_norm[k] = std::sqrt(std::inner_product(w.cbegin(), w.cend(), w.cbegin(), 0.0f));
    }
}

Calibration VectorIndex::estimateCalibration(const Vector &queryMean) const
{
    std::vector<qsizetype> live;
    live.reserve(m_owner.size());
    for (size_t k = 0; k < m_owner.size(); ++k) {
        if (m_owner[k] >= 0)
            live.push_back(qsizetype(k));
    }
    const size_t d = size_t(m_dim);
    return Calibration::estimate(
            qsizetype(live.size()), m_dim,
            [this, &live, d](qsizetype i) { return m_raw.data() + size_t(live[size_t(i)]) * d; },
            [this, &live](qsizetype i) { return bool(m_english[size_t(live[size_t(i)])]); }, queryMean);
}

VectorIndex::Query VectorIndex::prepare(const Vector &query, bool calibrated) const
{
    Query q;
    q.calibrated = calibrated && !m_calibration.isEmpty();
    q.q = q.calibrated ? m_calibration.apply(query, true) : query;
    if (q.calibrated && m_calibration.passageMean.size() == m_dim)
        q.c = dot(q.q, m_calibration.passageMean);
    return q;
}

float VectorIndex::chunkScore(const Query &q, qsizetype k) const
{
    const size_t d = size_t(m_dim);
    const float *v = m_raw.data() + size_t(k) * d;
    const float s = std::inner_product(q.q.cbegin(), q.q.cend(), v, 0.0f);
    if (!q.calibrated)
        return s;
    const float n = m_norm[size_t(k)];
    return n > 0 ? (s - q.c) / n : 0.0f;
}

QList<VectorIndex::Hit> VectorIndex::search(const Vector &query, int limit, bool calibrated) const
{
    if (query.size() != m_dim || chunkCount() == 0 || limit <= 0)
        return {};
    const Query q = prepare(query, calibrated);
    constexpr float none = -std::numeric_limits<float>::infinity();
    std::vector<float> best(size_t(m_slots.size()), none);
    std::vector<int> bestLine(best.size(), 0);
    for (size_t k = 0; k < m_owner.size(); ++k) {
        const int slot = m_owner[k];
        if (slot < 0)
            continue;
        const float s = chunkScore(q, qsizetype(k));
        if (s > best[size_t(slot)]) {
            best[size_t(slot)] = s;
            bestLine[size_t(slot)] = m_line[k];
        }
    }
    QList<Hit> hits;
    for (size_t slot = 0; slot < best.size(); ++slot) {
        if (best[slot] != none)
            hits.append({m_slots.at(qsizetype(slot)).id, best[slot], bestLine[slot]});
    }
    if (hits.size() > limit) {
        std::partial_sort(hits.begin(), hits.begin() + limit, hits.end(), better);
        hits.resize(limit);
    } else {
        std::ranges::sort(hits, better);
    }
    return hits;
}

QList<VectorIndex::Hit> VectorIndex::score(const QStringList &ids, const Vector &query, bool calibrated) const
{
    QList<Hit> out;
    if (query.size() != m_dim || ids.isEmpty())
        return out;
    const Query q = prepare(query, calibrated);
    for (const QString &id : ids) {
        const auto it = m_docs.constFind(id);
        if (it == m_docs.cend())
            continue;
        const Slot &s = m_slots.at(it.value());
        if (s.count == 0)
            continue;
        Hit hit{id, -std::numeric_limits<float>::infinity(), 0};
        for (qsizetype k = s.first; k < s.first + s.count; ++k) {
            const float v = chunkScore(q, k);
            if (v > hit.score) {
                hit.score = v;
                hit.line = m_line[size_t(k)];
            }
        }
        out.append(hit);
    }
    return out;
}

// ---- On the device ------------------------------------------------------------------------------
//
// "BRSV", FormatVersion, the model's id, ChunkerVersion, the dimension, the number of notes; then
// per note (sorted by id): its id, key and number of chunks, and per chunk its line, whether it
// reads as English and its vector as half floats. Little-endian.

QByteArray VectorIndex::save(const QString &model) const
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData(Magic, sizeof Magic);
    out << FormatVersion << model << ChunkerVersion << quint32(m_dim) << quint32(m_docs.size());
    const size_t d = size_t(m_dim);
    std::vector<qfloat16> half(d);
    for (const QString &id : ids()) {
        const Slot &s = m_slots.at(m_docs.value(id));
        out << id << s.key << quint32(s.count);
        for (qsizetype k = s.first; k < s.first + s.count; ++k) {
            out << qint32(m_line[size_t(k)]) << quint8(m_english[size_t(k)] ? 1 : 0);
            qFloatToFloat16(half.data(), m_raw.data() + size_t(k) * d, qsizetype(d));
            if constexpr (QSysInfo::ByteOrder == QSysInfo::BigEndian) {
                for (qfloat16 &h : half) {
                    quint16 bits = 0;
                    std::memcpy(&bits, &h, sizeof bits);
                    bits = qToLittleEndian(bits);
                    std::memcpy(&h, &bits, sizeof bits);
                }
            }
            out.writeRawData(reinterpret_cast<const char *>(half.data()), int(d * sizeof(qfloat16)));
        }
    }
    return bytes;
}

std::optional<VectorIndex> VectorIndex::load(QByteArrayView data, const QString &model)
{
    const QByteArray bytes = QByteArray::fromRawData(data.data(), data.size());
    QDataStream in(bytes);
    in.setVersion(QDataStream::Qt_6_0);
    in.setByteOrder(QDataStream::LittleEndian);
    char magic[sizeof Magic] = {};
    if (in.readRawData(magic, sizeof magic) != int(sizeof magic) || !std::equal(magic, magic + sizeof magic, Magic))
        return std::nullopt;
    quint32 format = 0, chunker = 0, dim = 0, notes = 0;
    QString storedModel;
    in >> format >> storedModel >> chunker >> dim >> notes;
    if (in.status() != QDataStream::Ok || format != FormatVersion || storedModel != model || chunker != ChunkerVersion
        || dim == 0 || dim > MaxDimension)
        return std::nullopt;
    VectorIndex index{int(dim)};
    const size_t d = dim;
    const qsizetype chunkBytes = qsizetype(4 + 1 + d * sizeof(qfloat16));
    std::vector<qfloat16> half(d);
    for (quint32 n = 0; n < notes; ++n) {
        QString id;
        quint64 key = 0;
        quint32 count = 0;
        in >> id >> key >> count;
        if (in.status() != QDataStream::Ok || id.isEmpty() || index.contains(id) || count > MaxChunksOfNote
            || qsizetype(count) * chunkBytes > bytes.size() - in.device()->pos())
            return std::nullopt;
        QList<Chunk> chunks;
        QList<Vector> vectors;
        chunks.reserve(count);
        vectors.reserve(count);
        QList<bool> english;
        english.reserve(count);
        for (quint32 k = 0; k < count; ++k) {
            qint32 line = 0;
            quint8 en = 0;
            in >> line >> en;
            if (in.readRawData(reinterpret_cast<char *>(half.data()), int(d * sizeof(qfloat16)))
                != int(d * sizeof(qfloat16)))
                return std::nullopt;
            if constexpr (QSysInfo::ByteOrder == QSysInfo::BigEndian) {
                for (qfloat16 &h : half) {
                    quint16 bits = 0;
                    std::memcpy(&bits, &h, sizeof bits);
                    bits = qFromLittleEndian(bits);
                    std::memcpy(&h, &bits, sizeof bits);
                }
            }
            Vector v(static_cast<qsizetype>(d));
            qFloatFromFloat16(v.data(), half.data(), qsizetype(d));
            if (std::ranges::any_of(v, [](float x) { return !qIsFinite(x); }))
                return std::nullopt;
            normalize(v);
            chunks.append(Chunk{QString(), line});
            vectors.append(std::move(v));
            english.append(en != 0);
        }
        if (in.status() != QDataStream::Ok)
            return std::nullopt;
        index.set(id, chunks, vectors, key);
        // (set() reads the language from the chunk's text, which is not kept: the stored one.)
        const Slot &s = index.m_slots.at(index.m_docs.value(id));
        for (qsizetype k = 0; k < s.count; ++k)
            index.m_english[size_t(s.first + k)] = english.at(k);
    }
    if (in.status() != QDataStream::Ok)
        return std::nullopt;
    return index;
}

} // namespace brulion::semantic
