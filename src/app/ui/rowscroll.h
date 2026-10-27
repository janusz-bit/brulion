// RowScroll — the scroll bar geometry of a long ListView whose rows differ a lot in height (the
// block editor, the journal stream, Tasks, the logs, the history), from *which row* is at the
// view's edges instead of from pixels. docs/UI.md "The scroll bar: RowScroll"; tests:
// tests/app/scroll.
//
//   BRowScrollBar { list: list }      // src/app/qml/components/BRowScrollBar.qml uses it
//
// Why: Qt's attached scroll bar takes its position and size from Flickable's visibleArea, i.e.
// from contentY, originY and contentHeight — which ListView estimates from the mean height of the
// rows it has created at the moment. As rows are created and destroyed while the list scrolls (a
// journal day of 40 blocks next to one of 1), the estimate changes and the thumb jumps, back and
// forth, although the content moves smoothly.
//
// Here the progress is t = above / (above + below), counted in rows: `above` the rows (and the
// fraction of a row) above the view's top edge, `below` those under its bottom edge; the header and
// the footer count as one row each. Creating rows off screen or moving originY changes neither the
// row at an edge nor where in it the edge is (ListView keeps the visible content in place), so the
// thumb moves only with the content, or when the row count changes. Its speed follows rows, not
// pixels: a tall day takes longer to cross than a short one.
//
// The thumb's size still comes from ListView's estimate (height / content height), but only while
// the list stands still and the bar is not held: it never "breathes" while scrolling (the bar is
// faded out when it changes). position = t * (1 - size) keeps the thumb in the track and reaching
// both ends exactly. Dragging the bar (scrollTo) inverts it: the row for the progress is put at
// the view's top edge (positionViewAtIndex), at most once a frame.
//
// Reads the ListView through its public QML API only (properties, indexAt, itemAtIndex,
// positionViewAt*): no private Qt headers. Without a row at an edge (in the middle of a relayout,
// an empty list) it falls back to Qt's estimate.
#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <optional>

namespace brulion::ui {

/// One created row, in content coordinates: from its top to the next row's top (spacing and the
/// next row's section header belong to it).
struct RowSpan {
    int index = -1;
    qreal top = 0;
    qreal bottom = 0;
};

/// What the view shows: the rows around its edges and the edges themselves (content coordinates).
struct RowEdges {
    int count = 0;
    bool hasHeader = false;
    bool hasFooter = false;
    qreal headerTop = 0, headerBottom = 0;
    qreal footerTop = 0, footerBottom = 0;
    /// Created rows, sorted by index (at least the ones at the view's edges).
    QList<RowSpan> rows;
    /// contentY + topMargin, contentY + height - bottomMargin.
    qreal viewTop = 0, viewBottom = 0;
    bool atBeginning = false;
    bool atEnd = false;

    /// Rows plus the header and the footer (one unit each).
    int units() const { return count + (hasHeader ? 1 : 0) + (hasFooter ? 1 : 0); }
};

/// The row-unit arithmetic of RowScroll. Pure.
class RowFraction {
public:
    /// Where to put the view's top edge: `row` -1 is the header, `count` the footer; `fraction`
    /// of that row's span (0 = its top).
    struct Target {
        int row = 0;
        qreal fraction = 0;
    };

    /// The edges are usable: rows sorted, finite, positive heights, a view of positive height.
    static bool valid(const RowEdges &edges);
    /// The position of `y` in row units: the header is unit 0 (when there is one), row i unit
    /// i + 1 (or i), the footer the last unit; inside a unit the fraction of its span. Between two
    /// spans of consecutive units (a section header above the first row, a gap before the footer)
    /// the boundary; above the first unit 0, below the last one units(). nullopt without the span
    /// that holds `y`, or with unusable edges.
    static std::optional<qreal> unitAt(const RowEdges &edges, qreal y);
    /// The units between the view's edges.
    static std::optional<qreal> visibleUnits(const RowEdges &edges);
    /// Progress t in [0, 1] = above / (above + below): exactly 0 at the beginning, 1 at the end.
    /// nullopt for an empty list, without a row at an edge or with unusable edges.
    static std::optional<qreal> progress(const RowEdges &edges);
    /// The inverse, for dragging: the unit at the view's top edge for progress `t` with
    /// `visibleUnits` units in view is t * (units - visibleUnits).
    static Target target(qreal t, const RowEdges &shape, qreal visibleUnits);
};

/// The QML element (registered as "RowScroll" by qmlforeign.h): `RowScroll { view: list }`.
class RowScroll : public QObject {
    Q_OBJECT

