#include "attachmentindexer.h"

#include "ocr.h"
#include "ocrdata.h"
#include "pdfpages.h"
#include "pdfrenderpool.h"
#include "pdfsearch.h"

#include "attachments/documenttext.h"
#include "index/vaultindex.h"
#include "log/categories.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>

using namespace Qt::StringLiterals;

namespace brulion::attachsearch {

using attachments::AttachmentSearch;
using attachments::Kind;
using attachments::ManifestEntry;
using attachments::Page;
using attachments::PageSource;
using attachments::Record;

namespace {

/// Pages of a file posted to the GUI thread at once (each post is its own event-loop turn: a long
/// PDF's documents never merge in one go).
constexpr int PagesPerPost = 24;
/// A text layer is read with one lease of the PDF for at most this many pages (then the lease goes
/// back: Windows can move the file between two batches).
constexpr int PagesPerLease = 16;
/// An OCR'd file's record is written after this many pages (and at its end).
constexpr int OcrPagesPerSave = 8;

bool isOcrPicture(const QString &path)
{
    const QString lower = path.toLower();
    for (const char16_t *ext : {u".png", u".jpg", u".jpeg", u".webp", u".bmp"}) {
        if (lower.endsWith(QStringView(ext)))
            return true;
    }
    return false;
}

/// What the file is read as: a PDF, a document (DOCX, PPTX, ODT, EPUB) or else a picture.
Kind kindOfFile(const QString &path)
{
    if (paths::isPdfPath(path))
        return Kind::Pdf;
    if (const std::optional<attachments::DocumentFormat> format = attachments::documentFormatOf(path))
        return attachments::kindOf(*format);
    return Kind::Picture;
}

/// This device decodes the picture's format (WebP needs Qt's image plugins, which some builds lack).
bool decodable(const QString &path)
{
    static const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    const QString suffix = QFileInfo(path).suffix().toLower();
    return formats.contains(suffix.toLatin1()) || (suffix == u"jpg"_s && formats.contains("jpeg"));
}

int lettersIn(QStringView text)
{
    int n = 0;
    for (const QChar c : text) {
        if (c.isLetterOrNumber())
            ++n;
    }
    return n;
}

QString fileVersion(qint64 size, qint64 mtime)
{
    return QString::number(mtime) + u'-' + QString::number(size);
}

/// Does `page` want OCR in `languages`?
bool wantsOcr(const Page &page, const QString &languages)
{
    switch (page.source) {
    case PageSource::NeedsOcr: return true;
    case PageSource::Ocr:
    case PageSource::OcrFailed: return page.ocrLanguages != languages;
    case PageSource::Unread:
    case PageSource::TextLayer: return false;
    }
    return false;
}

std::optional<Record> readRecordFile(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return attachments::decodeRecord(f.readAll());
}

bool writeRecordFile(const QString &file, const Record &record)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(attachments::encodeRecord(record));
    return out.commit();
}

/// The words OCR read as a page: fractions of the picture it read (a picture as its EXIF orientation
/// shows it, a PDF page).
Page pageFromOcr(const platform::OcrResult &result, QSize picture, const QString &languages)
{
    Page page;
    page.ocrLanguages = languages;
    if (!result.ok || picture.isEmpty()) {
        page.source = PageSource::OcrFailed;
        return page;
    }
    page.source = PageSource::Ocr;
    QStringList texts;
    QList<int> lines;
    QList<int> blocks;
    QList<attachments::Word> words;
    for (const platform::OcrWord &w : result.words) {
        texts.append(w.text);
        lines.append(w.line);
        blocks.append(w.block);
        attachments::Word word;
        word.box = QRectF(qreal(w.box.left()) / picture.width(), qreal(w.box.top()) / picture.height(),
                          qreal(w.box.width()) / picture.width(), qreal(w.box.height()) / picture.height());
        words.append(word);
    }
    page.text = attachments::joinOcrWords(texts, lines, blocks, words);
    page.words = std::move(words);
    return page;
}

/// The EXIF orientation of the picture `file` as QImageIOHandler::Transformations (its header only).
int exifTransformation(const QString &file)
{
    QImageReader reader(file);
    reader.setDecideFormatFromContent(true);
    return int(reader.transformation().toInt());
}

} // namespace

bool isSearchableAttachment(const QString &path)
{
    return VaultIndex::isIndexable(path)
           && (paths::isPdfPath(path) || isOcrPicture(path) || attachments::documentFormatOf(path).has_value());
}

QRectF orientedBox(const QRectF &box, int transformation)
{
    using T = QImageIOHandler::Transformation;
    const auto t = QImageIOHandler::Transformations::fromInt(transformation);
    // Qt's reader: mirror / flip first, then a quarter turn clockwise (Rotate270 is all three).
    const auto shown = [t](QPointF p) {
        if (t.testFlag(T::TransformationMirror))
            p.setX(1 - p.x());
        if (t.testFlag(T::TransformationFlip))
            p.setY(1 - p.y());
        if (t.testFlag(T::TransformationRotate90))
            p = QPointF(1 - p.y(), p.x());
        return p;
    };
    return QRectF(shown(box.topLeft()), shown(box.bottomRight())).normalized();
}

attachments::Record upgradedRecord(attachments::Record record, int transformation)
{
    if (record.extractor != 1)
        return record;
    record.extractor = 2;
    if (record.kind != Kind::Picture)
        return record;
    for (Page &page : record.pages) {
        for (attachments::Word &word : page.words)
            word.box = orientedBox(word.box, transformation);
    }
    return record;
}

QSize ocrPagePixels(QSizeF points)
{
    if (!(points.width() > 0) || !(points.height() > 0))
        return {};
    const double longSide = std::max(points.width(), points.height()) * 300.0 / 72.0;
    return platform::pdfPagePixels(points, int(std::clamp(longSide, 1000.0, 4200.0)));
}

// ---- The worker ----------------------------------------------------------------------------------

struct AttachmentIndexer::FileJob {
    QString path;
    QString file;
    qint64 size = 0;
    qint64 mtime = 0;
};

/// The worker thread: the text of the queued files first, then OCR of the pages that want it.
class IndexerWorker : public QThread {
public:
    struct Settings {
        bool ocr = false;
        QString languages;
    };
    /// OCR is on, in some language.
    static bool ocrWanted(const Settings &settings) { return settings.ocr && !settings.languages.isEmpty(); }

    IndexerWorker(AttachmentIndexer *owner, quint64 generation, QString cacheDir, Settings settings)
        : m_owner(owner), m_generation(generation), m_cacheDir(std::move(cacheDir)), m_settings(std::move(settings))
    { }
    ~IndexerWorker() override { stopAndWait(); }

