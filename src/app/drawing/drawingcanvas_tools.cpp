// DrawingCanvas's tools beyond the pen and the eraser (drawingcanvas.h; docs/UI.md "Drawing
// editor"): the selection — a lasso, a rectangle or a tap, moving, resizing and rotating it, its
// actions —, the shapes, and text written
// in QML's editor. Every edit goes through editElements(): the core's pure edits (drawing/edit.h) on
// the drawing, one undo step of their diff.
#include "drawingcanvas.h"

#include "drawingrender.h"

#include "drawing/clipboard.h"
#include "drawing/excalidraw.h"
#include "drawing/geometry.h"
#include "drawing/palette.h"

#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QLineF>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::drawingview {

namespace {

/// A polygon's corners (unrotated).
QList<QPointF> polygonOf(const drawing::Stroke &e)
{
    QList<QPointF> out;
    out.reserve(e.points.size());
    for (const drawing::StrokePoint &p : e.points)
        out.append(QPointF(p.x, p.y));
    return out;
}

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

/// The enum value with this name in the scene (one of `values`, nullopt: none).
template<typename Enum, typename Name>
std::optional<Enum> enumFromName(const QString &name, std::initializer_list<Enum> values, Name nameOf)
{
    for (const Enum value : values) {
        if (nameOf(value) == name)
            return value;
    }
    return std::nullopt;
}

std::optional<drawing::Arrowhead> arrowheadFromName(const QString &name)
{
    using A = drawing::Arrowhead;
    return enumFromName<A>(name,
                           {A::None, A::Arrow, A::Triangle, A::TriangleOutline, A::Bar, A::Circle, A::CircleOutline,
                            A::Diamond, A::DiamondOutline},
                           drawing::arrowheadName);
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

/// `p` within drawing::CoordinateLimit (a drawing with anything beyond it is read back as one
/// Brulion only shows).
QPointF clampedToLimit(QPointF p)
{
    constexpr double L = drawing::CoordinateLimit;
    return QPointF(std::clamp(p.x(), -L, L), std::clamp(p.y(), -L, L));
}

/// A new text's `box` moved, all of it, within drawing::CoordinateLimit; its corner in hundredths.
QRectF boxWithinLimit(const QRectF &box)
{
    constexpr double L = drawing::CoordinateLimit;
    const double x = std::clamp(box.x(), -L, std::max(-L, L - box.width()));
    const double y = std::clamp(box.y(), -L, std::max(-L, L - box.height()));
    return QRectF(std::round(x * 100) / 100, std::round(y * 100) / 100, box.width(), box.height());
}

} // namespace

DrawingCanvas::Pointer DrawingCanvas::touchPointer()
{
    return {.touch = true, .modifiers = QGuiApplication::keyboardModifiers()};
}

drawing::EditStamp DrawingCanvas::stamp() const
{
    return drawing::randomEditStamp(m_clock ? m_clock() : 0);
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
    for (const Prepared &p : m_prepared) {
        if (m_selected.contains(p.key) && !p.bounds.isNull())
            out |= p.bounds;
    }
    return out;
}

void DrawingCanvas::setSelectMode(const QString &mode)
{
    const QString m = mode == u"box" ? mode : u"lasso"_s;
    if (m == m_selectMode)
        return;
    if (m_gesture == Gesture::Lasso)
        cancelToolGesture();
    m_selectMode = m;
    Q_EMIT selectModeChanged();
}

DrawingCanvas::SelectionFrame DrawingCanvas::selectionFrame() const
{
    SelectionFrame frame;
    const drawing::Stroke *only = nullptr;
    int units = 0;
    QRectF all;
    for (qsizetype i = 0; i < m_prepared.size(); ++i) {
        if (!m_selected.contains(m_prepared.at(i).key))
            continue;
        if (const QRectF bounds = m_prepared.at(i).bounds; !bounds.isNull())
            all |= bounds;
        const drawing::Stroke &e = m_doc.strokes.at(i);
        // (A label goes with its shape or arrow.)
        if (e.type == drawing::ElementType::Text && m_selected.contains(e.containerId))
            continue;
        ++units;
        only = &e;
    }
    if (units == 0)
        return frame;
    frame.valid = true;
    if (units > 1) {
        frame.box = all;
        frame.proportional = true;
        frame.fixedShape = true;
        frame.edges = false;
        return frame;
    }
    frame.box = drawing::hasPoints(only->type) ? drawing::pointsRect(only->points) : only->box;
    frame.angle = only->elbowed ? 0 : only->angle;
    frame.rotates = !only->elbowed;
    const bool text = only->type == drawing::ElementType::Text;
    frame.proportional = text || only->type == drawing::ElementType::Image;
    frame.fixedShape = text;
    frame.edges = !text;
    return frame;
}

QRectF DrawingCanvas::paddedFrame(const SelectionFrame &frame) const
{
    const double pad = FramePadding / m_zoom;
    return frame.box.adjusted(-pad, -pad, pad, pad);
}

QPolygonF DrawingCanvas::frameCorners(const SelectionFrame &frame) const
{
    const QRectF r = paddedFrame(frame);
    const QPointF c = frame.box.center();
    return QPolygonF{drawing::rotatePoint(r.topLeft(), c, frame.angle),
                     drawing::rotatePoint(r.topRight(), c, frame.angle),
                     drawing::rotatePoint(r.bottomRight(), c, frame.angle),
                     drawing::rotatePoint(r.bottomLeft(), c, frame.angle)};
}

QList<std::pair<int, QPointF>> DrawingCanvas::frameHandles(const SelectionFrame &frame) const
{
    const QRectF r = paddedFrame(frame);
    const QPointF c = frame.box.center();
    const auto item = [&](QPointF local) { return toItem(drawing::rotatePoint(local, c, frame.angle)); };
    using H = drawing::Handle;
    QList<std::pair<int, QPointF>> out{{int(H::TopLeft), item(r.topLeft())},
                                       {int(H::TopRight), item(r.topRight())},
                                       {int(H::BottomRight), item(r.bottomRight())},
                                       {int(H::BottomLeft), item(r.bottomLeft())}};
    if (frame.edges) {
        const double mid = (r.left() + r.right()) / 2;
        const double middle = (r.top() + r.bottom()) / 2;
        // (No edge handle where the side has no length to scale, or none to sit on.)
        if (frame.box.height() > 0 && r.width() * m_zoom >= MinSideForEdgeHandle) {
            out.append({int(H::Top), item(QPointF(mid, r.top()))});
            out.append({int(H::Bottom), item(QPointF(mid, r.bottom()))});
        }
        if (frame.box.width() > 0 && r.height() * m_zoom >= MinSideForEdgeHandle) {
            out.append({int(H::Left), item(QPointF(r.left(), middle))});
            out.append({int(H::Right), item(QPointF(r.right(), middle))});
        }
    }
    return out;
}

std::optional<QPointF> DrawingCanvas::rotateHandlePos(const SelectionFrame &frame) const
{
    if (!frame.rotates)
        return std::nullopt;
    const QRectF r = paddedFrame(frame);
    const auto at = [&](double y) {
        return toItem(drawing::rotatePoint(QPointF((r.left() + r.right()) / 2, y), frame.box.center(), frame.angle));
    };
    const QPointF above = at(r.top() - RotateHandleDistance / m_zoom);
    // Under the frame when there is no room for it above (the topmost element as a drawing opens).
    const QRectF view = QRectF(0, 0, width(), height()).adjusted(8, 8, -8, -8);
    if (view.contains(above))
        return above;
    const QPointF below = at(r.bottom() + RotateHandleDistance / m_zoom);
    return view.contains(below) ? below : above;
}

int DrawingCanvas::frameHandleAt(const SelectionFrame &frame, QPointF itemPos, bool wideReach) const
{
    if (!frame.valid || !m_editable || m_textEditing)
        return -1;
    const double reach = wideReach ? TouchHandleReach : HandleReach;
    if (const std::optional<QPointF> at = rotateHandlePos(frame); at && QLineF(*at, itemPos).length() <= reach)
        return RotateHandle;
    const QRectF r = paddedFrame(frame);
    // A small element: inside its frame it moves (the handles would cover all of it).
    if (std::min(r.width(), r.height()) * m_zoom < MinSideForInnerHandles) {
        const QPointF local = drawing::rotatePoint(toScene(itemPos), frame.box.center(), -frame.angle);
        if (r.contains(local))
            return -1;
    }
    int best = -1;
    double bestDistance = reach;
    for (const auto handles = frameHandles(frame); const auto &[kind, at] : handles) {
        const double distance = QLineF(at, itemPos).length();
        if (distance <= bestDistance) {
            best = kind;
            bestDistance = distance;
        }
    }
    return best;
}

QRectF DrawingCanvas::selectionOuterRect() const
{
    const SelectionFrame frame = selectionFrame();
    if (!frame.valid)
        return {};
    QPolygonF corners = frameCorners(frame);
    for (QPointF &p : corners)
        p = toItem(p);
    QRectF out = corners.boundingRect();
    if (const std::optional<QPointF> at = rotateHandlePos(frame))
        out = out.united(QRectF(*at, QSizeF(0, 0)).adjusted(-6, -6, 6, 6));
    return m_gesture == Gesture::Move ? out.translated(m_moveDelta) : out;
}

QVariantMap DrawingCanvas::selectionStyle() const
{
    QVariantMap style;
    const auto first = [&](const QString &flag) {
        if (style.contains(flag))
            return false;
        style.insert(flag, true);
        return true;
    };
    for (const drawing::Stroke &e : m_doc.strokes) {
        if (!m_selected.contains(e.id))
            continue;
        if (!style.contains(u"opacity"_s))
            style.insert(u"opacity"_s, e.opacity);
        if (drawing::hasFill(e) && first(u"fillable"_s)) {
            style.insert(u"fill"_s, e.fill);
            style.insert(u"fillStyle"_s, drawing::fillStyleName(e.fillStyle));
        }
        if (drawing::hasThickness(e) && first(u"thick"_s))
            style.insert(u"thickness"_s, drawing::thicknessOf(e));
        if (drawing::hasDashes(e) && first(u"dashable"_s))
            style.insert(u"strokeStyle"_s, drawing::strokeStyleName(e.strokeStyle));
        if (drawing::canRound(e) && first(u"roundable"_s))
            style.insert(u"rounded"_s, e.roundness != 0);
        if (e.type == drawing::ElementType::Text && first(u"text"_s))
            style.insert(u"fontSize"_s, e.fontSize);
        if (drawing::hasHeads(e) && first(u"heads"_s)) {
            style.insert(u"startHead"_s, drawing::arrowheadName(e.startArrowhead));
            style.insert(u"endHead"_s, drawing::arrowheadName(e.endArrowhead));
        }
    }
    style.insert(u"canGroup"_s, drawing::canGroup(m_doc, m_selected));
    style.insert(u"canUngroup"_s, drawing::canUngroup(m_doc, m_selected));
    const int units = m_selected.isEmpty() ? 0 : drawing::arrangeUnits(m_doc, m_selected);
    style.insert(u"canAlign"_s, units >= 2);
    style.insert(u"canDistribute"_s, units >= 3);
    return style;
}

void DrawingCanvas::styleSelection(const QVariantMap &change)
{
    const QSet<QString> ids = selectionToEdit();
    if (ids.isEmpty())
        return;
    drawing::ElementStyle style;
    if (change.contains(u"fill"_s)) {
        const QString fill = change.value(u"fill"_s).toString();
        if (fill == u"transparent")
            style.fill = fill;
        else if (const QColor color(fill); color.isValid())
            style.fill = color.name(QColor::HexRgb);
    }
    using FS = drawing::FillStyle;
    using SS = drawing::StrokeStyle;
    if (change.contains(u"fillStyle"_s))
        style.fillStyle = enumFromName<FS>(change.value(u"fillStyle"_s).toString(),
                                           {FS::Solid, FS::Hachure, FS::CrossHatch}, drawing::fillStyleName);
    if (change.contains(u"thickness"_s))
        style.thickness = std::clamp(change.value(u"thickness"_s).toInt(), 0, 2);
    if (change.contains(u"strokeStyle"_s))
        style.strokeStyle = enumFromName<SS>(change.value(u"strokeStyle"_s).toString(),
                                             {SS::Solid, SS::Dashed, SS::Dotted}, drawing::strokeStyleName);
    if (change.contains(u"opacity"_s))
        style.opacity = std::clamp(change.value(u"opacity"_s).toInt(), 0, 100);
    if (change.contains(u"rounded"_s))
        style.rounded = change.value(u"rounded"_s).toBool();
    if (change.contains(u"fontSize"_s)) {
        const double size = change.value(u"fontSize"_s).toDouble();
        if (std::isfinite(size) && size >= 1 && size <= 1000)
            style.fontSize = size;
    }
    if (change.contains(u"startHead"_s))
        style.startArrowhead = arrowheadFromName(change.value(u"startHead"_s).toString());
    if (change.contains(u"endHead"_s))
        style.endArrowhead = arrowheadFromName(change.value(u"endHead"_s).toString());
    const drawing::TextMetrics metrics = textMetrics();
    editElements([&](drawing::Drawing &doc) { drawing::styleElements(doc, ids, style, metrics, stamp()); });
}

void DrawingCanvas::rotateSelection(qreal degrees)
{
    const SelectionFrame frame = selectionFrame();
    if (!frame.valid || busy() || !std::isfinite(degrees))
        return;
    const QSet<QString> ids = m_selected;
    editElements([&](drawing::Drawing &doc) {
        drawing::rotateElements(doc, ids, frame.box.center(), degrees * std::numbers::pi / 180, stamp());
    });
}

void DrawingCanvas::flipSelection(const QString &axis)
{
    const SelectionFrame frame = selectionFrame();
    if (!frame.valid || busy())
        return;
    const QSet<QString> ids = m_selected;
    const drawing::Flip flip = axis == u"vertical" ? drawing::Flip::Vertical : drawing::Flip::Horizontal;
    editElements([&](drawing::Drawing &doc) { drawing::flipElements(doc, ids, flip, frame.box.center(), stamp()); });
}

void DrawingCanvas::groupSelection()
{
    if (const QSet<QString> ids = selectionToEdit(); !ids.isEmpty())
        editElements([&](drawing::Drawing &doc) { drawing::groupElements(doc, ids, stamp()); });
}

void DrawingCanvas::ungroupSelection()
{
    if (const QSet<QString> ids = selectionToEdit(); !ids.isEmpty())
        editElements([&](drawing::Drawing &doc) { drawing::ungroupElements(doc, ids, stamp()); });
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
    const QString color = stored.name(QColor::HexRgb);
    if (const QSet<QString> ids = selectionToEdit(); !ids.isEmpty() && stored.isValid())
        editElements([&](drawing::Drawing &doc) { drawing::recolorElements(doc, ids, color, stamp()); });
}

void DrawingCanvas::reorderSelection(const QString &how)
{
    const drawing::Reorder order = how == u"forward"    ? drawing::Reorder::Forward
                                   : how == u"backward" ? drawing::Reorder::Backward
                                   : how == u"back"     ? drawing::Reorder::ToBack
                                                        : drawing::Reorder::ToFront;
    if (const QSet<QString> ids = selectionToEdit(); !ids.isEmpty())
        editElements([&](drawing::Drawing &doc) { drawing::reorderElements(doc, ids, order, stamp()); });
}

void DrawingCanvas::alignSelection(const QString &how)
{
    using A = drawing::Align;
    static constexpr std::pair<QStringView, A> names[] = {{u"left", A::Left},     {u"center", A::Center},
                                                          {u"right", A::Right},   {u"top", A::Top},
                                                          {u"middle", A::Middle}, {u"bottom", A::Bottom}};
    const auto named = std::ranges::find_if(names, [&how](const auto &name) { return name.first == how; });
    if (const QSet<QString> ids = selectionToEdit(); !ids.isEmpty() && named != std::ranges::end(names))
        editElements([&](drawing::Drawing &doc) { drawing::alignElements(doc, ids, named->second, stamp()); });
}

void DrawingCanvas::distributeSelection(const QString &axis)
{
    if (axis != u"horizontal" && axis != u"vertical")
        return;
    const drawing::Distribute along =
            axis == u"vertical" ? drawing::Distribute::Vertical : drawing::Distribute::Horizontal;
    if (const QSet<QString> ids = selectionToEdit(); !ids.isEmpty())
        editElements([&](drawing::Drawing &doc) { drawing::distributeElements(doc, ids, along, stamp()); });
}

bool DrawingCanvas::copySelection()
{
    if (m_selected.isEmpty() || !m_editable)
        return false;
    const QByteArray json = drawing::clipboardJson(m_doc, selection());
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (json.isEmpty() || !clipboard)
        return false;
    clipboard->setText(QString::fromUtf8(json));
    return true;
}

bool DrawingCanvas::cutSelection()
{
    if (busy() || !copySelection())
        return false;
    deleteSelection();
    return true;
}

QVariantMap DrawingCanvas::paste()
{
    const QClipboard *clipboard = QGuiApplication::clipboard();
    return pasteText(clipboard ? clipboard->text() : QString());
}

QVariantMap DrawingCanvas::pasteText(const QString &text)
{
    QVariantMap out{{u"pasted"_s, 0}, {u"skipped"_s, 0}, {u"text"_s, false}};
    if (!m_editable || busy() || m_textEditing || text.trimmed().isEmpty())
        return out;
    // At the mouse pointer over the paper, else in the middle of the view.
    const QPointF at = clampedToLimit(m_pointer ? toScene(*m_pointer) : m_center);
    if (const std::optional<drawing::ClipboardContent> content = drawing::parseClipboard(text.toUtf8())) {
        out.insert(u"skipped"_s, content->skipped);
        QStringList pasted;
        editElements([&](drawing::Drawing &doc) { pasted = drawing::pasteElements(doc, *content, at, stamp()); });
        if (!pasted.isEmpty()) {
            setTool(u"select"_s);
            select(pasted);
        }
        out.insert(u"pasted"_s, int(pasted.size()));
        return out;
    }
    if (text.size() > MaxPastedText)
        return out;
    // Plain text: a new text, its middle there.
    const drawing::TextMetrics metrics = textMetrics();
    const drawing::TextStyle style{.color = m_color.name(QColor::HexRgb), .fontSize = m_fontSize};
    QString id;
    editElements([&](drawing::Drawing &doc) {
        drawing::Stroke t = drawing::newText(at, text, style, metrics, stamp());
        t.box.moveCenter(at);
        t.box = boxWithinLimit(t.box);
        id = t.id;
        doc.strokes.append(std::move(t));
    });
    if (!id.isEmpty()) {
        setTool(u"select"_s);
        select({id});
        out.insert(u"pasted"_s, 1);
        out.insert(u"text"_s, true);
    }
    return out;
}

void DrawingCanvas::nudgeSelection(qreal dx, qreal dy)
{
    if (const QSet<QString> ids = selectionToEdit(); !ids.isEmpty() && std::isfinite(dx) && std::isfinite(dy))
        editElements([&](drawing::Drawing &doc) { drawing::moveElements(doc, ids, QPointF(dx, dy), stamp()); });
}

bool DrawingCanvas::cancelGesture()
{
    if (m_gesture == Gesture::Lasso || m_gesture == Gesture::Move || m_gesture == Gesture::Shape
        || m_gesture == Gesture::Tap || m_gesture == Gesture::Resize || m_gesture == Gesture::Rotate) {
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
    case Gesture::Resize: resizeSelectionTo(itemPos, p); break;
    case Gesture::Rotate: rotateSelectionTo(itemPos, p); break;
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
    case Gesture::Resize:
        resizeSelectionTo(itemPos, p);
        finishResize();
        break;
    case Gesture::Rotate:
        rotateSelectionTo(itemPos, p);
        finishRotate();
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
    case Gesture::Resize:
    case Gesture::Rotate:
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
    const bool shift = p.modifiers & Qt::ShiftModifier;
    const SelectionFrame frame = selectionFrame();
    const int handle = frameHandleAt(frame, itemPos, p.touch || p.stylus);
    if (handle >= 0) {
        m_transformFrame = frame;
        m_transformHandle = handle;
        m_resizeTo = frame.box;
        m_rotateBy = 0;
        m_gestureTransform = QTransform();
        setGesture(handle == RotateHandle ? Gesture::Rotate : Gesture::Resize);
        invalidateCache(); // (the selection is drawn on top meanwhile)
        return;
    }
    const double margin = p.touch ? 10 : 0;
    bool inside = false;
    if (frame.valid && frame.angle != 0) {
        // Inside the turned frame (no drag from the corners of what it covers).
        const double grow = margin / m_zoom;
        const QPointF local = drawing::rotatePoint(toScene(itemPos), frame.box.center(), -frame.angle);
        inside = paddedFrame(frame).adjusted(-grow, -grow, grow, grow).contains(local);
    } else if (frame.valid) {
        inside = selectionRect().adjusted(-margin, -margin, margin, margin).contains(itemPos);
    }
    if (inside && !shift) {
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
    if (m_selectMode == u"box") {
        const QRectF box = QRectF(m_gestureStart, itemPos).normalized();
        m_lasso = QPolygonF{box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()};
        m_lassoLength = QLineF(m_gestureStart, itemPos).length();
        m_lastItemPos = itemPos;
        update();
        return;
    }
    const double step = QLineF(m_lasso.constLast(), itemPos).length();
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
                || a.points.size() != b.points.size() || a.points.isEmpty() || prepared.version != b.version
                || prepared.nonce != b.versionNonce || prepared.updated != b.updated)
                continue;
            const QPointF shift(a.points.first().x - b.points.first().x, a.points.first().y - b.points.first().y);
            prepared.ink.translate(shift);
            prepared.area.translate(shift);
            prepared.bounds.translate(shift);
            prepared.version = a.version;
            prepared.nonce = a.versionNonce;
            prepared.updated = a.updated;
        }
    });
    if (!moved)
        invalidateCache();
    Q_EMIT selectionChanged();
}