    /// The ListView followed.
    Q_PROPERTY(QQuickItem *view READ view WRITE setView NOTIFY viewChanged FINAL)
    /// The bar is held: the size stays, and the bar's position is not pushed back (no feedback).
    Q_PROPERTY(bool dragging READ dragging WRITE setDragging NOTIFY draggingChanged FINAL)
    /// t in [0, 1] (rows above / rows above and below the view).
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged FINAL)
    /// For the bar: progress * (1 - size).
    Q_PROPERTY(qreal position READ position NOTIFY positionChanged FINAL)
    /// For the bar: view height / content height (ListView's estimate), frozen while the list
    /// moves or the bar is held; 1 when everything fits.
    Q_PROPERTY(qreal size READ size NOTIFY sizeChanged FINAL)
    /// The content is taller than the view (size < 1).
    Q_PROPERTY(bool scrollable READ scrollable NOTIFY sizeChanged FINAL)
    /// The latest progress is Qt's estimate (no row at an edge, an empty list): diagnostics, tests.
    Q_PROPERTY(bool estimated READ estimated NOTIFY progressChanged FINAL)

public:
    explicit RowScroll(QObject *parent = nullptr);
    ~RowScroll() override;

    /// The shortest time between two moves of the view by scrollTo (one a frame).
    static constexpr int ScrollIntervalMs = 16;

    QQuickItem *view() const { return m_view; }
    void setView(QQuickItem *view);
    bool dragging() const { return m_dragging; }
    void setDragging(bool dragging);
    qreal progress() const { return m_progress; }
    qreal position() const { return m_position; }
    qreal size() const { return m_size; }
    bool scrollable() const { return m_size < 1.0 - 1e-9; }
    bool estimated() const { return m_estimated; }

    /// Moves the view so that the bar's `position` (0 … 1 - size) is shown: right away, or a
    /// frame after the previous move (the latest position wins).
    Q_INVOKABLE void scrollTo(qreal position);
    /// Recomputes now (otherwise after the view's changes, coalesced: in the window's next frame,
    /// after its polish — or a 0 ms timer when no frame comes).
    Q_INVOKABLE void refresh();
    /// The view's current edges (diagnostics, tests).
    RowEdges edges() const;

Q_SIGNALS:
    void viewChanged();
    void draggingChanged();
    void progressChanged();
    void positionChanged();
    void sizeChanged();

private Q_SLOTS:
    void schedule();
    void rebindModel();
    void attachWindow(QQuickWindow *window);

private:
    void connectView();
    void bindModel();
    void applyScroll();
    void place(const RowFraction::Target &target, const RowEdges &shape);
    /// The created row whose span holds y (-1: none).
    int rowAt(qreal y, qreal viewTop, qreal viewBottom) const;
    /// The span of a created row, its index in units; nullopt for a hidden (zero-height) row.
    std::optional<RowSpan> span(int row, int count) const;
    QQuickItem *itemAt(int index) const;
    int unitCount(int count) const;
    int unitOfRow(int row) const;
    int rowOfUnit(int unit) const;
    qreal real(const char *name) const;

    QPointer<QQuickItem> m_view;
    QList<QMetaObject::Connection> m_connections;
    // The view's model when it hides rows (zero-height delegates: the editor's folded blocks) and
    // says which — `visibleCount`, `visibleRowsBefore(int)`, `visibleRowAt(int)`: then its shown
    // rows are the units, and a fold of many blocks is not a jump of many rows.
    QPointer<QObject> m_model;
    QMetaObject::Connection m_modelConnection;
    QMetaObject::Connection m_windowConnection;
    bool m_hidden = false;
    QTimer m_refreshTimer;
    QTimer m_scrollTimer;
    QElapsedTimer m_lastScroll;
    std::optional<qreal> m_pendingScroll;
    mutable int m_lastRow = -1;
    bool m_dragging = false;
    bool m_hasSize = false;
    bool m_estimated = true;
    qreal m_progress = 0;
    qreal m_position = 0;
    qreal m_size = 1;
};

} // namespace brulion::ui
