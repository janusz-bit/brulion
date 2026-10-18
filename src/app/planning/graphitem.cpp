#include "graphitem.h"

#include "graphmodel.h"

#include <QCursor>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPointingDevice>

#include <algorithm>
#include <cmath>

namespace brulion::planning {

namespace {

constexpr double kFitMargin = 36;
constexpr double kMaxFitZoom = 2.2;
constexpr int kAntialiasNodeLimit = 900;
constexpr double kLabelMaxWidth = 220;
constexpr int kAntialiasEdgeLimit = 2500;
constexpr double kSpriteMaxRadius = 24;
constexpr int kMaxSprites = 600;
/// Throttled repaints of a moving time-boxed layout: at least this far apart, at most this far.
constexpr int kMinMovingRepaintMs = 66;
constexpr int kMaxMovingRepaintMs = 600;

QColor withAlpha(QColor c, double factor)
{
    c.setAlphaF(float(std::clamp(c.alphaF() * factor, 0.0, 1.0)));
    return c;
}

double distance(QPointF a, QPointF b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

} // namespace

GraphItem::GraphItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setAcceptTouchEvents(true);
    setOpaquePainting(true);
    setAntialiasing(true);
    setFillColor(m_bg);
    m_font.setPixelSize(11);
    m_camera.setStartValue(0.0);
    m_camera.setEndValue(1.0);
    m_camera.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_camera, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        const double t = v.toDouble();
        m_cx = m_camFrom[0] + (m_camTo[0] - m_camFrom[0]) * t;
        m_cy = m_camFrom[1] + (m_camTo[1] - m_camFrom[1]) * t;
        const double z0 = std::max(1e-6, m_camFrom[2]);
        m_zoom = z0 * std::pow(m_camTo[2] / z0, t);
        Q_EMIT viewChanged();
        emitHighlightPosition();
        update();
    });
    connect(&m_camera, &QVariantAnimation::finished, this, [this] { update(); });
    m_clock.start();
    m_repaintTimer.setSingleShot(true);
    connect(&m_repaintTimer, &QTimer::timeout, this, [this] { update(); });
}

GraphItem::~GraphItem() = default;

// ---- properties ----------------------------------------------------------------------------

void GraphItem::setModel(GraphModel *model)
{
    if (m_model == model)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    m_hovered = -1;
    m_selected = -1;
    m_labels.clear();
    m_labelTexts.clear();
    if (m_model) {
        connect(m_model, &GraphModel::positionsChanged, this, &GraphItem::onPositionsChanged);
        connect(m_model, &GraphModel::structureChanged, this, &GraphItem::onStructureChanged);
        connect(m_model, &GraphModel::runningChanged, this, [this] { update(); });
        onStructureChanged();
    }
    Q_EMIT modelChanged();
    update();
}

void GraphItem::setZoom(qreal zoom)
{
    zoom = std::clamp(double(zoom), m_minZoom, m_maxZoom);
    if (qFuzzyCompare(m_zoom, zoom))
        return;
    m_zoom = zoom;
    Q_EMIT viewChanged();
    emitHighlightPosition();
    update();
}

void GraphItem::setCenterX(qreal x)
{
    if (qFuzzyCompare(m_cx, double(x)))
        return;
    m_cx = x;
    Q_EMIT viewChanged();
    emitHighlightPosition();
    update();
}

void GraphItem::setCenterY(qreal y)
{
    if (qFuzzyCompare(m_cy, double(y)))
        return;
    m_cy = y;
    Q_EMIT viewChanged();
    emitHighlightPosition();
    update();
}

void GraphItem::setMinZoom(qreal z)
{
    m_minZoom = std::max(0.001, double(z));
    Q_EMIT viewChanged();
}

void GraphItem::setMaxZoom(qreal z)
{
    m_maxZoom = std::max(m_minZoom, double(z));
    Q_EMIT viewChanged();
}

void GraphItem::setLabelZoom(qreal z)
{
    if (qFuzzyCompare(m_labelZoom, double(z)))
        return;
    m_labelZoom = z;
    Q_EMIT viewChanged();
    update();
}

void GraphItem::setAutoFit(bool on)
{
    if (m_autoFit == on)
        return;
    m_autoFit = on;
    Q_EMIT autoFitChanged();
    if (on)
        applyAutoFit();
}

void GraphItem::setAnimationDuration(int ms)
{
    ms = std::max(0, ms);
    if (m_animationDuration == ms)
        return;
    m_animationDuration = ms;
    Q_EMIT animationDurationChanged();
}

void GraphItem::setFitTopMargin(qreal margin)
{
    if (qFuzzyCompare(m_fitTop, double(margin)))
        return;
    m_fitTop = margin;
    Q_EMIT fitMarginsChanged();
    applyAutoFit();
    update();
}