void DrawingCanvas::resizeSelectionTo(QPointF itemPos, const Pointer &p)
{
    if (m_gesture != Gesture::Resize)
        return;
    m_lastItemPos = itemPos;
    const SelectionFrame &frame = m_transformFrame;
    const QPointF drag = drawing::rotatePoint((itemPos - m_gestureStart) / m_zoom, QPointF(), -frame.angle);
    const bool proportional = frame.fixedShape || (frame.proportional != bool(p.modifiers & Qt::ShiftModifier));
    m_resizeTo = drawing::resizedFrame(frame.box, drawing::Handle(m_transformHandle), drag, proportional,
                                       MinResizedSide / m_zoom);
    // Scene → the frame's axes → scaled → back (the same as drawing::resizeElements places them).
    const QRectF &from = frame.box;
    const QPointF pivot = from.center();
    const double sx = from.width() > 0 ? m_resizeTo.width() / from.width() : 1;
    const double sy = from.height() > 0 ? m_resizeTo.height() / from.height() : 1;
    QTransform t;
    t.translate(pivot.x(), pivot.y());
    t.rotateRadians(frame.angle);
    t.translate(-pivot.x(), -pivot.y());
    t.translate(m_resizeTo.left(), m_resizeTo.top());
    t.scale(sx, sy);
    t.translate(-from.left(), -from.top());
    t.translate(pivot.x(), pivot.y());
    t.rotateRadians(-frame.angle);
    t.translate(-pivot.x(), -pivot.y());
    m_gestureTransform = t;
    update();
}

