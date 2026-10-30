#include "backdrops.h"

#include "backdropimage.h"
#include "pdfpageimageprovider.h"
#include "pdfpages.h"

#include "drawing/backdrop.h"
#include "editorhost.h"
#include "util/paths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QPointer>
#include <QSet>

#ifdef Q_OS_WASM
#include "pickedfiles.h"
#include "wasm/wasmfiles.h"
#endif

#if QT_CONFIG(thread) && !defined(Q_OS_WASM)
#include <QThreadPool>
#define BRULION_BACKDROP_THREADS 1
#endif

#include <atomic>
#include <cmath>
#include <mutex>

using namespace Qt::StringLiterals;

namespace brulion::drawingview {

namespace {

std::mutex s_resolverLock;
std::function<QString(const QString &)> s_resolver;
std::function<QString(const QString &, const QByteArray &)> s_importer;

} // namespace

struct DrawingBackdrops::Job {
    int id = 0;
    QString path;
    QString file;
    /// The PDF's pages; {0}: the file as a picture.
    QList<int> pages;
    /// The items become drawings' files too (prepare()'s `drawings`), stamped `now`.
    bool drawings = false;
    qint64 now = 0;
    qsizetype next = 0;
    QVariantList items;
    std::atomic_bool cancelled{false};
};

DrawingBackdrops::DrawingBackdrops(QObject *parent) : QObject(parent) { }

DrawingBackdrops::~DrawingBackdrops()
{
    for (const std::shared_ptr<Job> &job : std::as_const(m_jobs))
        job->cancelled = true;
}

void DrawingBackdrops::setPathResolver(std::function<QString(const QString &)> resolver)
{
    const std::lock_guard lock(s_resolverLock);
    s_resolver = std::move(resolver);
}

QString DrawingBackdrops::resolve(const QString &vaultPath)
{
    const std::lock_guard lock(s_resolverLock);
    return s_resolver && !vaultPath.isEmpty() ? s_resolver(vaultPath) : QString();
}

void DrawingBackdrops::setImporter(std::function<QString(const QString &, const QByteArray &)> importer)
{
    const std::lock_guard lock(s_resolverLock);
    s_importer = std::move(importer);
}

QList<int> DrawingBackdrops::parsePages(const QString &text, int count) const
{
    // (One more than taken: the dialog says when the text named too many.)
    return drawing::parsePageRanges(text, count, drawing::MaxPagesPerInsert + 1);
}

QString DrawingBackdrops::formatPages(const QList<int> &pages) const
{
    return drawing::formatPageRanges(pages);
}

int DrawingBackdrops::pickInBrowser()
{
#ifdef Q_OS_WASM
    const int pick = ++m_lastPick;
    const QPointer<DrawingBackdrops> self(this);
    // Pictures only: no PDF renderer in the browser.
    QStringList pictures;
    for (const QString &ext : paths::pictureExtensions())
        pictures.append(u'*' + ext);
    platform::wasm::openFiles({tr("Pictures (%1)").arg(pictures.join(u' '))}, false, MaxAttachmentBytes,
                              MaxAttachmentBytes, [self, pick](const QList<platform::PickedFile> &files) {
                                  if (!self || files.isEmpty())
                                      return; // cancelled
                                  const platform::PickedFile &file = files.first();
                                  std::function<QString(const QString &, const QByteArray &)> importer;
                                  {
                                      const std::lock_guard lock(s_resolverLock);
                                      importer = s_importer;
                                  }
                                  const QString path = importer && file.isRead() && !file.data.isEmpty()
                                                               ? importer(file.name, file.data)
                                                               : QString();
                                  if (path.isEmpty())
                                      Q_EMIT self->pickFailed(pick, file.name);
                                  else
                                      Q_EMIT self->picked(pick, path);
                              });
    return pick;
#else
    return 0;
#endif
}

bool DrawingBackdrops::pdfAvailable() const
{
    return platform::pdfAvailable();
}

QStringList DrawingBackdrops::nameFilters() const
{
    QStringList pictures;
    for (const QString &ext : paths::pictureExtensions())
        pictures.append(u'*' + ext);
    if (!pdfAvailable()) {
        //: %1: the file name patterns of pictures, e.g. "*.png *.jpg *.jpeg"
        return {tr("Pictures (%1)").arg(pictures.join(u' ')), tr("All files (*)")};
    }
    //: %1: the file name patterns of pictures and PDF, e.g. "*.png *.jpg *.pdf"
    return {tr("Pictures and PDF (%1)").arg((pictures + QStringList{u"*.pdf"_s}).join(u' ')), tr("PDF (*.pdf)"),
            tr("All files (*)")};
}

bool DrawingBackdrops::isPdf(const QString &vaultPath) const
{
    return vaultPath.endsWith(u".pdf"_s, Qt::CaseInsensitive);
}

QVariantMap DrawingBackdrops::pdfInfo(const QString &vaultPath) const
{
    const QString file = resolve(vaultPath);
    if (!pdfAvailable())
        return {{u"pages"_s, -1}, {u"error"_s, errorText(backdroperror::NoPdf.toString())}, {u"locked"_s, false}};
    if (file.isEmpty() || !QFileInfo(file).isFile())
        return {{u"pages"_s, -1}, {u"error"_s, errorText(backdroperror::Unreadable.toString())}, {u"locked"_s, false}};
    // (The count and the first page's size only: a long PDF opens fast.)
    const platform::PdfInfo info = platform::pdfInfo(file, false);
    QString error;
    if (info.locked)
        error = errorText(backdroperror::Locked.toString());
    else if (info.pages <= 0)
        error = errorText(backdroperror::Pdf.toString());
    const QSizeF first = info.pages > 0 ? info.pagePoints.value(0) : QSizeF();
    const bool sized =
            first.isValid() && !first.isEmpty() && std::isfinite(first.width()) && std::isfinite(first.height());
    return {{u"pages"_s, info.pages},
            {u"error"_s, error},
            {u"locked"_s, info.locked},
            {u"width"_s, sized ? first.width() : 0.0},
            {u"height"_s, sized ? first.height() : 0.0}};
}

QUrl DrawingBackdrops::pageUrl(const QString &vaultPath, int page) const
{
    const QString file = resolve(vaultPath);
    if (file.isEmpty() || !pdfAvailable())
        return {};
    const QFileInfo info(file);
    if (!info.isFile())
        return {};
    return PdfPageImageProvider::url(
            file, page, QString::number(info.lastModified().toMSecsSinceEpoch()) + u'-' + QString::number(info.size()));
}

QString DrawingBackdrops::errorText(const QString &code) const
{
    if (code == backdroperror::Unreadable)
        return tr("The file cannot be read.");
    if (code == backdroperror::Format)
        return tr("This picture's format cannot be shown on this device.");
    if (code == backdroperror::Locked)
        return tr("This PDF is protected with a password: Brulion cannot open it.");
    if (code == backdroperror::Pdf)
        return tr("This PDF cannot be read.");
    if (code == backdroperror::NoPdf)
        return tr("PDF pages cannot be drawn here: choose a picture.");
    return code.isEmpty() ? QString() : tr("The backdrop could not be made.");
}

int DrawingBackdrops::prepare(const QString &vaultPath, const QList<int> &pages, bool drawings)
{
    auto job = std::make_shared<Job>();
    job->id = ++m_lastJob;
    job->path = vaultPath;
    job->file = resolve(vaultPath);
    job->drawings = drawings;
    job->now = QDateTime::currentMSecsSinceEpoch();
    QSet<int> seen;
    for (const int page : pages) {
        if (page >= 1 && !seen.contains(page) && job->pages.size() < drawing::MaxPagesPerInsert) {
            seen.insert(page);
            job->pages.append(page);
        }
    }
    if (job->pages.isEmpty())
        job->pages = {0};
    m_jobs.insert(job->id, job);
    // (Begun in the next event-loop turn: QML has the job's id before any signal.)
    QMetaObject::invokeMethod(this, [this, job] { step(job); }, Qt::QueuedConnection);
    return job->id;
}

void DrawingBackdrops::cancel(int id)
{
    if (const std::shared_ptr<Job> job = m_jobs.value(id))
        job->cancelled = true;
}

void DrawingBackdrops::step(const std::shared_ptr<Job> &job)
{
    if (job->cancelled) {
        done(job, true);
        return;
    }
    if (job->next >= job->pages.size()) {
        done(job, false);
        return;
    }
    const int page = job->pages.at(job->next);
    const QString file = job->file;
    const QString path = job->path;
    const bool drawings = job->drawings;
    const qint64 now = job->now;
    const auto work = [file, path, page, drawings, now] {
        if (file.isEmpty()) {
            PreparedBackdrop failed;
            failed.source = drawing::BackdropSource{path, page};
            failed.error = backdroperror::Unreadable.toString();
            return toVariant(failed);
        }
        const PreparedBackdrop prepared =
                page == 0 ? prepareBackdropPicture(file, path) : prepareBackdropPdfPage(file, path, page);
        return drawings ? toDrawingVariant(prepared, now) : toVariant(prepared);
    };
    const QPointer<DrawingBackdrops> self(this);
    const auto take = [self, job](const QVariantMap &item) {
        if (!self)
            return;
        job->items.append(item);
        ++job->next;
        Q_EMIT self->progress(job->id, int(job->next), int(job->pages.size()));
        self->step(job);
    };
#ifdef BRULION_BACKDROP_THREADS
    QThreadPool::globalInstance()->start([work, take] {
        const QVariantMap item = work();
        // Back on the GUI thread (the application object's), where `self` is checked.
        QMetaObject::invokeMethod(QCoreApplication::instance(), [take, item] { take(item); }, Qt::QueuedConnection);
    });
#else
    // No threads (the browser): one file per event-loop turn, so the progress shows.
    QMetaObject::invokeMethod(this, [work, take] { take(work()); }, Qt::QueuedConnection);
#endif
}

void DrawingBackdrops::done(const std::shared_ptr<Job> &job, bool cancelled)
{
    m_jobs.remove(job->id);
    Q_EMIT finished(job->id, job->items, cancelled);
}

} // namespace brulion::drawingview
