#pragma once

// The bookkeeping of the browser's file picker (wasm/wasmfiles.h), pure and built everywhere so
// that tests/platform checks it without a browser. Qt's private QWasmLocalFileAccess::openFiles
// reports a pick in three callbacks: how many files were chosen (0: cancelled), then for each
// file in order "accept it?" (a buffer for its bytes, or nullptr to skip it) and, for every
// accepted one, "its bytes are in the buffer". PickCollector answers them and hands the files
// over once every one was read or skipped.

#include <QByteArray>
#include <QList>
#include <QString>

#include <deque>
#include <functional>

namespace brulion::platform {

/// One file from the browser's picker.
struct PickedFile {
    QString name;    ///< as the browser reports it (Polish letters kept)
    QByteArray data; ///< empty when not read (tooLarge, overTotal)
    qint64 size = 0;
    bool tooLarge = false;  ///< above the limit per file: never read into memory
    bool overTotal = false; ///< the pick's total was reached before it: not read (nor any file after it)
    /// Its bytes are in `data`.
    bool isRead() const { return !tooLarge && !overTotal; }
};

class PickCollector {
public:
    using Done = std::function<void(const QList<PickedFile> &files)>;

    /// Files above `maxFileBytes` are skipped and reported tooLarge; reading stops at the file that
    /// would take the read files above `maxTotalBytes` (it and the ones after it: overTotal) —
    /// FileGatherer's two limits (GatherLimits; < 0: no limit). Every read file stays in memory
    /// until done(), so the total keeps a pick of many large files within the browser's memory.
    PickCollector(qint64 maxFileBytes, qint64 maxTotalBytes, Done done);

    /// The picker closed with `count` files chosen; 0 (cancelled): done({}) at once.
    void closed(int count);
    /// The next file: a buffer of `size` bytes for its data, or nullptr to skip it (too large,
    /// over the total).
    char *accept(quint64 size, const QString &name);
    /// The last accepted file's bytes are in its buffer.
    void ready();
    /// done() was called.
    bool finished() const { return m_finished; }

private:
    void finishIfComplete();

    qint64 m_maxBytes;
    qint64 m_maxTotalBytes;
    qint64 m_readBytes = 0;
    bool m_stopped = false; ///< the total was reached: no more files are read
    Done m_done;
    // A deque: the buffers handed out must stay where they are while later files are added.
    std::deque<PickedFile> m_files;
    int m_expected = -1;
    int m_processed = 0;
    bool m_finished = false;
};

} // namespace brulion::platform
