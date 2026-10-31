#include "pdfviewmodel.h"

#include "pdfrenderpool.h"

#include "pdfpages.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPointer>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::pdfview {

namespace {

/// `task` back on the GUI thread, unless the model went meanwhile.
void post(const std::shared_ptr<bool> &alive, std::function<void()> task)
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return;
    QMetaObject::invokeMethod(app, [alive, task = std::move(task)] {
        if (*alive)
            task();
    }, Qt::QueuedConnection);
}

QString number(qreal value)
{
    return QString::number(value, 'f', 2);
}

constexpr qreal NaN = std::numeric_limits<qreal>::quiet_NaN();

} // namespace

// ---- PdfVisiblePages -----------------------------------------------------------------------

PdfVisiblePages::PdfVisiblePages(PdfViewModel *model) : QAbstractListModel(model), m_model(model) { }

int PdfVisiblePages::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || m_first < 0 ? 0 : m_last - m_first + 1;
}

QVariant PdfVisiblePages::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const int i = m_first + index.row();
    switch (role) {
    case PageRole: return i + 1;
    case XRole: return m_model->rectOfIndex(i).x();
    case YRole: return m_model->rectOfIndex(i).y();
    case WidthRole: return m_model->rectOfIndex(i).width();
    case HeightRole: return m_model->rectOfIndex(i).height();
    case BaseWidthRole: return std::ceil(m_model->baseSizeOfIndex(i).width());
    case BaseHeightRole: return std::ceil(m_model->baseSizeOfIndex(i).height());
    case FailedRole: return m_model->failedIndex(i);
    default: return {};
    }
}

QHash<int, QByteArray> PdfVisiblePages::roleNames() const
{
    return {{PageRole, "page"},
            {XRole, "pageX"},
            {YRole, "pageY"},
            {WidthRole, "pageWidth"},
            {HeightRole, "pageHeight"},
            {BaseWidthRole, "baseWidth"},
            {BaseHeightRole, "baseHeight"},
            {FailedRole, "failed"}};
}

void PdfVisiblePages::reset()
{
    setRange(-1, -1);
}

void PdfVisiblePages::setRange(int first, int last)
{
    if (first < 0 || last < first) {
        if (m_first >= 0) {
            beginRemoveRows({}, 0, m_last - m_first);
            m_first = m_last = -1;
            endRemoveRows();
        }
        return;
    }
    if (m_first < 0 || last < m_first || first > m_last) {
        // Disjoint (a jump): all rows anew.
        reset();
        beginInsertRows({}, 0, last - first);
        m_first = first;
        m_last = last;
        endInsertRows();
        return;
    }
    if (first > m_first) {
        beginRemoveRows({}, 0, first - m_first - 1);
        m_first = first;
        endRemoveRows();
    }
    if (last < m_last) {
        beginRemoveRows({}, last - m_first + 1, m_last - m_first);
        m_last = last;
        endRemoveRows();
    }
    if (first < m_first) {
        beginInsertRows({}, 0, m_first - first - 1);
        m_first = first;
        endInsertRows();
    }
    if (last > m_last) {
        beginInsertRows({}, m_last - m_first + 1, last - m_first);
        m_last = last;
        endInsertRows();
    }
}

void PdfVisiblePages::geometryChanged()
{
    if (m_first >= 0)
        Q_EMIT dataChanged(index(0), index(m_last - m_first),
                           {XRole, YRole, WidthRole, HeightRole, BaseWidthRole, BaseHeightRole});
}

void PdfVisiblePages::pageChanged(int i)
{
    if (m_first >= 0 && i >= m_first && i <= m_last)
        Q_EMIT dataChanged(index(i - m_first), index(i - m_first), {FailedRole});
}

// ---- PdfViewModel ---------------------------------------------------------------------------

struct PdfViewModel::SizeJob {
    QString file;
    QString version;
    int laid = 0;
    int generation = 0;
    std::shared_ptr<std::atomic_bool> cancel;
    std::shared_ptr<bool> alive;
    QPointer<PdfViewModel> self;
};


