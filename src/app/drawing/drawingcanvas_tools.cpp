// DrawingCanvas's tools beyond the pen and the eraser (drawingcanvas.h; docs/UI.md "Drawing
// editor"): the selection — a lasso or a tap, moving it, its actions —, the shapes, and text written
// in QML's editor. Every edit goes through editElements(): the core's pure edits (drawing/edit.h) on
// the drawing, one undo step of their diff.
#include "drawingcanvas.h"

#include "drawingrender.h"

#include "drawing/geometry.h"
#include "drawing/palette.h"

#include <QGuiApplication>
#include <QLineF>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::drawingview {

namespace {

drawing::ElementType shapeType(const QString &tool)
{
    if (tool == u"ellipse")
        return drawing::ElementType::Ellipse;
    if (tool == u"diamond")
        return drawing::ElementType::Diamond;
    if (tool == u"line")
        return drawing::ElementType::Line;
    if (tool == u"arrow")
        return drawing::ElementType::Arrow;
    return drawing::ElementType::Rectangle;
}

QString alignName(drawing::TextAlign align)
{
    switch (align) {
    case drawing::TextAlign::Center: return u"center"_s;
    case drawing::TextAlign::Right: return u"right"_s;
    case drawing::TextAlign::Left: break;
    }
    return u"left"_s;
}

} // namespace

DrawingCanvas::Pointer DrawingCanvas::touchPointer()
{
    Pointer p;
    p.touch = true;
    p.modifiers = QGuiApplication::keyboardModifiers();
    return p;
}

drawing::EditStamp DrawingCanvas::stamp() const
{
    drawing::EditStamp s;
    s.now = m_clock ? m_clock() : 0;
    s.random = [] { return QRandomGenerator::global()->bounded(1u, 2147483647u); };
    s.newId = [this] { return newId(); };
    return s;
}

bool DrawingCanvas::editElements(const std::function<void(drawing::Drawing &)> &edit)
{
    if (!m_editable)
        return false;
    const QList<drawing::Stroke> before = m_doc.strokes;
    edit(m_doc);
    drawing::ElementDiff diff = drawing::diffElements(before, m_doc.strokes);
    if (diff.isEmpty()) {
        m_doc.strokes = before;
        return false;
    }
    syncPrepared();
    pruneSelection();
    invalidateCache();
    Op op;
    op.kind = Op::Elements;
    op.diff = std::move(diff);
    push(std::move(op));
    return true;
}

// --- The selection ----------------------------------------------------------------------------

void DrawingCanvas::setSelection(const QSet<QString> &ids)
{
    if (ids == m_selected)
        return;
    m_selected = ids;
    update();
    Q_EMIT selectionChanged();
}

void DrawingCanvas::pruneSelection()
{
    if (m_selected.isEmpty())
        return;
    QSet<QString> kept;
    for (const drawing::Stroke &e : std::as_const(m_doc.strokes)) {
        if (!e.locked && m_selected.contains(e.id))
            kept.insert(e.id);
    }
    if (kept != m_selected)
        setSelection(kept);
    else
        Q_EMIT selectionChanged(); // (its frame may have moved)
}

QStringList DrawingCanvas::selection() const
{
    QStringList out;
    for (const drawing::Stroke &e : m_doc.strokes) {
        if (m_selected.contains(e.id))
            out.append(e.id);
    }
    return out;
}

void DrawingCanvas::select(const QStringList &ids)
{
    const QStringList closure = drawing::selectionClosure(m_doc, ids);
    setSelection(QSet<QString>(closure.cbegin(), closure.cend()));
}

QRectF DrawingCanvas::selectionSceneRect() const
{
    QRectF out;
    for (qsizetype i = 0; i < m_prepared.size(); ++i) {
        const Prepared &p = m_prepared.at(i);
        if (!m_selected.contains(p.key) || p.bounds.isNull())
            continue;
        out = out.isNull() ? p.bounds : out.united(p.bounds);
    }
    return out;
}

QRectF DrawingCanvas::selectionRect() const
{
    const QRectF scene = selectionSceneRect();
    if (scene.isNull())
        return {};
    QRectF r = sceneToItem().mapRect(scene).adjusted(-6, -6, 6, 6);
    if (m_gesture == Gesture::Move)
        r.translate(m_moveDelta);
    return r;
}

