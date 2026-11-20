#include "clipper/nativemessaging.h"

#include <QJsonDocument>
#include <QJsonParseError>

#include <cstring>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::clipper {

namespace {
constexpr qsizetype HeaderBytes = 4;
} // namespace

QString frameErrorCode(FrameError error)
{
    switch (error) {
    case FrameError::None: return u"none"_s;
    case FrameError::TooLarge: return u"too_large"_s;
    case FrameError::Truncated: return u"truncated"_s;
    case FrameError::InvalidJson: return u"invalid_json"_s;
    case FrameError::NotObject: return u"not_object"_s;
    }
    return u"none"_s;
}

quint32 frameLength(QByteArrayView header)
{
    quint32 length = 0;
    if (header.size() >= HeaderBytes)
        std::memcpy(&length, header.data(), sizeof length); // the machine's byte order
    return length;
}

void FrameDecoder::feed(QByteArrayView bytes)
{
    if (m_broken || bytes.isEmpty())
        return;
    // Drop what next() has taken before growing the buffer: a long session keeps one frame at most.
    if (m_offset > 0 && m_offset == m_buffer.size()) {
        m_buffer.clear();
        m_offset = 0;
    } else if (m_offset > 64 * 1024 && m_offset * 2 > m_buffer.size()) {
        m_buffer.remove(0, m_offset);
        m_offset = 0;
    }
    m_buffer.append(bytes.data(), bytes.size());
}

std::optional<FrameDecoder::Frame> FrameDecoder::next()
{
    if (m_broken || buffered() < HeaderBytes)
        return std::nullopt;
    Frame frame;
    frame.length = frameLength(QByteArrayView(m_buffer).sliced(m_offset, HeaderBytes));
    if (frame.length > m_max) {
        // Never wait for (or allocate) what the length announces: it may be anything, e.g. the
        // first four letters of text typed at a terminal.
        m_broken = true;
        m_buffer.clear();
        m_offset = 0;
        frame.error = FrameError::TooLarge;
        return frame;
    }
    if (buffered() - HeaderBytes < qsizetype(frame.length))
        return std::nullopt;
    const QByteArray json = m_buffer.sliced(m_offset + HeaderBytes, frame.length);
    m_offset += HeaderBytes + frame.length;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || doc.isNull())
        frame.error = FrameError::InvalidJson;
    else if (!doc.isObject())
        frame.error = FrameError::NotObject;
    else
        frame.message = doc.object();
    return frame;
}

FrameError FrameDecoder::finish() const
{
    return !m_broken && buffered() > 0 ? FrameError::Truncated : FrameError::None;
}

QByteArray encodeFrame(const QJsonObject &message, qsizetype maxBytes)
{
    const QByteArray json = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (json.size() > maxBytes || json.size() > qsizetype(std::numeric_limits<quint32>::max()))
        return {};
    const quint32 length = quint32(json.size());
    QByteArray out(HeaderBytes, Qt::Uninitialized);
    std::memcpy(out.data(), &length, sizeof length);
    out += json;
    return out;
}

} // namespace brulion::clipper