PdfViewModel::PdfViewModel(QObject *parent) : QObject(parent), m_settleTimer(this), m_searchTimer(this)
{
    m_pages = new PdfVisiblePages(this);
    m_searchAvailable = platform::pdfTextAvailable();
    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(SettleMs);
    connect(&m_settleTimer, &QTimer::timeout, this, &PdfViewModel::settle);
    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(SearchDebounceMs);
    connect(&m_searchTimer, &QTimer::timeout, this, &PdfViewModel::startSearch);
    connect(&PdfReaderPool::instance(), &PdfReaderPool::pageFailed, this, &PdfViewModel::onPageFailed);
    m_layout.setGeometry(m_viewportWidth, m_margin, m_gap, m_maxFitWidth);
}

PdfViewModel::~PdfViewModel()
{
    *m_alive = false;
    if (m_openCancel)
        m_openCancel->store(true);
    if (m_searchCancel)
        m_searchCancel->store(true);
}

QAbstractListModel *PdfViewModel::pages() const
{
    return m_pages;
}

void PdfViewModel::setFile(const QString &file)
{
    if (file == m_file)
        return;
    m_file = file;
    // Another file starts at its top, fitted.
    m_keepPage = 0;
    m_keepScale = 0;
    m_keepFit = true;
    Q_EMIT sourceChanged();
    scheduleReload();
}

void PdfViewModel::setVersion(const QString &version)
{
    if (version == m_version)
        return;
    m_version = version;
    Q_EMIT sourceChanged();
    // The same file changed (sync, another program): opened again at the same page and zoom.
    if (m_status == Status::Ready && m_currentPage > 0) {
        const QRectF r = m_layout.rect(m_currentPage - 1);
        m_keepPage = m_currentPage;
        m_keepFraction = r.height() > 0 ? std::clamp((m_contentY - r.y()) / r.height(), 0.0, 1.0) : 0;
        m_keepScale = m_layout.scale();
        m_keepFit = m_fitWidth;
    }
    scheduleReload();
}

void PdfViewModel::scheduleReload()
{
    if (m_reloadQueued)
        return;
    m_reloadQueued = true;
    // (One open for a file and a version set one after the other by QML's bindings.)
    QMetaObject::invokeMethod(this, [this] {
        m_reloadQueued = false;
        open();
    }, Qt::QueuedConnection);
}

void PdfViewModel::reload()
{
    if (m_status == Status::Ready && m_currentPage > 0) {
        const QRectF r = m_layout.rect(m_currentPage - 1);
        m_keepPage = m_currentPage;
        m_keepFraction = r.height() > 0 ? std::clamp((m_contentY - r.y()) / r.height(), 0.0, 1.0) : 0;
        m_keepScale = m_layout.scale();
        m_keepFit = m_fitWidth;
    }
    open();
}

void PdfViewModel::setStatus(Status status)
{
    m_status = status;
    Q_EMIT statusChanged();
}

void PdfViewModel::fail(const QString &message, bool locked)
{
    m_error = message;
    m_locked = locked;
    m_layout.setPages({});
    m_totalPages = 0;
    m_pages->reset();
    setStatus(Status::Failed);
    Q_EMIT layoutChanged();
    updateCurrentPage();
}