    void enqueue(AttachmentIndexer::FileJob job, bool front)
    {
        {
            const std::lock_guard lock(m_mutex);
            const auto same = [&job](const AttachmentIndexer::FileJob &j) { return j.path == job.path; };
            std::erase_if(m_text, same);
            std::erase_if(m_ocr, [&job](const OcrJob &j) { return j.job.path == job.path; });
            if (front)
                m_text.push_front(std::move(job));
            else
                m_text.push_back(std::move(job));
            ++m_passTotal;
            m_textQueued = true;
        }
        m_wake.notify_all();
    }
    void drop(const QString &path)
    {
        const std::lock_guard lock(m_mutex);
        std::erase_if(m_text, [&path](const AttachmentIndexer::FileJob &j) { return j.path == path; });
        std::erase_if(m_ocr, [&path](const OcrJob &j) { return j.job.path == path; });
    }
    void wake() { m_wake.notify_all(); }
    void stopAndWait()
    {
        m_stop = true;
        m_wake.notify_all();
        wait();
    }
    bool busy() const
    {
        const std::lock_guard lock(m_mutex);
        return m_working || !m_text.empty() || (!m_ocr.empty() && m_settings.ocr);
    }

protected:
    void run() override;

private:
    struct OcrJob {
        AttachmentIndexer::FileJob job;
        QByteArray hash;
        Kind kind = Kind::Pdf;
        QList<int> pages;
    };

    bool stopped() const { return m_stop.load(); }
    /// Wait while paused or while a sync may move files; false when stopping.
    bool waitUntilFree();
    bool cancelled() const { return m_stop.load() || m_owner->m_paused.load(); }
    void processText(const AttachmentIndexer::FileJob &job);
    void processOcr(OcrJob job);
    std::optional<Record> extract(const AttachmentIndexer::FileJob &job, Kind kind);
    std::optional<QByteArray> hashOf(const QString &file);
    QImage ocrPicture(const OcrJob &job, int page, int *dpi);
    /// The manifest's entry of `job`'s file while its size and time are the same (nullopt: read it).
    std::optional<ManifestEntry> knownEntry(const AttachmentIndexer::FileJob &job) const;
    /// The record of the file of content `hash`: the cache's (an older extractor's brought up to
    /// date), else read now and cached (nullopt: stopping).
    std::optional<Record> recordOf(const AttachmentIndexer::FileJob &job, Kind kind, const QByteArray &hash);
    /// The record of content `hash` in the cache (version 1's brought up to date: upgradedRecord()).
    std::optional<Record> cachedRecord(const AttachmentIndexer::FileJob &job, Kind kind, const QByteArray &hash);
    void writeRecord(const QByteArray &hash, const Record &record);
    /// One page of an OCR job: recognized, not a page of the file, or the job stops or waits.
    enum class OcrStep : quint8 { Done, Skipped, Later, Again, Stop };
    /// The job's pages from this one go back to the queue (Later: at its back, Again: at its front).
    static bool putsTheRestBack(OcrStep step) { return step == OcrStep::Later || step == OcrStep::Again; }
    OcrStep ocrStep(const OcrJob &job, qsizetype i, Record &record, const QString &languages, quint64 generation);
    /// `job`'s record written after OCR read pages (the indexer told).
    void writeOcrRecord(const OcrJob &job, const Record &record, quint64 generation);
    /// Page `page` OCR read, handed to the indexer's search at once (unless the notebook of
    /// `generation` has gone since).
    void publishPage(const QString &path, int page, const Page &read, quint64 generation);
    /// The pages of `job` from the `from`-th back in the queue (`first`: at its front), unless stopping.
    void requeueRest(const OcrJob &job, qsizetype from, bool first);
    /// `job` in the OCR queue: at its front (`first`) or at its back.
    void requeueOcr(OcrJob job, bool first);
    /// OCR is on and its engine is there.
    bool ocrReady(const Settings &settings);
    /// The OCR engine for `languages` is there (made when they changed; false: none, said once).
    bool engineFor(const QString &languages);
    /// Files wait for their text (which comes before OCR).
    bool textWaiting();
    /// Page `page` of `job` read by OCR, with its own words (`before`); nullopt: cancelled.
    std::optional<Page> recognizePage(const OcrJob &job, int page, const Page &before, const QString &languages);
    void post(std::function<void(AttachmentIndexer *)> apply);
    void postPrepared(const QString &path, const ManifestEntry &entry, AttachmentSearch::Prepared prepared);
    void progress(const QString &state, bool force = false);
    void pruneCache();
    QString recordPath(const QByteArray &hash) const
    {
        return QDir(m_cacheDir).filePath(attachments::recordFileName(hash));
    }

    AttachmentIndexer *m_owner;
    const quint64 m_generation;
    const QString m_cacheDir;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<AttachmentIndexer::FileJob> m_text;
    std::deque<OcrJob> m_ocr;
    Settings m_settings;
    std::atomic_bool m_stop = false;
    bool m_textQueued = false;
    bool m_working = false;
    std::unique_ptr<platform::OcrEngine> m_engine;
    QString m_engineLanguages;
    bool m_engineFailed = false;
    // Progress (worker thread)
    int m_passTotal = 0;
    int m_passDone = 0;
    int m_ocrDone = 0;
    /// Pages of the file being recognized still to do (with the current one).
    int m_ocrLeft = 0;
    QString m_currentFile;
    int m_currentPage = 0;
    int m_currentPages = 0;
    QElapsedTimer m_lastProgress;
    QString m_lastState;
    /// Records were written since the cache was measured (true: measured at the first idle).
    bool m_written = true;

public:
    void setSettings(Settings settings)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_settings = std::move(settings);
        }
        m_wake.notify_all();
    }
};

void IndexerWorker::post(std::function<void(AttachmentIndexer *)> apply)
{
    AttachmentIndexer *owner = m_owner;
    QMetaObject::invokeMethod(owner, [owner, apply = std::move(apply)] { apply(owner); }, Qt::QueuedConnection);
}

void IndexerWorker::progress(const QString &state, bool force)
{
    if (!force && state == m_lastState && m_lastProgress.isValid() && m_lastProgress.elapsed() < 250)
        return;
    m_lastState = state;
    m_lastProgress.start();
    int queuedOcr = 0;
    int passTotal = 0;
    {
        const std::lock_guard lock(m_mutex);
        for (const OcrJob &j : m_ocr)
            queuedOcr += int(j.pages.size());
        passTotal = m_passTotal; // (the GUI thread counts the files it queues)
    }
    const QVariantMap p{{u"state"_s, state},
                        {u"filesTotal"_s, passTotal},
                        {u"filesDone"_s, m_passDone},
                        {u"currentFile"_s, m_currentFile},
                        {u"currentPage"_s, m_currentPage},
                        {u"currentPages"_s, m_currentPages},
                        {u"pagesToRecognize"_s, m_ocrDone + queuedOcr + m_ocrLeft},
                        {u"pagesRecognized"_s, m_ocrDone}};
    const quint64 generation = m_generation;
    post([generation, p](AttachmentIndexer *owner) { owner->applyProgress(generation, p); });
}

