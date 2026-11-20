// CanvasEditor's pointer input: the mouse, touch, the wheel, a touchpad's pinch (canvaseditor.h).
#include "canvaseditor.h"

#include "canvasmodels.h"

#include "canvas/edit.h"
#include "canvas/geometry.h"

#include <QCursor>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QStyleHints>
#include <QTouchEvent>
#include <QWheelEvent>

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::canvasview {

namespace {

double distance(QPointF a, QPointF b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

Qt::CursorShape resizeCursor(int edges)
{
    const bool l = edges & Qt::LeftEdge;
    const bool r = edges & Qt::RightEdge;
    const bool t = edges & Qt::TopEdge;
    const bool b = edges & Qt::BottomEdge;
    if ((l && t) || (r && b))
        return Qt::SizeFDiagCursor;
    if ((r && t) || (l && b))
        return Qt::SizeBDiagCursor;
    if (l || r)
        return Qt::SizeHorCursor;
    return Qt::SizeVerCursor;
}

/// The side a new card dropped at the end of an edge gets it on: the one facing the start.
QString oppositeSide(const QString &side)
{
    if (side == u"left")
        return u"right"_s;
    if (side == u"right")
        return u"left"_s;
    if (side == u"top")
        return u"bottom"_s;
    return u"top"_s;
}

} // namespace

CanvasEditor::Hit CanvasEditor::hitAt(QPointF itemPos) const
{
    Hit h;
    const QPointF p = toCanvas(itemPos.x(), itemPos.y());
    const double r = m_handleRadius / m_zoom;
    // The single selected card: its corners and sides resize it.
    QString single;
    if (m_selNodes.size() == 1 && m_selEdges.isEmpty())
        single = *m_selNodes.cbegin();
    // The side handles of the card under the pointer and of the selected one.
    for (const QString &id : {m_hoverNode, single}) {
        if (id.isEmpty() || id == m_editingNode)
            continue;
        const canvas::Node *n = m_canvas.node(id);
        if (!n)
            continue;
        const QRectF rect = canvas::nodeRect(*n);
        for (const canvas::Side side :
             {canvas::Side::Top, canvas::Side::Right, canvas::Side::Bottom, canvas::Side::Left}) {
            if (distance(p, canvas::anchor(rect, side)) <= r) {
                h.part = Part::Handle;
                h.node = id;
                h.side = canvas::sideName(side);
                return h;
            }
        }
    }
    if (!single.isEmpty() && single != m_editingNode) {
        if (const canvas::Node *n = m_canvas.node(single)) {
            const QRectF rect = canvas::nodeRect(*n);
            const double band = r * 0.8;
            if (rect.adjusted(-band, -band, band, band).contains(p)) {
                int edges = 0;
                if (std::abs(p.x() - rect.left()) <= band)
                    edges |= Qt::LeftEdge;
                if (std::abs(p.x() - rect.right()) <= band)
                    edges |= Qt::RightEdge;
                if (std::abs(p.y() - rect.top()) <= band)
                    edges |= Qt::TopEdge;
                if (std::abs(p.y() - rect.bottom()) <= band)
                    edges |= Qt::BottomEdge;
                // A group's top edge is where it is dragged by (its label above it moves it too).
                if (edges != 0 && !(n->isGroup() && edges == Qt::TopEdge)) {
                    h.part = Part::Resize;
                    h.node = single;
                    h.edges = edges;
                    return h;
                }
            }
        }
    }
    const qsizetype i = canvas::nodeAt(m_canvas, p, true, 14 / m_zoom);
    if (i >= 0) {
        h.part = Part::Body;
        h.node = m_canvas.nodes.at(i).id;
        // A card's edge (not a group's): it may still be an edge passing over the card's border…
        return h;
    }
    const qsizetype e = canvas::edgeAt(m_canvas, p, r);
    if (e >= 0) {
        h.part = Part::Body;
        h.edge = m_canvas.edges.at(e).id;
    }
    return h;
}

void CanvasEditor::setGesture(Gesture g)
{
    if (m_gesture == g)
        return;
    m_gesture = g;
    Q_EMIT gestureChanged();
}

void CanvasEditor::pressAt(QPointF pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers, bool touch)
{
    m_viewAnimation->stop();
    m_pressPos = pos;
    m_lastPos = pos;
    m_pressCanvas = toCanvas(pos.x(), pos.y());
    m_pressModifiers = modifiers;
    m_pressButton = button;
    m_pressTouch = touch;
    m_longPressed = false;
    m_pressHit = Hit();
    if (button == Qt::MiddleButton || (m_panMode && button == Qt::LeftButton)) {
        setCursor(Qt::ClosedHandCursor);
        setGesture(Gesture::Pan);
        return;
    }
    const Hit h = hitAt(pos);
    m_pressHit = h;
    if (button == Qt::RightButton) {
        setGesture(Gesture::Press);
        return;
    }
    if (h.part == Part::Handle) {
        m_connectFrom = h.node;
        m_connectSide = h.side;
        setGesture(Gesture::Connect);
        updateConnect(pos);
        return;
    }
    if (h.part == Part::Resize) {
        m_gestureStart = m_canvas;
        if (const canvas::Node *n = m_canvas.node(h.node))
            m_resizeStart = canvas::nodeRect(*n);
        setGesture(Gesture::Resize);
        return;
    }
    const bool adding = (modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier)) || m_selectMode;
    if (!h.node.isEmpty()) {
        m_pressSelected = m_selNodes.contains(h.node);
        // A finger on a card that is not selected moves the canvas (a tap selects it).
        if (!m_pressSelected && (!touch || m_selectMode)) {
            QSet<QString> nodes = adding ? m_selNodes : QSet<QString>();
            QSet<QString> edges = adding ? m_selEdges : QSet<QString>();
            nodes.insert(h.node);
            setSelection(std::move(nodes), std::move(edges));
        }
    } else if (!h.edge.isEmpty()) {
        m_pressSelected = m_selEdges.contains(h.edge);
        if (!m_pressSelected && !touch) {
            QSet<QString> nodes = adding ? m_selNodes : QSet<QString>();
            QSet<QString> edges = adding ? m_selEdges : QSet<QString>();
            edges.insert(h.edge);
            setSelection(std::move(nodes), std::move(edges));
        }
    }
    setGesture(Gesture::Press);
    if (touch)
        m_longPress.start();
}

void CanvasEditor::beginMove()
{
    m_gestureStart = m_canvas;
    m_moving = canvas::withGroupContents(m_canvas, m_selNodes);
    setCursor(Qt::SizeAllCursor);
    setGesture(Gesture::Move);
}

void CanvasEditor::moveTo(QPointF pos)
{
    switch (m_gesture) {
    case Gesture::None:
    case Gesture::Pinch: return;
    case Gesture::Press: {
        const double threshold = m_pressTouch ? 10 : 4;
        if (distance(pos, m_pressPos) < threshold)
            return;
        m_longPress.stop();
        if (m_pressButton == Qt::RightButton || m_longPressed)
            return;
        const Hit &h = m_pressHit;
        if (!h.node.isEmpty() && m_selNodes.contains(h.node) && (!m_pressTouch || m_pressSelected || m_selectMode)) {
            beginMove();
        } else if (h.node.isEmpty() && (!m_pressTouch || m_selectMode)) {
            // The mouse on empty canvas (or on an edge): a rubber band; Shift adds to the selection.
            const bool adding = (m_pressModifiers & (Qt::ShiftModifier | Qt::ControlModifier)) || m_selectMode;
            m_bandBase = adding ? m_selNodes : QSet<QString>();
            if (!adding)
                setSelection({}, {});
            setGesture(Gesture::Band);
        } else {
            setCursor(Qt::ClosedHandCursor);
            setGesture(Gesture::Pan);
        }
        moveTo(pos);
        return;
    }
    case Gesture::Move: {
        const QPointF delta = (pos - m_pressPos) / m_zoom;
        setDocument(canvas::moveNodes(m_gestureStart, m_moving, delta));
        break;
    }
    case Gesture::Resize: {
        const QPointF d = (pos - m_pressPos) / m_zoom;
        QRectF r = m_resizeStart;
        const int edges = m_pressHit.edges;
        const double min = canvas::MinNodeSize;
        if (edges & Qt::LeftEdge)
            r.setLeft(std::min(r.left() + d.x(), r.right() - min));
        if (edges & Qt::RightEdge)
            r.setRight(std::max(r.right() + d.x(), r.left() + min));
        if (edges & Qt::TopEdge)
            r.setTop(std::min(r.top() + d.y(), r.bottom() - min));
        if (edges & Qt::BottomEdge)
            r.setBottom(std::max(r.bottom() + d.y(), r.top() + min));
        setDocument(canvas::setNodeRect(m_gestureStart, m_pressHit.node, r));
        break;
    }
    case Gesture::Band: {
        m_lastPos = pos;
        const QRectF band = QRectF(m_pressCanvas, toCanvas(pos.x(), pos.y())).normalized();
        QSet<QString> nodes = m_bandBase;
        // Cards the band touches; a group only once it holds the whole group (a band drawn inside a
        // group picks cards, not the group).
        const QSet<QString> touched = canvas::nodesIn(m_canvas, band, false);
        for (const canvas::Node &n : m_canvas.nodes) {
            if (!touched.contains(n.id))
                continue;
            if (!n.isGroup() || band.contains(canvas::nodeRect(n)))
                nodes.insert(n.id);
        }
        setSelection(std::move(nodes), {});
        Q_EMIT gestureChanged();
        break;
    }
    case Gesture::Connect: updateConnect(pos); break;
    case Gesture::Pan: {
        const QPointF d = pos - m_lastPos;
        m_lastPos = pos;
        setView(m_zoom, m_pan + d);
        break;
    }
    }
    m_lastPos = pos;
}

void CanvasEditor::releaseAt(QPointF pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers, bool touch)
{
    m_longPress.stop();
    const Gesture g = m_gesture;
    const Hit h = m_pressHit;
    setGesture(Gesture::None);
    unsetCursor();
    switch (g) {
    case Gesture::Press: {
        if (m_longPressed)
            break;
        const QPointF c = toCanvas(pos.x(), pos.y());
        if (button == Qt::RightButton) {
            if (!h.node.isEmpty() && !m_selNodes.contains(h.node))
                setSelection({h.node}, {});
            else if (!h.edge.isEmpty() && !m_selEdges.contains(h.edge))
                setSelection({}, {h.edge});
            else if (h.node.isEmpty() && h.edge.isEmpty())
                setSelection({}, {});
            Q_EMIT contextMenuRequested(h.node, h.edge, pos.x(), pos.y());
            break;
        }
        const bool adding = (modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier)) || m_selectMode;
        if (!h.node.isEmpty()) {
            if (adding && m_pressSelected) {
                QSet<QString> nodes = m_selNodes;
                nodes.remove(h.node);
                setSelection(std::move(nodes), m_selEdges);
            } else if (adding) {
                QSet<QString> nodes = m_selNodes;
                nodes.insert(h.node);
                setSelection(std::move(nodes), m_selEdges);
            } else {
                setSelection({h.node}, {});
            }
        } else if (!h.edge.isEmpty()) {
            if (adding && m_pressSelected) {
                QSet<QString> edges = m_selEdges;
                edges.remove(h.edge);
                setSelection(m_selNodes, std::move(edges));
            } else if (!adding) {
                setSelection({}, {h.edge});
            } else {
                QSet<QString> edges = m_selEdges;
                edges.insert(h.edge);
                setSelection(m_selNodes, std::move(edges));
            }
        } else if (!adding) {
            setSelection({}, {});
        }
        Q_EMIT tapped(h.node, c.x(), c.y(), int(button), int(modifiers));
        if (touch) {
            // A second tap close by and soon after: a double tap.
            static constexpr qint64 DoubleTapMs = 350;
            if (m_lastTap.isValid() && m_lastTap.elapsed() < DoubleTapMs && distance(pos, m_lastTapPos) < 24) {
                m_lastTap.invalidate();
                Q_EMIT doubleTapped(h.node, h.edge, c.x(), c.y());
            } else {
                m_lastTap.start();
                m_lastTapPos = pos;
            }
        }
        break;
    }
    case Gesture::Move:
    case Gesture::Resize:
        if (!m_canvas.sameContent(m_gestureStart)) {
            pushUndo(m_gestureStart);
            setModified(true);
            Q_EMIT contentChanged(); // (the gesture's changes were not modifications until now)
        }
        m_moving.clear();
        break;
    case Gesture::Band: Q_EMIT gestureChanged(); break;
    case Gesture::Connect: finishConnect(pos); break;
    case Gesture::Pan:
    case Gesture::Pinch:
    case Gesture::None: break;
    }
    Q_EMIT selectionGeometryChanged();
    takePendingExternal();
}

