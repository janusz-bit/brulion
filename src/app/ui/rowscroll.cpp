#include "rowscroll.h"

#include <QMetaMethod>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace brulion::ui {

namespace {

bool finite(qreal v)
{
    return std::isfinite(v);
}

/// A unit's span in content coordinates (the header, a row, the footer).
struct Segment {
    qreal unit = 0;
    qreal top = 0;
    qreal bottom = 0;
    bool openAbove = false;
    bool openBelow = false;
};

QList<Segment> segments(const RowEdges &e)
{
    QList<Segment> out;
    out.reserve(e.rows.size() + 2);
    const int h = e.hasHeader ? 1 : 0;
    if (e.hasHeader)
        out.append({0, e.headerTop, e.headerBottom});
    for (const RowSpan &r : e.rows)
        out.append({qreal(h + r.index), r.top, r.bottom, r.openAbove, r.openBelow});
    if (e.hasFooter)
        out.append({qreal(h + e.count), e.footerTop, e.footerBottom});
    return out;
}

bool changed(qreal a, qreal b)
{
    return std::abs(a - b) > 1e-9;
}

// Rounding of item geometry: spans may overlap by this much.
constexpr qreal Overlap = 0.5;

} // namespace

// ---- RowFraction --------------------------------------------------------------------------------

bool RowFraction::valid(const RowEdges &e)
{
    if (e.count < 0 || !finite(e.viewTop) || !finite(e.viewBottom) || !(e.viewBottom > e.viewTop))
        return false;
    const auto ok = [](qreal top, qreal bottom) { return finite(top) && finite(bottom) && bottom > top; };
    if (e.hasHeader && !ok(e.headerTop, e.headerBottom))
        return false;
    if (e.hasFooter && !ok(e.footerTop, e.footerBottom))
        return false;
    int last = -1;
    for (const RowSpan &r : e.rows) {
        if (r.index <= last || r.index >= e.count || !ok(r.top, r.bottom))
            return false;
        last = r.index;
    }
    const QList<Segment> s = segments(e);
    for (qsizetype i = 1; i < s.size(); ++i) {
        if (s.at(i).top < s.at(i - 1).bottom - Overlap)
            return false;
    }
    return true;
}

std::optional<qreal> RowFraction::unitAt(const RowEdges &e, qreal y)
{
    if (!finite(y) || !valid(e))
        return std::nullopt;
    const QList<Segment> s = segments(e);
    if (s.isEmpty())
        return std::nullopt;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const Segment &seg = s.at(i);
        if (y < seg.top) {
            // In the gap before this unit: its top boundary, when nothing can lie between.
            if (i == 0)
                return seg.unit == 0 || seg.openAbove ? std::optional<qreal>(seg.unit) : std::nullopt;
            const Segment &prev = s.at(i - 1);
            if (prev.unit + 1 == seg.unit || (seg.openAbove && !prev.openBelow))
                return seg.unit;
            if (prev.openBelow && !seg.openAbove)
                return prev.unit + 1;
            return std::nullopt;
        }
        if (y < seg.bottom)
            return seg.unit + (y - seg.top) / (seg.bottom - seg.top);
    }
    const qreal units = e.units();
    const Segment &last = s.constLast();
    return last.unit + 1 == units || last.openBelow ? std::optional<qreal>(last.unit + 1) : std::nullopt;
}

std::optional<qreal> RowFraction::visibleUnits(const RowEdges &e)
{
    const std::optional<qreal> top = unitAt(e, e.viewTop);
    const std::optional<qreal> bottom = unitAt(e, e.viewBottom);
    if (!top || !bottom)
        return std::nullopt;
    return std::max<qreal>(0, *bottom - *top);
}

