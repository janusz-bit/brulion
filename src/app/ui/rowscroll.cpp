#include "rowscroll.h"

#include <QMetaMethod>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVarLengthArray>
#include <QVariant>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace brulion::ui {

namespace {

bool finite(qreal v)
{
    return std::isfinite(v);
}

QMetaProperty propertyOf(const QMetaObject *mo, const char *name)
{
    const int index = mo ? mo->indexOfProperty(name) : -1;
    return index >= 0 ? mo->property(index) : QMetaProperty();
}

QMetaMethod methodOf(const QMetaObject *mo, const char *signature)
{
    const int index = mo ? mo->indexOfMethod(QMetaObject::normalizedSignature(signature).constData()) : -1;
    return index >= 0 ? mo->method(index) : QMetaMethod();
}

/// `sender`'s `signal` connected to `receiver`'s `slot` (normalized signatures), when the sender
/// has that signal (a view or a model of another kind may lack it: no warning then).
QMetaObject::Connection connectIfAny(QObject *sender, const char *signal, QObject *receiver, const char *slot)
{
    const int index = sender->metaObject()->indexOfSignal(signal);
    if (index < 0)
        return {};
    const QMetaObject *mo = receiver->metaObject();
    return QObject::connect(sender, sender->metaObject()->method(index), receiver, mo->method(mo->indexOfSlot(slot)));
}

/// A unit's span in content coordinates (the header, a row, the footer).
struct Segment {
    qreal unit = 0;
    qreal top = 0;
    qreal bottom = 0;
    bool openAbove = false;
    bool openBelow = false;
};

// The header, the rows at the view's edges and their neighbours, the footer: on the stack, as
// refresh() needs them every frame while the list moves.
using Segments = QVarLengthArray<Segment, 8>;

bool changed(qreal a, qreal b)
{
    return std::abs(a - b) > 1e-9;
}

// Rounding of item geometry: spans may overlap by this much.
constexpr qreal Overlap = 0.5;

/// The units' spans in order when the edges are usable (RowFraction::valid), else nullopt.
std::optional<Segments> usableSegments(const RowEdges &e)
{
    if (e.count < 0 || !finite(e.viewTop) || !finite(e.viewBottom) || !(e.viewBottom > e.viewTop))
        return std::nullopt;
    const auto ok = [](qreal top, qreal bottom) { return finite(top) && finite(bottom) && bottom > top; };
    if ((e.hasHeader && !ok(e.headerTop, e.headerBottom)) || (e.hasFooter && !ok(e.footerTop, e.footerBottom)))
        return std::nullopt;
    Segments s;
    const int h = e.hasHeader ? 1 : 0;
    if (e.hasHeader)
        s.append({0, e.headerTop, e.headerBottom});
    int last = -1;
    for (const RowSpan &r : e.rows) {
        if (r.index <= last || r.index >= e.count || !ok(r.top, r.bottom))
            return std::nullopt;
        last = r.index;
        s.append({qreal(h + r.index), r.top, r.bottom, r.openAbove, r.openBelow});
    }
    if (e.hasFooter)
        s.append({qreal(h + e.count), e.footerTop, e.footerBottom});
    for (qsizetype i = 1; i < s.size(); ++i) {
        if (s[i].top < s[i - 1].bottom - Overlap)
            return std::nullopt;
    }
    return s;
}

/// RowFraction::unitAt over the spans of usable edges.
std::optional<qreal> unitIn(const Segments &s, const RowEdges &e, qreal y)
{
    if (!finite(y) || s.isEmpty())
        return std::nullopt;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const Segment &seg = s[i];
        if (y < seg.top) {
            // In the gap before this unit: its top boundary, when nothing can lie between.
            if (i == 0)
                return seg.unit == 0 || seg.openAbove ? std::optional<qreal>(seg.unit) : std::nullopt;
            const Segment &prev = s[i - 1];
            if (prev.unit + 1 == seg.unit || (seg.openAbove && !prev.openBelow))
                return seg.unit;
            if (prev.openBelow && !seg.openAbove)
                return prev.unit + 1;
            return std::nullopt;
        }
        if (y < seg.bottom)
            return seg.unit + (y - seg.top) / (seg.bottom - seg.top);
    }
    const Segment &last = s.back();
    return last.unit + 1 == e.units() || last.openBelow ? std::optional<qreal>(last.unit + 1) : std::nullopt;
}

} // namespace

// ---- RowFraction --------------------------------------------------------------------------------

bool RowFraction::valid(const RowEdges &e)
{
    return usableSegments(e).has_value();
}