void PdfViewModel::open()
{
    ++m_generation;
    if (m_openCancel)
        m_openCancel->store(true);
    m_openCancel = std::make_shared<std::atomic_bool>(false);
    cancelSearch();
    m_failed.clear();
    m_error.clear();
    m_locked = false;
    if (m_file.isEmpty()) {
        m_layout.setPages({});
        m_pages->reset();
        setStatus(Status::Loading);
        Q_EMIT layoutChanged();
        return;
    }
    if (!platform::pdfAvailable()) {
        m_layout.setPages({});
        m_pages->reset();
        setStatus(Status::Unsupported);
        Q_EMIT layoutChanged();
        return;
    }
    if (!QFileInfo(m_file).isFile()) {
        fail(tr("The file is no longer in the notebook."));
        return;
    }
    setStatus(Status::Loading);
    const int generation = m_generation;
    const QString file = m_file;
    const QString version = m_version;
    const std::shared_ptr<std::atomic_bool> cancel = m_openCancel;
    const std::shared_ptr<bool> alive = m_alive;
    const QPointer<PdfViewModel> self(this);
    PdfRenderPool::start(
            [=] {
                if (cancel->load())
                    return;
                platform::PdfInfo info;
                PdfReaderPool::Lease lease = PdfReaderPool::instance().acquire(file, version, &info);
                if (!lease) {
                    const bool locked = info.locked;
                    const bool empty = info.pages == 0 || info.error.contains(u"no pages"_s);
                    post(alive, [self, generation, locked, empty] {
                        if (!self || generation != self->m_generation)
                            return;
                        self->fail(locked  ? tr("This PDF is protected with a password: Brulion cannot open it.")
                                   : empty ? tr("This PDF has no pages.")
                                           : tr("This PDF cannot be read."),
                                   locked);
                    });
                    return;
                }
                const int count = lease->pageCount();
                const QSizeF first = lease->pageSize(1);
                post(alive, [self, generation, count, first] {
                    if (!self || generation != self->m_generation)
                        return;
                    if (count <= 0) {
                        self->fail(tr("This PDF has no pages."));
                        return;
                    }
                    PdfViewModel &m = *self;
                    m.m_totalPages = count;
                    m.m_layout.setPages(QList<QSizeF>(std::min(count, MaxViewerPages), first));
                    if (m.m_keepScale > 0 && !m.m_keepFit) {
                        m.m_fitWidth = false;
                        m.m_layout.setScale(m.m_keepScale);
                    } else {
                        m.m_fitWidth = true;
                    }
                    m.setStatus(Status::Ready);
                    m.applyGeometry();
                    const QPointF at = m.m_keepPage > 0 ? m.positionForPage(m.m_keepPage, m.m_keepFraction) : QPointF();
                    m.m_contentX = at.x();
                    m.m_contentY = at.y();
                    m.updateVisible();
                    m.updateCurrentPage();
                    Q_EMIT m.moveViewport(at.x(), at.y());
                    if (!m.m_searchText.trimmed().isEmpty())
                        m.startSearch();
                });
                // The real sizes, a chunk per task (pages drawn meanwhile go first: higher
                // priority). Qt PDF and poppler: microseconds a page; Android: ~1 ms.
                lease = {};
                auto job = std::make_shared<SizeJob>();
                job->file = file;
                job->version = version;
                job->laid = std::min(count, MaxViewerPages);
                job->generation = generation;
                job->cancel = cancel;
                job->alive = alive;
                job->self = self;
                readSizes(job, 2);
            },
            PdfRenderPool::Sizes);
}


void PdfViewModel::readSizes(const std::shared_ptr<SizeJob> &job, int from)
{
    if (job->cancel->load() || from > job->laid)
        return;
    QList<QSizeF> sizes;
    const int to = std::min(job->laid, from + SizeChunk - 1);
    {
        PdfReaderPool::Lease reader = PdfReaderPool::instance().acquire(job->file, job->version);
        if (!reader)
            return;
        for (int page = from; page <= to && !job->cancel->load(); ++page)
            sizes.append(reader->pageSize(page));
    }
    post(job->alive, [self = job->self, generation = job->generation, from, sizes] {
        if (self)
            self->sizesArrived(generation, from, sizes);
    });
    if (to < job->laid)
        PdfRenderPool::start([job, to] { readSizes(job, to + 1); }, PdfRenderPool::Sizes);
}

