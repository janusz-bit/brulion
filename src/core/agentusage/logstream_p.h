// LogStream — the text of one session log, read in pieces (agentusage/history.cpp): the file itself,
// or — a Codex rollout Codex compressed ("rollout-….jsonl.zst": zstd frames, Codex's
// codex-rs/rollout/src/compression.rs) — what its frames hold, decompressed while it is read (the
// vendored decoder, 3rdparty/zstd; never the whole file in memory).
//
//   LogStream text(path, compressed, MaxUnpackedBytes);
//   if (!text.open()) …                       // not readable now
//   const QByteArray head = text.read(4096);  // at most that much, less only at the end
//   text.skipTo(offset);                      // a plain file seeks; frames are decoded and dropped
//   for (QByteArray piece; !(piece = text.read(chunk)).isEmpty();) …
//   if (text.failed()) …                      // the rest could not be read
//
// Bounded for files from anywhere: a frame's window at most 2^MaxWindowLog bytes (Codex writes
// level 3: 2^21 at most), at most `maxBytes` of a compressed file's text (more is a "zip bomb": the
// text ends there), a broken or cut frame, one that asks for a larger window or anything that is no
// zstd ends the text with failed() set — what came before it stays read.
#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>

#include <memory>

namespace brulion::agentusage {

class LogStream {
public:
    /// The largest window a frame may ask for (2^24 bytes = 16 MiB of the decoder's memory).
    static constexpr int MaxWindowLog = 24;

    LogStream(const QString &path, bool compressed, qint64 maxBytes);
    ~LogStream();
    LogStream(const LogStream &) = delete;
    LogStream &operator=(const LogStream &) = delete;

    /// False when the file cannot be opened (or no decoder made).
    bool open();
    /// Up to `max` bytes of the text from where it stands — fewer only at its end; empty there (and
    /// after it ended early: failed()).
    QByteArray read(qint64 max);
    /// On to `position` bytes from the text's start (never back); false when the text ends before.
    bool skipTo(qint64 position);
    /// Where the text stands: the bytes read or skipped so far.
    qint64 position() const { return m_position; }
    /// The text ended early: a broken, cut or too large frame, or no zstd at all.
    bool failed() const { return m_failed; }

private:
    QByteArray readFrames(qint64 max);

    struct Decoder;
    QFile m_file;
    bool m_compressed = false;
    qint64 m_maxBytes = 0;
    qint64 m_position = 0;
    bool m_failed = false;
    bool m_ended = false;
    std::unique_ptr<Decoder> m_decoder;
};

} // namespace brulion::agentusage