void GraphItem::setFitBottomMargin(qreal margin)
{
    if (qFuzzyCompare(m_fitBottom, double(margin)))
        return;
    m_fitBottom = margin;
    Q_EMIT fitMarginsChanged();
    applyAutoFit();
    update();
}

void GraphItem::setTouchMode(bool on)
{
    if (m_touchMode == on)
        return;
    m_touchMode = on;
    if (on)
        setHovered(-1);
    Q_EMIT touchModeChanged();
}

#define BRULION_COLOR_SETTER(Name, member)                                                                             \
    void GraphItem::set##Name(const QColor &c)                                                                         \
    {                                                                                                                  \
        if (member == c)                                                                                               \
            return;                                                                                                    \
        member = c;                                                                                                    \
        Q_EMIT colorsChanged();                                                                                        \
        update();                                                                                                      \
    }

void GraphItem::setBackgroundColor(const QColor &c)
{
    if (m_bg == c)
        return;
    m_bg = c;
    m_sprites.clear(); // rings are filled with the background
    setFillColor(c);
    Q_EMIT colorsChanged();
    update();
}
BRULION_COLOR_SETTER(EdgeColor, m_edge)
BRULION_COLOR_SETTER(HighlightColor, m_highlight)
BRULION_COLOR_SETTER(PageColor, m_page)
BRULION_COLOR_SETTER(JournalColor, m_journal)
BRULION_COLOR_SETTER(TagColor, m_tag)
BRULION_COLOR_SETTER(MissingColor, m_missing)
BRULION_COLOR_SETTER(FocusColor, m_focus)
BRULION_COLOR_SETTER(LabelColor, m_label)
BRULION_COLOR_SETTER(LabelBackground, m_labelBg)
#undef BRULION_COLOR_SETTER

void GraphItem::setFont(const QFont &font)
{
    if (m_font == font)
        return;
    m_font = font;
    m_labels.clear();
    m_labelTexts.clear();
    Q_EMIT fontChanged();
    update();
}

QString GraphItem::hoveredId() const
{
    return m_hoveredIdCache;
}

QString GraphItem::selectedId() const
{
    return m_selectedIdCache;
}

void GraphItem::setSelectedId(const QString &id)
{
    if (m_selectedIdCache == id)
        return;
    const QString before = highlightedId();
    m_selectedIdCache = id;
    m_selected = m_model && !id.isEmpty() ? m_model->indexOf(id) : -1;
    Q_EMIT selectedIdChanged();
    if (highlightedId() != before)
        Q_EMIT highlightedIdChanged();
    emitHighlightPosition();
    update();
}

QString GraphItem::highlightedId() const
{
    return !m_hoveredIdCache.isEmpty() ? m_hoveredIdCache : m_selectedIdCache;
}

QPointF GraphItem::highlightedPosition() const
{
    const int hl = m_hovered >= 0 ? m_hovered : m_selected;
    if (!m_model || hl < 0 || hl >= m_model->nodeCount())
        return {};
    const QPointF p = m_model->position(hl);
    return toScreen(p.x(), p.y());
}

// ---- camera --------------------------------------------------------------------------------

QPointF GraphItem::toScreen(double x, double y) const
{
    return {(x - m_cx) * m_zoom + width() / 2, (y - m_cy) * m_zoom + height() / 2};
}

QPointF GraphItem::toWorld(QPointF s) const
{
    return {(s.x() - width() / 2) / m_zoom + m_cx, (s.y() - height() / 2) / m_zoom + m_cy};
}

void GraphItem::stopCameraAnimation()
{
    if (m_camera.state() == QAbstractAnimation::Running)
        m_camera.stop();
}

void GraphItem::animateCamera(double cx, double cy, double zoom, bool animated)
{
    zoom = std::clamp(zoom, m_minZoom, m_maxZoom);
    stopCameraAnimation();
    if (!animated || m_animationDuration <= 0 || !isVisible()) {
        m_cx = cx;
        m_cy = cy;
        m_zoom = zoom;
        Q_EMIT viewChanged();
        emitHighlightPosition();
        update();
        return;
    }
    m_camFrom[0] = m_cx;
    m_camFrom[1] = m_cy;
    m_camFrom[2] = m_zoom;
    m_camTo[0] = cx;
    m_camTo[1] = cy;
    m_camTo[2] = zoom;
    m_camera.setDuration(m_animationDuration);
    m_camera.start();
}

void GraphItem::fitCamera(double &cx, double &cy, double &zoom) const
{
    const QRectF b = m_model ? m_model->bounds() : QRectF();
    // Labels hang below the nodes: a little extra room at the bottom.
    const double availW = std::max(40.0, width() - 2 * kFitMargin);
    const double availH = std::max(40.0, height() - m_fitTop - m_fitBottom - 14);
    zoom = std::clamp(std::min(availW / std::max(1.0, b.width()), availH / std::max(1.0, b.height())), m_minZoom,
                      std::min(m_maxZoom, kMaxFitZoom));
    cx = b.center().x();
    // The free area's centre is (m_fitTop - m_fitBottom) / 2 px below the item's centre.
    cy = b.center().y() - (m_fitTop - m_fitBottom - 14) / 2 / zoom;
}