void PdfViewModel::sizesArrived(int generation, int from, const QList<QSizeF> &sizes)
{
    if (generation != m_generation || m_status != Status::Ready)
        return;
    // The current position anchored: a uniform PDF never moves, a mixed one keeps its page.
    const PdfPageLayout::Anchor anchor = m_layout.anchorAt(QPointF(m_contentX + m_viewportWidth / 2, m_contentY));
    const qreal before = m_layout.scale();
    if (!m_layout.setPageSizes(from - 1, sizes))
        return;
    if (m_fitWidth)
        m_layout.setScale(m_layout.fitWidthScale());
    const QPointF point = m_layout.pointOf(anchor);
    const QPointF at = clampedPosition(QPointF(point.x() - m_viewportWidth / 2, point.y()));
    Q_EMIT layoutChanged();
    m_pages->geometryChanged();
    if (std::abs(at.x() - m_contentX) > 0.5 || std::abs(at.y() - m_contentY) > 0.5) {
        m_contentX = at.x();
        m_contentY = at.y();
        Q_EMIT moveViewport(at.x(), at.y());
    }
    updateVisible();
    updateCurrentPage();
    if (before != m_layout.scale())
        m_settleTimer.start();
}

void PdfViewModel::setViewportWidth(qreal width)
{
    if (qFuzzyCompare(width + 1, m_viewportWidth + 1))
        return;
    m_viewportWidth = std::max<qreal>(0, width);
    Q_EMIT geometryInputChanged();
    applyGeometry();
}

void PdfViewModel::setViewportHeight(qreal height)
{
    if (qFuzzyCompare(height + 1, m_viewportHeight + 1))
        return;
    m_viewportHeight = std::max<qreal>(0, height);
    Q_EMIT geometryInputChanged();
    updateVisible();
    updateCurrentPage();
    m_pages->geometryChanged(); // (the base pictures' cap follows the viewport)
}

void PdfViewModel::setDevicePixelRatio(qreal ratio)
{
    if (!(ratio > 0) || qFuzzyCompare(ratio, m_dpr))
        return;
    m_dpr = ratio;
    Q_EMIT geometryInputChanged();
    m_pages->geometryChanged();
}

void PdfViewModel::setMargin(qreal margin)
{
    if (qFuzzyCompare(margin + 1, m_margin + 1))
        return;
    m_margin = margin;
    Q_EMIT geometryInputChanged();
    applyGeometry();
}

void PdfViewModel::setGap(qreal gap)
{
    if (qFuzzyCompare(gap + 1, m_gap + 1))
        return;
    m_gap = gap;
    Q_EMIT geometryInputChanged();
    applyGeometry();
}

void PdfViewModel::setMaxFitWidth(qreal width)
{
    if (qFuzzyCompare(width + 1, m_maxFitWidth + 1))
        return;
    m_maxFitWidth = width;
    Q_EMIT geometryInputChanged();
    applyGeometry();
}

void PdfViewModel::applyGeometry()
{
    const PdfPageLayout::Anchor anchor = m_layout.anchorAt(QPointF(m_contentX, m_contentY));
    m_layout.setGeometry(m_viewportWidth, m_margin, m_gap, m_maxFitWidth);
    if (m_fitWidth)
        m_layout.setScale(m_layout.fitWidthScale());
    Q_EMIT layoutChanged();
    m_pages->geometryChanged();
    if (m_status == Status::Ready && anchor.index >= 0 && m_fitWidth) {
        // Fit width follows the view's width; the page at the top stays at the top.
        const QPointF point = m_layout.pointOf(PdfPageLayout::Anchor{anchor.index, 0, anchor.fy});
        const QPointF at = clampedPosition(QPointF(0, point.y()));
        if (std::abs(at.y() - m_contentY) > 0.5 || std::abs(at.x() - m_contentX) > 0.5) {
            m_contentX = at.x();
            m_contentY = at.y();
            Q_EMIT moveViewport(at.x(), at.y());
        }
    }
    updateVisible();
    updateCurrentPage();
}

int PdfViewModel::zoomPercent() const
{
    return int(std::lround(m_layout.scale() / PdfPageLayout::PointsToLogical * 100));
}

qreal PdfViewModel::contentWidth() const
{
    return m_layout.contentSize().width();
}

qreal PdfViewModel::contentHeight() const
{
    return m_layout.contentSize().height();
}

