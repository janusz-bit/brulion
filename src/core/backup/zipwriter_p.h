// ZipWriter — a ZIP archive written entry by entry, a file's bytes handed over in pieces and
// deflated in pieces (miniz's compressor, tdefl), so that no step needs more than one piece: the
// writer of a backup can stop between two pieces of a large file and let the event loop run (the
// browser has no threads), and neither the file nor its compressed bytes are ever in memory whole.
// miniz's own zip writer (mz_zip_writer_add_read_buf_callback) deflates a file in one call and
// cannot be resumed. The reading side stays miniz's. Internal to the backup module.
//
// The format written: UTF-8 names (bit 11), the sizes of a file in its local header (the header of
// a deflated entry is written again once they are known, which needs a seekable device — the
// archive's temporary file —, so that a streaming reader such as Android's ZipInputStream takes
// stored entries too), ZIP64 records only where a size or an offset needs them (an entry of 4 GiB
// and more, an archive of 4 GiB and more, 65 535 entries and more), and a comment after the
// end-of-directory record (the backup's manifest).
#pragma once

#include <QByteArray>
#include <QIODevice>

#include <memory>

namespace brulion::backup::detail {

class ZipWriter {
public:
    enum class Error {
        None,
        WriteFailed, ///< the device refused a write or a seek
        TooLarge,    ///< the archive would grow above Options::maxBytes
        Compression, ///< the compressor failed (never with a working miniz)
        Misuse,      ///< a size that is not the file's, a name too long, an entry in the wrong state
    };
    struct Options {
        qint64 maxBytes = 0;      ///< the archive may not grow above this; 0: no limit
        bool forceZip64 = false;  ///< tests: ZIP64 records everywhere, as for a huge archive
    };

    /// `out` is open for writing and seekable; it is written from its start.
    ZipWriter(QIODevice &out, const Options &options);
    ~ZipWriter();
    Q_DISABLE_COPY_MOVE(ZipWriter)

    Error error() const { return m_error; }
    /// Bytes of the archive written so far (after finish(): its size).
    qint64 offset() const { return m_offset; }

    /// An empty folder ("a/b": the name's "/" is added). `mtimeMs`: since the epoch.
    bool addFolder(const QByteArray &utf8Name, qint64 mtimeMs);

    /// Start a file of exactly `size` bytes: handed over with write(), deflated at `level` (1 … 10)
    /// unless it is stored (`level` 0, or a file of no use to deflate: up to 3 bytes).
    bool beginFile(const QByteArray &utf8Name, qint64 mtimeMs, qint64 size, int level);
    /// The next bytes of the file in progress: its checksum and size grow, the compressed bytes
    /// that come out go to the device. At most `size` bytes in all.
    bool write(const char *data, qsizetype n);
    /// Finish the file in progress: the compressor flushed, the entry recorded. All `size` bytes
    /// must have been written.
    bool endFile();
    /// Give the file in progress up: the next entry starts where it began and overwrites what it
    /// wrote (a device that is not shortened afterwards keeps those bytes behind the end).
    void abandonFile();
    bool inFile() const { return m_open; }

    /// The central directory and the end record with `comment` (at most 65 535 bytes). The archive
    /// ends at offset(); a device that held more is to be shortened to it.
    bool finish(const QByteArray &comment);

private:
    struct Entry;
    bool put(const char *data, qsizetype n);
    bool putAt(qint64 offset, const QByteArray &data);
    bool fail(Error error);
    bool begin(const QByteArray &name, qint64 mtimeMs, qint64 size, int level, bool folder);
    friend struct DeflateSink;
    QByteArray localHeader(bool final) const;
    void recordEntry();

    QIODevice &m_out;
    Options m_options;
    Error m_error = Error::None;
    qint64 m_offset = 0;
    bool m_open = false;
    bool m_finished = false;
    std::unique_ptr<Entry> m_entry;
    QByteArray m_directory; ///< the central directory so far
    qint64 m_entries = 0;
};

} // namespace brulion::backup::detail
