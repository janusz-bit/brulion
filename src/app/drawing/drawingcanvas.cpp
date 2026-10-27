#include "drawingcanvas.h"

#include "drawing/stroke.h"
#include "drawing/svgwrite.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QTabletEvent>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::drawingview {

namespace {

/// The shortest distance between the segments ab and cd.
double segmentDistance(QPointF a, QPointF b, QPointF c, QPointF d)
{
    const auto pointSegment = [](QPointF p, QPointF s, QPointF e) {
        const QPointF se = e - s;
        const double len2 = QPointF::dotProduct(se, se);
        double t = len2 > 0 ? QPointF::dotProduct(p - s, se) / len2 : 0;
        t = std::clamp(t, 0.0, 1.0);
        const QPointF q = s + se * t;
        return std::hypot(p.x() - q.x(), p.y() - q.y());
    };
    // Crossing segments.
    const auto cross = [](QPointF o, QPointF p, QPointF q) {
        return (p.x() - o.x()) * (q.y() - o.y()) - (p.y() - o.y()) * (q.x() - o.x());
    };
    const double d1 = cross(c, d, a), d2 = cross(c, d, b), d3 = cross(a, b, c), d4 = cross(a, b, d);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0)))
        return 0;
    return std::min({pointSegment(a, c, d), pointSegment(b, c, d), pointSegment(c, a, b), pointSegment(d, a, b)});
}

drawing::Background backgroundOf(const QString &name)
{
    return drawing::backgroundFromName(name);
}

} // namespace

DrawingCanvas::DrawingCanvas(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptTouchEvents(true);
    setAcceptHoverEvents(true);
    setOpaquePainting(true);
    setCursor(Qt::CrossCursor);
    m_settle.setSingleShot(true);
    m_settle.setInterval(160);
    connect(&m_settle, &QTimer::timeout, this, &DrawingCanvas::endViewGesture);
    m_clock = [] { return QDateTime::currentMSecsSinceEpoch(); };
    m_doc.paper = m_paper.name();
    // Tablet events pass the window first: the canvas takes the stylus's (see the header). (An
    // item made with a parent has its window before this constructor runs.)
    connect(this, &QQuickItem::windowChanged, this, &DrawingCanvas::filterWindow);
    filterWindow(window());
}

void DrawingCanvas::filterWindow(QQuickWindow *w)
{
    if (m_filteredWindow == w)
        return;
    if (m_filteredWindow)
        m_filteredWindow->removeEventFilter(this);
    m_filteredWindow = w;
    if (w)
        w->installEventFilter(this);
}

DrawingCanvas::~DrawingCanvas()
{
    if (m_filteredWindow)
        m_filteredWindow->removeEventFilter(this);
}

// --- Properties -------------------------------------------------------------------------------

void DrawingCanvas::setTool(const QString &tool)
{
    const QString t = tool == u"highlighter" || tool == u"eraser" ? tool : u"pen"_s;
    if (t == m_tool)
        return;
    m_tool = t;
    m_hover = QPointF(-1, -1);
    update();
    Q_EMIT toolChanged();
}

void DrawingCanvas::setColor(const QColor &color)
{
    if (!color.isValid() || color == m_color)
        return;
    m_color = color;
    Q_EMIT colorChanged();
}

void DrawingCanvas::setPenWidth(qreal width)
{
    width = std::clamp(width, 0.1, 40.0);
    if (qFuzzyCompare(width, m_penWidth))
        return;
    m_penWidth = width;
    Q_EMIT penWidthChanged();
}

QString DrawingCanvas::background() const
{
    return drawing::backgroundName(m_doc.background);
}

void DrawingCanvas::setBackground(const QString &name)
{
    const drawing::Background b = backgroundOf(name);
    if (b == m_doc.background || !m_editable)
        return;
    Op op;
    op.kind = Op::Background;
    op.before = m_doc.background;
    op.after = b;
    apply(op, true);
    push(std::move(op));
}

void DrawingCanvas::setFingerDraws(bool on)
{
    if (on == m_fingerDraws)
        return;
    m_fingerDraws = on;
    Q_EMIT fingerDrawsChanged();
}

void DrawingCanvas::setPanMode(bool on)
{
    if (on == m_panMode)
        return;
    m_panMode = on;
    setCursor(on ? Qt::OpenHandCursor : Qt::CrossCursor);
    Q_EMIT panModeChanged();
}

void DrawingCanvas::setZoom(qreal zoom)
{
    zoom = std::clamp(zoom, MinZoom, MaxZoom);
    if (!std::isfinite(zoom) || qFuzzyCompare(zoom, m_zoom))
        return;
    m_zoom = zoom;
    viewMoved(false);
}

void DrawingCanvas::setCenterX(qreal x)
{
    if (!std::isfinite(x) || qFuzzyCompare(x, m_center.x()))
        return;
    m_center.setX(x);
    viewMoved(false);
}

void DrawingCanvas::setCenterY(qreal y)
{
    if (!std::isfinite(y) || qFuzzyCompare(y, m_center.y()))
        return;
    m_center.setY(y);
    viewMoved(false);
}

// --- The drawing ------------------------------------------------------------------------------