void GraphItem::applyAutoFit()
{
    if (!m_autoFit || !m_model || width() <= 0 || height() <= 0)
        return;
    if (m_model->nodeCount() == 0) {
        m_cx = 0;
        m_cy = 0;
        m_zoom = 1;
        Q_EMIT viewChanged();
        return;
    }
    fitCamera(m_cx, m_cy, m_zoom);
    Q_EMIT viewChanged();
}

void GraphItem::fitAll(bool animated)
{
    if (!m_model || m_model->nodeCount() == 0)
        return;
    double cx = 0, cy = 0, z = 1;
    fitCamera(cx, cy, z);
    animateCamera(cx, cy, z, animated);
}

void GraphItem::resetView()
{
    stopCameraAnimation();
    m_follow.clear();
    m_autoFit = false; // setAutoFit(true) below always fits
    setAutoFit(true);
    update();
}

void GraphItem::centerOn(const QString &id, qreal zoom, bool animated)
{
    if (!m_model)
        return;
    const int i = m_model->indexOf(id);
    if (i < 0)
        return;
    if (m_autoFit) {
        m_autoFit = false;
        Q_EMIT autoFitChanged();
    }
    const QPointF p = m_model->position(i);
    m_follow = id;
    animateCamera(p.x(), p.y(), zoom > 0 ? zoom : std::max(m_zoom, m_labelZoom * 1.15), animated);
}

void GraphItem::zoomBy(qreal factor, qreal x, qreal y)
{
    if (factor <= 0)
        return;
    stopCameraAnimation();
    const QPointF p = (x < 0 || y < 0) ? QPointF(width() / 2, height() / 2) : QPointF(x, y);
    const QPointF world = toWorld(p);
    const double z = std::clamp(m_zoom * factor, m_minZoom, m_maxZoom);
    m_zoom = z;
    m_cx = world.x() - (p.x() - width() / 2) / z;
    m_cy = world.y() - (p.y() - height() / 2) / z;
    userMoved();
    Q_EMIT viewChanged();
    emitHighlightPosition();
    update();
}

void GraphItem::panBy(qreal dx, qreal dy)
{
    stopCameraAnimation();
    m_cx -= dx / m_zoom;
    m_cy -= dy / m_zoom;
    userMoved();
    Q_EMIT viewChanged();
    emitHighlightPosition();
    update();
}

void GraphItem::userMoved()
{
    m_follow.clear();
    if (m_autoFit) {
        m_autoFit = false;
        Q_EMIT autoFitChanged();
    }
    Q_EMIT userMovedView();
}

// ---- model notifications ----------------------------------------------------------------------

void GraphItem::onStructureChanged()
{
    m_labels.clear();
    m_labelTexts.clear();
    const QString before = highlightedId();
    m_hovered = m_model && !m_hoveredIdCache.isEmpty() ? m_model->indexOf(m_hoveredIdCache) : -1;
    if (m_hovered < 0 && !m_hoveredIdCache.isEmpty()) {
        m_hoveredIdCache.clear();
        Q_EMIT hoveredIdChanged();
    }
    m_selected = m_model && !m_selectedIdCache.isEmpty() ? m_model->indexOf(m_selectedIdCache) : -1;
    if (m_selected < 0 && !m_selectedIdCache.isEmpty()) {
        m_selectedIdCache.clear();
        Q_EMIT selectedIdChanged();
    }
    if (highlightedId() != before)
        Q_EMIT highlightedIdChanged();
    applyAutoFit();
    emitHighlightPosition();
    update();
}

void GraphItem::onPositionsChanged()
{
    // Keep a node chosen with centerOn() in the middle while the layout settles.
    if (!m_follow.isEmpty() && m_model) {
        const int i = m_model->indexOf(m_follow);
        if (i < 0) {
            m_follow.clear();
        } else {
            const QPointF p = m_model->position(i);
            if (m_camera.state() == QAbstractAnimation::Running) {
                m_camTo[0] = p.x();
                m_camTo[1] = p.y();
            } else {
                m_cx = p.x();
                m_cy = p.y();
                Q_EMIT viewChanged();
            }
        }
    }
    applyAutoFit();
    emitHighlightPosition();
    repaintMoving();
}

int GraphItem::movingRepaintInterval(double paintMs)
{
    return std::clamp(int(std::ceil(3 * paintMs)), kMinMovingRepaintMs, kMaxMovingRepaintMs);
}