bool IndexerWorker::waitUntilFree()
{
    bool told = false;
    while (!stopped()) {
        const bool paused = m_owner->m_paused.load();
        const bool syncing = pdfview::PdfReaderPool::instance().idleSuspended();
        if (!paused && !syncing)
            return true;
        if (!told) {
            progress(paused ? u"paused"_s : u"waiting"_s, true);
            told = true;
        }
        std::unique_lock lock(m_mutex);
        m_wake.wait_for(lock, std::chrono::milliseconds(500));
    }
    return false;
}

void IndexerWorker::run()
{
    while (!stopped()) {
        std::optional<AttachmentIndexer::FileJob> text;
        std::optional<OcrJob> ocr;
        {
            std::unique_lock lock(m_mutex);
            m_working = false;
            if (m_text.empty() && (m_ocr.empty() || !m_settings.ocr)) {
                if (m_written) {
                    m_written = false;
                    lock.unlock();
                    pruneCache();
                    lock.lock();
                }
                // A pass is over: the counters start again with the next change.
                m_passTotal = 0;
                m_passDone = 0;
                m_ocrDone = 0;
                m_ocrLeft = 0;
                m_currentFile.clear();
                m_currentPage = 0;
                m_currentPages = 0;
                lock.unlock();
                progress(u"idle"_s, true);
                lock.lock();
                m_wake.wait(lock,
                            [this] { return m_stop.load() || !m_text.empty() || (!m_ocr.empty() && m_settings.ocr); });
                continue;
            }
            m_working = true;
            if (!m_text.empty()) {
                text = std::move(m_text.front());
                m_text.pop_front();
                m_textQueued = false;
            } else {
                ocr = std::move(m_ocr.front());
                m_ocr.pop_front();
            }
        }
        if (!waitUntilFree())
            break;
        if (text) {
            processText(*text);
            ++m_passDone;
        } else if (ocr) {
            processOcr(std::move(*ocr));
        }
    }
    m_engine.reset();
}

std::optional<QByteArray> IndexerWorker::hashOf(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return attachments::hashFile(f, [this] { return stopped(); });
}

std::optional<Record> IndexerWorker::extract(const AttachmentIndexer::FileJob &job, Kind kind)
{
    if (const std::optional<attachments::DocumentFormat> format = attachments::documentFormatOf(kind)) {
        // A document: its paragraphs from its XML, bounded (core attachments/documenttext.h) — quick
        // enough to read whole between two checks of the stop flag.
        QFile file(job.file);
        if (!file.open(QIODevice::ReadOnly))
            return std::nullopt; // (gone or unreadable now: a change queues it again)
        const std::optional<attachments::DocumentText> text =
                attachments::extractDocumentText(file, *format, {}, [this] { return stopped(); });
        if (!text)
            return std::nullopt; // (stopping)
        return attachments::documentRecord(*format, *text, AttachmentIndexer::ExtractorVersion);
    }
    Record record;
    record.kind = kind;
    record.extractor = AttachmentIndexer::ExtractorVersion;
    if (kind == Kind::Picture) {
        record.pageCount = 1;
        record.pages.append(Page{PageSource::NeedsOcr, {}, {}, {}});
        return record;
    }
    // Its readers closed once it is read (after the leases below are given back): an idle reader
    // holds the file open, which Windows then refuses to rename, replace or delete — the user's
    // file manager, Syncthing, a rename past the Workspace.
    const auto letGo = qScopeGuard([&job] { pdfview::PdfReaderPool::instance().forget(job.file); });
    const QString version = fileVersion(job.size, job.mtime);
    platform::PdfInfo info;
    int pages = 0;
    {
        pdfview::PdfReaderPool::Lease lease = pdfview::PdfReaderPool::instance().acquire(job.file, version, &info);
        if (!lease) {
            record.locked = info.locked;
            record.unreadable = !info.locked;
            return record;
        }
        pages = lease->pageCount();
    }
    record.pageCount = pages;
    const int stored = std::min(pages, attachments::MaxPages);
    record.pages.reserve(stored);
    m_currentPages = pages;
    int page = 1;
    while (page <= stored) {
        if (stopped() || !waitUntilFree())
            return std::nullopt;
        pdfview::PdfReaderPool::Lease lease = pdfview::PdfReaderPool::instance().acquire(job.file, version, &info);
        if (!lease)
            break; // (changed or gone meanwhile: a change will queue it again)
        for (int n = 0; n < PagesPerLease && page <= stored; ++n, ++page) {
            Page p;
            p.text = lease->pageText(page).left(attachments::MaxPageText);
            p.source = lettersIn(p.text) >= AttachmentIndexer::MinTextLayerLetters ? PageSource::TextLayer
                                                                                   : PageSource::NeedsOcr;
            record.pages.append(std::move(p));
            m_currentPage = page;
        }
        progress(u"reading"_s);
    }
    // Pages that could not be read (the lease failed) are left for a later pass.
    while (record.pages.size() < stored)
        record.pages.append(Page());
    return record;
}

void IndexerWorker::postPrepared(const QString &path, const ManifestEntry &entry, AttachmentSearch::Prepared prepared)
{
    QList<AttachmentSearch::PreparedPage> rest;
    if (prepared.pages.size() > PagesPerPost) {
        rest = prepared.pages.mid(PagesPerPost);
        prepared.pages.resize(PagesPerPost);
    }
    const quint64 generation = m_generation;
    post([generation, path, entry, prepared = std::move(prepared)](AttachmentIndexer *owner) mutable {
        owner->applyFile(generation, path, entry, std::move(prepared));
    });
    for (qsizetype i = 0; i < rest.size(); i += PagesPerPost) {
        QList<AttachmentSearch::PreparedPage> chunk = rest.mid(i, PagesPerPost);
        post([generation, path, chunk = std::move(chunk)](AttachmentIndexer *owner) mutable {
            owner->applyPages(generation, path, std::move(chunk));
        });
    }
}