QPainterPath DrawingCanvas::outlinePath(const QList<QPointF> &outline)
{
    // The curves of the SVG path (perfect-freehand's getSvgPathFromStroke): quadratic segments
    // through the midpoints, so the screen and the file show the same outline.
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    if (outline.isEmpty())
        return path;
    path.moveTo(outline.first());
    const qsizetype max = outline.size() - 1;
    for (qsizetype i = 0; i <= max; ++i) {
        const QPointF p = outline.at(i);
        const QPointF next = i == max ? outline.first() : outline.at(i + 1);
        path.quadTo(p, (p + next) / 2);
    }
    path.closeSubpath();
    return path;
}

DrawingCanvas::Item DrawingCanvas::makeItem(drawing::Stroke stroke)
{
    Item item;
    item.path = outlinePath(drawing::strokeOutline(stroke));
    item.bounds = item.path.boundingRect();
    item.color = QColor(drawing::safeColor(stroke.color, u"#1e1e1e"_s));
    item.color.setAlphaF(item.color.alphaF() * std::clamp(stroke.opacity, 0, 100) / 100.0);
    item.stroke = std::move(stroke);
    return item;
}

QList<drawing::Stroke> DrawingCanvas::strokes() const
{
    QList<drawing::Stroke> out;
    out.reserve(m_items.size());
    for (const Item &item : m_items)
        out.append(item.stroke);
    return out;
}

drawing::Drawing DrawingCanvas::currentDrawing() const
{
    drawing::Drawing doc = m_doc;
    doc.strokes = strokes();
    return doc;
}

void DrawingCanvas::setDocument(drawing::Drawing doc)
{
    m_items.clear();
    m_items.reserve(doc.strokes.size());
    for (drawing::Stroke &s : doc.strokes)
        m_items.append(makeItem(std::move(s)));
    doc.strokes.clear();
    m_doc = std::move(doc);
    const QColor paper(drawing::safeColor(m_doc.paper, QString()));
    m_paper = paper.isValid() ? paper : QColor(Qt::white);
    m_erasing.clear();
    invalidateCache();
}

QString DrawingCanvas::load(const QByteArray &bytes)
{
    cancelStroke();
    QString status;
    drawing::Drawing doc;
    if (!bytes.isEmpty()) {
        drawing::ReadError error = drawing::ReadError::None;
        std::optional<drawing::Drawing> read = drawing::readDrawing(bytes, &error);
        if (!read)
            status = error == drawing::ReadError::Corrupt ? u"corrupt"_s : u"notDrawing"_s;
        else if (!drawing::editable(*read))
            status = u"readonly"_s;
        else
            doc = std::move(*read);
    }
    m_editable = status.isEmpty();
    setDocument(m_editable ? std::move(doc) : drawing::Drawing());
    m_baseIds.clear();
    for (const Item &item : std::as_const(m_items))
        m_baseIds.insert(item.stroke.id);
    m_known = bytes;
    const bool hadHistory = canUndo() || canRedo();
    m_undo.clear();
    m_redo.clear();
    if (hadHistory)
        Q_EMIT historyChanged();
    setModified(false);
    m_viewSet = false;
    placeView();
    Q_EMIT loadedChanged();
    Q_EMIT backgroundChanged();
    Q_EMIT contentChanged();
    return status;
}

bool DrawingCanvas::takeExternal(const QByteArray &bytes)
{
    std::optional<drawing::Drawing> theirs = drawing::readDrawing(bytes);
    if (!theirs || !drawing::editable(*theirs) || !m_editable)
        return false;
    cancelStroke();
    QSet<QString> theirIds;
    for (const drawing::Stroke &s : std::as_const(theirs->strokes))
        theirIds.insert(s.id);
    if (m_modified) {
        // Merged by stroke id against the last load or save: theirs, plus what was drawn here,
        // minus what was erased here; the background as chosen here.
        QSet<QString> ours;
        for (const Item &item : std::as_const(m_items))
            ours.insert(item.stroke.id);
        const QSet<QString> erasedHere = QSet<QString>(m_baseIds).subtract(ours);
        QList<drawing::Stroke> merged;
        for (drawing::Stroke &s : theirs->strokes) {
            if (!erasedHere.contains(s.id))
                merged.append(std::move(s));
        }
        for (const Item &item : std::as_const(m_items)) {
            if (!m_baseIds.contains(item.stroke.id) && !theirIds.contains(item.stroke.id))
                merged.append(item.stroke);
        }
        theirs->strokes = std::move(merged);
        theirs->background = m_doc.background;
    }
    setDocument(std::move(*theirs));
    m_baseIds = theirIds;
    m_known = bytes;
    if (canUndo() || canRedo()) {
        m_undo.clear();
        m_redo.clear();
        Q_EMIT historyChanged();
    }
    Q_EMIT backgroundChanged();
    Q_EMIT contentChanged();
    return true;
}

QByteArray DrawingCanvas::svg() const
{
    if (!m_editable)
        return {};
    return drawing::toSvg(currentDrawing());
}

void DrawingCanvas::markSaved(const QByteArray &written)
{
    if (!written.isEmpty())
        m_known = written;
    m_baseIds.clear();
    for (const Item &item : std::as_const(m_items))
        m_baseIds.insert(item.stroke.id);
    setModified(false);
}

void DrawingCanvas::setModified(bool on)
{
    if (on == m_modified)
        return;
    m_modified = on;
    Q_EMIT modifiedChanged();
}

