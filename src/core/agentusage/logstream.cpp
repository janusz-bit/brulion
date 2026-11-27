#include "agentusage/logstream_p.h"

#include <zstd.h>

#include <algorithm>

namespace brulion::agentusage {

struct LogStream::Decoder {
    ZSTD_DStream *stream = nullptr;
    QByteArray in;                        ///< compressed bytes read from the file
    ZSTD_inBuffer buffer{nullptr, 0, 0};  ///< what of them is not decoded yet
    size_t hint = 1;                      ///< the decoder's last answer: 0 once a frame ended and all of it is out
    bool fileEnded = false;

    Decoder() = default;
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;
    ~Decoder() { ZSTD_freeDStream(stream); }
};

LogStream::LogStream(const QString &path, bool compressed, qint64 maxBytes)
    : m_file(path), m_compressed(compressed), m_maxBytes(std::max<qint64>(maxBytes, 0))
{ }

LogStream::~LogStream() = default;

bool LogStream::open()
{
    if (!m_file.open(QIODevice::ReadOnly))
        return false;
    if (!m_compressed)
        return true;
    m_decoder = std::make_unique<Decoder>();
    m_decoder->stream = ZSTD_createDStream();
    return m_decoder->stream
           && !ZSTD_isError(ZSTD_DCtx_setParameter(m_decoder->stream, ZSTD_d_windowLogMax, MaxWindowLog));
}

QByteArray LogStream::read(qint64 max)
{
    if (max <= 0 || m_ended || !m_file.isOpen())
        return {};
    QByteArray out = m_compressed ? readFrames(max) : m_file.read(max);
    m_position += out.size();
    if (out.isEmpty())
        m_ended = true;
    return out;
}

bool LogStream::skipTo(qint64 position)
{
    if (position < m_position || !m_file.isOpen())
        return false;
    if (!m_compressed) {
        if (position > m_file.size() || !m_file.seek(position))
            return false;
        m_position = position;
        return true;
    }
    constexpr qint64 Piece = 256 * 1024;
    while (m_position < position) {
        if (read(std::min(position - m_position, Piece)).isEmpty())
            return false;
    }
    return true;
}

QByteArray LogStream::readFrames(qint64 max)
{
    Decoder &d = *m_decoder;
    // (One byte more than the text may still hold shows that it holds more: a "zip bomb".)
    const qint64 room = std::max<qint64>(m_maxBytes - m_position, 0);
    QByteArray out(std::min(max, room + 1), Qt::Uninitialized);
    ZSTD_outBuffer o{out.data(), size_t(out.size()), 0};
    while (o.pos < o.size) {
        const bool more = d.buffer.pos < d.buffer.size || refill();
        if (!more && d.hint == 0) {
            m_ended = true; // (the last frame ended: the text's end)
            break;
        }
        const size_t outBefore = o.pos;
        const size_t inBefore = d.buffer.pos;
        const size_t hint = ZSTD_decompressStream(d.stream, &o, &d.buffer);
        if (ZSTD_isError(hint)) {
            m_failed = true; // (no zstd, a broken frame, a window too large)
            break;
        }
        d.hint = hint;
        if (!more && o.pos == outBefore && d.buffer.pos == inBefore) {
            m_failed = true; // (the frame goes on beyond the file's end: cut short)
            break;
        }
    }
    out.truncate(qsizetype(o.pos));
    if (out.size() > room) {
        out.truncate(qsizetype(room));
        m_failed = true;
    }
    if (m_failed)
        m_ended = true;
    return out;
}

bool LogStream::refill()
{
    Decoder &d = *m_decoder;
    if (d.fileEnded)
        return false;
    d.in = m_file.read(qint64(ZSTD_DStreamInSize()));
    d.buffer = ZSTD_inBuffer{d.in.constData(), size_t(d.in.size()), 0};
    if (d.in.isEmpty())
        d.fileEnded = true;
    return !d.fileEnded;
}

} // namespace brulion::agentusage