void DrawingCanvas::selectAll()
{
    if (!m_editable)
        return;
    QStringList ids;
    for (const drawing::Stroke &e : std::as_const(m_doc.strokes)) {
        if (!e.id.isEmpty() && !e.locked)
            ids.append(e.id);
    }
    select(ids);
}

void DrawingCanvas::clearSelection()
{
    setSelection({});
}

void DrawingCanvas::deleteSelection()
{
    if (m_selected.isEmpty() || busy())
        return;
    const QSet<QString> ids = m_selected;
    editElements([&ids, this](drawing::Drawing &doc) { drawing::deleteElements(doc, ids, stamp()); });
    setSelection({});
}

void DrawingCanvas::duplicateSelection()
{
    if (m_selected.isEmpty() || busy())
        return;
    const QStringList ids = selection();
    QStringList copies;
    editElements([&](drawing::Drawing &doc) {
        copies = drawing::duplicateElements(doc, ids, QPointF(DuplicateOffset, DuplicateOffset), stamp());
    });
    if (!copies.isEmpty())
        select(copies);
}

void DrawingCanvas::recolorSelection(const QColor &stored)
{
    if (m_selected.isEmpty() || !stored.isValid() || busy())
        return;
    const QSet<QString> ids = m_selected;
    const QString color = stored.name(QColor::HexRgb);
    editElements([&](drawing::Drawing &doc) { drawing::recolorElements(doc, ids, color, stamp()); });
}

void DrawingCanvas::reorderSelection(const QString &how)
{
    if (m_selected.isEmpty() || busy())
        return;
    const drawing::Reorder order = how == u"forward"  ? drawing::Reorder::Forward
                                   : how == u"backward" ? drawing::Reorder::Backward
                                   : how == u"back"     ? drawing::Reorder::ToBack
                                                        : drawing::Reorder::ToFront;
    const QSet<QString> ids = m_selected;
    editElements([&](drawing::Drawing &doc) { drawing::reorderElements(doc, ids, order, stamp()); });
}

void DrawingCanvas::nudgeSelection(qreal dx, qreal dy)
{
    if (m_selected.isEmpty() || busy() || !std::isfinite(dx) || !std::isfinite(dy))
        return;
    const QSet<QString> ids = m_selected;
    editElements([&](drawing::Drawing &doc) { drawing::moveElements(doc, ids, QPointF(dx, dy), stamp()); });
}

bool DrawingCanvas::cancelGesture()
{
    if (m_gesture == Gesture::Lasso || m_gesture == Gesture::Move || m_gesture == Gesture::Shape
        || m_gesture == Gesture::Tap) {
        cancelToolGesture();
        return true;
    }
    return false;
}

// --- Gestures ---------------------------------------------------------------------------------

void DrawingCanvas::beginToolGesture(QPointF itemPos, const Pointer &p)
{
    if (!m_editable)
        return;
    m_lastItemPos = itemPos;
    if (p.eraser || m_tool == u"eraser")
        beginErase(itemPos);
    else if (m_tool == u"select")
        beginSelect(itemPos, p);
    else if (isShapeTool())
        beginShape(itemPos, p);
    else if (m_tool == u"text") {
        m_gestureStart = itemPos;
        setGesture(Gesture::Tap);
    } else
        beginStroke(itemPos, p);
}

void DrawingCanvas::moveToolGesture(QPointF itemPos, const Pointer &p)
{
    switch (m_gesture) {
    case Gesture::Draw: extendStroke(itemPos, p); break;
    case Gesture::Erase: eraseTo(itemPos); break;
    case Gesture::Pan: panTo(itemPos); break;
    case Gesture::Lasso: lassoTo(itemPos); break;
    case Gesture::Move: moveSelectionTo(itemPos); break;
    case Gesture::Shape: shapeTo(itemPos, p); break;
    case Gesture::Tap: m_lastItemPos = itemPos; break;
    case Gesture::Pinch:
    case Gesture::None: break;
    }
}

