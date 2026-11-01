// PdfViewModel — the PDF viewer's state (QML type `PdfViewModel` through pdfqml.h; qml/content/ContentPdfView.qml;
// docs/UI.md "PDF viewer"): the file opened off the GUI thread, the pages laid out exactly
// (PdfPageLayout), the pages near the viewport as a list model (PdfVisiblePages: rows inserted and
// removed at its edges, so the QML Repeater keeps its delegates and their pictures), zoom anchored
// at a point, the page under 40 % of the view's height, the pictures' URLs (PdfViewImageProvider)
// with a memory cap, and the search.
//
// Memory (docs/PERFORMANCE.md "PDF viewer"): a page's base picture is drawn at
// k = min(1, sqrt(viewport pixels / page pixels)) of its shown size (baseWidth/baseHeight), so it
// never has more than a screenful of pixels; zoomed in (k < 0.98) detail() names the visible part of
// the page at full resolution, after the view settled (150 ms without movement, or settle()). With
// the pages within half a viewport above and below, ≈ 4 × the viewport's pixels at any zoom.
//
// Search: setSearchText() (250 ms later) starts a job on PdfRenderPool at the lowest priority from
// the current page, wrapping, page by page (PdfReader::find: case- and diacritics-insensitive), the
// hits posted in batches; a new text cancels it between pages. The first hit at or after the current
// page becomes the current one and is shown (moveViewport()). At most MaxHits.
//
// The QML side moves its Flickable where moveViewport() says (sizes arriving, a reload, a hit, a
// zoom of fit width following the window) and tells the model where it is (setViewport()).
#pragma once

#include "pdflayout.h"

#include <QAbstractListModel>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>

namespace brulion::pdfview {

class PdfViewModel;

/// The pages near the viewport: roles page (1-based), pageX, pageY, pageWidth, pageHeight (content
/// coordinates), baseWidth, baseHeight (the base picture's size, logical), failed.
class PdfVisiblePages : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        PageRole = Qt::UserRole + 1,
        XRole,
        YRole,
        WidthRole,
        HeightRole,
        BaseWidthRole,
        BaseHeightRole,
        FailedRole
    };
    explicit PdfVisiblePages(PdfViewModel *model);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Rows for pages [first, last] (0-based; −1: none), changed at the edges where they overlap.
    void setRange(int first, int last);
    /// Every row's geometry changed (zoom, sizes).
    void geometryChanged();
    /// The row of page index `index` changed its failed state.
    void pageChanged(int index);
    void reset();
    int first() const { return m_first; }

private:
    PdfViewModel *m_model;
    int m_first = -1;
    int m_last = -1;
};

class PdfViewModel : public QObject {
    Q_OBJECT

public:
    enum class Status { Loading, Ready, Failed, Unsupported };
    Q_ENUM(Status)

private:
    // in
    Q_PROPERTY(QString file READ file WRITE setFile NOTIFY sourceChanged FINAL)
    Q_PROPERTY(QString version READ version WRITE setVersion NOTIFY sourceChanged FINAL)
    Q_PROPERTY(qreal viewportWidth READ viewportWidth WRITE setViewportWidth NOTIFY geometryInputChanged FINAL)
    Q_PROPERTY(qreal viewportHeight READ viewportHeight WRITE setViewportHeight NOTIFY geometryInputChanged FINAL)
    Q_PROPERTY(qreal devicePixelRatio READ devicePixelRatio WRITE setDevicePixelRatio NOTIFY geometryInputChanged FINAL)
    Q_PROPERTY(qreal margin READ margin WRITE setMargin NOTIFY geometryInputChanged FINAL)
    Q_PROPERTY(qreal gap READ gap WRITE setGap NOTIFY geometryInputChanged FINAL)
    Q_PROPERTY(qreal maxFitWidth READ maxFitWidth WRITE setMaxFitWidth NOTIFY geometryInputChanged FINAL)
    // out
    Q_PROPERTY(Status status READ status NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool locked READ locked NOTIFY statusChanged FINAL)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY layoutChanged FINAL)
    /// The PDF has more pages than are laid out (MaxViewerPages).
    Q_PROPERTY(int totalPages READ totalPages NOTIFY layoutChanged FINAL)
    Q_PROPERTY(qreal scale READ scale NOTIFY layoutChanged FINAL)
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY layoutChanged FINAL)
    Q_PROPERTY(bool fitWidth READ fitWidth NOTIFY layoutChanged FINAL)
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY layoutChanged FINAL)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY layoutChanged FINAL)
    Q_PROPERTY(qreal minScale READ minScale CONSTANT FINAL)
    Q_PROPERTY(qreal maxScale READ maxScale CONSTANT FINAL)
    /// The page under 40 % of the view's height (scrolled to the end: the last one in view).
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged FINAL)
    Q_PROPERTY(QAbstractListModel *pages READ pages CONSTANT FINAL)
    Q_PROPERTY(int detailRevision READ detailRevision NOTIFY detailChanged FINAL)
    // search
    Q_PROPERTY(bool searchAvailable READ searchAvailable NOTIFY searchAvailableChanged FINAL)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchChanged FINAL)
    Q_PROPERTY(int hitCount READ hitCount NOTIFY searchChanged FINAL)
    Q_PROPERTY(int currentHit READ currentHit NOTIFY searchChanged FINAL)
    Q_PROPERTY(bool searching READ searching NOTIFY searchChanged FINAL)
    Q_PROPERTY(int searchedPages READ searchedPages NOTIFY searchChanged FINAL)
    Q_PROPERTY(bool hitsCapped READ hitsCapped NOTIFY searchChanged FINAL)
    Q_PROPERTY(bool textless READ textless NOTIFY searchChanged FINAL)
    Q_PROPERTY(int hitRevision READ hitRevision NOTIFY searchChanged FINAL)