QSizeF PdfViewModel::baseSizeOfIndex(int index) const
{
    const QRectF r = m_layout.rect(index);
    if (r.isEmpty())
        return {};
    const qreal viewport = m_viewportWidth * m_viewportHeight;
    const qreal page = r.width() * r.height();
    // At most a screenful of pixels (the DPR cancels out: both sides are × dpr²).
    const qreal k = viewport > 0 ? std::min(1.0, std::sqrt(viewport / page)) : 1.0;
    return QSizeF(std::max(1.0, r.width() * k), std::max(1.0, r.height() * k));
}

QPointF PdfViewModel::clampedPosition(QPointF content) const
{
    const QSizeF size = m_layout.contentSize();
    const qreal maxX = std::max<qreal>(0, size.width() - m_viewportWidth);
    const qreal maxY = std::max<qreal>(0, size.height() - m_viewportHeight);
    return QPointF(std::clamp(content.x(), 0.0, maxX), std::clamp(content.y(), 0.0, maxY));
}

void PdfViewModel::setViewport(qreal contentX, qreal contentY)
{
    if (!std::isfinite(contentX) || !std::isfinite(contentY))
        return;
    m_contentX = contentX;
    m_contentY = contentY;
    updateVisible();
    updateCurrentPage();
    m_settleTimer.start();
}

void PdfViewModel::updateVisible()
{
    if (m_status != Status::Ready || m_layout.count() == 0 || m_viewportHeight <= 0) {
        m_pages->reset();
        return;
    }
    const auto [first, last] = m_layout.range(m_contentY - m_viewportHeight / 2, m_contentY + m_viewportHeight * 1.5);
    m_pages->setRange(first, last);
}

void PdfViewModel::updateCurrentPage()
{
    const int index = m_status == Status::Ready ? m_layout.indexAt(m_contentY + 0.4 * m_viewportHeight) : -1;
    const int page = index + 1;
    if (page == m_currentPage)
        return;
    m_currentPage = page;
    Q_EMIT currentPageChanged();
}

void PdfViewModel::settle()
{
    m_settleTimer.stop();
    m_settledScale = m_layout.scale();
    m_settledRect = QRectF(m_contentX, m_contentY, m_viewportWidth, m_viewportHeight);
    ++m_detailRevision;
    Q_EMIT detailChanged();
}

QRectF PdfViewModel::pageRect(int page) const
{
    return m_layout.rect(page - 1);
}

QPointF PdfViewModel::positionForPage(int page, qreal fraction) const
{
    const int index = std::clamp(page, 1, std::max(1, m_layout.count())) - 1;
    const QRectF r = m_layout.rect(index);
    if (r.isNull())
        return {};
    const qreal top = fraction > 0 ? r.y() + fraction * r.height() : r.y() - std::min(m_gap / 2, r.y());
    return clampedPosition(QPointF(m_contentX, top));
}

QPointF PdfViewModel::applyScale(qreal scale, QPointF viewportPoint, bool fit)
{
    if (m_layout.count() == 0) {
        m_fitWidth = fit;
        m_layout.setScale(fit ? m_layout.fitWidthScale() : scale);
        Q_EMIT layoutChanged();
        return {};
    }
    const QPointF contentPoint(m_contentX + viewportPoint.x(), m_contentY + viewportPoint.y());
    const PdfPageLayout::Anchor anchor = m_layout.anchorAt(contentPoint);
    m_fitWidth = fit;
    m_layout.setScale(fit ? m_layout.fitWidthScale() : scale);
    const QPointF point = m_layout.pointOf(anchor);
    const QPointF at = clampedPosition(point - viewportPoint);
    m_contentX = at.x();
    m_contentY = at.y();
    Q_EMIT layoutChanged();
    m_pages->geometryChanged();
    updateVisible();
    updateCurrentPage();
    m_settleTimer.start();
    return at;
}