void DrawingCanvas::changed()
{
    setModified(true);
    Q_EMIT contentChanged();
}

QRectF DrawingCanvas::contentBounds() const
{
    QRectF out;
    for (const Item &item : m_items)
        out = out.isNull() ? item.bounds : out.united(item.bounds);
    return out;
}

QString DrawingCanvas::newId() const
{
    // nanoid's alphabet and length, as Excalidraw's element ids.
    static constexpr char alphabet[] = "useandom-26T198340PX75pxJACKVERYMINDBUSHWOLF_GQZbfghjklqvwyzrict";
    QString id;
    id.reserve(21);
    for (int i = 0; i < 21; ++i)
        id += QLatin1Char(alphabet[QRandomGenerator::global()->bounded(64)]);
    return id;
}

// --- Undo -------------------------------------------------------------------------------------

void DrawingCanvas::push(Op op)
{
    m_undo.append(std::move(op));
    if (m_undo.size() > 500)
        m_undo.removeFirst();
    m_redo.clear();
    Q_EMIT historyChanged();
    changed();
}

void DrawingCanvas::apply(const Op &op, bool forward)
{
    switch (op.kind) {
    case Op::Background:
        m_doc.background = forward ? op.after : op.before;
        Q_EMIT backgroundChanged();
        break;
    case Op::Add:
    case Op::Remove: {
        const bool insert = (op.kind == Op::Add) == forward;
        if (insert) {
            for (const auto &[index, item] : op.items)
                m_items.insert(std::clamp<qsizetype>(index, 0, m_items.size()), item);
        } else {
            for (auto it = op.items.crbegin(); it != op.items.crend(); ++it) {
                if (it->first >= 0 && it->first < m_items.size())
                    m_items.removeAt(it->first);
            }
        }
        break;
    }
    }
    invalidateCache();
}

void DrawingCanvas::undo()
{
    if (m_undo.isEmpty() || busy())
        return;
    Op op = m_undo.takeLast();
    apply(op, false);
    m_redo.append(std::move(op));
    Q_EMIT historyChanged();
    changed();
}

void DrawingCanvas::redo()
{
    if (m_redo.isEmpty() || busy())
        return;
    Op op = m_redo.takeLast();
    apply(op, true);
    m_undo.append(std::move(op));
    Q_EMIT historyChanged();
    changed();
}

// --- The view ---------------------------------------------------------------------------------

QTransform DrawingCanvas::sceneToItem() const
{
    QTransform t;
    t.translate(width() / 2, height() / 2);
    t.scale(m_zoom, m_zoom);
    t.translate(-m_center.x(), -m_center.y());
    return t;
}

QPointF DrawingCanvas::toScene(QPointF item) const
{
    return (item - QPointF(width() / 2, height() / 2)) / m_zoom + m_center;
}

QPointF DrawingCanvas::toItem(QPointF scene) const
{
    return (scene - m_center) * m_zoom + QPointF(width() / 2, height() / 2);
}

void DrawingCanvas::placeView()
{
    if (m_viewSet || width() <= 0 || height() <= 0)
        return;
    constexpr double margin = 24;
    const QRectF b = contentBounds();
    if (b.isNull()) {
        m_zoom = 1;
        m_center = QPointF(width() / 2, height() / 2); // the scene's origin at the top left
    } else {
        // From the top left of the strokes, at zoom 1 — or as wide as the view (notes are read
        // down the page: a tall page is scrolled, not shrunk).
        m_zoom = std::clamp(std::min(1.0, (width() - 2 * margin) / std::max(1.0, b.width())), MinZoom, MaxZoom);
        m_center = b.topLeft() + (QPointF(width() / 2, height() / 2) - QPointF(margin, margin)) / m_zoom;
    }
    m_viewSet = true;
    invalidateCache();
    Q_EMIT viewChanged();
}

void DrawingCanvas::fitAll()
{
    const QRectF b = contentBounds();
    if (b.isNull() || width() <= 0 || height() <= 0) {
        m_zoom = 1;
        m_center = QPointF(width() / 2, height() / 2);
    } else {
        constexpr double margin = 24;
        const double z = std::min({(width() - 2 * margin) / std::max(1.0, b.width()),
                                   (height() - 2 * margin) / std::max(1.0, b.height()), 1.0});
        m_zoom = std::clamp(z, MinZoom, MaxZoom);
        // Centred across, from the top down (room left over stays below, where writing goes on).
        m_center = QPointF(b.center().x(), b.top() - margin / m_zoom + height() / (2 * m_zoom));
    }
    viewMoved(false);
}

void DrawingCanvas::zoomBy(qreal factor, qreal x, qreal y)
{
    if (!std::isfinite(factor) || factor <= 0)
        return;
    const QPointF anchor = x < 0 || y < 0 ? QPointF(width() / 2, height() / 2) : QPointF(x, y);
    const QPointF scene = toScene(anchor);
    m_zoom = std::clamp(m_zoom * factor, MinZoom, MaxZoom);
    m_center = scene - (anchor - QPointF(width() / 2, height() / 2)) / m_zoom;
    viewMoved(true);
}

void DrawingCanvas::panBy(qreal dx, qreal dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy))
        return;
    m_center -= QPointF(dx, dy) / m_zoom;
    viewMoved(true);
}

