#include "pdfthumbnails.h"

#include "pdfrenderpool.h"

#include "pdfpages.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::pdfview {

namespace {

constexpr auto PagesKey = "Brulion-Pages";
constexpr auto WidthKey = "Brulion-PageWidth";
constexpr auto HeightKey = "Brulion-PageHeight";

/// The default renderer: a lease of the shared reader pool.
PdfThumbnails::Rendered renderWithPool(const QString &file, const QString &version, int page, int longSide)
{
    PdfThumbnails::Rendered out;
    if (!platform::pdfAvailable()) {
        out.error = u"unsupported"_s;
        return out;
    }
    platform::PdfInfo info;
    PdfReaderPool::Lease lease = PdfReaderPool::instance().acquire(file, version, &info);
    if (!lease) {
        out.error = info.locked ? u"locked"_s : info.pages == 0 ? u"nopages"_s : u"unreadable"_s;
        return out;
    }
    out.pages = lease->pageCount();
    if (out.pages <= 0) {
        out.error = u"nopages"_s;
        return out;
    }
    const int shown = page >= 1 && page <= out.pages ? page : 1;
    out.pagePoints = lease->pageSize(shown);
    const QSize pixels = platform::pdfPagePixels(out.pagePoints, longSide);
    out.image = pixels.isEmpty() ? QImage() : lease->render(shown, pixels);
    if (out.image.isNull())
        out.error = u"render"_s;
    return out;
}

QString errorFile(const QString &dir, const QString &key)
{
    return QDir(dir).filePath(key + u".err"_s);
}

QString pngFile(const QString &dir, const QString &key)
{
    return QDir(dir).filePath(key + u".png"_s);
}

} // namespace

PdfThumbnails::PdfThumbnails(const QString &cacheDir, QObject *parent, Renderer renderer)
    : QObject(parent), m_cacheDir(cacheDir), m_renderer(std::move(renderer)), m_prune(this)
{
    m_customRenderer = bool(m_renderer);
    if (!m_renderer)
        m_renderer = renderWithPool;
    m_prune.setSingleShot(true);
    m_prune.setInterval(PruneDelayMs);
    connect(&m_prune, &QTimer::timeout, this, [this] {
        const QString dir = m_cacheDir;
        const qint64 cap = m_cacheBytes;
        if (dir.isEmpty() || cap <= 0)
            return;
        PdfRenderPool::start(
                [dir, cap] {
                    QFileInfoList files = QDir(dir).entryInfoList({u"*.png"_s, u"*.err"_s}, QDir::Files);
                    qint64 total = 0;
                    for (const QFileInfo &f : std::as_const(files))
                        total += f.size();
                    if (total <= cap)
                        return;
                    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
                        return a.lastModified() < b.lastModified();
                    });
                    for (const QFileInfo &f : std::as_const(files)) {
                        if (total <= cap)
                            break;
                        if (QFile::remove(f.absoluteFilePath()))
                            total -= f.size();
                    }
                },
                PdfRenderPool::Thumbnail);
    });
}

PdfThumbnails::~PdfThumbnails()
{
    *m_alive = false;
}

void PdfThumbnails::setLongSide(int pixels)
{
    m_longSide = std::clamp(pixels, 64, platform::MaxPdfPageSide);
}

int PdfThumbnails::longSideForRatio(qreal devicePixelRatio)
{
    const qreal wanted = 420 * (devicePixelRatio > 0 && std::isfinite(devicePixelRatio) ? devicePixelRatio : 1);
    for (const int side : {512, 768, 1024, 1280})
        if (wanted <= side)
            return side;
    return 1536;
}

QString PdfThumbnails::cacheKey(const QString &file, qint64 size, qint64 mtimeMs, int page, int side)
{
    const QString key = u"pdfthumb/1|%1|%2|%3|%4|%5"_s.arg(file).arg(size).arg(mtimeMs).arg(page).arg(side);
    return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
}

void PdfThumbnails::remember(const QString &key, const Entry &entry)
{
    if (const auto it = m_positions.find(key); it != m_positions.end()) {
        m_order.erase(it.value());
        m_positions.erase(it);
    }
    m_order.push_front(key);
    m_positions.insert(key, m_order.begin());
    m_entries.insert(key, entry);
    while (m_order.size() > MaxMemoryEntries) {
        const QString oldest = m_order.back();
        m_order.pop_back();
        m_positions.remove(oldest);
        m_entries.remove(oldest);
    }
}

