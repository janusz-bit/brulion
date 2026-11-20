#include "io/protobuf_p.h"

#include <bit>
#include <cstring>

namespace brulion::io::detail {

namespace {

int readVarint(QByteArrayView d, qsizetype at, quint64 *value)
{
    quint64 v = 0;
    for (int i = 0; i < 10; ++i) {
        if (at + i >= d.size())
            return 0;
        const auto b = static_cast<unsigned char>(d[at + i]);
        v |= quint64(b & 0x7F) << (7 * i);
        if (!(b & 0x80)) {
            *value = v;
            return i + 1;
        }
    }
    return 0;
}

} // namespace

float ProtoField::asFloat() const
{
    return std::bit_cast<float>(quint32(value));
}

std::optional<QList<ProtoField>> protoFields(QByteArrayView message, int maxFields)
{
    QList<ProtoField> out;
    qsizetype pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        int n = readVarint(message, pos, &key);
        if (n == 0 || key == 0 || key > (quint64(1) << 32))
            return std::nullopt;
        pos += n;
        ProtoField f;
        f.number = int(key >> 3);
        f.wire = int(key & 7);
        switch (f.wire) {
        case 0:
            n = readVarint(message, pos, &f.value);
            if (n == 0)
                return std::nullopt;
            pos += n;
            break;
        case 1:
        case 5: {
            const int size = f.wire == 1 ? 8 : 4;
            if (pos + size > message.size())
                return std::nullopt;
            quint64 v = 0;
            for (int i = size - 1; i >= 0; --i)
                v = (v << 8) | static_cast<unsigned char>(message[pos + i]); // little-endian
            f.value = v;
            pos += size;
            break;
        }
        case 2: {
            quint64 length = 0;
            n = readVarint(message, pos, &length);
            if (n == 0 || length > quint64(message.size() - pos - n))
                return std::nullopt;
            pos += n;
            f.bytes = message.sliced(pos, qsizetype(length));
            pos += qsizetype(length);
            break;
        }
        default: return std::nullopt; // groups (3, 4) and unknown wire types
        }
        out.append(f);
        if (out.size() > maxFields)
            return std::nullopt;
    }
    return out;
}

ProtoMessage::ProtoMessage(QByteArrayView message)
{
    if (auto fields = protoFields(message)) {
        m_fields = std::move(*fields);
        m_valid = true;
    }
}

const ProtoField *ProtoMessage::field(int number) const
{
    for (qsizetype i = m_fields.size() - 1; i >= 0; --i) {
        if (m_fields[i].number == number)
            return &m_fields[i];
    }
    return nullptr;
}

QList<ProtoField> ProtoMessage::repeated(int number) const
{
    QList<ProtoField> out;
    for (const ProtoField &f : m_fields) {
        if (f.number == number)
            out.append(f);
    }
    return out;
}

qint64 ProtoMessage::integer(int number, qint64 fallback) const
{
    const ProtoField *f = field(number);
    return f && f->wire != 2 ? f->asInt() : fallback;
}

QString ProtoMessage::string(int number) const
{
    const ProtoField *f = field(number);
    return f && f->wire == 2 ? f->asString() : QString();
}

QByteArrayView ProtoMessage::bytes(int number) const
{
    const ProtoField *f = field(number);
    return f && f->wire == 2 ? f->bytes : QByteArrayView();
}

ProtoMessage ProtoMessage::message(int number) const
{
    const ProtoField *f = field(number);
    return f && f->wire == 2 ? ProtoMessage(f->bytes) : ProtoMessage();
}

ProtoMessage ProtoMessage::first(int number) const
{
    for (const ProtoField &f : m_fields) {
        if (f.number == number && f.wire == 2)
            return ProtoMessage(f.bytes);
    }
    return {};
}

} // namespace brulion::io::detail