void DrawingCanvas::viewMoved(bool byUser)
{
    m_viewSet = true;
    if (m_gesture == Gesture::Draw)
        updateLive();
    update();
    Q_EMIT viewChanged();
    if (byUser)
        Q_EMIT userMovedView();
}

void DrawingCanvas::endViewGesture()
{
    if (m_gesture == Gesture::Pan || m_gesture == Gesture::Pinch)
        return; // still moving: ends with the gesture
    m_interactingView = false;
    update();
}

void DrawingCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size())
        return;
    if (!m_viewSet)
        placeView();
    else if (oldGeometry.width() > 0 && oldGeometry.height() > 0)
        // The scene point at the top left stays there (a phone's keyboard, a resized window).
        m_center += QPointF(newGeometry.width() - oldGeometry.width(), newGeometry.height() - oldGeometry.height())
                    / (2 * m_zoom);
    invalidateCache();
}

void DrawingCanvas::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemDevicePixelRatioHasChanged) {
        invalidateCache();
    }
    QQuickPaintedItem::itemChange(change, value);
}

// --- Rendering --------------------------------------------------------------------------------

void DrawingCanvas::invalidateCache()
{
    m_cacheValid = false;
    update();
}

void DrawingCanvas::renderCache()
{
    QElapsedTimer timer;
    timer.start();
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const QSize pixels = (QSizeF(width(), height()) * dpr).toSize();
    m_cacheZoom = m_zoom;
    m_cacheCenter = m_center;
    m_cacheSize = QSizeF(width(), height());
    m_cacheValid = true;
    if (pixels.isEmpty()) {
        m_cache = QImage();
        return;
    }
    // A new image when the snapshot of the render thread still shares this one.
    if (m_cache.size() != pixels || !m_cache.isDetached())
        m_cache = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
    m_cache.setDevicePixelRatio(dpr);
    m_cache.fill(m_paper);
    QPainter p(&m_cache);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF visible(toScene(QPointF(0, 0)), toScene(QPointF(width(), height())));
    // The lines or the grid at the scene's multiples of the spacing, as the SVG has them.
    if (m_doc.background != drawing::Background::Plain) {
        const double spacing =
                m_doc.background == drawing::Background::Lines ? drawing::LineSpacing : drawing::GridSpacing;
        if (spacing * m_zoom >= 6) {
            QPen pen(QColor(drawing::RuleColor.toString()));
            pen.setCosmetic(true);
            pen.setWidthF(1);
            p.setPen(pen);
            for (double y = std::ceil(visible.top() / spacing) * spacing; y <= visible.bottom(); y += spacing) {
                const double iy = std::round(toItem(QPointF(0, y)).y() * dpr) / dpr + 0.5 / dpr;
                p.drawLine(QPointF(0, iy), QPointF(width(), iy));
            }
            if (m_doc.background == drawing::Background::Grid) {
                for (double x = std::ceil(visible.left() / spacing) * spacing; x <= visible.right(); x += spacing) {
                    const double ix = std::round(toItem(QPointF(x, 0)).x() * dpr) / dpr + 0.5 / dpr;
                    p.drawLine(QPointF(ix, 0), QPointF(ix, height()));
                }
            }
        }
    }
    p.setPen(Qt::NoPen);
    p.setTransform(sceneToItem(), true);
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        const Item &item = m_items.at(i);
        if (m_erasing.contains(int(i)) || !item.bounds.intersects(visible))
            continue;
        p.fillPath(item.path, item.color);
    }
    p.end();
    m_lastRenderMs = double(timer.nsecsElapsed()) / 1e6;
}

void DrawingCanvas::drawItemIntoCache(const Item &item)
{
    if (!m_cacheValid || m_cache.isNull() || m_cacheZoom != m_zoom || m_cacheCenter != m_center) {
        invalidateCache();
        return;
    }
    QPainter p(&m_cache); // detaches from a snapshot the render thread may be drawing
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setTransform(sceneToItem(), true);
    p.fillPath(item.path, item.color);
}

void DrawingCanvas::updateLive()
{
    const QRectF before = m_liveDirty;
    if (m_live.points.isEmpty()) {
        m_livePath = QPainterPath();
        m_liveDirty = QRectF();
    } else {
        drawing::OutlineOptions o = drawing::outlineOptions(m_live, false);
        QList<QPointF> outline = drawing::strokeOutline(m_live.points, o);
        const QTransform t = sceneToItem();
        for (QPointF &p : outline)
            p = t.map(p);
        m_livePath = outlinePath(outline);
        m_liveDirty = m_livePath.boundingRect();
    }
    const QRectF dirty = before.isNull() ? m_liveDirty : m_liveDirty.isNull() ? before : before.united(m_liveDirty);
    if (!dirty.isNull())
        update(dirty.toAlignedRect().adjusted(-3, -3, 3, 3));
}