namespace {

/// The record of a file that is not read, only its name searched (nullopt: it is read): a document
/// too large to read (a hostile or a huge file); a picture without OCR, in a format this device
/// cannot decode, or huge (no reading, no hash).
std::optional<Record> nameOnlyRecord(const QString &file, const QString &path, Kind kind, bool known, bool ocr)
{
    if (known)
        return std::nullopt;
    Record name;
    name.kind = kind;
    if (attachments::isDocumentKind(kind) && QFileInfo(file).size() > attachments::DocumentLimits().maxFileBytes)
        return name;
    if (kind != Kind::Picture)
        return std::nullopt;
    name.pageCount = 1;
    if (!ocr || !decodable(path) || QFileInfo(file).size() > AttachmentIndexer::MaxPictureBytes)
        return name;
    return std::nullopt;
}

/// The pages of `record` OCR still reads in `languages`.
QList<int> pagesForOcr(const Record &record, const QString &languages)
{
    QList<int> wanted;
    if (record.locked || record.unreadable)
        return wanted;
    for (qsizetype i = 0; i < record.pages.size(); ++i) {
        if (wantsOcr(record.pages.at(i), languages))
            wanted.append(int(i + 1));
    }
    return wanted;
}

/// OCR's page `read` with the page's own few words (`before`: a title, a page number) after OCR's
/// text when OCR missed them.
Page withOwnWords(Page read, const Page &before)
{
    const QString own = before.source == PageSource::NeedsOcr ? before.text.trimmed() : QString();
    if (!own.isEmpty() && !text::foldKey(read.text).contains(text::foldKey(own)))
        read.text += (read.text.isEmpty() ? u""_s : u"\n\n"_s) + own;
    return read;
}

/// The picture `file` for OCR: the way up it was taken (its EXIF orientation), at most 4200 px and
/// MaxOcrPixels, a small one (a screenshot) twice the size — small text reads better so.
QImage ocrPictureOf(const QString &file)
{
    QImageReader reader(file);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const QSize raw = reader.size();
    if (raw.isValid()) {
        const double scale =
                std::min({1.0, 4200.0 / std::max(raw.width(), raw.height()),
                          std::sqrt(double(platform::MaxOcrPixels) / (double(raw.width()) * raw.height()))});
        if (scale < 1.0)
            reader.setScaledSize((QSizeF(raw) * scale).toSize());
    }
    QImage image = reader.read();
    if (!image.isNull() && std::max(image.width(), image.height()) < 1500)
        image = image.scaled(image.size() * 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

} // namespace

std::optional<ManifestEntry> IndexerWorker::knownEntry(const AttachmentIndexer::FileJob &job) const
{
    const std::lock_guard lock(m_owner->m_shared->lock);
    const auto it = m_owner->m_shared->manifest.constFind(job.path);
    if (it != m_owner->m_shared->manifest.constEnd() && it->size == job.size && it->mtime == job.mtime)
        return *it;
    return std::nullopt;
}

void IndexerWorker::writeRecord(const QByteArray &hash, const Record &record)
{
    if (writeRecordFile(recordPath(hash), record))
        m_written = true;
}

std::optional<Record> IndexerWorker::cachedRecord(const AttachmentIndexer::FileJob &job, Kind kind,
                                                  const QByteArray &hash)
{
    std::optional<Record> record = readRecordFile(recordPath(hash));
    if (record && record->extractor == 1) {
        record = upgradedRecord(*record, kind == Kind::Picture ? exifTransformation(job.file) : 0);
        writeRecord(hash, *record);
    }
    return record;
}

std::optional<Record> IndexerWorker::recordOf(const AttachmentIndexer::FileJob &job, Kind kind, const QByteArray &hash)
{
    std::optional<Record> record = cachedRecord(job, kind, hash);
    if (record && record->extractor == AttachmentIndexer::ExtractorVersion && record->kind == kind)
        return record;
    record = extract(job, kind);
    if (record)
        writeRecord(hash, *record);
    return record; // (nullopt: stopping)
}

void IndexerWorker::processText(const AttachmentIndexer::FileJob &job)
{
    // A link to a file elsewhere is never read (Vault::reaches()).
    if (QFileInfo(job.file).isSymLink())
        return;
    const Kind kind = kindOfFile(job.path);
    m_currentFile = job.path;
    m_currentPage = 0;
    m_currentPages = 0;
    progress(u"reading"_s);
    Settings settings;
    {
        const std::lock_guard lock(m_mutex);
        settings = m_settings;
    }
    const std::optional<ManifestEntry> known = knownEntry(job);
    const bool ocr = ocrWanted(settings) && platform::ocrAvailable();
    if (const std::optional<Record> name = nameOnlyRecord(job.file, job.path, kind, known.has_value(), ocr)) {
        postPrepared(job.path, ManifestEntry{}, AttachmentSearch::prepare(job.path, *name));
        return;
    }
    const std::optional<QByteArray> hash = known ? std::optional<QByteArray>(known->hash) : hashOf(job.file);
    if (!hash)
        return; // (gone, unreadable, or stopping)
    ManifestEntry entry;
    entry.size = job.size;
    entry.mtime = job.mtime;
    entry.hash = *hash;
    const std::optional<Record> record = recordOf(job, kind, entry.hash);
    if (!record)
        return; // stopping
    const QList<int> wanted = ocr ? pagesForOcr(*record, settings.languages) : QList<int>();
    postPrepared(job.path, entry, AttachmentSearch::prepare(job.path, *record));
    if (!wanted.isEmpty())
        requeueOcr(OcrJob{job, entry.hash, kind, wanted}, false);
}

void IndexerWorker::requeueOcr(OcrJob job, bool first)
{
    const std::lock_guard lock(m_mutex);
    if (first)
        m_ocr.push_front(std::move(job));
    else
        m_ocr.push_back(std::move(job));
}

QImage IndexerWorker::ocrPicture(const OcrJob &job, int page, int *dpi)
{
    *dpi = 0;
    if (job.kind == Kind::Picture)
        return ocrPictureOf(job.job.file);
    const QString version = fileVersion(job.job.size, job.job.mtime);
    // (As extract(): not kept open once the page is drawn — a page takes seconds to recognize.)
    const auto letGo = qScopeGuard([&job] { pdfview::PdfReaderPool::instance().forget(job.job.file); });
    pdfview::PdfReaderPool::Lease lease = pdfview::PdfReaderPool::instance().acquire(job.job.file, version);
    if (!lease || page < 1 || page > lease->pageCount())
        return {};
    const QSizeF points = lease->pageSize(page);
    const QSize pixels = ocrPagePixels(points);
    if (pixels.isEmpty())
        return {};
    *dpi = int(std::lround(pixels.width() / (points.width() / 72.0)));
    return lease->render(page, pixels);
}

bool IndexerWorker::ocrReady(const Settings &settings)
{
    return ocrWanted(settings) && engineFor(settings.languages);
}

bool IndexerWorker::engineFor(const QString &languages)
{
    if (m_engine && m_engineLanguages == languages)
        return true;
    m_engine.reset();
    QString error;
    m_engine = platform::OcrEngine::create(languages, &error);
    m_engineLanguages = languages;
    if (!m_engine) {
        if (!m_engineFailed)
            qCWarning(logging::lcIndex).noquote() << u"OCR: no engine for %1: %2"_s.arg(languages, error);
        m_engineFailed = true;
        return false;
    }
    m_engineFailed = false;
    return true;
}

bool IndexerWorker::textWaiting()
{
    const std::lock_guard lock(m_mutex);
    return m_textQueued;
}

std::optional<Page> IndexerWorker::recognizePage(const OcrJob &job, int page, const Page &before,
                                                 const QString &languages)
{
    int dpi = 0;
    const QImage image = ocrPicture(job, page, &dpi);
    platform::OcrResult result;
    if (!image.isNull())
        result = m_engine->recognize(image, dpi, [this] { return cancelled(); });
    if (result.cancelled)
        return std::nullopt;
    const Page read = withOwnWords(pageFromOcr(result, image.size(), languages), before);
    if (read.source == PageSource::OcrFailed)
        qCInfo(logging::lcIndex).noquote() << u"OCR: page %1 of %2 not read: %3"_s.arg(page).arg(
                job.job.path, image.isNull() ? u"no picture"_s : result.error);
    return read;
}

void IndexerWorker::writeOcrRecord(const OcrJob &job, const Record &record, quint64 generation)
{
    if (!writeRecordFile(recordPath(job.hash), record))
        return;
    m_written = true;
    // The pages read from the cache before are stale now.
    post([generation, path = job.job.path](AttachmentIndexer *owner) { owner->recordSaved(generation, path); });
}

void IndexerWorker::publishPage(const QString &path, int page, const Page &read, quint64 generation)
{
    AttachmentSearch::PreparedPage prepared = AttachmentSearch::preparePage(page, read);
    // The page itself too: until the record is written, excerpts and highlights take it.
    post([generation, path, page, prepared = std::move(prepared), read](AttachmentIndexer *owner) mutable {
        owner->applyPages(generation, path, {std::move(prepared)});
        if (generation == owner->m_generation)
            owner->m_pages.insert(path + u'\n' + QString::number(page), new Page(read), int(1 + read.text.size() / 4096));
    });
}

void IndexerWorker::requeueRest(const OcrJob &job, qsizetype from, bool first)
{
    if (!stopped())
        requeueOcr(OcrJob{job.job, job.hash, job.kind, job.pages.mid(from)}, first);
}

IndexerWorker::OcrStep IndexerWorker::ocrStep(const OcrJob &job, qsizetype i, Record &record, const QString &languages,
                                              quint64 generation)
{
    // New files' text comes first: the rest of this file's pages wait at the back.
    if (stopped() || textWaiting())
        return OcrStep::Later;
    if (!waitUntilFree())
        return OcrStep::Stop;
    const int page = job.pages.at(i);
    if (page < 1 || page > record.pages.size())
        return OcrStep::Skipped;
    m_currentPage = page;
    m_ocrLeft = int(job.pages.size() - i);
    progress(u"recognizing"_s, true);
    const std::optional<Page> read = recognizePage(job, page, record.pages.at(page - 1), languages);
    // Paused (or stopping) in the middle of the page: read again from this page.
    if (!read)
        return OcrStep::Again;
    record.pages[page - 1] = *read;
    ++m_ocrDone;
    publishPage(job.job.path, page, *read, generation);
    return OcrStep::Done;
}

void IndexerWorker::processOcr(OcrJob job)
{
    // (Pages not done go back to the queue, where they are counted.)
    const auto noneLeft = qScopeGuard([this] { m_ocrLeft = 0; });
    Settings settings;
    {
        const std::lock_guard lock(m_mutex);
        settings = m_settings;
    }
    if (!ocrReady(settings))
        return;
    std::optional<Record> record = readRecordFile(recordPath(job.hash));
    if (!record)
        return;
    m_currentFile = job.job.path;
    m_currentPages = record->pageCount;
    const quint64 generation = m_generation;
    int unsaved = 0;
    const auto save = [&] {
        if (unsaved > 0)
            writeOcrRecord(job, *record, generation);
        unsaved = 0;
    };
    for (qsizetype i = 0; i < job.pages.size(); ++i) {
        const OcrStep step = ocrStep(job, i, *record, settings.languages, generation);
        if (step == OcrStep::Stop)
            return;
        if (putsTheRestBack(step)) {
            save();
            requeueRest(job, i, step == OcrStep::Again);
            return;
        }
        unsaved += step == OcrStep::Done ? 1 : 0;
        if (unsaved >= OcrPagesPerSave)
            save();
    }
    save();
}

void IndexerWorker::pruneCache()
{
    QDir dir(m_cacheDir);
    qint64 total = 0;
    struct Entry {
        QString name;
        qint64 size;
        QDateTime modified;
    };
    QList<Entry> records;
    for (const QFileInfo &fi : dir.entryInfoList({u"*.bat"_s}, QDir::Files)) {
        records.append({fi.fileName(), fi.size(), fi.lastModified(QTimeZone::UTC)});
        total += fi.size();
    }
    const qint64 cap = m_owner->m_cacheCap;
    if (total > cap) {
        // Records any notebook's manifest on this device names stay (they are in use).
        QSet<QString> used;
        for (const QFileInfo &fi : QDir(dir.filePath(u"notebooks"_s)).entryInfoList({u"*.manifest"_s}, QDir::Files)) {
            QFile f(fi.filePath());
            if (!f.open(QIODevice::ReadOnly))
                continue;
            if (const std::optional<attachments::Manifest> m = attachments::decodeManifest(f.readAll())) {
                for (const ManifestEntry &e : *m)
                    used.insert(attachments::recordFileName(e.hash));
            }
        }
        {
            const std::lock_guard lock(m_owner->m_shared->lock);
            for (const ManifestEntry &e : std::as_const(m_owner->m_shared->manifest))
                used.insert(attachments::recordFileName(e.hash));
        }
        std::ranges::sort(records, [](const Entry &a, const Entry &b) { return a.modified < b.modified; });
        for (const Entry &e : std::as_const(records)) {
            if (total <= cap)
                break;
            if (used.contains(e.name))
                continue;
            if (dir.remove(e.name))
                total -= e.size;
        }
    }
    const quint64 generation = m_generation;
    post([generation, total](AttachmentIndexer *owner) {
        if (generation == owner->m_generation && owner->m_cacheBytesUsed != total) {
            owner->m_cacheBytesUsed = total;
            Q_EMIT owner->contentChanged();
        }
    });
}

// ---- The indexer ---------------------------------------------------------------------------------

AttachmentIndexer::AttachmentIndexer(QObject *parent) : QObject(parent)
{
    m_startTimer.setSingleShot(true);
    connect(&m_startTimer, &QTimer::timeout, this, [this] {
        m_started = true;
        queueAll();
    });
    m_contentTimer.setSingleShot(true);
    m_contentTimer.setInterval(250);
    connect(&m_contentTimer, &QTimer::timeout, this, &AttachmentIndexer::contentChanged);
    m_manifestTimer.setSingleShot(true);
    m_manifestTimer.setInterval(2000);
    connect(&m_manifestTimer, &QTimer::timeout, this, &AttachmentIndexer::saveManifest);
    // A build without OCR knows at once; the others look for their library when somebody asks —
    // OCR turned on, the settings page or a PDF without text reading `ocrAvailable` (probeEngine()).
    if (platform::ocrBackend() == u"none"_s) {
        m_probeAsked = true;
        applyEngine(false, platform::ocrUnavailableReason(), {}, {}, {});
    }
}

void AttachmentIndexer::probeEngine() const
{
    if (m_probeAsked)
        return;
    m_probeAsked = true;
    // The library is looked for once, off the GUI thread (it loads some 50 libraries) — and not
    // before it is needed: text recognition is off unless the user turned it on, and a start
    // must not load Tesseract with everything it brings for nothing (docs/PERFORMANCE.md "Memory:
    // only what is in use"). Until the answer comes the reason reads "probing".
    const QPointer<AttachmentIndexer> self(const_cast<AttachmentIndexer *>(this));
    QThreadPool::globalInstance()->start([self] {
        const bool available = platform::ocrAvailable();
        const QString reason = platform::ocrUnavailableReason();
        const QString name = platform::ocrEngineName();
        const QStringList languages = platform::ocrLanguages();
        const QString userDir = platform::ocrUserDataDir();
        QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [self, available, reason, name, languages, userDir] {
                    if (self)
                        self->applyEngine(available, reason, name, languages, userDir);
                },
                Qt::QueuedConnection);
    });
}

AttachmentIndexer::~AttachmentIndexer()
{
    detach();
}

QString AttachmentIndexer::languageNames(const QString &codes)
{
    QStringList names;
    for (const QString &code : codes.split(QRegularExpression(u"[+,\\s]+"_s), Qt::SkipEmptyParts)) {
        // Tesseract's codes are ISO 639-2 ("pol"; "chi_sim": a script after the language).
        const QString base = code.section(u'_', 0, 0);
        const QLocale::Language language = QLocale::codeToLanguage(base, QLocale::ISO639Part2);
        // (English's own name of en_US is "American English": the language, not a country's.)
        QString name = language == QLocale::AnyLanguage ? QString()
                       : language == QLocale::English   ? u"English"_s
                                                        : QLocale(language).nativeLanguageName();
        if (name.isEmpty())
            name = code;
        else
            name[0] = name.at(0).toUpper();
        names.append(name);
    }
    return names.join(u", "_s);
}

QString AttachmentIndexer::cacheSizeText() const
{
    return QLocale().formattedDataSize(m_cacheBytesUsed, 1, QLocale::DataSizeTraditionalFormat);
}

bool AttachmentIndexer::pdfTextAvailable() const
{
    return platform::pdfTextAvailable();
}

bool AttachmentIndexer::documentTextAvailable()
{
#if !QT_CONFIG(thread) || defined(Q_OS_WASM)
    return false; // (no worker thread in the browser)
#else
    return true;
#endif
}

void AttachmentIndexer::applyEngine(bool available, const QString &reason, const QString &name,
                                    const QStringList &languages, const QString &userDir)
{
    const QString before = ocrLanguages();
    m_ocrAvailable = available;
    m_ocrReason = reason;
    m_ocrEngine = name;
    m_ocrInstalled = languages;
    m_ocrUserDir = userDir;
    Q_EMIT engineChanged();
    if (ocrLanguages() != before)
        Q_EMIT settingsChanged();
    if (m_worker)
        m_worker->setSettings({m_ocrEnabled && m_ocrAvailable, ocrLanguages()});
    // OCR may be on already (the settings came first): the pages waiting for it are found again.
    if (m_ocrEnabled && m_ocrAvailable && m_started)
        queueAll();
    Q_EMIT stateChanged();
}

QString AttachmentIndexer::ocrLanguages() const
{
    return platform::ocrLanguagesFor(m_ocrLanguagesSetting, m_uiLanguage, m_ocrInstalled);
}

void AttachmentIndexer::attach(Vault *vault, const QString &cacheDir, const QString &manifestFile)
{
    detach();
    if (!vault)
        return;
    m_vault = vault;
    m_cacheDir = cacheDir;
    m_manifestFile = manifestFile;
    ++m_generation;
    {
        const std::lock_guard lock(m_shared->lock);
        m_shared->cacheDir = cacheDir;
        m_shared->manifest.clear();
        m_shared->pathOfFile.clear();
        QFile f(manifestFile);
        if (!manifestFile.isEmpty() && f.open(QIODevice::ReadOnly)) {
            if (std::optional<attachments::Manifest> m = attachments::decodeManifest(f.readAll()))
                m_shared->manifest = std::move(*m);
        }
    }
    connect(vault, &Vault::changed, this, &AttachmentIndexer::onVaultChanged);
}

void AttachmentIndexer::detach()
{
    m_startTimer.stop();
    stopWorker();
    if (m_manifestTimer.isActive())
        saveManifest();
    if (m_vault)
        disconnect(m_vault, nullptr, this, nullptr);
    m_vault = nullptr;
    m_started = false;
    ++m_generation;
    m_pages.clear();
    const bool had = m_search.fileCount() > 0;
    m_search.clear();
    {
        const std::lock_guard lock(m_shared->lock);
        m_shared->manifest.clear();
        m_shared->pathOfFile.clear();
    }
    m_filesTotal = m_filesDone = m_currentPage = m_currentPages = m_pagesToRecognize = m_pagesRecognized = 0;
    m_currentFile.clear();
    m_state = u"idle"_s;
    m_workerIdle = true;
    if (had)
        Q_EMIT contentChanged();
    Q_EMIT progressChanged();
    Q_EMIT stateChanged();
}

void AttachmentIndexer::start(int delayMs)
{
    if (!m_vault || m_started || m_startTimer.isActive())
        return;
    m_startTimer.start(std::max(0, delayMs));
}

void AttachmentIndexer::stopWorker()
{
    if (!m_worker)
        return;
    m_worker->stopAndWait();
    m_worker.reset();
    m_workerIdle = true;
}

void AttachmentIndexer::ensureWorker()
{
#if !QT_CONFIG(thread) || defined(Q_OS_WASM)
    return; // (no threads in the browser: nothing to read there anyway — no renderer, no OCR)
#else
    if (m_worker || !m_vault || m_cacheDir.isEmpty())
        return;
    const bool ocr = m_ocrEnabled && m_ocrAvailable;
    m_worker = std::make_unique<IndexerWorker>(this, m_generation, m_cacheDir,
                                               IndexerWorker::Settings{ocr, ocrLanguages()});
    m_worker->setObjectName(u"brulion-attachments"_s);
    // The lowest priority there is (Linux: SCHED_IDLE): only time nobody else wants.
    m_worker->start(QThread::IdlePriority);
#endif
}

bool AttachmentIndexer::idle() const
{
    return !m_startTimer.isActive() && (!m_worker || !m_worker->busy()) && m_workerIdle;
}

QString AttachmentIndexer::state() const
{
    if (!m_enabled)
        return u"off"_s;
    if (!platform::pdfTextAvailable() && !documentTextAvailable() && !m_ocrAvailable && m_ocrReason != u"probing"_s)
        return u"unavailable"_s;
    if (m_paused.load())
        return u"paused"_s;
    return m_state;
}

void AttachmentIndexer::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    Q_EMIT settingsChanged();
    if (!on) {
        stopWorker();
        m_pages.clear();
        m_search.clear();
        m_state = u"idle"_s;
        contentTouched();
    } else if (m_started) {
        queueAll();
    }
    Q_EMIT stateChanged();
}