std::optional<qreal> RowFraction::unitAt(const RowEdges &e, qreal y)
{
    const std::optional<Segments> s = usableSegments(e);
    return s ? unitIn(*s, e, y) : std::nullopt;
}

std::optional<qreal> RowFraction::visibleUnits(const RowEdges &e)
{
    const std::optional<Segments> s = usableSegments(e);
    if (!s)
        return std::nullopt;
    const std::optional<qreal> top = unitIn(*s, e, e.viewTop);
    const std::optional<qreal> bottom = unitIn(*s, e, e.viewBottom);
    if (!top || !bottom)
        return std::nullopt;
    return std::max<qreal>(0, *bottom - *top);
}

std::optional<qreal> RowFraction::progress(const RowEdges &e)
{
    const std::optional<Segments> s = e.count > 0 ? usableSegments(e) : std::nullopt;
    if (!s)
        return std::nullopt;
    if (e.atBeginning)
        return 0;
    if (e.atEnd)
        return 1;
    const std::optional<qreal> top = unitIn(*s, e, e.viewTop);
    const std::optional<qreal> bottom = unitIn(*s, e, e.viewBottom);
    if (!top || !bottom)
        return std::nullopt;
    const qreal above = std::max<qreal>(0, *top);
    const qreal below = std::max<qreal>(0, e.units() - *bottom);
    if (above + below <= 1e-9)
        return 0;
    return std::clamp<qreal>(above / (above + below), 0, 1);
}

RowFraction::Target RowFraction::target(qreal t, const RowEdges &shape, qreal visibleUnits)
{
    const int units = shape.units();
    t = finite(t) ? std::clamp<qreal>(t, 0, 1) : 0;
    const qreal v = finite(visibleUnits) ? std::clamp<qreal>(visibleUnits, 0, units) : 0;
    qreal u = t * (units - v);
    if (shape.hasHeader) {
        if (u < 1)
            return {-1, u};
        u -= 1;
    }
    const int row = int(std::floor(u));
    if (row >= shape.count)
        return {shape.count, std::clamp<qreal>(u - shape.count, 0, 1)};
    return {row, u - row};
}

// ---- RowScroll ----------------------------------------------------------------------------------

RowScroll::RowScroll(QObject *parent) : QObject(parent)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(0);
    connect(&m_refreshTimer, &QTimer::timeout, this, &RowScroll::refresh);
    m_scrollTimer.setSingleShot(true);
    connect(&m_scrollTimer, &QTimer::timeout, this, &RowScroll::applyScroll);
}

RowScroll::~RowScroll() = default;

void RowScroll::setView(QQuickItem *view)
{
    if (m_view == view)
        return;
    for (const QMetaObject::Connection &c : std::as_const(m_connections))
        disconnect(c);
    m_connections.clear();
    disconnect(m_modelConnection);
    disconnect(m_windowConnection);
    m_model = nullptr;
    m_hidden = false;
    m_modelMeta = {};
    m_view = view;
    const QMetaObject *mo = view ? view->metaObject() : nullptr;
    m_viewMeta = {propertyOf(mo, "count"),
                  propertyOf(mo, "contentY"),
                  propertyOf(mo, "contentHeight"),
                  propertyOf(mo, "originY"),
                  propertyOf(mo, "topMargin"),
                  propertyOf(mo, "bottomMargin"),
                  propertyOf(mo, "atYBeginning"),
                  propertyOf(mo, "atYEnd"),
                  propertyOf(mo, "headerItem"),
                  propertyOf(mo, "footerItem"),
                  propertyOf(mo, "moving"),
                  methodOf(mo, "itemAtIndex(int)"),
                  methodOf(mo, "indexAt(qreal,qreal)")};
    m_hasSize = false;
    m_lastRow = -1;
    m_pendingScroll.reset();
    m_scrollTimer.stop();
    if (view)
        connectView();
    Q_EMIT viewChanged();
    refresh();
}

void RowScroll::connectView()
{
    for (const char *signal : {"contentYChanged()", "heightChanged()", "contentHeightChanged()", "originYChanged()",
                               "countChanged()", "movingChanged()", "topMarginChanged()", "bottomMarginChanged()",
                               "headerItemChanged()", "footerItemChanged()"})
        m_connections.append(connectIfAny(m_view, signal, this, "schedule()"));
    m_connections.append(connect(m_view, &QQuickItem::windowChanged, this, &RowScroll::attachWindow));
    attachWindow(m_view->window());
    // A model with hidden rows (the editor's folded blocks): its shown rows are the units.
    m_connections.append(connectIfAny(m_view, "modelChanged()", this, "rebindModel()"));
    bindModel();
}