QPointF PdfViewModel::zoomAround(qreal factor, QPointF viewportPoint)
{
    if (!(factor > 0) || !std::isfinite(factor))
        return QPointF(m_contentX, m_contentY);
    return applyScale(m_layout.scale() * factor, viewportPoint, false);
}

QPointF PdfViewModel::setScaleAround(qreal scale, QPointF viewportPoint)
{
    if (!(scale > 0) || !std::isfinite(scale))
        return QPointF(m_contentX, m_contentY);
    return applyScale(scale, viewportPoint, false);
}

QPointF PdfViewModel::fitToWidth(QPointF viewportPoint)
{
    return applyScale(m_layout.fitWidthScale(), viewportPoint, true);
}

QUrl PdfViewModel::pageUrl(int page) const
{
    const QRectF r = m_layout.rect(page - 1);
    if (r.isEmpty() || m_file.isEmpty())
        return {};
    return QUrl(u"image://pdfview/"_s + QString::fromLatin1(QUrl::toPercentEncoding(m_file)) + u"?page="_s
                        + QString::number(page) + u"&v="_s + QString::fromLatin1(QUrl::toPercentEncoding(m_version))
                        + u"&w="_s + number(r.width()) + u"&h="_s + number(r.height()),
                QUrl::StrictMode);
}

QVariantMap PdfViewModel::detail(int page) const
{
    const int index = page - 1;
    const QRectF r = m_layout.rect(index);
    if (r.isEmpty() || m_status != Status::Ready || std::abs(m_settledScale - m_layout.scale()) > 1e-9)
        return {};
    if (baseSizeOfIndex(index).width() >= 0.98 * r.width())
        return {}; // the base picture has the page's resolution
    const QRectF visible = m_settledRect.intersected(r);
    if (visible.isEmpty())
        return {};
    // The visible part and 64 logical pixels around it, snapped to 64 (small moves keep the tile).
    constexpr qreal Snap = 64;
    const qreal x0 = std::max(0.0, std::floor((visible.left() - r.x() - Snap) / Snap) * Snap);
    const qreal y0 = std::max(0.0, std::floor((visible.top() - r.y() - Snap) / Snap) * Snap);
    const qreal x1 = std::min(r.width(), std::ceil((visible.right() - r.x() + Snap) / Snap) * Snap);
    const qreal y1 = std::min(r.height(), std::ceil((visible.bottom() - r.y() + Snap) / Snap) * Snap);
    if (x1 <= x0 || y1 <= y0)
        return {};
    QUrl url = pageUrl(page);
    url.setQuery(url.query(QUrl::FullyEncoded) + u"&clip="_s + number(x0) + u',' + number(y0) + u',' + number(x1 - x0)
                         + u',' + number(y1 - y0),
                 QUrl::StrictMode);
    return {{u"url"_s, url}, {u"x"_s, x0}, {u"y"_s, y0}, {u"width"_s, x1 - x0}, {u"height"_s, y1 - y0}};
}

// ---- Search ---------------------------------------------------------------------------------

void PdfViewModel::setSearchAvailable(bool available)
{
    if (available == m_searchAvailable)
        return;
    m_searchAvailable = available;
    Q_EMIT searchAvailableChanged();
    if (!available)
        clearSearch();
}

void PdfViewModel::setSearchText(const QString &text)
{
    if (text == m_searchText)
        return;
    m_searchText = text;
    if (!m_searchAvailable) {
        Q_EMIT searchChanged();
        return;
    }
    // The running search stops at once (its batches are dropped); the hits shown stay until the new
    // text's first ones, 250 ms later.
    cancelSearch();
    if (text.trimmed().isEmpty()) {
        startSearch();
        return;
    }
    m_searching = true;
    Q_EMIT searchChanged();
    m_searchTimer.start();
}

void PdfViewModel::clearSearch()
{
    m_searchTimer.stop();
    const bool changed = !m_searchText.isEmpty();
    m_searchText.clear();
    startSearch();
    if (changed)
        Q_EMIT searchChanged();
}

