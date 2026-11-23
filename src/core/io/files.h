// A file given by path and content — the unit exchanged by the io module: archive entries,
// importer input and exporter input. Part of the io module — see io/io.h for the overview.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <utility>

namespace brulion::io {

/// A file: a relative POSIX path ("folder/Note.md") and its bytes — in memory, or (`file` set) in
/// a file on disk: a large attachment unpacked from an archive into a staging folder
/// (ZipReadOptions::stagingFolder), or a picked archive not read in. Such an entry's `data` is
/// empty; importers pass it on as it is (ImportContext::addAttachment(), addFile()) and never
/// read it, and the import's writer copies the file into the notebook in pieces.
struct FileEntry {
    FileEntry() = default;
    FileEntry(QString entryPath, QByteArray bytes, QString onDiskFile = {}, qint64 onDiskSize = 0)
        : path(std::move(entryPath)), data(std::move(bytes)), file(std::move(onDiskFile)), fileSize(onDiskSize)
    { }

    QString path;
    QByteArray data;
    QString file;        ///< absolute path of the bytes on disk; "" = they are `data`
    qint64 fileSize = 0; ///< the size of `file`

    qint64 size() const { return file.isEmpty() ? data.size() : fileSize; }
    bool onDisk() const { return !file.isEmpty(); }

    friend bool operator==(const FileEntry &, const FileEntry &) = default;
};

/// The bytes of `entry`: its data, or its file read whole (for the rare reader of an entry on
/// disk; empty when it cannot be read).
QByteArray bytesOf(const FileEntry &entry);
/// The same content: equal data, or the bytes of their files compared in pieces.
bool sameContent(const FileEntry &a, const FileEntry &b);

/// A file handed to an importer (picked by the user or unpacked from an archive).
using InputFile = FileEntry;

} // namespace brulion::io
