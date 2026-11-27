#include "agentusage/logstream_p.h"

#include <zstd.h>

#include <algorithm>

namespace brulion::agentusage {

struct LogStream::Decoder {
    ZSTD_DStream *stream = nullptr;
    QByteArray in;                       ///< compressed bytes read from the file
    ZSTD_inBuffer buffer{nullptr, 0, 0}; ///< what of them is not decoded yet
    size_t hint = 1;                     ///< the decoder's last answer: 0 once a frame ended and all of it is out

    Decoder() = default;
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;
    ~Decoder() { ZSTD_freeDStream(stream); }

    enum class Step { More, End, Failed };
    /// One step of decoding into `o`: End once the last frame ended with the file, Failed for no
    /// zstd, a broken frame, a window too large — or a frame going on beyond the file's end.
    Step step(QFile &file, ZSTD_outBuffer &o)
    {
        const bool more = buffer.pos < buffer.size || refill(file);
        if (!more && hint == 0)
            return Step::End;
        const size_t outBefore = o.pos;
        const size_t inBefore = buffer.pos;
        const size_t answer = ZSTD_decompressStream(stream, &o, &buffer);
        if (ZSTD_isError(answer) || (!more && o.pos == outBefore && buffer.pos == inBefore))
            return Step::Failed;
        hint = answer;
        return Step::More;
    }

    /// More compressed bytes when those read are used up; false at the file's end.
    bool refill(QFile &file)
    {
        in = file.read(qint64(ZSTD_DStreamInSize()));
        buffer = ZSTD_inBuffer{in.constData(), size_t(in.size()), 0};
        return !in.isEmpty();
    }
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
    // (One byte more than the text may still hold shows that it holds more: a "zip bomb".)
    const qint64 room = std::max<qint64>(m_maxBytes - m_position, 0);
    QByteArray out(std::min(max, room + 1), Qt::Uninitialized);
    ZSTD_outBuffer o{out.data(), size_t(out.size()), 0};
    Decoder::Step step = Decoder::Step::More;
    while (o.pos < o.size && step == Decoder::Step::More)
        step = m_decoder->step(m_file, o);
    out.truncate(qsizetype(o.pos));
    m_failed = step == Decoder::Step::Failed || out.size() > room;
    m_ended = m_failed || step == Decoder::Step::End;
    out.truncate(std::min(out.size(), qsizetype(room)));
    return out;
}

} // namespace brulion::agentusage