void DrawingCanvas::endToolGesture(QPointF itemPos, const Pointer &p)
{
    switch (m_gesture) {
    case Gesture::Draw:
        extendStroke(itemPos, p);
        finishStroke();
        break;
    case Gesture::Erase:
        eraseTo(itemPos);
        finishErase();
        break;
    case Gesture::Pan:
    case Gesture::Pinch:
        setGesture(Gesture::None);
        endViewGesture();
        break;
    case Gesture::Lasso:
        lassoTo(itemPos);
        finishLasso(itemPos, p);
        break;
    case Gesture::Move:
        moveSelectionTo(itemPos);
        finishMove(itemPos, p);
        break;
    case Gesture::Shape:
        shapeTo(itemPos, p);
        finishShape(itemPos, p);
        break;
    case Gesture::Tap: finishTap(itemPos, p); break;
    case Gesture::None: break;
    }
}

void DrawingCanvas::cancelToolGesture()
{
    switch (m_gesture) {
    case Gesture::Draw: cancelStroke(); break;
    case Gesture::Erase: finishErase(); break;
    case Gesture::Lasso:
        m_lasso.clear();
        setGesture(Gesture::None);
        update();
        break;
    case Gesture::Move:
        m_moveDelta = QPointF();
        setGesture(Gesture::None);
        invalidateCache();
        Q_EMIT selectionChanged();
        break;
    case Gesture::Shape:
        m_liveShapeShown = false;
        setGesture(Gesture::None);
        update();
        break;
    case Gesture::Tap: setGesture(Gesture::None); break;
    case Gesture::Pan:
    case Gesture::Pinch:
        setGesture(Gesture::None);
        endViewGesture();
        break;
    case Gesture::None: break;
    }
}

void DrawingCanvas::beginSelect(QPointF itemPos, const Pointer &p)
{
    m_gestureStart = itemPos;
    const QRectF box = selectionRect().adjusted(p.touch ? -10 : 0, p.touch ? -10 : 0, p.touch ? 10 : 0,
                                                p.touch ? 10 : 0);
    if (!m_selected.isEmpty() && box.contains(itemPos) && !(p.modifiers & Qt::ShiftModifier)) {
        m_moveDelta = QPointF();
        setGesture(Gesture::Move);
        invalidateCache(); // (the selection is drawn on top while it moves)
        return;
    }
    m_lasso = QPolygonF{itemPos};
    m_lassoLength = 0;
    setGesture(Gesture::Lasso);
    update();
}

void DrawingCanvas::lassoTo(QPointF itemPos)
{
    if (m_gesture != Gesture::Lasso || m_lasso.isEmpty())
        return;
    const double step = QLineF(m_lasso.last(), itemPos).length();
    if (step < 2)
        return;
    m_lasso.append(itemPos);
    m_lassoLength += step;
    m_lastItemPos = itemPos;
    update();
}

void DrawingCanvas::finishLasso(QPointF itemPos, const Pointer &p)
{
    const QPolygonF lasso = std::exchange(m_lasso, {});
    setGesture(Gesture::None);
    update();
    if (m_lassoLength < TapLength) {
        selectTap(itemPos, p);
        return;
    }
    QList<QPointF> scene;
    scene.reserve(lasso.size());
    for (const QPointF &pt : lasso)
        scene.append(toScene(pt));
    const QRectF bounds = QPolygonF(scene).boundingRect();
    QStringList hit;
    for (qsizetype i = 0; i < m_doc.strokes.size(); ++i) {
        const drawing::Stroke &e = m_doc.strokes.at(i);
        if (e.id.isEmpty() || e.locked || !m_prepared.at(i).bounds.intersects(bounds))
            continue;
        if (drawing::lassoSelects(e, scene))
            hit.append(e.id);
    }
    const QStringList closure = drawing::selectionClosure(m_doc, hit);
    QSet<QString> chosen(closure.cbegin(), closure.cend());
    if (p.modifiers & Qt::ShiftModifier)
        chosen.unite(m_selected);
    setSelection(chosen);
}

void DrawingCanvas::moveSelectionTo(QPointF itemPos)
{
    if (m_gesture != Gesture::Move)
        return;
    m_moveDelta = itemPos - m_gestureStart;
    m_lastItemPos = itemPos;
    update();
    Q_EMIT selectionChanged();
}

