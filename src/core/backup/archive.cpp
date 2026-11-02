#include "backup/archive.h"

#include "backup/taskrunner_p.h"
#include "util/paths.h"
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
#include <mutex>
#include <new>

namespace brulion::backup {

using namespace Qt::StringLiterals;

namespace {

/// What a read callback returns to make miniz give the entry up (more than its buffer).
constexpr size_t GiveUp = size_t(-1);
constexpr int MaxReadAttempts = 3;
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

/// A name for the log: a hostile entry's control characters never reach a log line.
QString printable(QString name)
{
    for (QChar &c : name) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            c = u'?';
    }
    return name.left(300);
}

/// DOS timestamps cover 1980 … 2107; miniz would overflow its fields otherwise.
MZ_TIME_T entryTime(qint64 mtimeMs)
{
    static const qint64 earliest = QDateTime(QDate(1980, 1, 2), QTime(0, 0)).toSecsSinceEpoch();
    static const qint64 latest = QDateTime(QDate(2107, 12, 30), QTime(0, 0)).toSecsSinceEpoch();
    return MZ_TIME_T(std::clamp(mtimeMs / 1000, earliest, latest));
}

} // namespace

// ---- Writing ----------------------------------------------------------------------------------

namespace detail {

class WriteTask : public Task {
public:
    WriteTask(Sources sources, WriteOptions options, bool inlineRun)
        : m_sources(std::move(sources)), m_options(std::move(options)), m_inline(inlineRun)
    { }
    ~WriteTask() override { closeArchive(); }

    bool step(int budgetMs) override;
    void cancel() { m_cancel.store(true); }
    qreal progress() const
    {
        const qint64 total = m_sources.totalBytes;
        return total > 0 ? qMin(1.0, qreal(m_doneBytes.load()) / qreal(total)) : (m_finished.load() ? 1.0 : 0.0);
    }
    const WriteResult &result() const { return m_result; }

private:
    /// One attempt at reading a file (the read callback's state).
    struct Reading {
        WriteTask *task = nullptr;
        const SourceFile *source = nullptr;
        QFile *file = nullptr;
        qint64 size = 0;
        qint64 mtime = 0;
        qint64 counted = 0; ///< of the file's bytes in the progress
        bool changed = false;
        bool readFailed = false;
    };
    enum class Added { Done, Retry, Skip, Fatal };

    bool open();
    void addFolder(const QString &folder);
    void addFile(const SourceFile &source);
    Added addOnce(const SourceFile &source);
    bool finishArchive();
    void fail(WriteError error, const QString &detail);
    void closeArchive();

    static size_t readCallback(void *opaque, mz_uint64 offset, void *buffer, size_t n);
    static size_t writeCallback(void *opaque, mz_uint64 offset, const void *buffer, size_t n);

    Sources m_sources;
    WriteOptions m_options;
    bool m_inline = false;
    std::atomic_bool m_cancel{false};
    std::atomic<qint64> m_doneBytes{0};
    std::atomic_bool m_finished{false};
    WriteResult m_result;