void DrawingCanvas::prepareSnapshot()
{
    const bool current = m_cacheValid && m_cacheZoom == m_zoom && m_cacheCenter == m_center
                         && m_cacheSize == QSizeF(width(), height());
    if (!current && !m_interactingView)
        renderCache();
    Snapshot s;
    s.cache = m_cache;
    s.cacheCurrent = m_cacheValid && m_cacheZoom == m_zoom && m_cacheCenter == m_center;
    if (!s.cacheCurrent && m_cacheZoom > 0) {
        // The cached view moved and scaled to the current one (item_now ← item_then).
        QTransform t;
        t.translate(width() / 2, height() / 2);
        t.scale(m_zoom, m_zoom);
        t.translate(m_cacheCenter.x() - m_center.x(), m_cacheCenter.y() - m_center.y());
        t.scale(1 / m_cacheZoom, 1 / m_cacheZoom);
        t.translate(-m_cacheSize.width() / 2, -m_cacheSize.height() / 2);
        s.cacheToItem = t;
    }
    s.paper = m_paper;
    s.live = m_livePath;
    s.liveColor = QColor(drawing::safeColor(m_live.color, u"#1e1e1e"_s));
    s.liveOpacity = m_live.opacity / 100.0;
    if (m_gesture == Gesture::Erase || (m_tool == u"eraser" && m_gesture == Gesture::None && m_hover.x() >= 0)) {
        s.eraser = m_gesture == Gesture::Erase ? m_lastItemPos : m_hover;
        s.eraserRadius = EraserRadius;
    }
    m_snapshot = std::move(s);
}

void DrawingCanvas::paintSnapshot(QPainter *painter, const QSizeF &size) const
{
    const Snapshot &s = m_snapshot;
    if (!s.cacheCurrent || s.cache.isNull())
        painter->fillRect(QRectF(QPointF(0, 0), size), s.paper);
    if (!s.cache.isNull()) {
        painter->save();
        if (!s.cacheCurrent) {
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            painter->setTransform(s.cacheToItem, true);
        }
        painter->drawImage(QPointF(0, 0), s.cache);
        painter->restore();
    }
    painter->setRenderHint(QPainter::Antialiasing);
    if (!s.live.isEmpty()) {
        QColor c = s.liveColor;
        c.setAlphaF(c.alphaF() * std::clamp(s.liveOpacity, 0.0, 1.0));
        painter->fillPath(s.live, c);
    }
    if (s.eraser.x() >= 0) {
        painter->setPen(QPen(QColor(31, 36, 48, 150), 1));
        painter->setBrush(QColor(255, 255, 255, 90));
        painter->drawEllipse(s.eraser, s.eraserRadius, s.eraserRadius);
    }
}

QSGNode *DrawingCanvas::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data)
{
    prepareSnapshot(); // the GUI thread waits here: the state is consistent
    return QQuickPaintedItem::updatePaintNode(oldNode, data);
}

void DrawingCanvas::paint(QPainter *painter)
{
    paintSnapshot(painter, size());
}

void DrawingCanvas::render(QPainter *painter, const QSizeF &size)
{
    prepareSnapshot();
    paintSnapshot(painter, size);
}

// --- Input ------------------------------------------------------------------------------------

bool DrawingCanvas::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
        if (handleTablet(static_cast<QTabletEvent *>(event))) {
            event->accept(); // no mouse events made of it
            return true;
        }
        break;
    default: break;
    }
    return QQuickPaintedItem::eventFilter(watched, event);
}

namespace {

/// The topmost visible, enabled item under `scenePos` that takes mouse or touch input.
QQuickItem *topInputItem(QQuickItem *item, QPointF scenePos)
{
    if (!item->isVisible() || !item->isEnabled() || qFuzzyIsNull(item->opacity()))
        return nullptr;
    const QPointF local = item->mapFromScene(scenePos);
    if (item->clip() && !item->contains(local))
        return nullptr;
    QList<QQuickItem *> children = item->childItems();
    std::stable_sort(children.begin(), children.end(), [](QQuickItem *a, QQuickItem *b) { return a->z() < b->z(); });
    for (auto it = children.crbegin(); it != children.crend(); ++it) {
        if (QQuickItem *hit = topInputItem(*it, scenePos))
            return hit;
    }
    if (item->contains(local) && (item->acceptedMouseButtons() != Qt::NoButton || item->acceptTouchEvents()))
        return item;
    return nullptr;
}

} // namespace

bool DrawingCanvas::onTop(QPointF scenePos) const
{
    QQuickWindow *w = window();
    if (!w || !isVisible() || !isEnabled() || !contains(mapFromScene(scenePos)))
        return false;
    return topInputItem(w->contentItem(), scenePos) == this;
}