void DrawingCanvas::finishMove(QPointF itemPos, const Pointer &p)
{
    const QPointF delta = std::exchange(m_moveDelta, QPointF());
    setGesture(Gesture::None);
    if (std::hypot(delta.x(), delta.y()) < MinShape) {
        invalidateCache();
        Q_EMIT selectionChanged();
        selectTap(itemPos, p); // a tap on the selection
        return;
    }
    const QSet<QString> ids = m_selected;
    const QList<drawing::Stroke> before = m_doc.strokes;
    const bool moved = editElements([&](drawing::Drawing &doc) {
        drawing::moveElements(doc, ids, delta / m_zoom, stamp());
        // A freehand line moved as a whole keeps its outline, moved (no new outline to compute).
        for (qsizetype i = 0; i < doc.strokes.size() && i < before.size(); ++i) {
            const drawing::Stroke &a = doc.strokes.at(i);
            const drawing::Stroke &b = before.at(i);
            Prepared &prepared = m_prepared[i];
            if (a.type != drawing::ElementType::Freedraw || a.version == b.version || a.id != b.id
                || a.points.size() != b.points.size() || a.points.isEmpty() || prepared.version != b.version)
                continue;
            const QPointF shift(a.points.first().x - b.points.first().x, a.points.first().y - b.points.first().y);
            prepared.ink.translate(shift);
            prepared.area.translate(shift);
            prepared.bounds.translate(shift);
            prepared.version = a.version;
        }
    });
    if (!moved)
        invalidateCache();
    Q_EMIT selectionChanged();
}

qsizetype DrawingCanvas::elementAt(QPointF itemPos, const Pointer &p) const
{
    const QPointF scene = toScene(itemPos);
    const double tolerance = (p.touch || p.stylus ? TouchTapTolerance : TapTolerance) / m_zoom;
    for (qsizetype i = m_doc.strokes.size() - 1; i >= 0; --i) {
        const drawing::Stroke &e = m_doc.strokes.at(i);
        if (e.id.isEmpty() || e.locked)
            continue;
        if (!m_prepared.at(i).bounds.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(scene))
            continue;
        if (drawing::hitTest(e, scene, tolerance))
            return i;
    }
    return -1;
}

qsizetype DrawingCanvas::textTargetAt(QPointF itemPos, const Pointer &p) const
{
    const qsizetype hit = elementAt(itemPos, p);
    if (hit >= 0
        && (m_doc.strokes.at(hit).type == drawing::ElementType::Text || drawing::isShape(m_doc.strokes.at(hit).type)))
        return hit;
    // Inside a shape (an unfilled one is no hit there): its label.
    const QPointF scene = toScene(itemPos);
    for (qsizetype i = m_doc.strokes.size() - 1; i >= 0; --i) {
        const drawing::Stroke &e = m_doc.strokes.at(i);
        if (e.id.isEmpty() || e.locked || !drawing::isShape(e.type))
            continue;
        if (e.box.contains(drawing::rotatePoint(scene, e.box.center(), -e.angle)))
            return i;
    }
    return -1;
}

void DrawingCanvas::selectTap(QPointF itemPos, const Pointer &p)
{
    const bool twice = m_lastTap.isValid() && m_lastTap.elapsed() < 450
                       && QLineF(m_lastTapPos, itemPos).length() < 12;
    m_lastTap.start();
    m_lastTapPos = itemPos;
    const qsizetype i = elementAt(itemPos, p);
    const bool shift = p.modifiers & Qt::ShiftModifier;
    if (twice && !shift) {
        const qsizetype target = textTargetAt(itemPos, p);
        if (target >= 0) {
            m_lastTap.invalidate();
            requestTextEdit(target, toScene(itemPos));
            return;
        }
    }
    if (i < 0) {
        if (!shift)
            setSelection({});
        return;
    }
    const QStringList closure = drawing::selectionClosure(m_doc, {m_doc.strokes.at(i).id});
    QSet<QString> chosen(closure.cbegin(), closure.cend());
    if (shift) {
        if (m_selected.contains(chosen))
            chosen = QSet<QString>(m_selected).subtract(chosen);
        else
            chosen.unite(m_selected);
    }
    setSelection(chosen);
}

void DrawingCanvas::beginShape(QPointF itemPos, const Pointer &)
{
    m_gestureStart = itemPos;
    m_liveShapeShown = false;
    setGesture(Gesture::Shape);
}

