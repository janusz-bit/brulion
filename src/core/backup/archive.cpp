#include "backup/archive.h"

#include "backup/taskrunner_p.h"
#include "backup/zipwriter_p.h"
#include "util/paths.h"
#include "util/unicode.h"
#include "vault/vaultconfig.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStringDecoder>

#include <miniz.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <mutex>
#include <new>
#include <utility>

namespace brulion::backup {

using namespace Qt::StringLiterals;

namespace {

constexpr int MaxReadAttempts = 3;
/// A file is read, checksummed and deflated this much at a time: the unit a slice may overrun by.
constexpr qint64 ReadPieceBytes = 64 << 10;
/// More than any deflate stream can expand (about 1032:1), with room for headers.
constexpr qint64 MaxDeflateRatio = 1100;
constexpr qint64 ReadBufferBytes = 64 << 10;
/// ArchiveReader::read() keeps an entry in memory.
constexpr qint64 MaxReadEntryBytes = qint64(1) << 30;

bool isCompressedFormat(const QString &path)
{
    static const QSet<QString> extensions = {
            u"png"_s, u"jpg"_s, u"jpeg"_s, u"gif"_s,  u"webp"_s,  u"heic"_s, u"avif"_s, u"zip"_s,  u"gz"_s,
            u"bz2"_s, u"xz"_s,  u"7z"_s,   u"rar"_s,  u"mp3"_s,   u"m4a"_s,  u"aac"_s,  u"ogg"_s,  u"opus"_s,
            u"mp4"_s, u"m4v"_s, u"mov"_s,  u"webm"_s, u"mkv"_s,   u"pdf"_s,  u"docx"_s, u"xlsx"_s, u"pptx"_s,
            u"odt"_s, u"ods"_s, u"epub"_s, u"woff"_s, u"woff2"_s, u"3gp"_s,
    };
    const qsizetype dot = path.lastIndexOf(u'.');
    return dot >= 0 && extensions.contains(path.sliced(dot + 1).toLower());
}

bool isControl(QChar c)
{
    return c.unicode() < 0x20 || c.unicode() == 0x7f;
}

/// A name for the log: a hostile entry's control characters never reach a log line.
QString printable(QString name)
{
    name.truncate(300);
    std::ranges::replace_if(name, isControl, u'?');
    return name;
}

} // namespace

// ---- Writing ----------------------------------------------------------------------------------

namespace detail {

class WriteTask : public Task {
public:
    WriteTask(Sources sources, WriteOptions options) : m_sources(std::move(sources)), m_options(std::move(options)) { }

    bool step(int budgetMs) override;
    void cancel() { m_cancel.store(true); }
    qreal progress() const
    {
        const qint64 total = m_sources.totalBytes;
        if (total <= 0)
            return m_finished.load() ? 1.0 : 0.0;
        return std::min<qreal>(1.0, qreal(m_doneBytes.load()) / qreal(total));
    }
    const WriteResult &result() const { return m_result; }

private:
    /// The file being added: read in pieces, a few at a time per step, so that one slice (the
    /// browser's) never takes more than a moment whatever the file's size.
    struct Current {
        const SourceFile *source = nullptr;
        std::unique_ptr<QFile> file;
        qint64 size = 0;
        qint64 mtime = 0;
        qint64 offset = 0;  ///< bytes read and written
        qint64 counted = 0; ///< of the file's bytes in the progress
        int attempt = 0;
    };
    enum class Pumped { More, Done, Retry, Skip, Fatal };

    void open();
    void addFolder(const QString &folder);
    /// Start `source` (its attempt `attempt`): m_current, or nothing when it is left out.
    void startFile(const SourceFile &source, int attempt);
    Pumped pump(const QElapsedTimer &clock, int budgetMs);
    void finishFile(Pumped how);
    void finishArchive();
    /// The writer failed: the run fails with the reason it knows.
    void failForWriter();
    void fail(WriteError error, const QString &detail);