void GraphItem::repaintMoving()
{
    // Slow devices: a frame of the moving layout costs more than the simulation steps between
    // them (QPainter rasterises everything, on the GUI thread or while it waits): repaint now and
    // then, so the steps — and the user's input — get most of the time. The settled layout is
    // painted at once (runningChanged), so are gestures and camera moves (they call update()).
    const bool throttle = m_model && m_model->timeBoxed() && m_model->running() && m_gesture == Gesture::None
                          && m_camera.state() != QAbstractAnimation::Running;
    if (!throttle) {
        m_repaintTimer.stop();
        update();
        return;
    }
    if (m_repaintTimer.isActive())
        return;
    const qint64 last = m_lastPaintEnd.load(std::memory_order_relaxed);
    const qint64 interval = movingRepaintInterval(lastPaintMs());
    const qint64 since = last < 0 ? interval : (m_clock.nsecsElapsed() - last) / 1000000;
    if (since >= interval)
        update();
    else
        m_repaintTimer.start(int(interval - since));
}

void GraphItem::emitHighlightPosition()
{
    if (m_hovered >= 0 || m_selected >= 0)
        Q_EMIT highlightedPositionChanged();
}

void GraphItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        applyAutoFit();
        emitHighlightPosition();
        update();
    }
}

// ---- hit testing, hover, selection -------------------------------------------------------------

int GraphItem::nodeIndexAt(QPointF pos) const
{
    if (!m_model)
        return -1;
    const auto &nodes = m_model->nodes();
    const ForceLayout &layout = m_model->layout();
    const double slack = m_touchMode ? 20 : 5; // px around the drawn disc
    int best = -1;
    double bestD = 0;
    for (int i = 0; i < nodes.size() && i < layout.nodeCount(); ++i) {
        const QPointF s = toScreen(layout.x(i), layout.y(i));
        const double r = std::max(1.6, nodes.at(i).radius * m_zoom);
        const double dist = distance(s, pos);
        if (dist <= r + slack && (best < 0 || dist - r < bestD)) {
            best = i;
            bestD = dist - r;
        }
    }
    return best;
}

QString GraphItem::nodeAt(qreal x, qreal y) const
{
    const int i = nodeIndexAt(QPointF(x, y));
    return i >= 0 && m_model ? m_model->nodes().at(i).id : QString();
}

QPointF GraphItem::nodePosition(const QString &id) const
{
    if (!m_model)
        return {};
    const int i = m_model->indexOf(id);
    if (i < 0)
        return {};
    const QPointF p = m_model->position(i);
    return toScreen(p.x(), p.y());
}