std::optional<qreal> RowFraction::progress(const RowEdges &e)
{
    if (e.count <= 0 || !valid(e))
        return std::nullopt;
    if (e.atBeginning)
        return 0;
    if (e.atEnd)
        return 1;
    const std::optional<qreal> top = unitAt(e, e.viewTop);
    const std::optional<qreal> bottom = unitAt(e, e.viewBottom);
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
    m_view = view;
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
    const QMetaMethod slot = metaObject()->method(metaObject()->indexOfSlot("schedule()"));
    const auto follow = [this, &slot](QObject *object, const char *signal) {
        const int index = object->metaObject()->indexOfSignal(signal);
        if (index >= 0)
            m_connections.append(connect(object, object->metaObject()->method(index), this, slot));
    };
    for (const char *signal : {"contentYChanged()", "heightChanged()", "contentHeightChanged()", "originYChanged()",
                               "countChanged()", "movingChanged()", "topMarginChanged()", "bottomMarginChanged()",
                               "headerItemChanged()", "footerItemChanged()"})
        follow(m_view, signal);
    m_connections.append(connect(m_view, &QQuickItem::windowChanged, this, &RowScroll::attachWindow));
    attachWindow(m_view->window());
    // A model with hidden rows (the editor's folded blocks): its shown rows are the units.
    const int modelChanged = m_view->metaObject()->indexOfSignal("modelChanged()");
    if (modelChanged >= 0) {
        const QMetaMethod rebind = metaObject()->method(metaObject()->indexOfSlot("rebindModel()"));
        m_connections.append(connect(m_view, m_view->metaObject()->method(modelChanged), this, rebind));
    }
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
    m_hidden = mo && mo->indexOfProperty("visibleCount") >= 0 && mo->indexOfMethod("visibleRowsBefore(int)") >= 0
               && mo->indexOfMethod("visibleRowAt(int)") >= 0;
    if (m_hidden) {
        const int index = mo->indexOfSignal("visibleCountChanged()");
        if (index >= 0)
            m_modelConnection = connect(m_model, mo->method(index), this,
                                        metaObject()->method(metaObject()->indexOfSlot("schedule()")));
    }
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

qreal RowScroll::real(const char *name) const
{
    bool ok = false;
    const qreal v = m_view ? m_view->property(name).toReal(&ok) : 0;
    return ok && finite(v) ? v : 0;
}

QQuickItem *RowScroll::itemAt(int index) const
{
    QQuickItem *item = nullptr;
    if (m_view && index >= 0)
        QMetaObject::invokeMethod(m_view, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, index));
    return item;
}

int RowScroll::unitCount(int count) const
{
    return m_hidden && m_model ? std::min(count, m_model->property("visibleCount").toInt()) : count;
}

int RowScroll::unitOfRow(int row) const
{
    if (!m_hidden || !m_model)
        return row;
    int unit = row;
    QMetaObject::invokeMethod(m_model, "visibleRowsBefore", Q_RETURN_ARG(int, unit), Q_ARG(int, row));
    return unit;
}

int RowScroll::rowOfUnit(int unit) const
{
    if (!m_hidden || !m_model)
        return unit;
    int row = -1;
    QMetaObject::invokeMethod(m_model, "visibleRowAt", Q_RETURN_ARG(int, row), Q_ARG(int, unit));
    return row;
}

int RowScroll::rowAt(qreal y, qreal viewTop, qreal viewBottom) const
{
    const int count = m_view ? m_view->property("count").toInt() : 0;
    if (count <= 0)
        return -1;
    const auto indexAt = [this](qreal x, qreal py) {
        int index = -1;
        QMetaObject::invokeMethod(m_view, "indexAt", Q_RETURN_ARG(int, index), Q_ARG(qreal, x), Q_ARG(qreal, py));
        return index;
    };
    const qreal w = m_view->width();
    const QList<qreal> xs{w / 2, 1, std::max<qreal>(1, w - 1)};
    int hint = -1;
    for (const qreal x : xs) {
        if ((hint = indexAt(x, y)) >= 0)
            break;
    }
    if (hint < 0 && m_lastRow >= 0 && m_lastRow < count && itemAt(m_lastRow))
        hint = m_lastRow;
    // In a gap (spacing, a section header): any created row of the view to walk from.
    for (qreal py = viewTop; hint < 0 && py <= viewBottom; py += 24)
        hint = indexAt(w / 2, py);
    QQuickItem *item = hint >= 0 ? itemAt(hint) : nullptr;
    if (!item)
        return -1;
    // The row whose span holds y: the last created row starting at or above it.
    while (hint > 0 && item->y() > y) {
        QQuickItem *prev = itemAt(hint - 1);
        if (!prev)
            break;
        --hint;
        item = prev;
    }
    while (hint + 1 < count) {
        QQuickItem *next = itemAt(hint + 1);
        if (!next || next->y() > y)
            break;
        ++hint;
        item = next;
    }
    m_lastRow = hint;
    return hint;
}