    Sources m_sources;
    WriteOptions m_options;
    std::atomic_bool m_cancel{false};
    std::atomic<qint64> m_doneBytes{0};
    std::atomic_bool m_finished{false};
    WriteResult m_result;

    std::unique_ptr<QSaveFile> m_file;
    std::unique_ptr<ZipWriter> m_zip;
    QString m_workPath;
    bool m_started = false;
    int m_archivedNotebookFiles = 0; ///< .brulion/config.json not counted
    qsizetype m_nextFile = 0;
    qsizetype m_nextFolder = 0;
    std::optional<Current> m_current;
    QByteArray m_piece;
};

void WriteTask::open()
{
    const QFileInfo target(m_options.targetPath);
    const QString folder = target.absolutePath();
    if (!QFileInfo(folder).isDir()) {
        if (!m_options.createFolder) {
            fail(WriteError::FolderMissing, u"no folder %1"_s.arg(folder));
            return;
        }
        if (!QDir().mkpath(folder)) {
            fail(WriteError::FolderUnwritable, u"cannot create %1"_s.arg(folder));
            return;
        }
    }
    if (m_options.workFolder.isEmpty()) {
        m_workPath = m_options.targetPath;
    } else {
        QDir().mkpath(m_options.workFolder);
        m_workPath = QDir(m_options.workFolder).filePath(target.fileName());
    }
    m_file = std::make_unique<QSaveFile>(m_workPath);
    if (!m_file->open(QIODevice::WriteOnly)) {
        fail(WriteError::FolderUnwritable, m_file->errorString());
        return;
    }
    m_zip = std::make_unique<ZipWriter>(*m_file, ZipWriter::Options{.maxBytes = m_options.maxBytes});
    m_piece.resize(ReadPieceBytes);
}

void WriteTask::failForWriter()
{
    switch (m_zip->error()) {
    case ZipWriter::Error::TooLarge:
        fail(WriteError::TooLarge, u"above the limit of %1 bytes"_s.arg(m_options.maxBytes));
        break;
    case ZipWriter::Error::Compression: fail(WriteError::WriteFailed, u"the compression failed"_s); break;
    case ZipWriter::Error::Misuse: fail(WriteError::WriteFailed, u"the archive writer was misused"_s); break;
    default: fail(WriteError::WriteFailed, m_file->errorString()); break;
    }
}

void WriteTask::addFolder(const QString &folder)
{
    if (checkEntryPath(folder) != EntryPath::Ok) {
        m_result.skipped.append(folder);
        return;
    }
    if (!m_zip->addFolder(folder.toUtf8(), 0))
        failForWriter();
}

void WriteTask::startFile(const SourceFile &source, int attempt)
{
    const QFileInfo info(source.absolutePath);
    // A link is no file of the notebook (the vault lists none): one put in a file's place since
    // the files were listed — by a sync, by another program — would have the backup read what it
    // points at, a file anywhere on the device. Asked before the file is opened and after.
    if (info.isSymLink()) {
        m_result.skipped.append(source.path);
        return;
    }
    if (!info.exists()) {
        m_result.vanished.append(source.path);
        return;
    }
    auto file = std::make_unique<QFile>(source.absolutePath);
    if (!file->open(QIODevice::ReadOnly | QIODevice::Unbuffered) || QFileInfo(source.absolutePath).isSymLink()) {
        m_result.skipped.append(source.path);
        return;
    }
    Current current;
    current.source = &source;
    current.size = info.size();
    current.mtime = info.lastModified().toMSecsSinceEpoch();
    current.attempt = attempt;
    const int level = isCompressedFormat(source.path) ? 0 : m_options.compressionLevel;
    if (!m_zip->beginFile(source.path.toUtf8(), current.mtime, current.size, level)) {
        failForWriter();
        return;
    }
    current.file = std::move(file);
    m_current = std::move(current);
}

WriteTask::Pumped WriteTask::pump(const QElapsedTimer &clock, int budgetMs)
{
    Current &c = *m_current;
    while (c.offset <= c.size) {
        if (m_cancel.load()) {
            fail(WriteError::Cancelled, {});
            return Pumped::Fatal;
        }
        if (m_options.onRead)
            m_options.onRead(c.source->path, c.offset);
        if (c.offset >= c.size)
            break;
        const qint64 got = c.file->read(m_piece.data(), std::min<qint64>(m_piece.size(), c.size - c.offset));
        if (got < 0)
            return Pumped::Skip;
        if (got == 0) // shorter than when it was opened
            return Pumped::Retry;
        c.counted += got;
        m_doneBytes.fetch_add(got);
        if (!m_zip->write(m_piece.constData(), got)) {
            failForWriter();
            return Pumped::Fatal;
        }
        c.offset += got;
        if (clock.elapsed() >= budgetMs && c.offset < c.size)
            return Pumped::More;
    }
    // The end: the entry is committed only if the file still is what was read. (Anything else
    // costs this entry alone: the writer starts the next one where this began.)
    char extra = 0;
    const QFileInfo now(c.source->absolutePath);
    if (c.file->read(&extra, 1) != 0 || !now.exists() || now.size() != c.size
        || now.lastModified().toMSecsSinceEpoch() != c.mtime)
        return Pumped::Retry;
    if (!m_zip->endFile()) {
        failForWriter();
        return Pumped::Fatal;
    }
    return Pumped::Done;
}

void WriteTask::finishFile(Pumped how)
{
    Current c = std::move(*m_current);
    m_current.reset();
    switch (how) {
    case Pumped::Done:
        ++m_result.files;
        if (c.source->path != vaultConfigPath())
            ++m_archivedNotebookFiles;
        return;
    case Pumped::Retry:
        m_zip->abandonFile();
        m_doneBytes.fetch_sub(c.counted);
        if (c.attempt + 1 < MaxReadAttempts)
            startFile(*c.source, c.attempt + 1);
        else
            m_result.skipped.append(c.source->path); // it keeps changing; the next run takes it
        return;
    case Pumped::Skip:
        m_zip->abandonFile();
        m_doneBytes.fetch_sub(c.counted);
        m_result.skipped.append(c.source->path);
        return;
    case Pumped::More:
    case Pumped::Fatal: return;
    }
}

void WriteTask::finishArchive()
{
    if (m_archivedNotebookFiles == 0) {
        // The storage went, or nothing could be read: this is no copy of the notebook.
        fail(WriteError::NothingArchived, u"none of %1 files could be read"_s.arg(m_sources.notebookFileCount()));
        return;
    }
    // The manifest, the ZIP's comment: everything is known only now, whether anything was left out too.
    const QByteArray manifest = backupManifest(
            m_options.notebookId, m_result.skipped.isEmpty() && m_result.vanished.isEmpty(), m_result.files);
    if (!m_zip->finish(manifest)) {
        failForWriter();
        return;
    }
    // Cut after the manifest: an entry given up may have reached further than the archive's end.
    const qint64 size = m_zip->offset();
    if (!m_file->resize(size) || !m_file->commit()) {
        fail(WriteError::WriteFailed, m_file->errorString());
        return;
    }
    m_zip.reset();
    m_file.reset();
    if (m_workPath != m_options.targetPath && !QFile::rename(m_workPath, m_options.targetPath)) {
        QFile::remove(m_workPath);
        fail(WriteError::WriteFailed, u"cannot move the archive to %1"_s.arg(m_options.targetPath));
        return;
    }
    m_result.path = m_options.targetPath;
    m_result.bytes = size;
}

void WriteTask::fail(WriteError error, const QString &detail)
{
    // (Only the first reason counts; the temporary file is removed with the QSaveFile.)
    if (m_result.error == WriteError::None) {
        m_result.error = error;
        m_result.detail = detail;
    }
    m_zip.reset();
    m_file.reset();
}

bool WriteTask::step(int budgetMs)
{
    if (m_finished.load())
        return true;
    if (!std::exchange(m_started, true)) {
        if (m_cancel.load())
            fail(WriteError::Cancelled, {});
        else
            open();
    }
    QElapsedTimer clock;
    clock.start();
    while (m_result.ok()) {
        if (m_cancel.load()) {
            fail(WriteError::Cancelled, {});
            break;
        }
        if (m_current) {
            const Pumped how = pump(clock, budgetMs);
            if (how == Pumped::More)
                return false;
            finishFile(how);
        } else if (m_nextFile < m_sources.files.size()) {
            const SourceFile &source = m_sources.files.at(m_nextFile++);
            if (checkEntryPath(source.path) != EntryPath::Ok) {
                m_result.skipped.append(source.path);
                m_doneBytes.fetch_add(source.size);
            } else {
                startFile(source, 0);
            }
        } else if (m_nextFolder < m_sources.emptyFolders.size()) {
            addFolder(m_sources.emptyFolders.at(m_nextFolder++));
        } else {
            finishArchive();
            break;
        }
        if (clock.elapsed() >= budgetMs && m_result.ok())
            return false;
    }
    m_finished.store(true);
    return true;
}

} // namespace detail

BackupWriter::BackupWriter(const notebook::WorkerPool &pool, QObject *parent)
    : QObject(parent), m_runner(std::make_unique<detail::TaskRunner>(pool))
{
    qRegisterMetaType<WriteResult>();
}

BackupWriter::~BackupWriter()
{
    cancel();
}

void BackupWriter::start(const Sources &sources, const WriteOptions &options)
{
    if (m_task)
        return;
    m_task = std::make_shared<detail::WriteTask>(sources, options);
    m_runner->start(m_task, [this] {
        const WriteResult result = m_task->result();
        m_task.reset();
        Q_EMIT finished(result);
    });
}

void BackupWriter::cancel()
{
    if (m_task)
        m_task->cancel();
}

bool BackupWriter::isRunning() const
{
    return m_task != nullptr;
}

qreal BackupWriter::progress() const
{
    return m_task ? m_task->progress() : 0.0;
}

struct FolderWork::Shared {
    std::mutex mutex;
    FolderWork *receiver = nullptr;
};

FolderWork::FolderWork(const notebook::WorkerPool &pool, QObject *parent)
    : QObject(parent), m_shared(std::make_shared<Shared>())
{
    m_shared->receiver = this;
    if (!pool.isInline())
        m_start = pool.start;
}

FolderWork::~FolderWork()
{
    std::lock_guard lock(m_shared->mutex);
    m_shared->receiver = nullptr;
}

void FolderWork::run(std::function<void()> work, std::function<void()> done)
{
    auto job = [work = std::move(work), shared = m_shared, done = std::move(done)] {
        work();
        std::lock_guard lock(shared->mutex);
        if (shared->receiver && done)
            QMetaObject::invokeMethod(shared->receiver, done, Qt::QueuedConnection);
    };
    if (m_start)
        m_start(std::move(job));
    else
        QMetaObject::invokeMethod(this, std::move(job), Qt::QueuedConnection);
}

// ---- Reading ----------------------------------------------------------------------------------

EntryPath checkEntryPath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(u'/') || path.contains(u'\\') || path.contains(QChar(0xFFFD)))
        return EntryPath::Unsafe;
    if (path.size() >= 2 && path.at(1) == u':' && path.at(0).isLetter())
        return EntryPath::Unsafe;
    if (std::ranges::any_of(path, isControl) || unicode::nfc(path) != path)
        return EntryPath::Unsafe;
    // No segment empty or only dots and spaces (Windows drops trailing ones: "..  " is "..").
    const auto dotOrSpace = [](QChar c) { return c == u'.' || c == u' '; };
    for (const QStringView segment : path.tokenize(u'/')) {
        if (std::ranges::all_of(segment, dotOrSpace))
            return EntryPath::Unsafe;
    }
    if (path != vaultConfigPath() && paths::isIgnored(path))
        return EntryPath::Ignored;
    return EntryPath::Ok;
}