void PdfViewModel::cancelSearch()
{
    if (m_searchCancel)
        m_searchCancel->store(true);
    m_searchCancel.reset();
    ++m_searchJob;
    m_searching = false;
}

void PdfViewModel::startSearch()
{
    m_searchTimer.stop();
    cancelSearch();
    m_hits.clear();
    m_currentHit = -1;
    m_hitsCapped = false;
    m_textless = false;
    m_searchedPages = 0;
    ++m_hitRevision;
    const QString query = m_searchText.trimmed();
    m_searchedText = query;
    if (!m_searchAvailable || query.isEmpty() || m_status != Status::Ready || m_layout.count() == 0) {
        Q_EMIT searchChanged();
        return;
    }
    m_searching = true;
    const int job = m_searchJob;
    m_searchCancel = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancel = m_searchCancel;
    const std::shared_ptr<bool> alive = m_alive;
    const QPointer<PdfViewModel> self(this);
    const QString file = m_file;
    const QString version = m_version;
    const int count = m_layout.count();
    const int start = std::clamp(m_currentPage, 1, count);
    Q_EMIT searchChanged();
    PdfRenderPool::start(
            [=] {
                if (cancel->load())
                    return;
                const auto send = [&](const QList<Hit> &hits, int searched, bool sawText, bool done, bool capped) {
                    post(alive, [self, job, hits, searched, sawText, done, capped] {
                        if (self)
                            self->searchBatch(job, hits, searched, sawText, done, capped);
                    });
                };
                PdfReaderPool::Lease lease = PdfReaderPool::instance().acquire(file, version);
                if (!lease) {
                    send({}, 0, false, true, false);
                    return;
                }
                QList<Hit> batch;
                int total = 0;
                int searched = 0;
                int sinceSent = 0;
                bool sawText = false;
                QElapsedTimer timer;
                timer.start();
                for (int k = 0; k < count; ++k) {
                    if (cancel->load())
                        return;
                    const int page = (start - 1 + k) % count + 1;
                    if (!lease->pageText(page).trimmed().isEmpty())
                        sawText = true;
                    const QSizeF points = lease->pageSize(page);
                    const int room = std::min(platform::MaxPdfHitsPerPage, MaxHits - total);
                    for (const platform::PdfTextHit &h : lease->find(page, query, room)) {
                        Hit hit;
                        hit.page = page;
                        hit.start = h.start;
                        for (const QRectF &r : h.rects) {
                            if (points.width() > 0 && points.height() > 0)
                                hit.rects.append(QRectF(r.x() / points.width(), r.y() / points.height(),
                                                        r.width() / points.width(), r.height() / points.height()));
                        }
                        batch.append(hit);
                        ++total;
                    }
                    ++searched;
                    ++sinceSent;
                    const bool capped = total >= MaxHits;
                    const bool done = capped || k == count - 1;
                    if (done || sinceSent >= 8 || timer.elapsed() >= 100) {
                        send(batch, searched, sawText, done, capped);
                        batch.clear();
                        sinceSent = 0;
                        timer.restart();
                    }
                    if (capped)
                        return;
                }
            },
            PdfRenderPool::Search);
}

