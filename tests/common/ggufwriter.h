#pragma once
// A GGUF file written in a test as llama.cpp writes one (little-endian, version 3): metadata of
// every kind, tensors' entries, malformed values as raw bytes — the header only, the weights zeros
// (tst_ai_gguf, tst_shell_localai: models of the user's own).

#include <QByteArray>
#include <QList>

namespace brulion::test {

/// A GGUF file written as llama.cpp writes one (little-endian, version 3).
class GgufWriter {
public:
    quint32 version = 3;

    void text(const QByteArray &key, const QByteArray &value)
    {
        key8(key, 8);
        string(&m_kv, value);
    }
    void u32(const QByteArray &key, quint32 value)
    {
        key8(key, 4);
        number(&m_kv, value, 4);
    }
    void flag(const QByteArray &key, bool value)
    {
        key8(key, 7);
        m_kv.append(char(value ? 1 : 0));
    }
    void strings(const QByteArray &key, const QList<QByteArray> &values)
    {
        key8(key, 9);
        number(&m_kv, 8, 4);
        number(&m_kv, quint64(values.size()), 8);
        for (const QByteArray &v : values)
            string(&m_kv, v);
    }
    void i32s(const QByteArray &key, const QList<qint32> &values)
    {
        key8(key, 9);
        number(&m_kv, 5, 4);
        number(&m_kv, quint64(values.size()), 8);
        for (const qint32 v : values)
            number(&m_kv, quint32(v), 4);
    }
    /// Raw bytes as an entry's value (malformed ones), after its key and type.
    void raw(const QByteArray &key, quint32 type, const QByteArray &value)
    {
        key8(key, type);
        m_kv.append(value);
    }
    void tensor(const QByteArray &name, const QList<quint64> &dims, quint32 type, quint64 offset = 0)
    {
        string(&m_tensors, name);
        number(&m_tensors, quint64(dims.size()), 4);
        for (const quint64 d : dims)
            number(&m_tensors, d, 8);
        number(&m_tensors, type, 4);
        number(&m_tensors, offset, 8);
        ++m_tensorCount;
    }
    QByteArray bytes(qint64 data = 4096, quint64 tensorCount = ~quint64(0), quint64 kvCount = ~quint64(0)) const
    {
        QByteArray out("GGUF");
        number(&out, version, 4);
        number(&out, tensorCount == ~quint64(0) ? m_tensorCount : tensorCount, 8);
        number(&out, kvCount == ~quint64(0) ? m_kvCount : kvCount, 8);
        out += m_kv;
        out += m_tensors;
        out += QByteArray(data, '\0'); // (the weights)
        return out;
    }
    qint64 headerSize() const { return 24 + m_kv.size() + m_tensors.size(); }

    static void number(QByteArray *out, quint64 value, int bytes)
    {
        for (int i = 0; i < bytes; ++i)
            out->append(char((value >> (8 * i)) & 0xFF));
    }
    static void string(QByteArray *out, const QByteArray &value)
    {
        number(out, quint64(value.size()), 8);
        out->append(value);
    }

private:
    void key8(const QByteArray &key, quint32 type)
    {
        string(&m_kv, key);
        number(&m_kv, type, 4);
        ++m_kvCount;
    }
    QByteArray m_kv;
    QByteArray m_tensors;
    quint64 m_kvCount = 0;
    quint64 m_tensorCount = 0;
};

} // namespace brulion::test