struct ArchiveReader::Private {
    QFile file;
    mz_zip_archive zip{};
    bool open = false;

    explicit Private(const QString &path) : file(path) { }
    ~Private()
    {
        if (open)
            mz_zip_reader_end(&zip);
    }

    static size_t readCallback(void *opaque, mz_uint64 offset, void *buffer, size_t n)
    {
        auto *self = static_cast<Private *>(opaque);
        if (qint64(offset) != self->file.pos() && !self->file.seek(qint64(offset)))
            return 0;
        const qint64 got = self->file.read(static_cast<char *>(buffer), qint64(n));
        return got < 0 ? 0 : size_t(got);
    }

    /// The entry's name exactly as stored, or nothing when it is no valid UTF-8.
    std::optional<QString> nameOf(mz_uint index)
    {
        const mz_uint size = mz_zip_reader_get_filename(&zip, index, nullptr, 0);
        if (size <= 1)
            return QString();
        QByteArray raw(qsizetype(size), '\0');
        mz_zip_reader_get_filename(&zip, index, raw.data(), size);
        raw.truncate(qsizetype(size) - 1);
        QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        const QString name = decoder(raw);
        if (decoder.hasError())
            return std::nullopt;
        return name;
    }

    /// Inflate the entry, `sink` given its bytes piece by piece (miniz's: at most 64 KiB; false from
    /// it stops), checksum compared: false when it is damaged, gives other than exactly its size,
    /// the sink refuses or `cancel` is set.
    bool extract(const ArchiveEntry &entry, const std::function<bool(const char *, size_t)> &sink,
                 const std::atomic_bool *cancel)
    {
        struct State {
            const std::function<bool(const char *, size_t)> *sink;
            qint64 left;
            const std::atomic_bool *cancel;
        } state{&sink, entry.size, cancel};
        const auto write = [](void *opaque, mz_uint64, const void *data, size_t n) -> size_t {
            auto *s = static_cast<State *>(opaque);
            if ((s->cancel && s->cancel->load()) || qint64(n) > s->left)
                return 0;
            if (!(*s->sink)(static_cast<const char *>(data), n))
                return 0;
            s->left -= qint64(n);
            return n;
        };
        // miniz succeeds without a byte for an entry it takes for a folder (a DOS attribute, no "/")
        // or one without compressed bytes: only the whole claimed size counts.
        return mz_zip_reader_extract_to_callback(&zip, mz_uint(entry.index), write, &state, 0) && state.left == 0;
    }
};