void RowScroll::attachWindow(QQuickWindow *window)
{
    disconnect(m_windowConnection);
    // Once a frame, after its polish (where ListView lays out the rows): the bar is where the
    // content is in the same frame. The 0 ms timer covers changes without a frame.
    if (window)
        m_windowConnection = connect(window, &QQuickWindow::afterAnimating, this, [this] {
            if (m_refreshTimer.isActive())
                refresh();
        });
}

void RowScroll::rebindModel()
{
    disconnect(m_modelConnection);
    bindModel();
    schedule();
}

void RowScroll::bindModel()
{
    m_model = m_view ? qvariant_cast<QObject *>(m_view->property("model")) : nullptr;
    const QMetaObject *mo = m_model ? m_model->metaObject() : nullptr;
    m_modelMeta = {propertyOf(mo, "visibleCount"), methodOf(mo, "visibleRowsBefore(int)"),
                   methodOf(mo, "visibleRowAt(int)")};
    m_hidden = m_modelMeta.visibleCount.isValid() && m_modelMeta.visibleRowsBefore.isValid()
               && m_modelMeta.visibleRowAt.isValid();
    if (m_hidden)
        m_modelConnection = connectIfAny(m_model, "visibleCountChanged()", this, "schedule()");
}

void RowScroll::setDragging(bool dragging)
{
    if (m_dragging == dragging)
        return;
    m_dragging = dragging;
    Q_EMIT draggingChanged();
    if (!dragging && m_pendingScroll) {
        m_scrollTimer.stop();
        applyScroll();
    }
    refresh();
}

void RowScroll::schedule()
{
    if (!m_refreshTimer.isActive())
        m_refreshTimer.start();
}

qreal RowScroll::real(const QMetaProperty &property) const
{
    bool ok = false;
    const qreal v = m_view && property.isValid() ? property.read(m_view).toReal(&ok) : 0;
    return ok && finite(v) ? v : 0;
}

QQuickItem *RowScroll::itemAt(int index) const
{
    QQuickItem *item = nullptr;
    if (m_view && index >= 0 && m_viewMeta.itemAtIndex.isValid())
        m_viewMeta.itemAtIndex.invoke(m_view, Qt::DirectConnection, Q_RETURN_ARG(QQuickItem *, item),
                                      Q_ARG(int, index));
    return item;
}

int RowScroll::unitCount(int count) const
{
    return m_hidden && m_model ? std::min(count, m_modelMeta.visibleCount.read(m_model).toInt()) : count;
}

int RowScroll::unitOfRow(int row) const
{
    if (!m_hidden || !m_model)
        return row;
    int unit = row;
    m_modelMeta.visibleRowsBefore.invoke(m_model, Qt::DirectConnection, Q_RETURN_ARG(int, unit), Q_ARG(int, row));
    return unit;
}

int RowScroll::rowOfUnit(int unit) const
{
    if (!m_hidden || !m_model)
        return unit;
    int row = -1;
    m_modelMeta.visibleRowAt.invoke(m_model, Qt::DirectConnection, Q_RETURN_ARG(int, row), Q_ARG(int, unit));
    return row;
}

int RowScroll::rowAt(qreal y, qreal viewTop, qreal viewBottom, int count) const
{
    if (!m_view || count <= 0 || !m_viewMeta.indexAt.isValid())
        return -1;
    const auto indexAt = [this](qreal x, qreal py) {
        int index = -1;
        m_viewMeta.indexAt.invoke(m_view, Qt::DirectConnection, Q_RETURN_ARG(int, index), Q_ARG(qreal, x),
                                  Q_ARG(qreal, py));
        return index;
    };
    const qreal w = m_view->width();
    int hint = -1;
    for (const qreal x : {w / 2, qreal(1), std::max<qreal>(1, w - 1)}) {
        if ((hint = indexAt(x, y)) >= 0)
            break;
    }
    if (hint < 0 && m_lastRow >= 0 && m_lastRow < count && itemAt(m_lastRow))
        hint = m_lastRow;
    // In a gap (spacing, a section header): any created row of the view to walk from.
    for (qreal py = viewTop; hint < 0 && py <= viewBottom; py += 24)
        hint = indexAt(w / 2, py);
    const QQuickItem *item = hint >= 0 ? itemAt(hint) : nullptr;
    if (!item)
        return -1;
    // The row whose span holds y: the last created row starting at or above it.
    while (hint > 0 && item->y() > y) {
        const QQuickItem *prev = itemAt(hint - 1);
        if (!prev)
            break;
        --hint;
        item = prev;
    }
    while (hint + 1 < count) {
        const QQuickItem *next = itemAt(hint + 1);
        if (!next || next->y() > y)
            break;
        ++hint;
    }
    m_lastRow = hint;
    return hint;
}