void DrawingCanvas::shapeTo(QPointF itemPos, const Pointer &p)
{
    if (m_gesture != Gesture::Shape)
        return;
    m_lastItemPos = itemPos;
    const auto clamped = [this](QPointF item) {
        const QPointF s = toScene(item);
        return QPointF(std::clamp(s.x(), -drawing::CoordinateLimit, drawing::CoordinateLimit),
                       std::clamp(s.y(), -drawing::CoordinateLimit, drawing::CoordinateLimit));
    };
    drawing::EditStamp preview;
    preview.newId = [] { return u"~"_s; };
    const drawing::ShapeStyle style{m_color.name(QColor::HexRgb), std::clamp(m_penWidth * 2, 0.5, 40.0), true};
    m_liveShape = drawing::newShape(shapeType(m_tool), clamped(m_gestureStart), clamped(itemPos),
                                    p.modifiers & Qt::ShiftModifier, style, preview);
    m_liveShapeShown = QLineF(m_gestureStart, itemPos).length() >= MinShape;
    update();
}

void DrawingCanvas::finishShape(QPointF, const Pointer &)
{
    const bool shown = std::exchange(m_liveShapeShown, false);
    setGesture(Gesture::None);
    update();
    if (!shown)
        return; // a click: no shape
    drawing::Stroke shape = m_liveShape;
    const drawing::EditStamp s = stamp();
    shape.id = s.newId();
    shape.seed = int(s.random());
    shape.versionNonce = int(s.random());
    shape.updated = s.now;
    editElements([&shape](drawing::Drawing &doc) { doc.strokes.append(shape); });
}

void DrawingCanvas::finishTap(QPointF itemPos, const Pointer &p)
{
    setGesture(Gesture::None);
    if (QLineF(m_gestureStart, itemPos).length() >= TapLength)
        return; // a drag with the text tool: nothing
    textTap(itemPos, p);
}

void DrawingCanvas::textTap(QPointF itemPos, const Pointer &p)
{
    requestTextEdit(textTargetAt(itemPos, p), toScene(itemPos));
}

// --- Text -------------------------------------------------------------------------------------

void DrawingCanvas::requestTextEdit(qsizetype index, QPointF scene)
{
    if (!m_editable)
        return;
    endTextEdit();
    const auto shown = [this](const QString &stored) { return displayColor(QColor(stored)); };
    QVariantMap r;
    const auto label = [&](const drawing::Stroke &shape, const drawing::Stroke *text) {
        r.insert(u"id"_s, text ? text->id : QString());
        r.insert(u"containerId"_s, shape.id);
        r.insert(u"sceneX"_s, shape.box.left() + drawing::LabelPadding);
        r.insert(u"sceneY"_s, shape.box.center().y());
        r.insert(u"width"_s, std::max(1.0, shape.box.width() - 2 * drawing::LabelPadding));
        r.insert(u"text"_s, text ? (text->originalText.isEmpty() ? text->text : text->originalText) : QString());
        r.insert(u"fontSize"_s, text ? text->fontSize : m_fontSize);
        r.insert(u"fontFamily"_s, textFontFamily(text ? text->fontFamily : 2));
        r.insert(u"color"_s, shown(text ? text->color : m_color.name(QColor::HexRgb)));
        r.insert(u"storedColor"_s, text ? text->color : m_color.name(QColor::HexRgb));
        r.insert(u"align"_s, u"center"_s);
        r.insert(u"valign"_s, u"middle"_s);
        m_textEditKey = text ? text->id : QString();
    };
    if (index < 0) {
        // A new text: its first line's middle at the tap.
        r.insert(u"id"_s, QString());
        r.insert(u"containerId"_s, QString());
        r.insert(u"sceneX"_s, scene.x());
        r.insert(u"sceneY"_s, scene.y() - m_fontSize * drawing::NewTextLineHeight / 2);
        r.insert(u"width"_s, 0);
        r.insert(u"text"_s, QString());
        r.insert(u"fontSize"_s, m_fontSize);
        r.insert(u"fontFamily"_s, textFontFamily(2));
        r.insert(u"color"_s, shown(m_color.name(QColor::HexRgb)));
        r.insert(u"storedColor"_s, m_color.name(QColor::HexRgb));
        r.insert(u"align"_s, u"left"_s);
        r.insert(u"valign"_s, u"top"_s);
    } else {
        const drawing::Stroke &e = m_doc.strokes.at(index);
        if (e.type == drawing::ElementType::Text) {
            const qsizetype c = drawing::indexOfElement(m_doc, e.containerId);
            if (c >= 0 && drawing::isShape(m_doc.strokes.at(c).type)) {
                label(m_doc.strokes.at(c), &e);
            } else {
                r.insert(u"id"_s, e.id);
                r.insert(u"containerId"_s, QString());
                r.insert(u"sceneX"_s, e.box.left());
                r.insert(u"sceneY"_s, e.box.top());
                r.insert(u"width"_s, e.autoResize ? 0.0 : e.box.width());
                r.insert(u"text"_s, e.autoResize || e.originalText.isEmpty() ? e.text : e.originalText);
                r.insert(u"fontSize"_s, e.fontSize);
                r.insert(u"fontFamily"_s, textFontFamily(e.fontFamily));
                r.insert(u"color"_s, shown(e.color));
                r.insert(u"storedColor"_s, e.color);
                r.insert(u"align"_s, alignName(e.textAlign));
                r.insert(u"valign"_s, u"top"_s);
                m_textEditKey = e.id;
            }
        } else {
            const drawing::Stroke *existing = nullptr;
            for (const drawing::Stroke &t : std::as_const(m_doc.strokes)) {
                if (t.type == drawing::ElementType::Text && t.containerId == e.id && !e.id.isEmpty())
                    existing = &t;
            }
            label(e, existing);
        }
    }
    m_textEditing = true;
    invalidateCache();
    Q_EMIT textEditingChanged();
    Q_EMIT textEditRequested(r);
}