bool DrawingCanvas::handleTablet(QTabletEvent *event)
{
    const QPointF pos = mapFromScene(event->position());
    const bool eraser =
            event->pointerType() == QPointingDevice::PointerType::Eraser || (event->buttons() & Qt::RightButton);
    const Pointer p{true, eraser, std::clamp(double(event->pressure()), 0.0, 1.0)};
    const auto remember = [this, event] {
        m_lastTabletTime = event->timestamp();
        m_lastTabletGlobal = event->globalPosition();
    };
    switch (event->type()) {
    case QEvent::TabletPress:
        if (m_tabletGesture) {
            remember();
            return true; // another button of the pen held down
        }
        if (m_gesture != Gesture::None || !onTop(event->position()))
            return false;
        remember();
        noteStylus();
        m_tabletGesture = true;
        m_hover = QPointF(-1, -1);
        if (m_panMode || !m_editable)
            beginPan(pos);
        else if (p.eraser || m_tool == u"eraser")
            beginErase(pos);
        else
            beginStroke(pos, p);
        return true;
    case QEvent::TabletMove:
        if (!m_tabletGesture) {
            // Hovering: the eraser's ring follows the pen.
            if (m_tool == u"eraser" || eraser) {
                m_hover = contains(pos) ? pos : QPointF(-1, -1);
                update();
            }
            return false;
        }
        remember();
        switch (m_gesture) {
        case Gesture::Draw: extendStroke(pos, p); break;
        case Gesture::Erase: eraseTo(pos); break;
        case Gesture::Pan: panTo(pos); break;
        default: break;
        }
        return true;
    case QEvent::TabletRelease:
        if (!m_tabletGesture)
            return false;
        remember();
        if (event->buttons() != Qt::NoButton)
            return true; // a button let go, the tip still down
        m_tabletGesture = false;
        switch (m_gesture) {
        case Gesture::Draw:
            extendStroke(pos, p);
            finishStroke();
            break;
        case Gesture::Erase:
            eraseTo(pos);
            finishErase();
            break;
        case Gesture::Pan:
        case Gesture::Pinch:
            setGesture(Gesture::None);
            endViewGesture();
            break;
        case Gesture::None: break;
        }
        return true;
    default: break;
    }
    return false;
}

bool DrawingCanvas::fromTablet(const QMouseEvent *event) const
{
    if (m_tabletGesture)
        return true;
    const quint64 a = event->timestamp();
    const quint64 b = m_lastTabletTime;
    const QPointF delta = event->globalPosition() - m_lastTabletGlobal;
    return (a > b ? a - b : b - a) <= 500 && std::abs(delta.x()) + std::abs(delta.y()) < 12;
}

DrawingCanvas::Pointer DrawingCanvas::pointerOf(const QPointerEvent *event) const
{
    Pointer p;
    if (const QPointingDevice *device = event->pointingDevice()) {
        p.stylus = device->type() == QInputDevice::DeviceType::Stylus
                   || device->type() == QInputDevice::DeviceType::Airbrush;
        p.eraser = device->pointerType() == QPointingDevice::PointerType::Eraser;
    }
    return p;
}

void DrawingCanvas::noteStylus()
{
    if (m_stylusSeen)
        return;
    m_stylusSeen = true;
    Q_EMIT stylusSeenChanged();
}

void DrawingCanvas::setGesture(Gesture g)
{
    const bool wasBusy = busy();
    m_gesture = g;
    if (g == Gesture::Pan || g == Gesture::Pinch)
        m_interactingView = true;
    if (busy() != wasBusy)
        Q_EMIT busyChanged();
}

void DrawingCanvas::mousePressEvent(QMouseEvent *event)
{
    if (m_gesture != Gesture::None || m_ignoreMouse) {
        event->accept(); // another button during a gesture
        return;
    }
    if (fromTablet(event)) {
        m_ignoreMouse = true; // the stylus drew it already
        event->accept();
        return;
    }
    const Pointer p = pointerOf(event);
    if (p.stylus)
        noteStylus();
    const QPointF pos = event->position();
    const Qt::MouseButton button = event->button();
    if (button == Qt::MiddleButton || (button == Qt::RightButton && !p.stylus)
        || (button == Qt::LeftButton && (m_panMode || !m_editable))) {
        beginPan(pos);
    } else if (button == Qt::RightButton || (button == Qt::LeftButton && (p.eraser || m_tool == u"eraser"))) {
        beginErase(pos); // the stylus's barrel button, its eraser end, the eraser tool
    } else if (button == Qt::LeftButton) {
        beginStroke(pos, p);
    } else {
        event->ignore();
        return;
    }
    event->accept();
}

void DrawingCanvas::mouseMoveEvent(QMouseEvent *event)
{
    event->accept();
    if (m_ignoreMouse || m_tabletGesture)
        return;
    const QPointF pos = event->position();
    switch (m_gesture) {
    case Gesture::Draw: extendStroke(pos, pointerOf(event)); break;
    case Gesture::Erase: eraseTo(pos); break;
    case Gesture::Pan: panTo(pos); break;
    default: break;
    }
}

void DrawingCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    event->accept();
    if (m_ignoreMouse || m_tabletGesture) {
        if (event->buttons() == Qt::NoButton)
            m_ignoreMouse = false;
        return;
    }
    if (event->buttons() != Qt::NoButton && m_gesture != Gesture::None)
        return; // another button let go; the gesture's own is still held
    const QPointF pos = event->position();
    switch (m_gesture) {
    case Gesture::Draw:
        extendStroke(pos, pointerOf(event));
        finishStroke();
        break;
    case Gesture::Erase:
        eraseTo(pos);
        finishErase();
        break;
    case Gesture::Pan:
    case Gesture::Pinch:
        setGesture(Gesture::None);
        endViewGesture();
        break;
    case Gesture::None: break;
    }
}

void DrawingCanvas::mouseUngrabEvent()
{
    m_ignoreMouse = false;
    if (m_tabletGesture)
        return; // the pen's gesture goes on (the tablet events are the canvas's own)
    switch (m_gesture) {
    case Gesture::Draw: finishStroke(); break;
    case Gesture::Erase: finishErase(); break;
    case Gesture::Pan:
    case Gesture::Pinch:
        setGesture(Gesture::None);
        endViewGesture();
        break;
    case Gesture::None: break;
    }
}