void GraphItem::setHovered(int node)
{
    if (m_hovered == node)
        return;
    const QString before = highlightedId();
    m_hovered = node;
    m_hoveredIdCache = node >= 0 && m_model ? m_model->nodes().at(node).id : QString();
#if QT_CONFIG(cursor)
    setCursor(node >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
#endif
    Q_EMIT hoveredIdChanged();
    if (highlightedId() != before)
        Q_EMIT highlightedIdChanged();
    emitHighlightPosition();
    update();
}

void GraphItem::setSelected(int node)
{
    setSelectedId(node >= 0 && m_model ? m_model->nodes().at(node).id : QString());
}

void GraphItem::setInteracting(bool on)
{
    if (m_interacting == on)
        return;
    m_interacting = on;
    Q_EMIT interactingChanged();
    if (!on)
        update(); // the settled frame is drawn with antialiasing
}

// ---- pointer input -------------------------------------------------------------------------

void GraphItem::pressAt(QPointF pos)
{
    stopCameraAnimation();
    m_pressPos = pos;
    m_lastPos = pos;
    m_pressNode = nodeIndexAt(pos);
    m_gesture = Gesture::Pending;
}

void GraphItem::moveTo(QPointF pos)
{
    if (m_gesture == Gesture::Pending) {
        const double threshold = m_touchMode ? 10 : 4;
        if (distance(pos, m_pressPos) < threshold)
            return;
        if (m_pressNode >= 0 && m_model) {
            m_gesture = Gesture::DragNode;
            m_model->beginDrag(m_pressNode);
            userMoved();
        } else {
            m_gesture = Gesture::Pan;
        }
        setInteracting(true);
#if QT_CONFIG(cursor)
        setCursor(m_gesture == Gesture::Pan ? Qt::ClosedHandCursor : Qt::DragMoveCursor);
#endif
    }
    if (m_gesture == Gesture::DragNode && m_model) {
        m_model->dragTo(m_pressNode, toWorld(pos));
    } else if (m_gesture == Gesture::Pan) {
        const QPointF delta = pos - m_lastPos;
        m_cx -= delta.x() / m_zoom;
        m_cy -= delta.y() / m_zoom;
        userMoved();
        Q_EMIT viewChanged();
        emitHighlightPosition();
        update();
    }
    m_lastPos = pos;
}

void GraphItem::releaseAt(QPointF pos)
{
    Q_UNUSED(pos);
    if (m_gesture == Gesture::Pending) {
        if (m_pressNode >= 0 && m_model) {
            const QString id = m_model->nodes().at(m_pressNode).id;
            if (m_touchMode) {
                if (m_selected == m_pressNode) {
                    Q_EMIT nodeActivated(id);
                } else {
                    setSelected(m_pressNode);
                    Q_EMIT nodeTapped(id);
                }
            } else {
                setSelected(m_pressNode);
                Q_EMIT nodeActivated(id);
            }
        } else {
            setSelected(-1);
            Q_EMIT backgroundTapped();
        }
    } else if (m_gesture == Gesture::DragNode && m_model) {
        m_model->endDrag(m_pressNode);
    }
    m_gesture = Gesture::None;
    m_pressNode = -1;
    setInteracting(false);
#if QT_CONFIG(cursor)
    setCursor(m_hovered >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
#endif
}

void GraphItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    if (activeFocusOnTab() || (flags() & ItemIsFocusScope))
        forceActiveFocus(Qt::MouseFocusReason);
    pressAt(event->position());
    event->accept();
}

void GraphItem::mouseMoveEvent(QMouseEvent *event)
{
    moveTo(event->position());
    event->accept();
}

void GraphItem::mouseReleaseEvent(QMouseEvent *event)
{
    releaseAt(event->position());
    event->accept();
}

void GraphItem::mouseUngrabEvent()
{
    if (m_gesture == Gesture::DragNode && m_model)
        m_model->endDrag(m_pressNode);
    m_gesture = Gesture::None;
    m_pressNode = -1;
    setInteracting(false);
}

void GraphItem::hoverMoveEvent(QHoverEvent *event)
{
    if (!m_touchMode && m_gesture == Gesture::None)
        setHovered(nodeIndexAt(event->position()));
}

void GraphItem::hoverLeaveEvent(QHoverEvent *)
{
    setHovered(-1);
}

void GraphItem::wheelEvent(QWheelEvent *event)
{
    const QPoint pixels = event->pixelDelta();
    const bool touchpadScroll = event->phase() != Qt::NoScrollPhase && !pixels.isNull()
                                && !(event->modifiers() & Qt::ControlModifier);
    if (touchpadScroll) {
        panBy(pixels.x(), pixels.y());
    } else {
        const int steps = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
        if (steps == 0) {
            event->ignore();
            return;
        }
        zoomBy(std::pow(1.0018, double(steps)), event->position().x(), event->position().y());
    }
    event->accept();
}

void GraphItem::touchEvent(QTouchEvent *event)
{
    QList<QPointF> down;
    for (const QEventPoint &p : event->points()) {
        if (p.state() != QEventPoint::Released)
            down.append(p.position());
    }
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
        if (down.size() >= 2) {
            const QPointF a = down.at(0);
            const QPointF b = down.at(1);
            const QPointF mid = (a + b) / 2;
            if (m_gesture != Gesture::Pinch) {
                if (m_gesture == Gesture::DragNode && m_model)
                    m_model->endDrag(m_pressNode);
                stopCameraAnimation();
                m_gesture = Gesture::Pinch;
                m_pinchStartDistance = std::max(1.0, distance(a, b));
                m_pinchStartZoom = m_zoom;
                m_pinchWorldCenter = toWorld(mid);
                setInteracting(true);
            } else {
                const double z = std::clamp(m_pinchStartZoom * distance(a, b) / m_pinchStartDistance, m_minZoom, m_maxZoom);
                m_zoom = z;
                m_cx = m_pinchWorldCenter.x() - (mid.x() - width() / 2) / z;
                m_cy = m_pinchWorldCenter.y() - (mid.y() - height() / 2) / z;
                userMoved();
                Q_EMIT viewChanged();
                emitHighlightPosition();
                update();
            }
        } else if (down.size() == 1) {
            const QPointF p = down.first();
            if (m_gesture == Gesture::Pinch) {
                // One finger left: keep panning with it.
                m_gesture = Gesture::Pan;
                m_lastPos = p;
            } else if (event->type() == QEvent::TouchBegin || m_gesture == Gesture::None) {
                pressAt(p);
            } else {
                moveTo(p);
            }
        }
        break;
    case QEvent::TouchEnd:
        if (m_gesture == Gesture::Pinch || m_gesture == Gesture::Pan) {
            m_gesture = Gesture::None;
            setInteracting(false);
        } else if (!event->points().isEmpty()) {
            releaseAt(event->points().first().position());
        }
        break;
    case QEvent::TouchCancel:
        touchUngrabEvent();
        break;
    default: break;
    }
    event->accept();
}

void GraphItem::touchUngrabEvent()
{
    mouseUngrabEvent();
}