void AttachmentIndexer::setOcrEnabled(bool on)
{
    if (m_ocrEnabled == on)
        return;
    m_ocrEnabled = on;
    // Wanted: is it here? (applyEngine() starts the reading once the answer is yes.)
    if (on)
        probeEngine();
    Q_EMIT settingsChanged();
    if (m_worker)
        m_worker->setSettings({on && m_ocrAvailable, ocrLanguages()});
    // The pages waiting for OCR are found again (records from the cache: nothing read twice).
    if (on && m_started)
        queueAll();
}

void AttachmentIndexer::setOcrLanguagesSetting(const QString &setting)
{
    const QString s = setting.trimmed().isEmpty() ? u"auto"_s : setting.trimmed();
    if (m_ocrLanguagesSetting == s)
        return;
    const QString before = ocrLanguages();
    m_ocrLanguagesSetting = s;
    Q_EMIT settingsChanged();
    if (ocrLanguages() != before) {
        if (m_worker)
            m_worker->setSettings({m_ocrEnabled && m_ocrAvailable, ocrLanguages()});
        if (m_ocrEnabled && m_started)
            queueAll();
    }
}

void AttachmentIndexer::setUiLanguage(const QString &language)
{
    if (m_uiLanguage == language)
        return;
    const QString before = ocrLanguages();
    m_uiLanguage = language;
    if (ocrLanguages() != before) {
        Q_EMIT settingsChanged();
        if (m_worker)
            m_worker->setSettings({m_ocrEnabled && m_ocrAvailable, ocrLanguages()});
        if (m_ocrEnabled && m_started)
            queueAll();
    }
}