void DrawingCanvas::hoverMoveEvent(QHoverEvent *event)
{
    if (m_tool == u"eraser") {
        m_hover = event->position();
        update();
    }
    QQuickPaintedItem::hoverMoveEvent(event);
}

void DrawingCanvas::hoverLeaveEvent(QHoverEvent *event)
{
    if (m_hover.x() >= 0) {
        m_hover = QPointF(-1, -1);
        update();
    }
    QQuickPaintedItem::hoverLeaveEvent(event);
}

void DrawingCanvas::wheelEvent(QWheelEvent *event)
{
    m_interactingView = true;
    m_settle.start();
    if (event->modifiers() & Qt::ControlModifier) {
        const double steps = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->pixelDelta().y() * 4.0;
        zoomBy(std::pow(1.0015, steps), event->position().x(), event->position().y());
    } else {
        QPointF delta = !event->pixelDelta().isNull() ? QPointF(event->pixelDelta())
                                                      : QPointF(event->angleDelta()) / 120.0 * 48.0;
        if ((event->modifiers() & Qt::ShiftModifier) && qFuzzyIsNull(delta.x()))
            delta = QPointF(delta.y(), 0);
        panBy(delta.x(), delta.y());
    }
    event->accept();
}

bool DrawingCanvas::event(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture) {
        // A touchpad's pinch (macOS, Wayland, Windows precision touchpads).
        const auto *g = static_cast<QNativeGestureEvent *>(event);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
            m_interactingView = true;
            m_settle.start();
            zoomBy(1 + g->value(), g->position().x(), g->position().y());
            return true;
        }
    }
    return QQuickPaintedItem::event(event);
}

void DrawingCanvas::touchEvent(QTouchEvent *event)
{
    if (event->type() == QEvent::TouchCancel) {
        touchUngrabEvent();
        event->accept();
        return;
    }
    QList<const QEventPoint *> down;
    for (const QEventPoint &p : event->points()) {
        if (p.state() != QEventPoint::State::Released)
            down.append(&p);
    }
    if (down.size() >= 2) {
        if (m_gesture == Gesture::Draw) {
            // A second finger right after the first one began: a pinch, not a stroke.
            if (m_liveClock.elapsed() < 300 || m_live.points.size() < 8)
                cancelStroke();
            else
                finishStroke();
        } else if (m_gesture == Gesture::Erase) {
            finishErase();
        }
        const QPointF a = down.at(0)->position();
        const QPointF b = down.at(1)->position();
        const QPointF mid = (a + b) / 2;
        const double distance = std::max(1.0, std::hypot(a.x() - b.x(), a.y() - b.y()));
        if (m_gesture != Gesture::Pinch) {
            setGesture(Gesture::Pinch);
            m_pinchDistance = distance;
            m_pinchZoom = m_zoom;
            m_pinchScene = toScene(mid);
        } else {
            m_zoom = std::clamp(m_pinchZoom * distance / m_pinchDistance, MinZoom, MaxZoom);
            m_center = m_pinchScene - (mid - QPointF(width() / 2, height() / 2)) / m_zoom;
            viewMoved(true);
        }
    } else if (down.size() == 1) {
        const QEventPoint &pt = *down.at(0);
        switch (m_gesture) {
        case Gesture::Pinch:
            beginPan(pt.position()); // a finger lifted: the other one moves on
            break;
        case Gesture::None:
            if (m_fingerDraws && !m_panMode && m_editable) {
                m_touchDrawId = pt.id();
                if (m_tool == u"eraser")
                    beginErase(pt.position());
                else
                    beginStroke(pt.position(), Pointer{});
            } else {
                beginPan(pt.position());
            }
            break;
        case Gesture::Draw:
            if (pt.id() == m_touchDrawId)
                extendStroke(pt.position(), Pointer{});
            break;
        case Gesture::Erase: eraseTo(pt.position()); break;
        case Gesture::Pan: panTo(pt.position()); break;
        }
    } else {
        // Every finger lifted.
        const QPointF pos = event->points().isEmpty() ? m_lastItemPos : event->points().first().position();
        switch (m_gesture) {
        case Gesture::Draw:
            extendStroke(pos, Pointer{});
            finishStroke();
            break;
        case Gesture::Erase:
            eraseTo(pos);
            finishErase();
            break;
        case Gesture::Pan:
        case Gesture::Pinch:
            setGesture(Gesture::None);
            endViewGesture();
            break;
        case Gesture::None: break;
        }
        m_touchDrawId = -1;
    }
    event->accept();
}

void DrawingCanvas::touchUngrabEvent()
{
    mouseUngrabEvent();
    m_touchDrawId = -1;
}

// --- Gestures ---------------------------------------------------------------------------------

void DrawingCanvas::beginStroke(QPointF itemPos, const Pointer &p)
{
    if (!m_editable)
        return;
    const bool highlighter = m_tool == u"highlighter";
    m_live = drawing::Stroke();
    m_live.tool = highlighter ? drawing::Tool::Highlighter : drawing::Tool::Pen;
    m_live.color = m_color.name(QColor::HexRgb);
    m_live.width = m_penWidth;
    m_live.opacity = highlighter ? HighlighterOpacity : 100;
    // The highlighter: an even width (a constant pressure Excalidraw draws alike); a pen without
    // a stylus: pressure from the speed.
    m_live.simulatePressure = !highlighter && !p.stylus;
    m_liveStylus = p.stylus;
    m_liveMinPressure = 1;
    m_liveMaxPressure = 0;
    m_liveClock.start();
    setGesture(Gesture::Draw);
    m_lastItemPos = itemPos;
    extendStroke(itemPos, p);
}