void DrawingCanvas::finishResize()
{
    const QRectF from = m_transformFrame.box;
    const QRectF to = m_resizeTo;
    const double angle = m_transformFrame.angle;
    setGesture(Gesture::None);
    const QSet<QString> ids = m_selected;
    const drawing::TextMetrics metrics = textMetrics();
    if (!editElements(
                [&](drawing::Drawing &doc) { drawing::resizeElements(doc, ids, from, to, angle, metrics, stamp()); }))
        invalidateCache();
    Q_EMIT selectionChanged();
}

void DrawingCanvas::rotateSelectionTo(QPointF itemPos, const Pointer &p)
{
    if (m_gesture != Gesture::Rotate)
        return;
    m_lastItemPos = itemPos;
    const SelectionFrame &frame = m_transformFrame;
    const QPointF pivot = frame.box.center();
    const QPointF centre = toItem(pivot);
    const auto bearing = [&centre](QPointF at) { return std::atan2(at.y() - centre.y(), at.x() - centre.x()); };
    double by = bearing(itemPos) - bearing(m_gestureStart);
    if (p.modifiers & Qt::ShiftModifier) {
        // 15° steps: of the element's angle, or of the turn for several.
        const double step = std::numbers::pi / 12;
        by = std::round((frame.angle + by) / step) * step - frame.angle;
    }
    m_rotateBy = by;
    QTransform t;
    t.translate(pivot.x(), pivot.y());
    t.rotateRadians(by);
    t.translate(-pivot.x(), -pivot.y());
    m_gestureTransform = t;
    update();
}