void CanvasEditor::updateConnect(QPointF pos)
{
    const canvas::Node *from = m_canvas.node(m_connectFrom);
    if (!from)
        return;
    const QRectF fromRect = canvas::nodeRect(*from);
    const QPointF p = toCanvas(pos.x(), pos.y());
    const qsizetype at = canvas::nodeAt(m_canvas, p, false);
    QString target = at >= 0 ? m_canvas.nodes.at(at).id : QString();
    if (target == m_connectFrom)
        target.clear();
    canvas::EdgeShape shape;
    if (!target.isEmpty()) {
        canvas::Edge e;
        e.fromSide = m_connectSide;
        const QRectF toRect = canvas::nodeRect(m_canvas.nodes.at(at));
        e.toSide = canvas::sideName(canvas::nearestSide(toRect, p));
        shape = canvas::edgeShape(e, fromRect, toRect);
    } else {
        shape = canvas::looseShape(fromRect, canvas::sideFrom(m_connectSide).value_or(canvas::Side::Right), p);
    }
    m_connectPath = canvas::svgPath(shape);
    m_connectArrow = arrowsPath(shape.startArrow, shape.endArrow);
    m_connectTarget = target;
    Q_EMIT gestureChanged();
}

void CanvasEditor::finishConnect(QPointF pos)
{
    const QString from = std::exchange(m_connectFrom, QString());
    const QString fromSide = std::exchange(m_connectSide, QString());
    m_connectPath.clear();
    m_connectArrow.clear();
    const QString target = std::exchange(m_connectTarget, QString());
    Q_EMIT gestureChanged();
    const canvas::Node *source = m_canvas.node(from);
    if (!source)
        return;
    const QPointF p = toCanvas(pos.x(), pos.y());
    if (!target.isEmpty()) {
        const canvas::Node *t = m_canvas.node(target);
        if (!t)
            return;
        const QString toSide = canvas::sideName(canvas::nearestSide(canvas::nodeRect(*t), p));
        QString id;
        apply(canvas::connectNodes(m_canvas, from, fromSide, target, toSide, &id));
        if (!id.isEmpty())
            setSelection({}, {id});
        return;
    }
    // Dropped on empty canvas, away from the card: a new text card there, connected.
    if (distance(pos, m_pressPos) < 30)
        return;
    const QSizeF size = canvas::defaultSize(canvas::TextType);
    const QString toSide = oppositeSide(fromSide);
    // The new card's side the edge enters lies at the drop point.
    QRectF r(QPointF(), size);
    r.moveCenter(p);
    const canvas::Side side = canvas::sideFrom(toSide).value_or(canvas::Side::Left);
    const QPointF anchorOffset = canvas::anchor(r, side) - r.center();
    r.moveCenter(p - anchorOffset);
    canvas::Node card = canvas::makeTextNode(QString(), r);
    const QString id = card.id;
    canvas::Canvas next = canvas::addNode(m_canvas, std::move(card));
    next = canvas::connectNodes(next, from, fromSide, id, toSide);
    apply(std::move(next));
    setSelection({id}, {});
    Q_EMIT editRequested(id);
}