PdfThumbnails::Entry PdfThumbnails::fromDisk(const QString &key)
{
    Entry entry;
    entry.state = State::Loading;
    if (m_cacheDir.isEmpty())
        return entry;
    const QString png = pngFile(m_cacheDir, key);
    const QFileInfo info(png);
    if (info.isFile()) {
        QImageReader reader(png, "png");
        const QSize size = reader.size();
        if (size.isValid() && !size.isEmpty()) {
            entry.state = State::Ready;
            entry.file = png;
            entry.pixels = size;
            entry.pages = reader.text(QString::fromLatin1(PagesKey)).toInt();
            entry.pagePoints = QSizeF(reader.text(QString::fromLatin1(WidthKey)).toDouble(),
                                      reader.text(QString::fromLatin1(HeightKey)).toDouble());
            // Used: kept by the pruning (the mtime orders it), touched at most once a day.
            if (info.lastModified().secsTo(QDateTime::currentDateTime()) > 24 * 3600) {
                QFile f(png);
                if (f.open(QIODevice::ReadWrite))
                    f.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
            }
            return entry;
        }
        QFile::remove(png); // a broken or truncated file: drawn again
        return entry;
    }
    QFile err(errorFile(m_cacheDir, key));
    if (err.open(QIODevice::ReadOnly)) {
        const QJsonObject json = QJsonDocument::fromJson(err.read(4096)).object();
        const QString error = json.value(u"error"_s).toString();
        if (!error.isEmpty()) {
            entry.state = State::Failed;
            entry.error = error;
            entry.pages = json.value(u"pages"_s).toInt();
        }
    }
    return entry;
}

PdfThumbnails::Entry PdfThumbnails::entry(const QString &file, qint64 size, qint64 mtimeMs, int page)
{
    Entry unsupported;
    unsupported.state = State::Unsupported;
    if (!m_customRenderer && !platform::pdfAvailable())
        return unsupported;
    if (file.isEmpty())
        return Entry{State::Failed, {}, {}, 0, {}, u"unreadable"_s};
    const int side = m_longSide;
    const QString key = cacheKey(file, size, mtimeMs, page, side);
    if (const auto it = m_entries.constFind(key); it != m_entries.constEnd()) {
        const Entry found = it.value();
        remember(key, found); // (most recently asked)
        return found;
    }
    if (m_pending.contains(key))
        return {};
    const Entry disk = fromDisk(key);
    if (disk.state != State::Loading) {
        remember(key, disk);
        return disk;
    }
    // Drawn on the pool; the result is stored there too (PNG encoding is no GUI work).
    m_pending.insert(key);
    const QString version = QString::number(mtimeMs) + u'-' + QString::number(size);
    const QString dir = m_cacheDir;
    const Renderer renderer = m_renderer;
    const std::shared_ptr<bool> alive = m_alive;
    const QPointer<PdfThumbnails> self(this);
    PdfRenderPool::start(
            [=] {
                const Rendered r = renderer(file, version, page, side);
                Entry out;
                out.pages = r.pages;
                out.pagePoints = r.pagePoints;
                if (r.error == u"unsupported") {
                    out.state = State::Unsupported;
                } else if (r.image.isNull()) {
                    out.state = State::Failed;
                    out.error = r.error.isEmpty() ? u"render"_s : r.error;
                    if (!dir.isEmpty() && QDir().mkpath(dir)) {
                        QSaveFile f(errorFile(dir, key));
                        if (f.open(QIODevice::WriteOnly)) {
                            f.write(QJsonDocument(QJsonObject{{u"error"_s, out.error}, {u"pages"_s, out.pages}})
                                            .toJson(QJsonDocument::Compact));
                            f.commit();
                        }
                    }
                } else if (dir.isEmpty()) {
                    out.state = State::Ready;
                    out.pixels = r.image.size();
                } else {
                    bool stored = false;
                    if (QDir().mkpath(dir)) {
                        QSaveFile f(pngFile(dir, key));
                        if (f.open(QIODevice::WriteOnly)) {
                            QImageWriter writer(&f, "png");
                            writer.setText(QString::fromLatin1(PagesKey), QString::number(r.pages));
                            writer.setText(QString::fromLatin1(WidthKey), QString::number(r.pagePoints.width(), 'g', 8));
                            writer.setText(QString::fromLatin1(HeightKey), QString::number(r.pagePoints.height(), 'g', 8));
                            stored = writer.write(r.image) && f.commit();
                        }
                    }
                    if (stored) {
                        out.state = State::Ready;
                        out.file = pngFile(dir, key);
                        out.pixels = r.image.size();
                    } else {
                        out.state = State::Failed;
                        out.error = u"cache"_s;
                    }
                }
                QCoreApplication *app = QCoreApplication::instance();
                if (!app)
                    return;
                QMetaObject::invokeMethod(app, [self, alive, key, file, out] {
                    if (*alive && self)
                        self->finished(key, file, out);
                }, Qt::QueuedConnection);
            },
            PdfRenderPool::Thumbnail);
    return {};
}

void PdfThumbnails::finished(const QString &key, const QString &file, const Entry &entry)
{
    m_pending.remove(key);
    remember(key, entry);
    if (entry.state == State::Ready && !entry.file.isEmpty())
        schedulePrune();
    Q_EMIT changed(file);
}

void PdfThumbnails::schedulePrune()
{
    m_prune.start(); // (restarted: once 2 s after the last store)
}

} // namespace brulion::pdfview