struct Extraction::Private {
    mz_zip_reader_extract_iter_state *state = nullptr;
    qint64 size = 0;
    qint64 done = 0;
    QByteArray piece;
    bool finished = false;
    bool failed = false;

    ~Private()
    {
        if (state)
            mz_zip_reader_extract_iter_free(state); // (its verdict only matters at the end)
    }
};

Extraction::Extraction() : d(std::make_unique<Private>()) { }

Extraction::~Extraction() = default;

Extraction::Step Extraction::step(QIODevice *out, int budgetMs, const std::atomic_bool *cancel)
{
    if (d->failed)
        return Step::Failed;
    if (d->finished)
        return Step::Done;
    QElapsedTimer clock;
    clock.start();
    const auto fail = [this] {
        d->failed = true;
        if (d->state)
            mz_zip_reader_extract_iter_free(std::exchange(d->state, nullptr));
        return Step::Failed;
    };
    for (;;) {
        if (cancel && cancel->load())
            return fail();
        const size_t got = mz_zip_reader_extract_iter_read(d->state, d->piece.data(), size_t(d->piece.size()));
        if (got == 0) {
            // The end, or an error: the checksum and the size are asked at the end.
            const bool whole = d->done == d->size;
            const bool ok = mz_zip_reader_extract_iter_free(std::exchange(d->state, nullptr));
            if (!ok || !whole)
                return fail();
            d->finished = true;
            return Step::Done;
        }
        if (qint64(got) > d->size - d->done)
            return fail();
        if (out && out->write(d->piece.constData(), qint64(got)) != qint64(got))
            return fail();
        d->done += qint64(got);
        if (clock.elapsed() >= budgetMs)
            return Step::More;
    }
}