bool CanvasEditor::cancelGesture()
{
    switch (m_gesture) {
    case Gesture::Move:
    case Gesture::Resize:
        setDocument(m_gestureStart);
        m_moving.clear();
        break;
    case Gesture::Connect:
        m_connectFrom.clear();
        m_connectPath.clear();
        m_connectArrow.clear();
        m_connectTarget.clear();
        break;
    case Gesture::Band:
    case Gesture::Press:
    case Gesture::Pan:
    case Gesture::Pinch: break;
    case Gesture::None: return false;
    }
    m_longPress.stop();
    setGesture(Gesture::None);
    unsetCursor();
    ungrabMouse();
    takePendingExternal();
    return true;
}

void CanvasEditor::updateHover(QPointF pos)
{
    m_pointer = pos;
    if (m_gesture != Gesture::None)
        return;
    const Hit h = hitAt(pos);
    // The card stays "hovered" while the pointer is on one of its handles (just outside it).
    const QString node = h.part == Part::Handle ? h.node : h.node;
    if (node != m_hoverNode) {
        m_hoverNode = node;
        Q_EMIT hoverChanged();
    }
    if (m_panMode)
        setCursor(Qt::OpenHandCursor);
    else if (h.part == Part::Handle)
        setCursor(Qt::CrossCursor);
    else if (h.part == Part::Resize)
        setCursor(resizeCursor(h.edges));
    else
        unsetCursor();
}