void DrawingCanvas::finishRotate()
{
    const QPointF pivot = m_transformFrame.box.center();
    const double by = m_rotateBy;
    setGesture(Gesture::None);
    const QSet<QString> ids = m_selected;
    if (!editElements([&](drawing::Drawing &doc) { drawing::rotateElements(doc, ids, pivot, by, stamp()); }))
        invalidateCache();
    Q_EMIT selectionChanged();
}

void DrawingCanvas::updateSelectCursor(QPointF itemPos)
{
    if (m_panMode || busy())
        return;
    Qt::CursorShape shape = Qt::ArrowCursor;
    const SelectionFrame frame = selectionFrame();
    if (frame.valid) {
        const int handle = frameHandleAt(frame, itemPos, false);
        if (handle == RotateHandle) {
            shape = Qt::PointingHandCursor;
        } else if (handle >= 0) {
            // By where the handle lies as the frame is turned now.
            const QPointF centre = toItem(frame.box.center());
            double degrees = 0;
            for (const auto handles = frameHandles(frame); const auto &[kind, at] : handles) {
                if (kind == handle)
                    degrees = std::atan2(at.y() - centre.y(), at.x() - centre.x()) * 180 / std::numbers::pi;
            }
            const double line = std::fmod(degrees + 180, 180);
            shape = line < 22.5 || line >= 157.5 ? Qt::SizeHorCursor
                    : line < 67.5                ? Qt::SizeFDiagCursor
                    : line < 112.5               ? Qt::SizeVerCursor
                                                 : Qt::SizeBDiagCursor;
        } else {
            const QPointF local = drawing::rotatePoint(toScene(itemPos), frame.box.center(), -frame.angle);
            if (paddedFrame(frame).contains(local))
                shape = Qt::SizeAllCursor;
        }
    }
    if (cursor().shape() != shape)
        setCursor(shape);
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
        if (drawing::hitTest(e, scene, tolerance) && !drawing::isLocked(m_doc, e))
            return i;
    }
    return -1;
}

