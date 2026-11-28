// A minimal tar reader (POSIX ustar, old v7, GNU long names, PAX path records) — enough for
// Joplin JEX exports and other tar archives of notes. Part of the io module — see io/io.h.
#pragma once

#include "io/files.h"
#include "io/zip.h"

#include <QByteArrayView>
#include <QStringList>

namespace brulion::io {

/// True when `data` starts with a tar header (valid checksum; ustar magic or a plausible v7
/// header).
bool isTar(QByteArrayView data);

/// Regular files of a tar archive in archive order; paths are made safe with
/// sanitizeArchivePath() (io/zip.h). Directories, links and special files are skipped. Reading
/// stops with a warning at the first damaged header or truncated entry; the files read so far
/// are returned. `limits` bound it as they bound readZip(): files larger than maxEntrySize are
/// skipped with a warning, reading stops with one before maxTotalSize bytes or maxEntries files
/// are exceeded (archives inside are never unpacked). Never crashes on malformed input.
QList<FileEntry> readTar(QByteArrayView tar, QStringList *warnings = nullptr, const ZipReadOptions &limits = {});

/// readTar() of a tar archive on disk, read header by header — never whole: an entry larger than
/// `limits.stageAbove` of a kind that is passed on unread (a picture, audio, video, a PDF, an
/// archive) is copied into a new file of `limits.stagingFolder` in pieces (ZipReadOptions, as
/// readZipFile() does) and comes back with FileEntry::file set; one too large is skipped unread.
/// A file that cannot be opened gives a warning.
QList<FileEntry> readTarFile(const QString &file, QStringList *warnings = nullptr, const ZipReadOptions &limits = {});

} // namespace brulion::io