void DrawingCanvas::extendStroke(QPointF itemPos, const Pointer &p)
{
    if (m_gesture != Gesture::Draw)
        return;
    const QPointF scene = toScene(itemPos);
    const double pressure = m_live.tool == drawing::Tool::Highlighter || !m_liveStylus ? 0.5 : p.pressure;
    if (!m_live.points.isEmpty()) {
        const drawing::StrokePoint &last = m_live.points.last();
        if (std::hypot(scene.x() - last.x, scene.y() - last.y) * m_zoom < 0.75)
            return; // too close to show
    }
    if (m_liveStylus) {
        m_liveMinPressure = std::min(m_liveMinPressure, pressure);
        m_liveMaxPressure = std::max(m_liveMaxPressure, pressure);
    }
    m_live.points.append({scene.x(), scene.y(), pressure});
    m_lastItemPos = itemPos;
    updateLive();
}

void DrawingCanvas::finishStroke()
{
    if (m_gesture != Gesture::Draw)
        return;
    drawing::Stroke s = std::move(m_live);
    m_live = drawing::Stroke();
    setGesture(Gesture::None);
    updateLive();
    if (s.points.isEmpty())
        return;
    // A stylus whose pressure never changed (some report only 0 or 1) draws as a mouse does.
    if (s.tool == drawing::Tool::Pen && m_liveStylus && m_liveMaxPressure - m_liveMinPressure < 0.02)
        s.simulatePressure = true;
    if (s.simulatePressure) {
        for (drawing::StrokePoint &pt : s.points)
            pt.pressure = 0.5;
    }
    drawing::normalizeStroke(s);
    s.id = newId();
    s.seed = int(QRandomGenerator::global()->bounded(1, 2147483647));
    s.versionNonce = int(QRandomGenerator::global()->bounded(1, 2147483647));
    s.version = 1;
    s.updated = m_clock ? m_clock() : 0;
    Item item = makeItem(std::move(s));
    Op op;
    op.kind = Op::Add;
    op.items.append({int(m_items.size()), item});
    m_items.append(item);
    drawItemIntoCache(item);
    update();
    push(std::move(op));
}

void DrawingCanvas::cancelStroke()
{
    if (m_gesture != Gesture::Draw)
        return;
    m_live = drawing::Stroke();
    setGesture(Gesture::None);
    updateLive();
}

void DrawingCanvas::beginErase(QPointF itemPos)
{
    if (!m_editable)
        return;
    m_erasing.clear();
    setGesture(Gesture::Erase);
    m_lastItemPos = itemPos;
    eraseTo(itemPos);
}

void DrawingCanvas::eraseTo(QPointF itemPos)
{
    if (m_gesture != Gesture::Erase)
        return;
    const QPointF a = toScene(m_lastItemPos);
    const QPointF b = toScene(itemPos);
    const double r = EraserRadius / m_zoom;
    const QRectF sweep = QRectF(a, b).normalized().adjusted(-r, -r, r, r);
    bool erased = false;
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (m_erasing.contains(int(i)))
            continue;
        const Item &item = m_items.at(i);
        if (!item.bounds.intersects(sweep))
            continue;
        const double reach = r + item.stroke.width * 4.25 * 0.5;
        const QList<drawing::StrokePoint> &pts = item.stroke.points;
        bool hit = false;
        for (qsizetype k = 0; k < pts.size() && !hit; ++k) {
            const QPointF p(pts.at(k).x, pts.at(k).y);
            const QPointF q = k + 1 < pts.size() ? QPointF(pts.at(k + 1).x, pts.at(k + 1).y) : p;
            hit = segmentDistance(a, b, p, q) <= reach;
        }
        if (hit) {
            m_erasing.insert(int(i));
            erased = true;
        }
    }
    m_lastItemPos = itemPos;
    if (erased)
        invalidateCache();
    else
        update();
}

void DrawingCanvas::finishErase()
{
    if (m_gesture != Gesture::Erase)
        return;
    setGesture(Gesture::None);
    if (m_erasing.isEmpty()) {
        update();
        return;
    }
    QList<int> indices(m_erasing.cbegin(), m_erasing.cend());
    std::sort(indices.begin(), indices.end());
    m_erasing.clear();
    Op op;
    op.kind = Op::Remove;
    for (const int i : std::as_const(indices))
        op.items.append({i, m_items.at(i)});
    apply(op, true);
    push(std::move(op));
}

void DrawingCanvas::beginPan(QPointF itemPos)
{
    setGesture(Gesture::Pan);
    m_panStart = itemPos;
    m_panCenter = m_center;
    m_lastItemPos = itemPos;
}

void DrawingCanvas::panTo(QPointF itemPos)
{
    if (m_gesture != Gesture::Pan)
        return;
    m_center = m_panCenter - (itemPos - m_panStart) / m_zoom;
    m_lastItemPos = itemPos;
    viewMoved(true);
}

} // namespace brulion::drawingview