std::optional<RowSpan> RowScroll::span(int row, int count) const
{
    QQuickItem *item = itemAt(row);
    // A hidden row (zero height) is no unit.
    if (!item || !(item->height() > 0))
        return std::nullopt;
    RowSpan s;
    s.index = unitOfRow(row);
    s.top = item->y();
    // The delegate only: spacing and a section header are the gap to the next row, a boundary
    // whether or not the row on its other side is created (it is not once its end left the view).
    s.bottom = item->y() + item->height();
    s.openAbove = shownRow(row - 1, -1, count) < 0;
    s.openBelow = shownRow(row + 1, 1, count) < 0;
    return s;
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
        QQuickItem *item = shown >= 0 && shown < count ? itemAt(shown) : nullptr;
        return item && item->height() > 0 ? shown : -1;
    }
    for (; row >= 0 && row < count; row += step) {
        QQuickItem *item = itemAt(row);
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
    const int count = v->property("count").toInt();
    e.count = unitCount(count);
    const qreal contentY = real("contentY");
    e.viewTop = contentY + real("topMargin");
    e.viewBottom = contentY + v->height() - real("bottomMargin");
    e.atBeginning = v->property("atYBeginning").toBool();
    e.atEnd = v->property("atYEnd").toBool();
    if (auto *header = v->property("headerItem").value<QQuickItem *>(); header && header->height() > 0) {
        e.hasHeader = true;
        e.headerTop = header->y();
        e.headerBottom = header->y() + header->height();
    }
    if (auto *footer = v->property("footerItem").value<QQuickItem *>(); footer && footer->height() > 0) {
        e.hasFooter = true;
        e.footerTop = footer->y();
        e.footerBottom = footer->y() + footer->height();
    }
    // Keeps e.rows sorted and without duplicates (the edges' rows and their neighbours).
    const auto add = [&e](const RowSpan &s) {
        const auto it = std::lower_bound(e.rows.begin(), e.rows.end(), s.index,
                                         [](const RowSpan &r, int index) { return r.index < index; });
        if (it == e.rows.end() || it->index != s.index)
            e.rows.insert(it, s);
    };
    for (const qreal y : {e.viewTop, e.viewBottom}) {
        if ((e.hasHeader && y < e.headerBottom) || (e.hasFooter && y >= e.footerTop))
            continue;
        const int at = rowAt(y, e.viewTop, e.viewBottom);
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
        const qreal total = real("contentHeight") + real("topMargin") + real("bottomMargin");
        const qreal range = total - height;
        size = m_size;
        if (!m_hasSize || (!v->property("moving").toBool() && !m_dragging)) {
            size = total > 0 && height > 0 && range > 0.5 ? std::clamp<qreal>(height / total, 0, 1) : 1;
            m_hasSize = true;
        }
        if (const std::optional<qreal> t = RowFraction::progress(edges())) {
            progress = *t;
            estimated = false;
        } else if (range > 0) {
            // Qt's estimate (what visibleArea says).
            progress = std::clamp<qreal>((real("contentY") - real("originY") + real("topMargin")) / range, 0, 1);
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
    if (m_lastScroll.isValid() && m_lastScroll.elapsed() < ScrollIntervalMs) {
        if (!m_scrollTimer.isActive())
            m_scrollTimer.start(int(ScrollIntervalMs - m_lastScroll.elapsed()));
        return;
    }
    applyScroll();
}

void RowScroll::applyScroll()
{
    if (!m_pendingScroll || !m_view)
        return;
    const qreal position = *m_pendingScroll;
    m_pendingScroll.reset();
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

void RowScroll::place(const RowFraction::Target &target, const RowEdges &shape)
{
    QQuickItem *v = m_view;
    const int count = v->property("count").toInt();
    if (target.row < 0) {
        QMetaObject::invokeMethod(v, "positionViewAtBeginning");
        auto *header = v->property("headerItem").value<QQuickItem *>();
        if (header && target.fraction > 0)
            v->setProperty("contentY", real("contentY") + target.fraction * header->height());
        return;
    }
    if (target.row >= shape.count) {
        QMetaObject::invokeMethod(v, "positionViewAtEnd");
        return;
    }
    const int row = rowOfUnit(target.row);
    if (row < 0 || row >= count) {
        QMetaObject::invokeMethod(v, "positionViewAtEnd");
        return;
    }
    QMetaObject::invokeMethod(v, "positionViewAtIndex", Q_ARG(int, row), Q_ARG(int, 0 /* ListView.Beginning */));
    const std::optional<RowSpan> s = span(row, count);
    if (!s)
        return;
    const qreal topMargin = real("topMargin");
    const qreal y = s->top + target.fraction * (s->bottom - s->top) - topMargin;
    const qreal maxY = real("originY") + real("contentHeight") + real("bottomMargin") - v->height();
    if (y >= maxY)
        QMetaObject::invokeMethod(v, "positionViewAtEnd");
    else
        v->setProperty("contentY", std::max(y, real("originY") - topMargin));
}

} // namespace brulion::ui