std::optional<RowSpan> RowScroll::span(int row, int count) const
{
    const QQuickItem *item = itemAt(row);
    // A hidden row (zero height) is no unit.
    if (!item || !(item->height() > 0))
        return std::nullopt;
    // The delegate only: spacing and a section header are the gap to the next row, a boundary
    // whether or not the row on its other side is created (it is not once its end left the view).
    return RowSpan{.index = unitOfRow(row),
                   .top = item->y(),
                   .bottom = item->y() + item->height(),
                   .openAbove = shownRow(row - 1, -1, count) < 0,
                   .openBelow = shownRow(row + 1, 1, count) < 0};
}

int RowScroll::shownRow(int row, int step, int count) const
{
    if (row < 0 || row >= count)
        return -1;
    if (m_hidden && m_model) {
        // The model knows: no walk over a fold of many rows.
        const int unit = unitOfRow(row);
        const int first = rowOfUnit(unit); // the first shown row at or after `row`
        const int shown = step > 0 || first == row ? first : rowOfUnit(unit - 1);
        const QQuickItem *item = shown >= 0 && shown < count ? itemAt(shown) : nullptr;
        return item && item->height() > 0 ? shown : -1;
    }
    for (; row >= 0 && row < count; row += step) {
        const QQuickItem *item = itemAt(row);
        if (!item)
            return -1;
        if (item->height() > 0)
            return row;
    }
    return -1;
}

RowEdges RowScroll::edges() const
{
    RowEdges e;
    QQuickItem *v = m_view;
    if (!v)
        return e;
    const ViewMeta &m = m_viewMeta;
    const int count = m.count.read(v).toInt();
    e.count = unitCount(count);
    const qreal contentY = real(m.contentY);
    e.viewTop = contentY + real(m.topMargin);
    e.viewBottom = contentY + v->height() - real(m.bottomMargin);
    e.atBeginning = m.atYBeginning.read(v).toBool();
    e.atEnd = m.atYEnd.read(v).toBool();
    if (auto *header = m.headerItem.read(v).value<QQuickItem *>(); header && header->height() > 0) {
        e.hasHeader = true;
        e.headerTop = header->y();
        e.headerBottom = header->y() + header->height();
    }
    if (auto *footer = m.footerItem.read(v).value<QQuickItem *>(); footer && footer->height() > 0) {
        e.hasFooter = true;
        e.footerTop = footer->y();
        e.footerBottom = footer->y() + footer->height();
    }
    // Keeps e.rows sorted and without duplicates (the edges' rows and their neighbours).
    e.rows.reserve(4);
    const auto add = [&e](const RowSpan &s) {
        const auto it = std::ranges::lower_bound(e.rows, s.index, {}, &RowSpan::index);
        if (it == e.rows.end() || it->index != s.index)
            e.rows.insert(it, s);
    };
    for (const qreal y : {e.viewTop, e.viewBottom}) {
        if ((e.hasHeader && y < e.headerBottom) || (e.hasFooter && y >= e.footerTop))
            continue;
        const int at = rowAt(y, e.viewTop, e.viewBottom, count);
        if (at < 0)
            continue;
        // The shown row at or above y (folded rows have no height), else the first one below it.
        int row = shownRow(at, -1, count);
        if (row < 0)
            row = shownRow(at, 1, count);
        const std::optional<RowSpan> s = row >= 0 ? span(row, count) : std::nullopt;
        if (!s)
            continue;
        add(*s);
        // Past its end, in the gap to the next row: that one too, when it is created.
        if (y >= s->bottom) {
            const int next = shownRow(row + 1, 1, count);
            if (const std::optional<RowSpan> n = next >= 0 ? span(next, count) : std::nullopt)
                add(*n);
        }
    }
    return e;
}