std::unique_ptr<Extraction> ArchiveReader::beginExtract(const ArchiveEntry &entry)
{
    if (!d->open || entry.folder)
        return nullptr;
    std::unique_ptr<Extraction> extraction(new Extraction);
    extraction->d->size = entry.size;
    extraction->d->piece.resize(ReadBufferBytes);
    // (An entry of no bytes has no iterator to read from: it is done at once.)
    if (entry.size == 0) {
        extraction->d->finished = true;
        return extraction;
    }
    extraction->d->state = mz_zip_reader_extract_iter_new(&d->zip, mz_uint(entry.index), 0);
    if (!extraction->d->state)
        extraction->d->failed = true;
    return extraction;
}

ArchiveReader::ArchiveReader(const QString &path) : d(std::make_unique<Private>(path)) { }

ArchiveReader::~ArchiveReader() = default;

ArchiveInfo ArchiveReader::readEntries(const ArchiveLimits &limits)
{
    ArchiveInfo out;
    const auto refuse = [&out](ArchiveProblem problem, const QString &detail) {
        out.problem = problem;
        out.detail = detail;
        out.entries.clear();
        return out;
    };
    if (!d->open) {
        if (!d->file.open(QIODevice::ReadOnly))
            return refuse(ArchiveProblem::Unreadable, d->file.errorString());
        d->zip.m_pRead = &Private::readCallback;
        d->zip.m_pIO_opaque = d.get();
        if (d->file.size() <= 0
            || !mz_zip_reader_init(&d->zip, mz_uint64(d->file.size()), MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY)) {
            // A ZIP starts with the first entry's header; its end — the directory — is what an
            // interrupted copy or download misses.
            const QByteArray start = d->file.size() >= 4 && d->file.seek(0) ? d->file.read(4) : QByteArray();
            if (start == "PK\x03\x04" || start == "PK\x05\x06")
                return refuse(ArchiveProblem::Truncated, u"a ZIP archive without its end: cut short or damaged"_s);
            return refuse(ArchiveProblem::NotAZip, u"no ZIP archive"_s);
        }
        d->open = true;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&d->zip);
    if (qint64(count) > limits.maxEntries)
        return refuse(ArchiveProblem::TooLarge, u"%1 entries"_s.arg(count));
    out.entries.reserve(count);
    QSet<QString> files;
    QSet<QString> folders;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&d->zip, i, &stat))
            return refuse(ArchiveProblem::NotAZip, u"a damaged entry"_s);
        const std::optional<QString> rawName = d->nameOf(i);
        if (!rawName)
            return refuse(ArchiveProblem::UnsafePath, u"a name that is no UTF-8"_s);
        QString path = *rawName;
        const bool folder = path.endsWith(u'/');
        if (folder)
            path.chop(1);
        switch (checkEntryPath(path)) {
        case EntryPath::Ok: break;
        case EntryPath::Unsafe:
            return refuse(ArchiveProblem::UnsafePath, u"unsafe entry “%1”"_s.arg(printable(*rawName)));
        case EntryPath::Ignored:
            return refuse(ArchiveProblem::IgnoredPath, u"ignored entry “%1”"_s.arg(printable(*rawName)));
        }
        if (stat.m_is_encrypted || !stat.m_is_supported || (stat.m_method != 0 && stat.m_method != MZ_DEFLATED))
            return refuse(ArchiveProblem::Unsupported, u"entry “%1”"_s.arg(printable(path)));
        const quint64 size = stat.m_uncomp_size;
        const quint64 compressed = stat.m_comp_size;
        if (size > quint64(limits.maxEntryBytes))
            return refuse(ArchiveProblem::TooLarge, u"entry “%1”"_s.arg(printable(path)));
        if (stat.m_method == 0 ? size != compressed : size > (compressed + 1) * quint64(MaxDeflateRatio))
            return refuse(ArchiveProblem::TooLarge, u"impossible sizes of “%1”"_s.arg(printable(path)));
        out.totalBytes += qint64(size);
        if (out.totalBytes > limits.maxTotalBytes)
            return refuse(ArchiveProblem::TooLarge, u"%1 bytes in all"_s.arg(out.totalBytes));
        if (folder && size != 0)
            return refuse(ArchiveProblem::NotAZip, u"a folder with content: “%1”"_s.arg(printable(path)));
        if (folder ? folders.contains(path) : files.contains(path))
            return refuse(ArchiveProblem::Duplicate, u"“%1” twice"_s.arg(printable(path)));
        (folder ? folders : files).insert(path);
        out.entries.append(ArchiveEntry{.path = std::move(path),
                                        .index = int(i),
                                        .folder = folder,
                                        .size = qint64(size),
                                        .compressedSize = qint64(compressed),
                                        .crc32 = stat.m_crc32});
    }
    // A file used as a folder ("a" and "a/b") could not be written back whole.
    for (const ArchiveEntry &entry : std::as_const(out.entries)) {
        for (qsizetype slash = entry.path.indexOf(u'/'); slash > 0; slash = entry.path.indexOf(u'/', slash + 1)) {
            if (files.contains(entry.path.first(slash)))
                return refuse(ArchiveProblem::Duplicate, u"“%1” is a file and a folder"_s.arg(printable(entry.path)));
        }
        if (entry.folder && files.contains(entry.path))
            return refuse(ArchiveProblem::Duplicate, u"“%1” is a file and a folder"_s.arg(printable(entry.path)));
    }
    return out;
}