// ---- events -----------------------------------------------------------------------------------------

void CanvasEditor::mousePressEvent(QMouseEvent *event)
{
    if (m_gesture != Gesture::None) {
        event->accept();
        return;
    }
    forceActiveFocus(Qt::MouseFocusReason);
    pressAt(event->position(), event->button(), event->modifiers(), false);
    event->accept();
}

void CanvasEditor::mouseMoveEvent(QMouseEvent *event)
{
    m_pointer = event->position();
    moveTo(event->position());
    event->accept();
}

void CanvasEditor::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != m_pressButton) {
        event->accept();
        return;
    }
    releaseAt(event->position(), event->button(), event->modifiers(), false);
    updateHover(event->position());
    event->accept();
}

void CanvasEditor::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->accept();
        return;
    }
    const Hit h = hitAt(event->position());
    const QPointF c = toCanvas(event->position().x(), event->position().y());
    // (The second press of the double click comes here instead of mousePressEvent.)
    m_pressButton = Qt::LeftButton;
    setGesture(Gesture::None);
    if (h.part != Part::Handle && h.part != Part::Resize)
        Q_EMIT doubleTapped(h.node, h.edge, c.x(), c.y());
    event->accept();
}

void CanvasEditor::mouseUngrabEvent()
{
    if (m_gesture != Gesture::None && m_gesture != Gesture::Pinch && m_touchPoints == 0)
        cancelGesture();
}