void RowScroll::refresh()
{
    m_refreshTimer.stop();
    qreal size = 1;
    qreal progress = 0;
    bool estimated = true;
    if (QQuickItem *v = m_view) {
        const qreal height = v->height();
        const qreal total = real(m_viewMeta.contentHeight) + real(m_viewMeta.topMargin) + real(m_viewMeta.bottomMargin);
        const qreal range = total - height;
        size = m_size;
        if (!m_hasSize || (!m_viewMeta.moving.read(v).toBool() && !m_dragging)) {
            size = total > 0 && height > 0 && range > 0.5 ? std::clamp<qreal>(height / total, 0, 1) : 1;
            m_hasSize = true;
        }
        if (const std::optional<qreal> t = RowFraction::progress(edges())) {
            progress = *t;
            estimated = false;
        } else if (range > 0) {
            // Qt's estimate (what visibleArea says).
            progress = std::clamp<qreal>(
                    (real(m_viewMeta.contentY) - real(m_viewMeta.originY) + real(m_viewMeta.topMargin)) / range, 0, 1);
        }
    }
    const qreal position = progress * (1 - size);
    const bool sizeMoved = changed(size, m_size);
    const bool progressMoved = changed(progress, m_progress) || estimated != m_estimated;
    const bool positionMoved = changed(position, m_position);
    m_size = size;
    m_progress = progress;
    m_estimated = estimated;
    m_position = position;
    if (sizeMoved)
        Q_EMIT sizeChanged();
    if (progressMoved)
        Q_EMIT progressChanged();
    if (positionMoved)
        Q_EMIT positionChanged();
}

void RowScroll::scrollTo(qreal position)
{
    if (!m_view || !finite(position))
        return;
    m_pendingScroll = position;
    // Read once: a second read can be past the interval, and a negative one starts no timer.
    const qint64 since = m_lastScroll.isValid() ? m_lastScroll.elapsed() : ScrollIntervalMs;
    if (since < ScrollIntervalMs) {
        if (!m_scrollTimer.isActive())
            m_scrollTimer.start(std::chrono::milliseconds(ScrollIntervalMs - since));
        return;
    }
    applyScroll();
}

void RowScroll::applyScroll()
{
    if (!m_pendingScroll || !m_view)
        return;
    const qreal position = *std::exchange(m_pendingScroll, std::nullopt);
    m_lastScroll.start();
    const qreal t = m_size < 1 ? std::clamp<qreal>(position / (1 - m_size), 0, 1) : 0;
    if (t <= 0) {
        QMetaObject::invokeMethod(m_view, "positionViewAtBeginning");
    } else if (t >= 1) {
        QMetaObject::invokeMethod(m_view, "positionViewAtEnd");
    } else {
        // Twice: the rows in view at the new place differ from those here (another visible
        // unit count), and they exist only once the view went there.
        RowEdges e = edges();
        qreal visible = RowFraction::visibleUnits(e).value_or(e.units() * m_size);
        for (int pass = 0; pass < 2; ++pass) {
            place(RowFraction::target(t, e, visible), e);
            e = edges();
            const std::optional<qreal> now = RowFraction::visibleUnits(e);
            if (!now || std::abs(*now - visible) < 0.01)
                break;
            visible = *now;
        }
    }
    refresh();
}

void RowScroll::place(RowFraction::Target target, const RowEdges &shape)
{
    QQuickItem *v = m_view;
    const int count = m_viewMeta.count.read(v).toInt();
    if (target.row < 0) {
        QMetaObject::invokeMethod(v, "positionViewAtBeginning");
        const auto *header = m_viewMeta.headerItem.read(v).value<QQuickItem *>();
        if (header && target.fraction > 0)
            m_viewMeta.contentY.write(v, real(m_viewMeta.contentY) + target.fraction * header->height());
        return;
    }
    // The footer, or a unit without a row: the end.
    const int row = target.row < shape.count ? rowOfUnit(target.row) : -1;
    if (row < 0 || row >= count) {
        QMetaObject::invokeMethod(v, "positionViewAtEnd");
        return;
    }
    QMetaObject::invokeMethod(v, "positionViewAtIndex", Q_ARG(int, row), Q_ARG(int, 0 /* ListView.Beginning */));
    const std::optional<RowSpan> s = span(row, count);
    if (!s)
        return;
    const qreal topMargin = real(m_viewMeta.topMargin);
    const qreal y = s->top + target.fraction * (s->bottom - s->top) - topMargin;
    const qreal maxY =
            real(m_viewMeta.originY) + real(m_viewMeta.contentHeight) + real(m_viewMeta.bottomMargin) - v->height();
    if (y >= maxY)
        QMetaObject::invokeMethod(v, "positionViewAtEnd");
    else
        m_viewMeta.contentY.write(v, std::max(y, real(m_viewMeta.originY) - topMargin));
}

} // namespace brulion::ui
