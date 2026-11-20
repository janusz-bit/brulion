// The protocol buffers wire format (https://protobuf.dev/programming-guides/encoding/), read
// without a schema: a message as its fields in order. For Apple Notes' note data. Bounded and
// checked: malformed input gives nullopt. Internal to the io module.
#pragma once

#include <QByteArrayView>
#include <QList>
#include <QString>

#include <optional>

namespace brulion::io::detail {

struct ProtoField {
    int number = 0;
    int wire = 0;         ///< 0 varint, 1 fixed64, 2 length-delimited, 5 fixed32
    quint64 value = 0;    ///< varint / fixed values
    QByteArrayView bytes; ///< length-delimited contents (a view into the message)

    qint64 asInt() const { return qint64(value); }
    float asFloat() const;
    QString asString() const { return QString::fromUtf8(bytes); }
};

/// The fields of `message` in order; nullopt when it is malformed (a truncated varint or length,
/// a group, an unknown wire type) or has more than `maxFields` fields.
std::optional<QList<ProtoField>> protoFields(QByteArrayView message, int maxFields = 1'000'000);

/// A message's fields by number (empty when malformed).
class ProtoMessage {
public:
    ProtoMessage() = default;
    explicit ProtoMessage(QByteArrayView message);

    bool valid() const { return m_valid; }
    const QList<ProtoField> &fields() const { return m_fields; }
    /// The last field with `number` (protobuf: the last one wins), nullptr when absent.
    const ProtoField *field(int number) const;
    /// The fields with `number` in order — copies, safe to iterate over a temporary message (their
    /// bytes view the data the message was read from).
    QList<ProtoField> repeated(int number) const;
    qint64 integer(int number, qint64 fallback = 0) const;
    QString string(int number) const;
    QByteArrayView bytes(int number) const;
    ProtoMessage message(int number) const;
    /// The first field with `number` as a message (a repeated field's first element).
    ProtoMessage first(int number) const;
    bool has(int number) const { return field(number) != nullptr; }

private:
    QList<ProtoField> m_fields;
    bool m_valid = false;
};

} // namespace brulion::io::detail