bool GraphItem::event(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture) {
        auto *g = static_cast<QNativeGestureEvent *>(event);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
            zoomBy(1.0 + g->value(), g->position().x(), g->position().y());
            event->accept();
            return true;
        }
    }
    return QQuickPaintedItem::event(event);
}

// ---- painting ------------------------------------------------------------------------------

const QStaticText &GraphItem::labelFor(int node)
{
    auto it = m_labels.find(node);
    if (it != m_labels.end())
        return it.value();
    const QString title = m_model ? m_model->nodes().at(node).title : QString();
    const QFontMetricsF fm(m_font);
    QStaticText st(fm.elidedText(title, Qt::ElideRight, kLabelMaxWidth));
    st.setTextFormat(Qt::PlainText);
    st.setPerformanceHint(QStaticText::AggressiveCaching);
    st.prepare(QTransform(), m_font);
    return m_labels.insert(node, st).value();
}

const QImage &GraphItem::spriteFor(const QColor &color, bool ring, double radius, qreal dpr)
{
    // Radius in quarter pixels; colour with alpha; ring style; device pixel ratio.
    const quint64 q = quint64(std::lround(radius * 4));
    const quint64 key = (quint64(color.rgba()) << 32) ^ (q << 2) ^ (ring ? 1u : 0u) ^ (quint64(std::lround(dpr * 8)) << 20);
    auto it = m_sprites.find(key);
    if (it != m_sprites.end())
        return it.value();
    if (m_sprites.size() > kMaxSprites)
        m_sprites.clear();
    const double r = double(q) / 4.0;
    const double ringWidth = ring ? std::clamp(r * 0.35, 1.0, 2.0) : 0;
    const int side = int(std::ceil((2 * r + 2 + ringWidth) * dpr));
    QImage img(side, side, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QPointF centre(side / dpr / 2, side / dpr / 2);
        if (ring) {
            p.setPen(QPen(color, ringWidth));
            p.setBrush(m_bg);
            p.drawEllipse(centre, r - ringWidth / 2, r - ringWidth / 2);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawEllipse(centre, r, r);
        }
    }
    return m_sprites.insert(key, img).value();
}

void GraphItem::paint(QPainter *painter)
{
    render(painter, size());
}