void AttachmentIndexer::setPaused(bool paused)
{
    if (m_paused.load() == paused)
        return;
    m_paused = paused;
    if (m_worker)
        m_worker->wake();
    Q_EMIT stateChanged();
}

void AttachmentIndexer::queueAll()
{
    if (!m_vault || !m_enabled || !m_started)
        return;
    QList<FileJob> jobs;
    QSet<QString> present;
    // (No look at the disk here — a phone's thousands of photos on slow storage: the worker skips a
    // file that is a link itself; the vault lists nothing behind a link it does not follow.)
    for (const FileStat &st : m_vault->files()) {
        if (st.isDir || !isSearchableAttachment(st.path))
            continue;
        present.insert(st.path);
        jobs.append(FileJob{st.path, m_vault->absolutePath(st.path), st.size, st.mtime});
    }
    // Gone while the app was closed: out of the manifest (their records may serve another notebook).
    bool dropped = false;
    {
        const std::lock_guard lock(m_shared->lock);
        for (auto it = m_shared->manifest.begin(); it != m_shared->manifest.end();) {
            if (!present.contains(it.key())) {
                it = m_shared->manifest.erase(it);
                dropped = true;
            } else {
                ++it;
            }
        }
    }
    if (dropped)
        saveManifestLater();
    for (const QString &path : m_search.paths()) {
        if (!present.contains(path))
            m_search.remove(path);
    }
    if (jobs.isEmpty())
        return;
    ensureWorker();
    if (!m_worker)
        return;
    m_workerIdle = false;
    for (FileJob &job : jobs)
        m_worker->enqueue(std::move(job), false);
}