void DrawingCanvas::endTextEdit()
{
    if (!m_textEditing)
        return;
    m_textEditing = false;
    m_textEditKey.clear();
    invalidateCache();
    Q_EMIT textEditingChanged();
}

void DrawingCanvas::editText(const QString &id)
{
    const qsizetype i = drawing::indexOfElement(m_doc, id);
    if (i >= 0)
        requestTextEdit(i, m_doc.strokes.at(i).box.center());
}

void DrawingCanvas::commitText(const QVariantMap &request, const QString &text)
{
    endTextEdit();
    if (!m_editable)
        return;
    const QString id = request.value(u"id"_s).toString();
    const QString containerId = request.value(u"containerId"_s).toString();
    const drawing::TextMetrics metrics = textMetrics();
    drawing::TextStyle style;
    const QString stored = request.value(u"storedColor"_s).toString();
    style.color = QColor(stored).isValid() ? QColor(stored).name(QColor::HexRgb) : m_color.name(QColor::HexRgb);
    style.fontSize = std::clamp(request.value(u"fontSize"_s, m_fontSize).toDouble(), 1.0, 1000.0);
    if (!id.isEmpty()) {
        editElements([&](drawing::Drawing &doc) { drawing::setText(doc, id, text, metrics, stamp()); });
    } else if (!containerId.isEmpty()) {
        editElements([&](drawing::Drawing &doc) { drawing::addLabel(doc, containerId, text, style, metrics, stamp()); });
    } else if (!text.trimmed().isEmpty()) {
        const QPointF at(request.value(u"sceneX"_s).toDouble(), request.value(u"sceneY"_s).toDouble());
        if (!std::isfinite(at.x()) || !std::isfinite(at.y()))
            return;
        const QPointF clamped(std::clamp(at.x(), -drawing::CoordinateLimit, drawing::CoordinateLimit),
                              std::clamp(at.y(), -drawing::CoordinateLimit, drawing::CoordinateLimit));
        editElements([&](drawing::Drawing &doc) {
            doc.strokes.append(drawing::newText(clamped, text, style, metrics, stamp()));
        });
    }
}

void DrawingCanvas::cancelText()
{
    endTextEdit();
}

void DrawingCanvas::ensureVisible(const QRectF &sceneRect, qreal bottomInset)
{
    if (!sceneRect.isValid() || width() <= 0 || height() <= 0)
        return;
    constexpr double margin = 8;
    const QRectF r = sceneToItem().mapRect(sceneRect);
    const double bottom = height() - std::max<qreal>(0, bottomInset) - margin;
    double dx = 0, dy = 0;
    if (r.bottom() > bottom)
        dy = r.bottom() - bottom;
    if (r.top() - dy < margin)
        dy = r.top() - margin;
    if (r.right() > width() - margin)
        dx = r.right() - (width() - margin);
    if (r.left() - dx < margin)
        dx = r.left() - margin;
    if (dx == 0 && dy == 0)
        return;
    m_center += QPointF(dx, dy) / m_zoom;
    viewMoved(true);
}

} // namespace brulion::drawingview
