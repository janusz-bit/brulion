// A file given by path and content — the unit exchanged by the io module: archive entries,
// importer input and exporter input. Part of the io module — see io/io.h for the overview.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace brulion::io {

/// A file: a relative POSIX path ("folder/Note.md") and its bytes.
struct FileEntry {
    QString path;
    QByteArray data;

    friend bool operator==(const FileEntry &, const FileEntry &) = default;
};

/// A file handed to an importer (picked by the user or unpacked from an archive).
using InputFile = FileEntry;

} // namespace brulion::io