public:
    static constexpr int MaxViewerPages = 20'000;
    static constexpr int MaxHits = 5000;
    static constexpr int SizeChunk = 64;
    static constexpr int SearchDebounceMs = 250;
    static constexpr int SettleMs = 150;

    explicit PdfViewModel(QObject *parent = nullptr);
    ~PdfViewModel() override;

    QString file() const { return m_file; }
    void setFile(const QString &file);
    QString version() const { return m_version; }
    void setVersion(const QString &version);
    qreal viewportWidth() const { return m_viewportWidth; }
    void setViewportWidth(qreal width);
    qreal viewportHeight() const { return m_viewportHeight; }
    void setViewportHeight(qreal height);
    qreal devicePixelRatio() const { return m_dpr; }
    void setDevicePixelRatio(qreal ratio);
    qreal margin() const { return m_margin; }
    void setMargin(qreal margin);
    qreal gap() const { return m_gap; }
    void setGap(qreal gap);
    qreal maxFitWidth() const { return m_maxFitWidth; }
    void setMaxFitWidth(qreal width);

    Status status() const { return m_status; }
    QString error() const { return m_error; }
    bool locked() const { return m_locked; }
    int pageCount() const { return m_layout.count(); }
    int totalPages() const { return m_totalPages; }
    qreal scale() const { return m_layout.scale(); }
    int zoomPercent() const;
    bool fitWidth() const { return m_fitWidth; }
    qreal contentWidth() const;
    qreal contentHeight() const;
    qreal minScale() const { return PdfPageLayout::MinScale; }
    qreal maxScale() const { return PdfPageLayout::MaxScale; }
    int currentPage() const { return m_currentPage; }
    QAbstractListModel *pages() const;
    int detailRevision() const { return m_detailRevision; }

    bool searchAvailable() const { return m_searchAvailable; }
    /// Tests: the search turned off (as where the renderer has no text API).
    void setSearchAvailable(bool available);
    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    int hitCount() const { return int(m_hits.size()); }
    int currentHit() const { return m_currentHit; }
    bool searching() const { return m_searching; }
    int searchedPages() const { return m_searchedPages; }
    bool hitsCapped() const { return m_hitsCapped; }
    bool textless() const { return m_textless; }
    int hitRevision() const { return m_hitRevision; }

    /// The Flickable's position (content coordinates): the visible pages, the current page.
    Q_INVOKABLE void setViewport(qreal contentX, qreal contentY);
    /// The view stopped moving: the detail pictures for this position and zoom (also 150 ms after
    /// the last setViewport() by itself).
    Q_INVOKABLE void settle();
    /// Page `page` (1-based) in content coordinates.
    Q_INVOKABLE QRectF pageRect(int page) const;
    /// The Flickable's contentX/Y showing page `page` from `fraction` of its height.
    Q_INVOKABLE QPointF positionForPage(int page, qreal fraction = 0) const;
    /// Page `page` is the current one while the view stays where it is now (a jump to a page that
    /// cannot reach the view's top — the last ones, small pages — still names it).
    Q_INVOKABLE void pinPage(int page);
    /// Zoom by `factor` keeping the point under `viewportPoint` (in the view) in place; returns the
    /// Flickable's new contentX/Y.
    Q_INVOKABLE QPointF zoomAround(qreal factor, QPointF viewportPoint);
    Q_INVOKABLE QPointF setScaleAround(qreal scale, QPointF viewportPoint);
    /// The widest page as wide as the view (capped by maxFitWidth); it follows the view's width.
    Q_INVOKABLE QPointF fitToWidth(QPointF viewportPoint);
    /// The base picture of page `page`:
    /// image://pdfview/<file>?page=N&v=<version>&w=<logical width>&h=<logical height>.
    Q_INVOKABLE QUrl pageUrl(int page) const;
    /// The detail picture of page `page`: {url, x, y, width, height} (page-local logical), or {}
    /// when the base picture has the page's full resolution or the view has not settled at this zoom.
    Q_INVOKABLE QVariantMap detail(int page) const;
    /// The search hits on page `page`: [{x, y, width, height, current}] in fractions of the page.
    Q_INVOKABLE QVariantList hitRects(int page) const;
    /// The next / previous hit made current; the Flickable's contentX/Y showing it (NaN, NaN: none).
    Q_INVOKABLE QPointF nextHit();
    Q_INVOKABLE QPointF previousHit();
    /// The search cleared (the search bar closed).
    Q_INVOKABLE void clearSearch();
    /// Opened again (the current page and the zoom kept).
    Q_INVOKABLE void reload();

    // For PdfVisiblePages.
    QRectF rectOfIndex(int index) const { return m_layout.rect(index); }
    QSizeF baseSizeOfIndex(int index) const;
    bool failedIndex(int index) const { return m_failed.contains(index); }