void GraphItem::render(QPainter *painter, const QSizeF &size)
{
    QElapsedTimer timer;
    timer.start();
    // Painting may run on the render thread (while the GUI thread waits): atomics for the timing.
    const auto finished = [&] {
        const double ms = double(timer.nsecsElapsed()) / 1e6;
        m_lastPaintMs.store(ms, std::memory_order_relaxed);
        m_lastPaintEnd.store(m_clock.nsecsElapsed(), std::memory_order_relaxed);
        if (m_model)
            m_model->reportPaintCost(ms);
    };
    painter->fillRect(QRectF(QPointF(0, 0), size), m_bg);
    if (!m_model || m_model->nodeCount() == 0) {
        finished();
        return;
    }
    const auto &nodes = m_model->nodes();
    const ForceLayout &layout = m_model->layout();
    const auto &edges = m_model->edges();
    const auto &adjacency = m_model->adjacency();
    const int n = std::min(int(nodes.size()), layout.nodeCount());
    const double w = size.width();
    const double h = size.height();
    const double z = m_zoom;
    const auto sx = [&](double x) { return (x - m_cx) * z + w / 2; };
    const auto sy = [&](double y) { return (y - m_cy) * z + h / 2; };

    const int hl = m_hovered >= 0 ? m_hovered : m_selected;
    std::vector<char> hood;
    if (hl >= 0 && hl < n) {
        hood.assign(size_t(n), 0);
        hood[size_t(hl)] = 1;
        for (const int j : adjacency.at(hl))
            hood[size_t(j)] = 1;
    }
    const bool dimming = !hood.empty();
    const int focus = m_model->focusPath().isEmpty() ? -1 : m_model->indexOf(m_model->focusPath());

    const bool busy = m_model->running() || m_gesture != Gesture::None
                      || m_camera.state() == QAbstractAnimation::Running;
    const bool smooth = !busy || n < kAntialiasNodeLimit;
    painter->setRenderHint(QPainter::Antialiasing, smooth);
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;

    // Screen positions + visibility (with a margin for discs and labels).
    std::vector<QPointF> pos(static_cast<size_t>(n));
    std::vector<char> visible(size_t(n), 0);
    const double margin = 40;
    for (int i = 0; i < n; ++i) {
        const QPointF p(sx(layout.x(i)), sy(layout.y(i)));
        pos[size_t(i)] = p;
        const double r = std::max(1.6, nodes.at(i).radius * z);
        visible[size_t(i)] = p.x() + r >= -margin && p.x() - r <= w + margin && p.y() + r >= -margin
                             && p.y() - r <= h + margin;
    }

    // Edges: one batched call for the normal ones, one for the highlighted neighbourhood.
    QList<QLineF> normal;
    QList<QLineF> strong;
    normal.reserve(qsizetype(edges.size()));
    for (const auto &[a, b] : edges) {
        if (a >= n || b >= n)
            continue;
        const QPointF &pa = pos[size_t(a)];
        const QPointF &pb = pos[size_t(b)];
        // Cull segments entirely on one side of the viewport.
        if ((pa.x() < 0 && pb.x() < 0) || (pa.x() > w && pb.x() > w) || (pa.y() < 0 && pb.y() < 0)
            || (pa.y() > h && pb.y() > h))
            continue;
        if (dimming && (a == hl || b == hl))
            strong.append(QLineF(pa, pb));
        else
            normal.append(QLineF(pa, pb));
    }
    const double edgeWidth = std::clamp(z * 0.9, 0.6, 1.6);
    QPen edgePen(dimming ? withAlpha(m_edge, 0.45) : m_edge, edgeWidth);
    edgePen.setCosmetic(true);
    painter->setPen(edgePen);
    // Thousands of antialiased lines dominate a frame: draw them aliased while busy / when many.
    painter->setRenderHint(QPainter::Antialiasing, smooth && normal.size() <= kAntialiasEdgeLimit);
    painter->drawLines(normal);
    painter->setRenderHint(QPainter::Antialiasing, smooth);
    if (!strong.isEmpty()) {
        QPen strongPen(m_highlight, edgeWidth * 1.7);
        strongPen.setCosmetic(true);
        painter->setPen(strongPen);
        painter->drawLines(strong);
    }

    // Nodes, grouped by colour: dimmed ones below, then the rest.
    const auto colorOf = [&](GraphModel::Kind k) -> const QColor & {
        switch (k) {
        case GraphModel::Kind::Journal: return m_journal;
        case GraphModel::Kind::Tag: return m_tag;
        case GraphModel::Kind::Missing: return m_missing;
        case GraphModel::Kind::Page: break;
        }
        return m_page;
    };
    static constexpr GraphModel::Kind kinds[] = {GraphModel::Kind::Journal, GraphModel::Kind::Page,
                                                 GraphModel::Kind::Tag, GraphModel::Kind::Missing};
    for (int pass = 0; pass < 2; ++pass) {
        const bool dimPass = pass == 0;
        if (dimPass && !dimming)
            continue;
        for (const GraphModel::Kind kind : kinds) {
            const QColor base = colorOf(kind);
            const QColor c = dimPass ? withAlpha(base, 0.28) : base;
            if (kind == GraphModel::Kind::Missing) {
                QPen ring(c, std::clamp(z * 1.4, 1.0, 2.0));
                painter->setPen(ring);
                painter->setBrush(m_bg);
            } else {
                painter->setPen(Qt::NoPen);
                painter->setBrush(c);
            }
            for (int i = 0; i < n; ++i) {
                if (!visible[size_t(i)] || nodes.at(i).kind != kind)
                    continue;
                if (dimming && (hood[size_t(i)] == 0) != dimPass)
                    continue;
                const double r = std::max(1.6, nodes.at(i).radius * z);
                if (r > kSpriteMaxRadius) {
                    painter->drawEllipse(pos[size_t(i)], r, r);
                    continue;
                }
                // Small discs (the common case) are blitted from cached antialiased sprites:
                // much cheaper than rasterising thousands of ellipses per frame.
                const QImage &sprite = spriteFor(c, kind == GraphModel::Kind::Missing, r, dpr);
                const double half = sprite.deviceIndependentSize().width() / 2;
                painter->drawImage(QPointF(pos[size_t(i)].x() - half, pos[size_t(i)].y() - half), sprite);
            }
        }
    }
    // Rings: the local graph's centre and the highlighted node.
    painter->setBrush(Qt::NoBrush);
    const auto ring = [&](int i, const QColor &c, double gap) {
        if (i < 0 || i >= n)
            return;
        const double r = std::max(1.6, nodes.at(i).radius * z) + gap;
        QPen pen(c, 2);
        painter->setPen(pen);
        painter->drawEllipse(pos[size_t(i)], r, r);
    };
    ring(focus, m_focus, 3);
    ring(hl, m_highlight, 2.5);

    // Labels: everything visible at sufficient zoom; otherwise the highlighted neighbourhood,
    // the local centre and the best connected nodes. No overlaps (except the highlighted one).
    // A time-boxed layout on the move: only the highlighted node and the local centre (labels
    // are the costly part of a frame, and they would only flicker past).
    const bool sparse = m_model->timeBoxed() && m_model->running();
    const bool all = z >= m_labelZoom && !sparse;
    std::vector<int> candidates;
    if (sparse) {
        if (hl >= 0 && hl < n && visible[size_t(hl)])
            candidates.push_back(hl);
        if (focus >= 0 && visible[size_t(focus)])
            candidates.push_back(focus);
    } else if (all) {
        for (int i = 0; i < n; ++i) {
            if (visible[size_t(i)])
                candidates.push_back(i);
        }
    } else {
        std::vector<int> byDegree;
        if (z >= m_labelZoom * 0.2) {
            for (int i = 0; i < n; ++i) {
                if (visible[size_t(i)] && nodes.at(i).degree > 0)
                    byDegree.push_back(i);
            }
            // The hubs first: a few when far away, more when closer.
            const size_t top = std::min<size_t>(byDegree.size(), z >= m_labelZoom * 0.7 ? 40 : z >= m_labelZoom * 0.45 ? 14 : 8);
            std::partial_sort(byDegree.begin(), byDegree.begin() + qsizetype(top), byDegree.end(),
                              [&](int a, int b) { return nodes.at(a).degree > nodes.at(b).degree; });
            byDegree.resize(top);
        }
        if (dimming) {
            for (int i = 0; i < n; ++i) {
                if (hood[size_t(i)] && visible[size_t(i)])
                    candidates.push_back(i);
            }
        }
        if (focus >= 0 && visible[size_t(focus)])
            candidates.push_back(focus);
        candidates.insert(candidates.end(), byDegree.begin(), byDegree.end());
    }
    const auto priority = [&](int i) {
        if (i == hl)
            return 0;
        if (i == focus)
            return 1;
        if (dimming && hood[size_t(i)])
            return 2;
        return 3;
    };
    std::stable_sort(candidates.begin(), candidates.end(), [&](int a, int b) {
        const int pa = priority(a), pb = priority(b);
        if (pa != pb)
            return pa < pb;
        return nodes.at(a).degree > nodes.at(b).degree;
    });
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    painter->setFont(m_font);
    const double cell = 64;
    QHash<quint64, QList<QRectF>> grid;
    const auto cellKey = [](int cx, int cy) { return (quint64(quint32(cx)) << 32) | quint32(cy); };
    std::vector<char> labelled(size_t(n), 0);
    int drawn = 0;
    const QColor labelDim = withAlpha(m_label, 0.4);
    for (const int i : candidates) {
        if (drawn >= MaxLabels)
            break;
        if (labelled[size_t(i)])
            continue;
        const QStaticText &st = labelFor(i);
        const QSizeF ts = st.size();
        const double r = std::max(1.6, nodes.at(i).radius * z);
        const QPointF p = pos[size_t(i)];
        const double lw = ts.width() + 6;
        const double lh = ts.height() + 2;
        // Below the node; if taken, above, right, left.
        const QRectF options[] = {QRectF(p.x() - lw / 2, p.y() + r + 2, lw, lh), QRectF(p.x() - lw / 2, p.y() - r - 2 - lh, lw, lh),
                                  QRectF(p.x() + r + 3, p.y() - lh / 2, lw, lh), QRectF(p.x() - r - 3 - lw, p.y() - lh / 2, lw, lh)};
        const auto overlaps = [&](const QRectF &rect) {
            const int c0 = int(std::floor(rect.left() / cell)), c1 = int(std::floor(rect.right() / cell));
            const int r0 = int(std::floor(rect.top() / cell)), r1 = int(std::floor(rect.bottom() / cell));
            for (int cx = c0; cx <= c1; ++cx) {
                for (int cy = r0; cy <= r1; ++cy) {
                    const auto it = grid.constFind(cellKey(cx, cy));
                    if (it == grid.cend())
                        continue;
                    for (const QRectF &o : it.value()) {
                        if (o.intersects(rect))
                            return true;
                    }
                }
            }
            return false;
        };
        QRectF rect;
        bool placed = false;
        for (const QRectF &candidate : options) {
            if (i == hl || !overlaps(candidate)) {
                rect = candidate;
                placed = true;
                break;
            }
        }
        if (!placed)
            continue;
        const int c0 = int(std::floor(rect.left() / cell)), c1 = int(std::floor(rect.right() / cell));
        const int r0 = int(std::floor(rect.top() / cell)), r1 = int(std::floor(rect.bottom() / cell));
        for (int cx = c0; cx <= c1; ++cx) {
            for (int cy = r0; cy <= r1; ++cy)
                grid[cellKey(cx, cy)].append(rect);
        }
        labelled[size_t(i)] = 1;
        ++drawn;
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_labelBg);
        painter->drawRoundedRect(rect, 3, 3);
        painter->setPen(dimming && !hood[size_t(i)] ? labelDim : m_label);
        painter->drawStaticText(QPointF(rect.left() + 3, rect.top() + 1), st);
    }
    finished();
}

} // namespace brulion::planning
