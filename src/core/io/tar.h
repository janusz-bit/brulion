// A minimal tar reader (POSIX ustar, old v7, GNU long names, PAX path records) — enough for
// Joplin JEX exports and other tar archives of notes. Part of the io module — see io/io.h.
#pragma once

#include "io/files.h"

#include <QByteArrayView>
#include <QStringList>

namespace brulion::io {

/// True when `data` starts with a tar header (valid checksum; ustar magic or a plausible v7
/// header).
bool isTar(QByteArrayView data);

/// Regular files of a tar archive in archive order; paths are made safe with
/// sanitizeArchivePath() (io/zip.h). Directories, links and special files are skipped. Reading
/// stops with a warning at the first damaged header or truncated entry; the files read so far
/// are returned. Never crashes on malformed input.
QList<FileEntry> readTar(QByteArrayView tar, QStringList *warnings = nullptr);

} // namespace brulion::io
