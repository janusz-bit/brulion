// Native messaging frames (docs/CLIPPER.md "The host"): how a browser and its native messaging host
// talk over the host's standard input and output — each message is UTF-8 JSON preceded by its
// length in bytes as a 32-bit unsigned integer in the machine's byte order (Chrome, Chromium,
// Brave, Edge and Firefox alike). Pure: no I/O here; brulion-cli's host mode reads standard input
// into a FrameDecoder and writes encodeFrame()'s bytes. Part of the clipper module — see
// clipper/clipper.h.
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace brulion::clipper {

/// The largest message the host reads (its JSON's bytes): a whole page's HTML with a screenshot of
/// the visible part. Chrome sends at most 64 MiB to a host, too.
inline constexpr quint32 MaxIncomingBytes = 64u * 1024u * 1024u;
/// The largest message a host may send: browsers drop a host that sends more than 1 MiB.
inline constexpr qsizetype MaxOutgoingBytes = 1024 * 1024;

enum class FrameError {
    None,
    TooLarge,    ///< the length is over the limit: the stream cannot go on (the bytes are never read)
    Truncated,   ///< the input ended inside a frame
    InvalidJson, ///< the frame's bytes are no JSON (also invalid UTF-8, an empty frame)
    NotObject,   ///< JSON, but not an object
};

/// A stable snake_case name of `error` ("too_large", "invalid_json", …) for error replies.
QString frameErrorCode(FrameError error);

/// Messages from a byte stream that arrives in pieces of any size.
class FrameDecoder {
public:
    explicit FrameDecoder(quint32 maxBytes = MaxIncomingBytes) : m_max(maxBytes) { }

    struct Frame {
        QJsonObject message;                 ///< empty unless error is None
        FrameError error = FrameError::None; ///< InvalidJson / NotObject: this frame only; TooLarge: the end
        quint32 length = 0;                  ///< the length the frame announced
    };

    /// Append bytes as they arrive (nothing is kept once broken()).
    void feed(QByteArrayView bytes);
    /// The next frame complete in what was fed, or nullopt when more bytes are needed. After a
    /// TooLarge frame there is none (broken()).
    std::optional<Frame> next();
    /// A frame announced more than the limit: nothing more can be read from this stream.
    bool broken() const { return m_broken; }
    /// The input ended: Truncated when a frame (or its length) was started and not finished.
    FrameError finish() const;
    /// Bytes fed and not yet taken by next().
    qsizetype buffered() const { return m_buffer.size() - m_offset; }

private:
    QByteArray m_buffer;
    qsizetype m_offset = 0;
    quint32 m_max;
    bool m_broken = false;
};

/// The bytes of one frame: the length (4 bytes, the machine's byte order) and the compact JSON.
/// Empty when the JSON would be longer than `maxBytes` (send a smaller answer instead).
QByteArray encodeFrame(const QJsonObject &message, qsizetype maxBytes = MaxOutgoingBytes);

/// The length a frame header announces (the first 4 bytes of `header`, machine order).
quint32 frameLength(QByteArrayView header);

} // namespace brulion::clipper