qsizetype DrawingCanvas::textTargetAt(QPointF itemPos, const Pointer &p) const
{
    const qsizetype hit = elementAt(itemPos, p);
    if (hit >= 0
        && (m_doc.strokes.at(hit).type == drawing::ElementType::Text || drawing::holdsLabel(m_doc.strokes.at(hit))))
        return hit;
    // Inside a shape or a polygon (an unfilled one is no hit there): its label.
    const QPointF scene = toScene(itemPos);
    for (qsizetype i = m_doc.strokes.size() - 1; i >= 0; --i) {
        const drawing::Stroke &e = m_doc.strokes.at(i);
        if (e.id.isEmpty() || e.locked || !drawing::holdsLabel(e))
            continue;
        const QPointF local = drawing::rotatePoint(scene, drawing::elementCenter(e), -e.angle);
        if (drawing::isShape(e.type) ? e.box.contains(local) : drawing::pointInPolygon(local, polygonOf(e)))
            return i;
    }
    return -1;
}

void DrawingCanvas::selectTap(QPointF itemPos, const Pointer &p)
{
    const bool twice = m_lastTap.isValid() && m_lastTap.elapsed() < 450 && QLineF(m_lastTapPos, itemPos).length() < 12;
    m_lastTap.start();
    m_lastTapPos = itemPos;
    const bool shift = p.modifiers & Qt::ShiftModifier;
    if (twice && !shift) {
        if (const qsizetype target = textTargetAt(itemPos, p); target >= 0) {
            m_lastTap.invalidate();
            requestTextEdit(target, toScene(itemPos));
            return;
        }
    }
    const qsizetype i = elementAt(itemPos, p);
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
    drawing::EditStamp preview;
    preview.newId = [] { return u"~"_s; };
    const drawing::ShapeStyle style{m_color.name(QColor::HexRgb), std::clamp(m_penWidth * 2, 0.5, 40.0), true};
    const bool constrain = p.modifiers & Qt::ShiftModifier;
    const QPointF from = clampedToLimit(toScene(m_gestureStart));
    const QPointF to = clampedToLimit(toScene(itemPos));
    const std::optional<drawing::PolygonKind> polygon = drawing::polygonKindFromName(m_tool);
    m_liveShape = polygon ? drawing::newPolygon(*polygon, from, to, constrain, style, preview)
                          : drawing::newShape(shapeType(m_tool), from, to, constrain, style, preview);
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
    editElements([&shape](drawing::Drawing &doc) { doc.strokes.append(std::move(shape)); });
}

