// Internal: little-endian integers for the attachments module's files (record.h, manifest.h) and a
// bounds-checked reader. Not part of the public API.
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QtEndian>

namespace brulion::attachments::codec {

inline void putU8(QByteArray &out, quint8 v)
{
    out.append(char(v));
}

inline void putU16(QByteArray &out, quint16 v)
{
    char b[2];
    qToLittleEndian(v, b);
    out.append(b, 2);
}

inline void putU32(QByteArray &out, quint32 v)
{
    char b[4];
    qToLittleEndian(v, b);
    out.append(b, 4);
}

inline void putU64(QByteArray &out, quint64 v)
{
    char b[8];
    qToLittleEndian(v, b);
    out.append(b, 8);
}

/// FNV-1a 32 (the files' checksums: corruption, not attacks).
inline quint32 fnv1a32(QByteArrayView data)
{
    quint32 h = 2166136261u;
    for (const char c : data) {
        h ^= quint8(c);
        h *= 16777619u;
    }
    return h;
}

/// Reads integers and byte runs; past the end every read gives 0 / nothing and ok() turns false.
class Reader {
public:
    explicit Reader(QByteArrayView data) : m_data(data) { }

    bool ok() const { return m_ok; }
    qsizetype remaining() const { return m_ok ? m_data.size() - m_pos : 0; }

    quint8 u8() { return take(1) ? quint8(m_data.at(m_pos - 1)) : 0; }
    quint16 u16() { return take(2) ? qFromLittleEndian<quint16>(m_data.data() + m_pos - 2) : 0; }
    quint32 u32() { return take(4) ? qFromLittleEndian<quint32>(m_data.data() + m_pos - 4) : 0; }
    quint64 u64() { return take(8) ? qFromLittleEndian<quint64>(m_data.data() + m_pos - 8) : 0; }
    QByteArrayView bytes(qint64 n) { return n >= 0 && take(n) ? m_data.sliced(m_pos - n, n) : QByteArrayView(); }

private:
    bool take(qint64 n)
    {
        if (!m_ok || n < 0 || n > m_data.size() - m_pos) {
            m_ok = false;
            return false;
        }
        m_pos += n;
        return true;
    }

    QByteArrayView m_data;
    qsizetype m_pos = 0;
    bool m_ok = true;
};

} // namespace brulion::attachments::codec
