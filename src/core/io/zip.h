// ZIP archives (vendored miniz, 3rdparty/miniz) with a Qt-friendly API: whole archives in
// memory, UTF-8 entry names, nested archives, defensive limits. Part of the io module — see
// io/io.h for the overview.
#pragma once

#include "io/files.h"

#include <QByteArrayView>
#include <QDateTime>
#include <QStringList>
#include <QStringView>

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
};

/// True when `data` starts like a ZIP archive (local file header or empty-archive marker).
bool isZip(QByteArrayView data);

/// All files of a ZIP archive in archive order. Directories are skipped; entry names are decoded
/// as UTF-8 (when flagged or valid) or else as CP437, and made safe with sanitizeArchivePath()
/// (no absolute paths, no "..", "\\" -> "/"); entries whose name becomes empty are skipped.
/// Encrypted, unsupported or corrupt entries are skipped with a warning; an invalid archive gives
/// an empty list and a warning. Never throws or crashes on malformed input.
QList<FileEntry> readZip(QByteArrayView zip, QStringList *warnings = nullptr, const ZipReadOptions &options = {});

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

/// A relative, safe path from an archive entry name: "\\" -> "/", drive letters and leading "/"
/// removed, "." and empty segments dropped, ".." resolved inside the archive (never above it),
/// control characters removed, NFC. "" when nothing usable is left.
QString sanitizeArchivePath(QStringView name);

} // namespace brulion::io