void DrawingCanvas::finishTap(QPointF itemPos, const Pointer &p)
{
    setGesture(Gesture::None);
    if (QLineF(m_gestureStart, itemPos).length() < TapLength)
        requestTextEdit(textTargetAt(itemPos, p), toScene(itemPos));
}

// --- Text -------------------------------------------------------------------------------------

void DrawingCanvas::requestTextEdit(qsizetype index, QPointF scene)
{
    if (!m_editable)
        return;
    endTextEdit();
    // The text edited (none: a new one) and the shape it is, or becomes, the label of.
    const drawing::Stroke *text = nullptr;
    const drawing::Stroke *shape = nullptr;
    if (index >= 0) {
        const drawing::Stroke &e = m_doc.strokes.at(index);
        if (e.type == drawing::ElementType::Text) {
            text = &e;
            const qsizetype c = drawing::indexOfElement(m_doc, e.containerId);
            if (c >= 0 && drawing::holdsLabel(m_doc.strokes.at(c)))
                shape = &m_doc.strokes.at(c);
        } else {
            shape = &e;
            for (const drawing::Stroke &t : std::as_const(m_doc.strokes)) {
                if (t.type == drawing::ElementType::Text && t.containerId == e.id && !e.id.isEmpty())
                    text = &t;
            }
        }
    }
    const QString stored = text ? text->color : m_color.name(QColor::HexRgb);
    QVariantMap r{{u"id"_s, text ? text->id : QString()},
                  {u"containerId"_s, shape ? shape->id : QString()},
                  {u"text"_s, text ? drawing::editableText(*text) : QString()},
                  {u"fontSize"_s, text ? text->fontSize : m_fontSize},
                  {u"fontFamily"_s, textFontFamily(text ? text->fontFamily : 2)},
                  {u"color"_s, displayColor(QColor(stored))},
                  {u"storedColor"_s, stored}};
    if (shape) {
        // As wide as the label wraps in its shape (narrower in an ellipse, a diamond or a polygon),
        // centred.
        const double width = std::max(1.0, drawing::labelMaxWidth(*shape));
        const QPointF centre = drawing::labelCenter(*shape);
        r.insert({{u"sceneX"_s, centre.x() - width / 2},
                  {u"sceneY"_s, centre.y()},
                  {u"width"_s, width},
                  {u"align"_s, u"center"_s},
                  {u"valign"_s, u"middle"_s}});
    } else if (text) {
        r.insert({{u"sceneX"_s, text->box.left()},
                  {u"sceneY"_s, text->box.top()},
                  {u"width"_s, text->autoResize ? 0.0 : text->box.width()},
                  {u"align"_s, alignName(text->textAlign)},
                  {u"valign"_s, u"top"_s}});
    } else {
        // A new text: its first line's middle at the tap.
        r.insert({{u"sceneX"_s, scene.x()},
                  {u"sceneY"_s, scene.y() - m_fontSize * drawing::NewTextLineHeight / 2},
                  {u"width"_s, 0},
                  {u"align"_s, u"left"_s},
                  {u"valign"_s, u"top"_s}});
    }
    m_textEditKey = text ? text->id : QString();
    m_textEditing = true;
    invalidateCache();
    Q_EMIT textEditingChanged();
    m_textRequest = r;
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
    const QColor stored(request.value(u"storedColor"_s).toString());
    const drawing::TextStyle style{
            .color = (stored.isValid() ? stored : m_color).name(QColor::HexRgb),
            .fontSize = std::clamp(request.value(u"fontSize"_s, m_fontSize).toDouble(), 1.0, 1000.0)};
    if (!id.isEmpty()) {
        editElements([&](drawing::Drawing &doc) { drawing::setText(doc, id, text, metrics, stamp()); });
    } else if (!containerId.isEmpty()) {
        editElements(
                [&](drawing::Drawing &doc) { drawing::addLabel(doc, containerId, text, style, metrics, stamp()); });
    } else if (!text.trimmed().isEmpty()) {
        const QPointF at(request.value(u"sceneX"_s).toDouble(), request.value(u"sceneY"_s).toDouble());
        if (!std::isfinite(at.x()) || !std::isfinite(at.y()))
            return;
        editElements([&](drawing::Drawing &doc) {
            // (All of its box within CoordinateLimit, as pasteText() keeps it — not only its top left.)
            drawing::Stroke t = drawing::newText(clampedToLimit(at), text, style, metrics, stamp());
            t.box = boxWithinLimit(t.box);
            doc.strokes.append(std::move(t));
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