Q_SIGNALS:
    void sourceChanged();
    void geometryInputChanged();
    void statusChanged();
    void layoutChanged();
    void currentPageChanged();
    void detailChanged();
    void searchAvailableChanged();
    void searchChanged();
    /// The Flickable should go to contentX/Y (sizes arrived, a reload, a hit, fit width followed the
    /// view's width).
    void moveViewport(qreal contentX, qreal contentY);

private:
    struct Hit {
        int page = 0; ///< 1-based
        int start = 0;
        QList<QRectF> rects; ///< fractions of the page
    };
    struct SizeJob;
    /// Pages [from, from + SizeChunk) of the job's file measured on the pool, then the next chunk
    /// queued (pages drawn meanwhile go first).
    static void readSizes(const std::shared_ptr<SizeJob> &job, int from);

    void scheduleReload();
    void open();
    void fail(const QString &message, bool locked = false);
    void setStatus(Status status);
    void applyGeometry();
    void updateVisible();
    void updateCurrentPage();
    QPointF clampedPosition(QPointF content) const;
    QPointF applyScale(qreal scale, QPointF viewportPoint, bool fit);
    void sizesArrived(int generation, int from, const QList<QSizeF> &sizes);
    void startSearch();
    void cancelSearch();
    void searchBatch(int job, const QList<Hit> &hits, int searched, bool sawText, bool done, bool capped);
    QPointF positionForHit(int index) const;
    void onPageFailed(const QString &file, const QString &version, int page);

    QString m_file;
    QString m_version;
    qreal m_viewportWidth = 0;
    qreal m_viewportHeight = 0;
    qreal m_dpr = 1;
    qreal m_margin = 16;
    qreal m_gap = 12;
    qreal m_maxFitWidth = 1e6;

    Status m_status = Status::Loading;
    QString m_error;
    bool m_locked = false;
    int m_totalPages = 0;
    PdfPageLayout m_layout;
    bool m_fitWidth = true;
    qreal m_contentX = 0;
    qreal m_contentY = 0;
    int m_currentPage = 0;
    int m_pinnedPage = 0;
    qreal m_pinnedY = 0;
    PdfVisiblePages *m_pages = nullptr;
    QSet<int> m_failed;
    int m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_openCancel;
    bool m_reloadQueued = false;
    /// Kept across a reload of the same file (a new version): the page and the zoom.
    int m_keepPage = 0;
    qreal m_keepFraction = 0;
    qreal m_keepScale = 0;
    bool m_keepFit = true;

    QTimer m_settleTimer;
    int m_detailRevision = 0;
    qreal m_settledScale = 0;
    QRectF m_settledRect;

    bool m_searchAvailable = false;
    QString m_searchText;
    QString m_searchedText;
    QTimer m_searchTimer;
    QList<Hit> m_hits;
    int m_currentHit = -1;
    int m_currentHitPage = 0;
    int m_currentHitStart = 0;
    bool m_searching = false;
    int m_searchedPages = 0;
    bool m_hitsCapped = false;
    bool m_textless = false;
    int m_hitRevision = 0;
    int m_searchJob = 0;
    std::shared_ptr<std::atomic_bool> m_searchCancel;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace brulion::pdfview