void AttachmentIndexer::queue(const QString &path, bool front)
{
    if (!m_vault || !m_enabled || !m_started || !isSearchableAttachment(path))
        return;
    const std::optional<FileStat> st = m_vault->stat(path);
    if (!st || st->isDir || !m_vault->reaches(path))
        return;
    ensureWorker();
    if (!m_worker)
        return;
    m_workerIdle = false;
    m_worker->enqueue(FileJob{path, m_vault->absolutePath(path), st->size, st->mtime}, front);
}

void AttachmentIndexer::forget(const QString &path)
{
    if (m_worker)
        m_worker->drop(path);
    {
        const std::lock_guard lock(m_shared->lock);
        m_shared->manifest.remove(path);
        for (auto it = m_shared->pathOfFile.begin(); it != m_shared->pathOfFile.end();) {
            if (it.value() == path)
                it = m_shared->pathOfFile.erase(it);
            else
                ++it;
        }
    }
    if (m_search.contains(path)) {
        m_search.remove(path);
        contentTouched();
    }
    saveManifestLater();
}

void AttachmentIndexer::onVaultChanged(const QList<brulion::VaultChange> &changes)
{
    for (const VaultChange &change : changes) {
        switch (change.type) {
        case ChangeType::Deleted:
            if (isSearchableAttachment(change.path) || m_search.contains(change.path))
                forget(change.path);
            break;
        case ChangeType::Renamed: {
            if (!isSearchableAttachment(change.oldPath) && !m_search.contains(change.oldPath)) {
                queue(change.path, true);
                break;
            }
            // Its text follows it (the record is the content's): nothing is read again.
            std::optional<ManifestEntry> entry;
            {
                const std::lock_guard lock(m_shared->lock);
                if (const auto it = m_shared->manifest.constFind(change.oldPath); it != m_shared->manifest.constEnd())
                    entry = *it;
            }
            if (m_worker)
                m_worker->drop(change.oldPath);
            const bool searchable = isSearchableAttachment(change.path);
            if (m_search.contains(change.oldPath) && searchable) {
                m_search.rename(change.oldPath, change.path);
                contentTouched();
            } else if (m_search.contains(change.oldPath)) {
                m_search.remove(change.oldPath);
                contentTouched();
            }
            {
                const std::lock_guard lock(m_shared->lock);
                m_shared->manifest.remove(change.oldPath);
                if (entry && searchable)
                    m_shared->manifest.insert(change.path, *entry);
                for (auto it = m_shared->pathOfFile.begin(); it != m_shared->pathOfFile.end();) {
                    if (it.value() == change.oldPath)
                        it = m_shared->pathOfFile.erase(it);
                    else
                        ++it;
                }
                if (m_vault && searchable)
                    m_shared->pathOfFile.insert(m_vault->absolutePath(change.path), change.path);
            }
            saveManifestLater();
            // (Its size and time are the same: the record is found again, and OCR goes on there.)
            queue(change.path, true);
            break;
        }
        case ChangeType::Created:
        case ChangeType::Modified:
            if (isSearchableAttachment(change.path)) {
                m_pages.clear();
                queue(change.path, true);
            }
            break;
        }
    }
}

void AttachmentIndexer::applyFile(quint64 generation, const QString &path, const ManifestEntry &entry,
                                  AttachmentSearch::Prepared prepared)
{
    if (generation != m_generation || !m_vault || !m_enabled)
        return;
    {
        const std::lock_guard lock(m_shared->lock);
        if (!entry.hash.isEmpty()) {
            if (m_shared->manifest.value(path) != entry) {
                m_shared->manifest.insert(path, entry);
                saveManifestLater();
            }
            m_shared->pathOfFile.insert(m_vault->absolutePath(path), path);
        }
    }
    m_search.set(std::move(prepared));
    contentTouched();
}

void AttachmentIndexer::applyPages(quint64 generation, const QString &path, QList<AttachmentSearch::PreparedPage> pages)
{
    if (generation != m_generation || !m_enabled)
        return;
    for (AttachmentSearch::PreparedPage &page : pages) {
        m_pages.remove(path + u'\n' + QString::number(page.page));
        m_search.setPage(path, std::move(page));
    }
    contentTouched();
}

void AttachmentIndexer::recordSaved(quint64 generation, const QString &path)
{
    if (generation != m_generation)
        return;
    const QString prefix = path + u'\n';
    for (const QString &key : m_pages.keys()) {
        if (key.startsWith(prefix))
            m_pages.remove(key);
    }
}