void PdfViewModel::searchBatch(int job, const QList<Hit> &hits, int searched, bool sawText, bool done, bool capped)
{
    if (job != m_searchJob)
        return; // an older text's
    for (const Hit &hit : hits) {
        const auto at = std::upper_bound(m_hits.begin(), m_hits.end(), hit, [](const Hit &a, const Hit &b) {
            return a.page != b.page ? a.page < b.page : a.start < b.start;
        });
        m_hits.insert(at, hit);
    }
    m_searchedPages = searched;
    if (done) {
        m_searching = false;
        m_hitsCapped = capped;
        m_textless = !sawText && searched >= m_layout.count();
        m_searchCancel.reset();
    }
    bool show = false;
    if (m_currentHit < 0 && !m_hits.isEmpty()) {
        // The first hit at or after the page the search started from.
        int chosen = 0;
        for (qsizetype i = 0; i < m_hits.size(); ++i) {
            if (m_hits.at(i).page >= m_currentPage) {
                chosen = int(i);
                break;
            }
        }
        m_currentHitPage = m_hits.at(chosen).page;
        m_currentHitStart = m_hits.at(chosen).start;
        show = true;
    }
    if (!m_hits.isEmpty()) {
        // The current hit kept (earlier pages' hits arrive after the search wrapped).
        m_currentHit = 0;
        for (qsizetype i = 0; i < m_hits.size(); ++i) {
            if (m_hits.at(i).page == m_currentHitPage && m_hits.at(i).start == m_currentHitStart) {
                m_currentHit = int(i);
                break;
            }
        }
    }
    ++m_hitRevision;
    Q_EMIT searchChanged();
    if (show) {
        const QPointF at = positionForHit(m_currentHit);
        if (std::isfinite(at.x())) {
            m_contentX = at.x();
            m_contentY = at.y();
            updateVisible();
            updateCurrentPage();
            Q_EMIT moveViewport(at.x(), at.y());
        }
    }
}

QVariantList PdfViewModel::hitRects(int page) const
{
    QVariantList out;
    Hit probe;
    probe.page = page;
    probe.start = -1;
    auto it = std::lower_bound(m_hits.cbegin(), m_hits.cend(), probe, [](const Hit &a, const Hit &b) {
        return a.page != b.page ? a.page < b.page : a.start < b.start;
    });
    for (; it != m_hits.cend() && it->page == page; ++it) {
        const bool current = int(it - m_hits.cbegin()) == m_currentHit;
        for (const QRectF &r : it->rects)
            out.append(QVariantMap{{u"x"_s, r.x()},
                                   {u"y"_s, r.y()},
                                   {u"width"_s, r.width()},
                                   {u"height"_s, r.height()},
                                   {u"current"_s, current}});
    }
    return out;
}

QPointF PdfViewModel::positionForHit(int index) const
{
    if (index < 0 || index >= m_hits.size())
        return QPointF(NaN, NaN);
    const Hit &hit = m_hits.at(index);
    const QRectF page = m_layout.rect(hit.page - 1);
    if (page.isNull())
        return QPointF(NaN, NaN);
    QRectF bounds;
    for (const QRectF &r : hit.rects)
        bounds = bounds.united(QRectF(page.x() + r.x() * page.width(), page.y() + r.y() * page.height(),
                                      r.width() * page.width(), r.height() * page.height()));
    if (bounds.isNull())
        bounds = QRectF(page.x(), page.y(), page.width(), 1);
    const qreal x = m_layout.contentSize().width() > m_viewportWidth ? bounds.center().x() - m_viewportWidth / 2 : 0;
    return clampedPosition(QPointF(x, bounds.top() - m_viewportHeight * 0.3));
}

QPointF PdfViewModel::nextHit()
{
    if (m_hits.isEmpty())
        return QPointF(NaN, NaN);
    m_currentHit = (m_currentHit + 1) % int(m_hits.size());
    m_currentHitPage = m_hits.at(m_currentHit).page;
    m_currentHitStart = m_hits.at(m_currentHit).start;
    ++m_hitRevision;
    Q_EMIT searchChanged();
    return positionForHit(m_currentHit);
}

QPointF PdfViewModel::previousHit()
{
    if (m_hits.isEmpty())
        return QPointF(NaN, NaN);
    m_currentHit = m_currentHit <= 0 ? int(m_hits.size()) - 1 : m_currentHit - 1;
    m_currentHitPage = m_hits.at(m_currentHit).page;
    m_currentHitStart = m_hits.at(m_currentHit).start;
    ++m_hitRevision;
    Q_EMIT searchChanged();
    return positionForHit(m_currentHit);
}

void PdfViewModel::onPageFailed(const QString &file, const QString &version, int page)
{
    if (file != m_file || version != m_version || page < 1)
        return;
    m_failed.insert(page - 1);
    m_pages->pageChanged(page - 1);
}

} // namespace brulion::pdfview
