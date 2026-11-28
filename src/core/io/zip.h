// ZIP archives (vendored miniz, 3rdparty/miniz) with a Qt-friendly API: archives in memory or
// read from a file piece by piece, large entries unpacked into files of a staging folder, UTF-8
// entry names, nested archives, defensive limits. Part of the io module — see io/io.h for the
// overview.
#pragma once

#include "io/files.h"

#include <QByteArrayView>
#include <QDateTime>
#include <QStringList>
#include <QStringView>

#include <functional>
#include <optional>

namespace brulion::io {

struct ZipReadOptions {
    /// Entries ending in ".zip" are unpacked in place: their files appear in the folder of the
    /// nested archive (the nested .zip itself is dropped). Notion exports nest their parts so.
    bool expandNested = true;
    /// Deepest nesting unpacked (deeper archives stay plain entries).
    int maxNestingDepth = 3;
    /// Entries larger than this (uncompressed) are skipped with a warning.
    qint64 maxEntrySize = qint64(512) << 20;
    /// Reading stops (with a warning) once this many uncompressed bytes were read.
    qint64 maxTotalSize = qint64(1536) << 20;
    /// Reading stops (with a warning) after this many entries.
    int maxEntries = 200'000;
    /// A folder (existing; its owner removes it after the import) for the large entries: a
    /// picture, audio, video, PDF or archive — what importers pass on without reading it, or
    /// prepareImportFiles() unpacks — larger than `stageAbove` is unpacked into a new file there
    /// piece by piece (miniz's read buffer and inflate window: some 100 KB in memory, whatever its
    /// size) and comes back with FileEntry::file set and no data. "": every entry in memory.
    QString stagingFolder;
    qint64 stageAbove = qint64(1) << 20;
    /// Tests: the size of each piece written to a staged file.
    std::function<void(qint64 bytes)> onStagedPiece;
};

/// True when `data` starts like a ZIP archive (local file header or empty-archive marker).
bool isZip(QByteArrayView data);

/// All files of a ZIP archive in archive order. Directories are skipped; entry names are decoded
/// as UTF-8 (when flagged or valid) or else as CP437, and made safe with sanitizeArchivePath()
/// (no absolute paths, no "..", "\\" -> "/"); entries whose name becomes empty are skipped.
/// Encrypted, unsupported or corrupt entries are skipped with a warning; an invalid archive gives
/// an empty list and a warning. Never throws or crashes on malformed input.
QList<FileEntry> readZip(QByteArrayView zip, QStringList *warnings = nullptr, const ZipReadOptions &options = {});
/// readZip() of the archive in the file `file`, read piece by piece (never loaded whole).
QList<FileEntry> readZipFile(const QString &file, QStringList *warnings = nullptr, const ZipReadOptions &options = {});
/// One entry of listZipFile(): its path (sanitizeArchivePath()), its size unpacked and packed, and
/// whether it is encrypted.
struct ZipListEntry {
    QString path;
    qint64 size = 0;
    qint64 packedSize = 0;
    bool encrypted = false;
};

/// The files of a ZIP archive listed, nothing unpacked: `ok` false when `file` is no readable
/// archive; at most `maxEntries` of them (`cut` when there are more). Directories are left out.
struct ZipListing {
    bool ok = false;
    bool cut = false;
    QList<ZipListEntry> entries;
};
ZipListing listZipFile(const QString &file, int maxEntries = 10'000);

/// The first `bytes` of a file ("" when it cannot be read): enough for isZip() / isTar().
QByteArray fileHead(const QString &file, qint64 bytes = 512);

struct ZipWriteOptions {
    /// Deflate level 0 (store) … 9; 6 is zlib's default.
    int compressionLevel = 6;
    /// Store files that are already compressed (images, archives, audio, video, PDF) uncompressed.
    bool storeCompressedFormats = true;
    /// Modification time written for every entry (local time). Invalid: 1980-01-01 00:00, the
    /// earliest ZIP date, so archives are byte-for-byte reproducible.
    QDateTime modified;
};

/// A ZIP archive (UTF-8 names) with `entries` in the given order. Paths are made safe with
/// sanitizeArchivePath(); entries whose path is empty after that are skipped. Later entries with
/// the same path are skipped too (an archive never contains duplicates).
QByteArray writeZip(const QList<FileEntry> &entries, const ZipWriteOptions &options = {});

/// The data of a gzip member (RFC 1952: Apple Notes' note data, `.gz` files) or a zlib stream
/// (RFC 1950); nullopt when it is neither, is damaged, or unpacks to more than `maxSize` bytes
/// (a "zip bomb").
std::optional<QByteArray> gunzip(QByteArrayView data, qint64 maxSize = qint64(256) << 20);

/// A relative, safe path from an archive entry name: "\\" -> "/", drive letters and leading "/"
/// removed, "." and empty segments dropped, ".." resolved inside the archive (never above it),
/// control characters removed, NFC. "" when nothing usable is left.
QString sanitizeArchivePath(QStringView name);

} // namespace brulion::io