    mz_zip_archive m_zip{};
    bool m_zipOpen = false;
    std::unique_ptr<QSaveFile> m_file;
    QString m_workPath;
    bool m_writeFailed = false;
    bool m_tooLarge = false;
    bool m_started = false;
    qsizetype m_nextFile = 0;
    qsizetype m_nextFolder = 0;
};

size_t WriteTask::writeCallback(void *opaque, mz_uint64 offset, const void *buffer, size_t n)
{
    auto *task = static_cast<WriteTask *>(opaque);
    if (task->m_options.maxBytes > 0 && qint64(offset + n) > task->m_options.maxBytes) {
        task->m_tooLarge = true;
        return 0;
    }
    if (qint64(offset) != task->m_file->pos() && !task->m_file->seek(qint64(offset))) {
        task->m_writeFailed = true;
        return 0;
    }
    const qint64 written = task->m_file->write(static_cast<const char *>(buffer), qint64(n));
    if (written != qint64(n)) {
        task->m_writeFailed = true;
        return 0;
    }
    return n;
}

size_t WriteTask::readCallback(void *opaque, mz_uint64 offset, void *buffer, size_t n)
{
    auto *r = static_cast<Reading *>(opaque);
    if (r->task->m_cancel.load())
        return GiveUp;
    if (r->task->m_options.onRead)
        r->task->m_options.onRead(r->source->path, qint64(offset));
    if (qint64(offset) >= r->size) {
        // The end: the entry is committed only if the file still is what was read. (Anything else
        // costs this entry alone: miniz has not recorded it, and the next one overwrites it.)
        char extra = 0;
        const QFileInfo now(r->source->absolutePath);
        if (r->file->read(&extra, 1) != 0 || !now.exists() || now.size() != r->size
            || now.lastModified().toMSecsSinceEpoch() != r->mtime) {
            r->changed = true;
            return GiveUp;
        }
        return 0;
    }
    if (qint64(offset) != r->file->pos() && !r->file->seek(qint64(offset))) {
        r->readFailed = true;
        return GiveUp;
    }
    const qint64 wanted = std::min<qint64>(qint64(n), r->size - qint64(offset));
    const qint64 got = r->file->read(static_cast<char *>(buffer), wanted);
    if (got < 0) {
        r->readFailed = true;
        return GiveUp;
    }
    if (got == 0) { // shorter than when it was opened
        r->changed = true;
        return GiveUp;
    }
    r->counted += got;
    r->task->m_doneBytes.fetch_add(got);
    return size_t(got);
}

bool WriteTask::open()
{
    const QFileInfo target(m_options.targetPath);
    const QString folder = target.absolutePath();
    if (!QFileInfo(folder).isDir()) {
        if (!m_options.createFolder) {
            fail(WriteError::FolderMissing, u"no folder %1"_s.arg(folder));
            return false;
        }
        if (!QDir().mkpath(folder)) {
            fail(WriteError::FolderUnwritable, u"cannot create %1"_s.arg(folder));
            return false;
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
        return false;
    }
    m_zip.m_pWrite = &WriteTask::writeCallback;
    m_zip.m_pIO_opaque = this;
    if (!mz_zip_writer_init_v2(&m_zip, 0, 0)) {
        fail(WriteError::WriteFailed, u"cannot start the archive"_s);
        return false;
    }
    m_zipOpen = true;
    return true;
}

void WriteTask::addFolder(const QString &folder)
{
    if (checkEntryPath(folder) != EntryPath::Ok) {
        m_result.skipped.append(folder);
        return;
    }
    const QByteArray name = folder.toUtf8() + '/';
    MZ_TIME_T time = entryTime(0);
    if (!mz_zip_writer_add_mem_ex_v2(&m_zip, name.constData(), nullptr, 0, nullptr, 0, 0, 0, 0, &time, nullptr, 0,
                                     nullptr, 0)) {
        if (m_tooLarge)
            fail(WriteError::TooLarge, u"above the limit"_s);
        else if (m_cancel.load())
            fail(WriteError::Cancelled, {});
        else
            fail(WriteError::WriteFailed, m_file->errorString());
    }
}

WriteTask::Added WriteTask::addOnce(const SourceFile &source)
{
    QFile file(source.absolutePath);
    const QFileInfo info(source.absolutePath);
    if (!info.exists()) {
        ++m_result.vanished;
        return Added::Skip;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) {
        m_result.skipped.append(source.path);
        return Added::Skip;
    }
    Reading reading;
    reading.task = this;
    reading.source = &source;
    reading.file = &file;
    reading.size = info.size();
    reading.mtime = info.lastModified().toMSecsSinceEpoch();

    int level = m_options.compressionLevel;
    if (isCompressedFormat(source.path) || (m_inline && reading.size > m_options.inlineDeflateLimit))
        level = 0;
    const QByteArray name = source.path.toUtf8();
    const MZ_TIME_T time = entryTime(reading.mtime);
    const mz_uint flags = mz_uint(level) | MZ_ZIP_FLAG_WRITE_HEADER_SET_SIZE;
    if (mz_zip_writer_add_read_buf_callback(&m_zip, name.constData(), &WriteTask::readCallback, &reading,
                                            mz_uint64(reading.size), &time, nullptr, 0, flags, nullptr, 0, nullptr, 0))
        return Added::Done;

    m_doneBytes.fetch_sub(reading.counted);
    if (m_cancel.load()) {
        fail(WriteError::Cancelled, {});
        return Added::Fatal;
    }
    if (m_tooLarge) {
        fail(WriteError::TooLarge, u"above the limit of %1 bytes"_s.arg(m_options.maxBytes));
        return Added::Fatal;
    }
    if (m_writeFailed) {
        fail(WriteError::WriteFailed, m_file->errorString());
        return Added::Fatal;
    }
    if (reading.changed)
        return Added::Retry;
    if (reading.readFailed) {
        m_result.skipped.append(source.path);
        return Added::Skip;
    }
    fail(WriteError::WriteFailed, QString::fromLatin1(mz_zip_get_error_string(mz_zip_get_last_error(&m_zip))));
    return Added::Fatal;
}

void WriteTask::addFile(const SourceFile &source)
{
    if (checkEntryPath(source.path) != EntryPath::Ok) {
        m_result.skipped.append(source.path);
        m_doneBytes.fetch_add(source.size);
        return;
    }
    for (int attempt = 0; attempt < MaxReadAttempts; ++attempt) {
        const Added added = addOnce(source);
        if (added == Added::Done) {
            ++m_result.files;
            return;
        }
        if (added != Added::Retry)
            return;
    }
    m_result.skipped.append(source.path); // it keeps changing; the next run takes it
}

bool WriteTask::finishArchive()
{
    if (!mz_zip_writer_finalize_archive(&m_zip)) {
        fail(m_tooLarge ? WriteError::TooLarge : WriteError::WriteFailed, m_file->errorString());
        return false;
    }
    // An entry given up may have reached further than the archive's end.
    const qint64 size = qint64(m_zip.m_archive_size);
    closeArchive();
    if (!m_file->resize(size)) {
        fail(WriteError::WriteFailed, m_file->errorString());
        return false;
    }
    if (!m_file->commit()) {
        fail(WriteError::WriteFailed, m_file->errorString());
        return false;
    }
    m_file.reset();
    if (m_workPath != m_options.targetPath && !QFile::rename(m_workPath, m_options.targetPath)) {
        QFile::remove(m_workPath);
        fail(WriteError::WriteFailed, u"cannot move the archive to %1"_s.arg(m_options.targetPath));
        return false;
    }
    m_result.path = m_options.targetPath;
    m_result.bytes = size;
    return true;
}

void WriteTask::fail(WriteError error, const QString &detail)
{
    // (Only the first reason counts; closing removes the temporary file.)
    if (m_result.error == WriteError::None) {
        m_result.error = error;
        m_result.detail = detail;
    }
    closeArchive();
    m_file.reset();
}

void WriteTask::closeArchive()
{
    if (m_zipOpen) {
        mz_zip_writer_end(&m_zip);
        m_zipOpen = false;
    }
}

bool WriteTask::step(int budgetMs)
{
    if (m_finished.load())
        return true;
    if (!m_started) {
        m_started = true;
        if (m_cancel.load()) {
            fail(WriteError::Cancelled, {});
        } else {
            open();
        }
    }
    QElapsedTimer clock;
    clock.start();
    while (m_result.ok()) {
        if (m_cancel.load()) {
            fail(WriteError::Cancelled, {});
            break;
        }
        if (m_nextFile < m_sources.files.size()) {
            addFile(m_sources.files.at(m_nextFile++));
        } else if (m_nextFolder < m_sources.emptyFolders.size()) {
            addFolder(m_sources.emptyFolders.at(m_nextFolder++));
        } else {
            finishArchive();
            break;
        }
        if (clock.elapsed() >= budgetMs)
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
    if (m_task)
        m_task->cancel();
}

void BackupWriter::start(const Sources &sources, const WriteOptions &options)
{
    if (m_task)
        return;
    m_task = std::make_shared<detail::WriteTask>(sources, options, m_runner->isInline());
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
    const auto finish = [shared = m_shared, done = std::move(done)] {
        std::lock_guard lock(shared->mutex);
        if (shared->receiver && done)
            QMetaObject::invokeMethod(shared->receiver, done, Qt::QueuedConnection);
    };
    if (m_start) {
        m_start([work = std::move(work), finish] {
            work();
            finish();
        });
        return;
    }
    QMetaObject::invokeMethod(
            this,
            [work = std::move(work), finish] {
                work();
                finish();
            },
            Qt::QueuedConnection);
}

// ---- Reading ----------------------------------------------------------------------------------

EntryPath checkEntryPath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(u'/') || path.contains(u'\\') || path.contains(QChar(0xFFFD)))
        return EntryPath::Unsafe;
    if (path.size() >= 2 && path.at(1) == u':' && path.at(0).isLetter())
        return EntryPath::Unsafe;
    if (path.normalized(QString::NormalizationForm_C) != path)
        return EntryPath::Unsafe;
    for (const QChar c : path) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            return EntryPath::Unsafe;
    }
    const QStringList segments = path.split(u'/');
    for (const QString &segment : segments) {
        // Windows drops trailing dots and spaces: "..  " is "..".
        const bool onlyDotsAndSpaces =
                std::all_of(segment.cbegin(), segment.cend(), [](QChar c) { return c == u'.' || c == u' '; });
        if (segment.isEmpty() || onlyDotsAndSpaces)
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
};

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
            || !mz_zip_reader_init(&d->zip, mz_uint64(d->file.size()), MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY))
            return refuse(ArchiveProblem::NotAZip, u"no ZIP archive"_s);
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
        if (stat.m_is_encrypted || !stat.m_is_supported)
            return refuse(ArchiveProblem::Unsupported, u"entry “%1”"_s.arg(printable(path)));
        if (stat.m_method != 0 && stat.m_method != MZ_DEFLATED)
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
        ArchiveEntry entry;
        entry.path = path;
        entry.index = int(i);
        entry.folder = folder;
        entry.size = qint64(size);
        entry.compressedSize = qint64(compressed);
        entry.crc32 = stat.m_crc32;
        out.entries.append(entry);
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
    struct Sink {
        qint64 left;
        const std::atomic_bool *cancel;
    } sink{entry.size, cancel};
    const auto write = [](void *opaque, mz_uint64, const void *, size_t n) -> size_t {
        auto *s = static_cast<Sink *>(opaque);
        if ((s->cancel && s->cancel->load()) || qint64(n) > s->left)
            return 0;
        s->left -= qint64(n);
        return n;
    };
    return mz_zip_reader_extract_to_callback(&d->zip, mz_uint(entry.index), write, &sink, 0);
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
    struct Sink {
        char *out;
        qint64 left;
    } sink{bytes.data(), entry.size};
    const auto write = [](void *opaque, mz_uint64, const void *data, size_t n) -> size_t {
        auto *s = static_cast<Sink *>(opaque);
        if (qint64(n) > s->left)
            return 0;
        std::memcpy(s->out, data, n);
        s->out += n;
        s->left -= qint64(n);
        return n;
    };
    if (!mz_zip_reader_extract_to_callback(&d->zip, mz_uint(entry.index), write, &sink, 0) || sink.left != 0)
        return std::nullopt;
    return bytes;
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