bool ArchiveReader::verify(const ArchiveEntry &entry, const std::atomic_bool *cancel)
{
    if (!d->open || entry.folder)
        return d->open;
    return d->extract(
            entry, [](const char *, size_t) { return true; }, cancel);
}

std::optional<QByteArray> ArchiveReader::read(const ArchiveEntry &entry)
{
    if (!d->open || entry.folder)
        return std::nullopt;
    if (entry.size == 0)
        return QByteArray(""); // not null: an empty file
    if (entry.size > MaxReadEntryBytes)
        return std::nullopt;
    QByteArray bytes;
    try {
        bytes.resize(qsizetype(entry.size));
    } catch (const std::bad_alloc &) {
        return std::nullopt;
    }
    char *out = bytes.data();
    const bool ok = d->extract(
            entry,
            [&out](const char *data, size_t n) {
                std::memcpy(out, data, n);
                out += n;
                return true;
            },
            nullptr);
    if (!ok)
        return std::nullopt;
    return bytes;
}

bool ArchiveReader::extractTo(const ArchiveEntry &entry, QIODevice &out, const std::atomic_bool *cancel)
{
    if (!d->open || entry.folder)
        return false;
    return d->extract(
            entry,
            [&out](const char *data, size_t n) { return out.write(data, qint64(n)) == qint64(n); },
            cancel);
}

std::optional<quint32> fileCrc32(const QString &absolutePath, const std::atomic_bool *cancel)
{
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
        return std::nullopt;
    QByteArray buffer(ReadBufferBytes, Qt::Uninitialized);
    mz_ulong crc = MZ_CRC32_INIT;
    for (;;) {
        if (cancel && cancel->load())
            return std::nullopt;
        const qint64 got = file.read(buffer.data(), buffer.size());
        if (got < 0)
            return std::nullopt;
        if (got == 0)
            return quint32(crc);
        crc = mz_crc32(crc, reinterpret_cast<const unsigned char *>(buffer.constData()), size_t(got));
    }
}

} // namespace brulion::backup