void CanvasEditor::hoverMoveEvent(QHoverEvent *event)
{
    updateHover(event->position());
    QQuickItem::hoverMoveEvent(event);
}

void CanvasEditor::hoverLeaveEvent(QHoverEvent *event)
{
    m_pointer.reset();
    if (!m_hoverNode.isEmpty() && m_gesture == Gesture::None) {
        m_hoverNode.clear();
        Q_EMIT hoverChanged();
    }
    QQuickItem::hoverLeaveEvent(event);
}

void CanvasEditor::wheelEvent(QWheelEvent *event)
{
    m_viewAnimation->stop();
    if (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
        const double steps = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->pixelDelta().y() * 4.0;
        zoomBy(std::pow(1.0015, steps), event->position().x(), event->position().y());
        event->accept();
        return;
    }
    QPointF delta =
            !event->pixelDelta().isNull() ? QPointF(event->pixelDelta()) : QPointF(event->angleDelta()) / 120.0 * 60.0;
    if ((event->modifiers() & Qt::ShiftModifier) && qFuzzyIsNull(delta.x()))
        delta = QPointF(delta.y(), 0);
    // A selected card whose content is longer than it scrolls its content.
    if (qFuzzyIsNull(delta.x()) && !(event->modifiers() & Qt::ShiftModifier)) {
        const QString node = nodeAtScreen(event->position().x(), event->position().y());
        if (!node.isEmpty() && m_selNodes.contains(node) && m_scrollable.contains(node)) {
            Q_EMIT cardScrollRequested(node, -delta.y() / m_zoom);
            event->accept();
            return;
        }
    }
    // (A wheel turned away moves the canvas up: the view moves down the canvas.)
    panBy(-delta.x(), -delta.y());
    event->accept();
}

