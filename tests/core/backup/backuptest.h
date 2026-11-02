// Helpers of the backup tests: running the module's jobs to their end on a worker pool or in slices,
// files on disk, and archives written directly with miniz (names Brulion itself never writes).
#pragma once

#include "backup/archive.h"
#include "backup/restore.h"
#include "notebook/indexloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>
#include <QThreadPool>

#include <miniz.h>

#include <utility>

namespace backuptest {

using namespace brulion;
using namespace brulion::backup;
using namespace Qt::StringLiterals;

inline notebook::WorkerPool poolOn(QThreadPool *threads)
{
    notebook::WorkerPool pool;
    if (!threads)
        return pool;
    pool.threads = 1;
    pool.start = [threads](std::function<void()> job) { threads->start(std::move(job)); };
    return pool;
}

inline void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    QCOMPARE(file.write(bytes), bytes.size());
}

inline QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// An archive of (name, bytes) written with miniz as given; a name ending in "/" is a directory.
inline QByteArray rawZip(const QList<QPair<QByteArray, QByteArray>> &entries, mz_uint level = 6)
{
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0))
        return {};
    for (const auto &[name, data] : entries) {
        mz_zip_writer_add_mem(&zip, name.constData(), data.constData(), size_t(data.size()), level);
    }
    void *buffer = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
    const QByteArray out(static_cast<const char *>(buffer), qsizetype(size));
    mz_free(buffer);
    mz_zip_writer_end(&zip);
    return out;
}

/// `zip` as Brulion's own writer finishes an archive: with a manifest in its comment.
inline QByteArray withManifest(QByteArray zip, const QString &notebookId = {}, bool complete = true)
{
    const QByteArray manifest = backupManifest(notebookId, complete, 1);
    zip[zip.size() - 2] = char(manifest.size() & 0xff);
    zip[zip.size() - 1] = char((manifest.size() >> 8) & 0xff);
    return zip + manifest;
}

inline WriteResult runWriter(const notebook::WorkerPool &pool, const Sources &sources, const WriteOptions &options)
{
    BackupWriter writer(pool);
    QSignalSpy spy(&writer, &BackupWriter::finished);
    writer.start(sources, options);
    if (!spy.wait(60000))
        return WriteResult{WriteError::WriteFailed, u"timed out"_s, {}, 0, 0, {}, {}};
    return spy.first().first().value<WriteResult>();
}

inline RestoreCheck runChecker(const notebook::WorkerPool &pool, const QString &archive,
                               const QList<NotebookFile> &current, const QStringList &folders,
                               const RestoreCheckOptions &options = {})
{
    RestoreChecker checker(pool);
    QSignalSpy spy(&checker, &RestoreChecker::finished);
    checker.start(archive, current, folders, options);
    if (!spy.wait(60000))
        return RestoreCheck{ArchiveProblem::Unreadable, false, u"timed out"_s, {}};
    return spy.first().first().value<RestoreCheck>();
}

} // namespace backuptest