void AttachmentIndexer::applyProgress(quint64 generation, const QVariantMap &p)
{
    if (generation != m_generation)
        return;
    const QString state = p.value(u"state"_s).toString();
    m_workerIdle = state == u"idle"_s;
    const auto set = [](auto &field, auto value, bool &changed) {
        if (field != value) {
            field = value;
            changed = true;
        }
    };
    bool progress = false;
    set(m_filesTotal, p.value(u"filesTotal"_s).toInt(), progress);
    set(m_filesDone, p.value(u"filesDone"_s).toInt(), progress);
    set(m_currentFile, p.value(u"currentFile"_s).toString(), progress);
    set(m_currentPage, p.value(u"currentPage"_s).toInt(), progress);
    set(m_currentPages, p.value(u"currentPages"_s).toInt(), progress);
    set(m_pagesToRecognize, p.value(u"pagesToRecognize"_s).toInt(), progress);
    set(m_pagesRecognized, p.value(u"pagesRecognized"_s).toInt(), progress);
    bool stateChange = false;
    // (Paused and waiting are the indexer's own state; the worker only tells what it does.)
    set(m_state, state == u"paused"_s ? (m_paused.load() ? u"paused"_s : m_state) : state, stateChange);
    if (progress)
        Q_EMIT progressChanged();
    if (stateChange)
        Q_EMIT stateChanged();
}

void AttachmentIndexer::contentTouched()
{
    if (!m_contentTimer.isActive())
        m_contentTimer.start();
}

QString AttachmentIndexer::recordPath(const QByteArray &hash) const
{
    return QDir(m_cacheDir).filePath(attachments::recordFileName(hash));
}

void AttachmentIndexer::saveManifestLater()
{
    if (!m_manifestFile.isEmpty())
        m_manifestTimer.start();
}

void AttachmentIndexer::saveManifest()
{
    m_manifestTimer.stop();
    if (m_manifestFile.isEmpty())
        return;
    QByteArray bytes;
    {
        const std::lock_guard lock(m_shared->lock);
        bytes = attachments::encodeManifest(m_shared->manifest);
    }
    QDir().mkpath(QFileInfo(m_manifestFile).absolutePath());
    QSaveFile out(m_manifestFile);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(bytes);
        if (!out.commit())
            qCWarning(logging::lcIndex) << "Attachment text: the manifest could not be saved";
    }
}

QList<AttachmentSearch::Hit> AttachmentIndexer::search(QStringView query, int limit, attachments::Scopes kinds)
{
    if (!m_enabled)
        return {};
    return m_search.search(query, limit, kinds,
                           [this](const QString &path, int page) { return this->page(path, page); });
}

std::optional<Page> AttachmentIndexer::page(const QString &path, int page)
{
    const QString key = path + u'\n' + QString::number(page);
    if (const Page *cached = m_pages.object(key))
        return *cached;
    QString file;
    {
        const std::lock_guard lock(m_shared->lock);
        const auto it = m_shared->manifest.constFind(path);
        if (it == m_shared->manifest.constEnd())
            return std::nullopt;
        file = recordPath(it->hash);
    }
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    std::optional<Page> read = attachments::readRecordPage(f, page);
    if (read)
        m_pages.insert(key, new Page(*read), int(1 + read->text.size() / 4096));
    return read;
}

std::optional<Page> AttachmentIndexer::pageOf(Shared &shared, const QString &absoluteFile, qint64 size, qint64 mtime,
                                              int page)
{
    QString file;
    {
        const std::lock_guard lock(shared.lock);
        const auto path = shared.pathOfFile.constFind(absoluteFile);
        if (path == shared.pathOfFile.constEnd())
            return std::nullopt;
        const auto it = shared.manifest.constFind(*path);
        if (it == shared.manifest.constEnd() || it->size != size || it->mtime != mtime || shared.cacheDir.isEmpty())
            return std::nullopt;
        file = QDir(shared.cacheDir).filePath(attachments::recordFileName(it->hash));
    }
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return attachments::readRecordPage(f, page);
}

std::optional<Page> AttachmentIndexer::pageOfFile(const QString &absoluteFile, qint64 size, qint64 mtime,
                                                  int page) const
{
    return pageOf(*m_shared, absoluteFile, size, mtime, page);
}

AttachmentIndexer::PageLookup AttachmentIndexer::pageLookup() const
{
    return [weak = std::weak_ptr<Shared>(m_shared)](const QString &absoluteFile, qint64 size, qint64 mtime,
                                                    int page) -> std::optional<Page> {
        const std::shared_ptr<Shared> shared = weak.lock();
        return shared ? pageOf(*shared, absoluteFile, size, mtime, page) : std::nullopt;
    };
}

QVariantList AttachmentIndexer::matchRects(const QString &path, int pageNumber, const QString &query)
{
    QVariantList out;
    const std::optional<Page> p = page(path, std::max(1, pageNumber));
    if (!p || p->words.isEmpty() || query.trimmed().isEmpty())
        return out;
    const QList<QRectF> boxes = attachments::ocrCharBoxes(*p);
    QList<platform::PdfTextRange> ranges = platform::findPdfText(p->text, query, platform::MaxPdfHitsPerPage);
    if (ranges.isEmpty()) {
        // The words one by one (the search matched them apart).
        for (const QString &word : query.split(u' ', Qt::SkipEmptyParts))
            ranges.append(platform::findPdfText(p->text, word, platform::MaxPdfHitsPerPage));
    }
    for (const platform::PdfTextRange &range : std::as_const(ranges)) {
        for (const QRectF &r : platform::pdfRangeRects(boxes, range))
            out.append(QVariantMap{{u"x"_s, r.x()},
                                   {u"y"_s, r.y()},
                                   {u"width"_s, r.width()},
                                   {u"height"_s, r.height()},
                                   {u"box"_s, r}});
    }
    return out;
}

QString AttachmentIndexer::recognizedText(const QString &path, int pageNumber)
{
    const std::optional<Page> p = page(path, std::max(1, pageNumber));
    return p && p->source == PageSource::Ocr ? p->text : QString();
}

void AttachmentIndexer::clearCache()
{
    stopWorker();
    m_pages.clear();
    if (!m_cacheDir.isEmpty()) {
        QDir dir(m_cacheDir);
        for (const QString &name : dir.entryList({u"*.bat"_s}, QDir::Files))
            dir.remove(name);
        QDir notebooks(dir.filePath(u"notebooks"_s));
        for (const QString &name : notebooks.entryList({u"*.manifest"_s}, QDir::Files))
            notebooks.remove(name);
    }
    {
        const std::lock_guard lock(m_shared->lock);
        m_shared->manifest.clear();
        m_shared->pathOfFile.clear();
    }
    ++m_generation;
    m_search.clear();
    m_cacheBytesUsed = 0;
    contentTouched();
    Q_EMIT contentChanged();
    queueAll();
}

} // namespace brulion::attachsearch