bool CanvasEditor::event(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture) {
        const auto *g = static_cast<QNativeGestureEvent *>(event);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
            zoomBy(1 + g->value(), g->position().x(), g->position().y());
            return true;
        }
        if (g->gestureType() == Qt::SmartZoomNativeGesture) {
            zoomToFit();
            return true;
        }
    }
    return QQuickItem::event(event);
}

void CanvasEditor::touchEvent(QTouchEvent *event)
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
    m_touchPoints = int(down.size());
    if (down.size() >= 2) {
        const QPointF a = down.at(0)->position();
        const QPointF b = down.at(1)->position();
        const QPointF mid = (a + b) / 2;
        const double d = std::max(1.0, distance(a, b));
        if (m_gesture != Gesture::Pinch) {
            // A second finger: whatever the first one began becomes a pinch.
            if (m_gesture == Gesture::Move || m_gesture == Gesture::Resize || m_gesture == Gesture::Connect
                || m_gesture == Gesture::Band)
                cancelGesture();
            m_longPress.stop();
            m_viewAnimation->stop();
            setGesture(Gesture::Pinch);
            m_pinchDistance = d;
            m_pinchZoom = m_zoom;
            m_pinchCanvas = toCanvas(mid.x(), mid.y());
        } else {
            const double z = clampZoom(m_pinchZoom * d / m_pinchDistance);
            setView(z, mid - m_pinchCanvas * z);
        }
    } else if (down.size() == 1) {
        const QEventPoint &p = *down.at(0);
        if (m_gesture == Gesture::Pinch) {
            // One finger lifted: the other one moves the canvas on.
            m_lastPos = p.position();
            m_pressPos = p.position();
            setGesture(Gesture::Pan);
        } else if (m_gesture == Gesture::None) {
            pressAt(p.position(), Qt::LeftButton, event->modifiers(), true);
        } else {
            moveTo(p.position());
        }
    } else {
        const QPointF pos = event->points().isEmpty() ? m_lastPos : event->points().first().position();
        if (m_gesture == Gesture::Pinch || m_gesture == Gesture::Pan) {
            setGesture(Gesture::None);
            takePendingExternal();
        } else if (m_gesture != Gesture::None) {
            releaseAt(pos, Qt::LeftButton, event->modifiers(), true);
        }
    }
    event->accept();
}

void CanvasEditor::touchUngrabEvent()
{
    m_touchPoints = 0;
    if (m_gesture != Gesture::None)
        cancelGesture();
}

} // namespace brulion::canvasview
