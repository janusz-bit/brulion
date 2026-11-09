#include "drawing/edit.h"

#include "drawing/geometry.h"
#include "drawing/merge.h"

#include <QHash>
#include <QRandomGenerator>

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

namespace {

int randomOf(const EditStamp &s)
{
    const quint32 r = s.random ? s.random() : 1;
    return int(std::max<quint32>(1, r % 2147483647u));
}

QString newIdOf(const EditStamp &s)
{
    if (s.newId)
        return s.newId();
    static std::atomic<int> counter{0};
    return u"e"_s + QString::number(++counter);
}

void stamp(Stroke &e, const EditStamp &s)
{
    e.version = e.version < INT_MAX ? e.version + 1 : 2;
    e.versionNonce = randomOf(s);
    e.updated = s.now;
}

double round2(double v)
{
    return std::round(v * 100) / 100;
}

void translate(Stroke &e, QPointF delta)
{
    for (StrokePoint &p : e.points) {
        p.x = round2(p.x + delta.x());
        p.y = round2(p.y + delta.y());
    }
    e.box.translate(delta);
    e.box = QRectF(round2(e.box.x()), round2(e.box.y()), e.box.width(), e.box.height());
}

double lineWidth(QStringView line, int family, double size, const TextMetrics &m)
{
    if (m.width)
        return std::max(0.0, m.width(line, family, size));
    return 0.55 * size * double(line.size());
}

/// The end point `k` of a line or an arrow moved by `delta`; an elbow arrow also moves the
/// neighbouring corner `j` along the segment's axis, keeping right angles.
void moveEnd(Stroke &e, qsizetype k, qsizetype j, QPointF delta)
{
    if (e.elbowed && e.points.size() >= 3) {
        StrokePoint &tip = e.points[k];
        StrokePoint &corner = e.points[j];
        if (std::abs(tip.y - corner.y) < 1e-6)
            corner.y = round2(corner.y + delta.y());
        else if (std::abs(tip.x - corner.x) < 1e-6)
            corner.x = round2(corner.x + delta.x());
    }
    e.points[k].x = round2(e.points[k].x + delta.x());
    e.points[k].y = round2(e.points[k].y + delta.y());
    e.box = pointsRect(e.points);
}

/// The point halfway along a polyline.
QPointF polylineMiddle(const QList<StrokePoint> &points)
{
    if (points.isEmpty())
        return {};
    double total = 0;
    for (qsizetype i = 0; i + 1 < points.size(); ++i)
        total += std::hypot(points.at(i + 1).x - points.at(i).x, points.at(i + 1).y - points.at(i).y);
    double rest = total / 2;
    for (qsizetype i = 0; i + 1 < points.size(); ++i) {
        const QPointF a(points.at(i).x, points.at(i).y), b(points.at(i + 1).x, points.at(i + 1).y);
        const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (len >= rest && len > 0)
            return a + (b - a) * (rest / len);
        rest -= len;
    }
    return QPointF(points.first().x, points.first().y);
}

QHash<QString, qsizetype> indexById(const Drawing &d)
{
    QHash<QString, qsizetype> index;
    index.reserve(d.strokes.size());
    for (qsizetype i = 0; i < d.strokes.size(); ++i) {
        if (!d.strokes.at(i).id.isEmpty())
            index.insert(d.strokes.at(i).id, i); // (a duplicate id: the last one)
    }
    return index;
}

/// The element is locked, or the label of a locked shape.
bool lockedIn(const Drawing &d, const QHash<QString, qsizetype> &index, const Stroke &e)
{
    if (e.locked)
        return true;
    if (!e.containerId.isEmpty()) {
        const qsizetype c = index.value(e.containerId, -1);
        if (c >= 0 && d.strokes.at(c).locked)
            return true;
    }
    return false;
}

} // namespace

QString newElementId()
{
    // nanoid's alphabet and length, as Excalidraw's element ids.
    static constexpr char alphabet[] = "useandom-26T198340PX75pxJACKVERYMINDBUSHWOLF_GQZbfghjklqvwyzrict";
    QString id;
    id.reserve(21);
    for (int i = 0; i < 21; ++i)
        id += QLatin1Char(alphabet[QRandomGenerator::global()->bounded(64)]);
    return id;
}

EditStamp randomEditStamp(qint64 now)
{
    EditStamp s;
    s.now = now;
    s.random = [] { return QRandomGenerator::global()->bounded(1u, 2147483647u); };
    s.newId = [] { return newElementId(); };
    return s;
}

qsizetype indexOfElement(const Drawing &d, QStringView id)
{
    if (id.isEmpty())
        return -1;
    for (qsizetype i = 0; i < d.strokes.size(); ++i) {
        if (d.strokes.at(i).id == id)
            return i;
    }
    return -1;
}

bool isLocked(const Drawing &d, const Stroke &e)
{
    if (e.locked)
        return true;
    if (e.containerId.isEmpty())
        return false;
    const qsizetype c = indexOfElement(d, e.containerId);
    return c >= 0 && d.strokes.at(c).locked;
}

QStringList selectionClosure(const Drawing &d, const QStringList &ids)
{
    const QHash<QString, qsizetype> index = indexById(d);
    QHash<QString, QStringList> groups;
    QHash<QString, QStringList> labels;
    for (const Stroke &e : d.strokes) {
        for (const QString &g : e.groupIds)
            groups[g].append(e.id);
        if (e.type == ElementType::Text && !e.containerId.isEmpty())
            labels[e.containerId].append(e.id);
    }
    QSet<QString> taken;
    QStringList queue = ids;
    while (!queue.isEmpty()) {
        const QString id = queue.takeLast();
        const qsizetype i = index.value(id, -1);
        if (i < 0 || taken.contains(id))
            continue;
        const Stroke &e = d.strokes.at(i);
        if (lockedIn(d, index, e))
            continue;
        taken.insert(id);
        if (!e.groupIds.isEmpty() && !taken.contains(e.groupIds.last() + QChar(0x1f))) {
            taken.insert(e.groupIds.last() + QChar(0x1f)); // (each group's members queued once)
            queue.append(groups.value(e.groupIds.last()));
        }
        for (const QString &b : e.boundIds) {
            const qsizetype k = index.value(b, -1);
            if (k >= 0 && d.strokes.at(k).type == ElementType::Text && d.strokes.at(k).containerId == e.id)
                queue.append(b);
        }
        if (!e.id.isEmpty())
            queue.append(labels.value(e.id));
        if (!e.containerId.isEmpty() && index.contains(e.containerId))
            queue.append(e.containerId);
    }
    QStringList out;
    for (const Stroke &e : d.strokes) {
        if (taken.contains(e.id)) {
            out.append(e.id);
            taken.remove(e.id); // (a duplicate id once)
        }
    }
    return out;
}

void moveElements(Drawing &d, const QSet<QString> &ids, QPointF delta, const EditStamp &s)
{
    if (!std::isfinite(delta.x()) || !std::isfinite(delta.y()) || (delta.x() == 0 && delta.y() == 0))
        return;
    const QHash<QString, qsizetype> index = indexById(d);
    QSet<QString> moved;
    QRectF extent;
    const auto extend = [&extent](const QRectF &r) { extent = extent.isNull() ? r : extent.united(r); };
    for (const Stroke &e : std::as_const(d.strokes)) {
        if (!ids.contains(e.id) || lockedIn(d, index, e))
            continue;
        moved.insert(e.id);
        extend(hasPoints(e.type) ? pointsRect(e.points).adjusted(0, 0, 1e-9, 1e-9) : e.box);
    }
    // Labels of moved shapes and arrows go with them.
    for (Stroke &e : d.strokes) {
        if (e.type == ElementType::Text && !e.locked && moved.contains(e.containerId) && !moved.contains(e.id)) {
            moved.insert(e.id);
            extend(e.box);
        }
    }
    if (moved.isEmpty())
        return;
    // Nothing leaves CoordinateLimit.
    const double L = CoordinateLimit;
    delta.setX(std::clamp(delta.x(), -L - extent.left(), L - extent.right()));
    delta.setY(std::clamp(delta.y(), -L - extent.top(), L - extent.bottom()));
    if (delta.x() == 0 && delta.y() == 0)
        return;
    QHash<QString, QPointF> arrowLabelShift;
    for (Stroke &e : d.strokes) {
        if (moved.contains(e.id)) {
            translate(e, delta);
            stamp(e, s);
            continue;
        }
        if (!isLinear(e.type) || e.locked || e.points.isEmpty())
            continue;
        const bool start = !e.startBound.isEmpty() && moved.contains(e.startBound);
        const bool end = !e.endBound.isEmpty() && moved.contains(e.endBound);
        if (!start && !end)
            continue;
        const QPointF before = polylineMiddle(e.points);
        if (start && end) {
            translate(e, delta);
        } else {
            const qsizetype n = e.points.size();
            const qsizetype k = start ? 0 : n - 1;
            moveEnd(e, k, start ? 1 : n - 2, delta);
        }
        stamp(e, s);
        arrowLabelShift.insert(e.id, polylineMiddle(e.points) - before);
    }
    if (arrowLabelShift.isEmpty())
        return;
    for (Stroke &e : d.strokes) {
        if (e.type == ElementType::Text && !moved.contains(e.id) && arrowLabelShift.contains(e.containerId)) {
            translate(e, arrowLabelShift.value(e.containerId));
            stamp(e, s);
        }
    }
}

QStringList duplicateElements(Drawing &d, const QStringList &ids, QPointF offset, const EditStamp &s)
{
    QSet<QString> want(ids.cbegin(), ids.cend());
    for (const Stroke &e : d.strokes) {
        if (e.type == ElementType::Text && want.contains(e.containerId))
            want.insert(e.id);
    }
    const QHash<QString, qsizetype> index = indexById(d);
    QList<qsizetype> originals;
    QHash<QString, QString> idMap;
    for (qsizetype i = 0; i < d.strokes.size(); ++i) {
        const Stroke &e = d.strokes.at(i);
        if (!want.contains(e.id) || idMap.contains(e.id) || lockedIn(d, index, e))
            continue;
        originals.append(i);
        idMap.insert(e.id, newIdOf(s));
    }
    if (originals.isEmpty())
        return {};
    QHash<QString, QString> groupMap;
    QList<Stroke> copies;
    QStringList out;
    for (const qsizetype i : std::as_const(originals)) {
        Stroke c = d.strokes.at(i);
        c.id = idMap.value(c.id);
        c.seed = randomOf(s);
        c.versionNonce = randomOf(s);
        c.version = 1;
        c.updated = s.now;
        for (QString &g : c.groupIds) {
            if (!groupMap.contains(g))
                groupMap.insert(g, newIdOf(s));
            g = groupMap.value(g);
        }
        QStringList bound;
        for (const QString &b : std::as_const(c.boundIds)) {
            if (idMap.contains(b))
                bound.append(idMap.value(b));
        }
        c.boundIds = bound;
        c.containerId = idMap.value(c.containerId);
        c.startBound = idMap.value(c.startBound);
        c.endBound = idMap.value(c.endBound);
        translate(c, offset);
        // (Its fractional index belongs to the original: Excalidraw gives the copy one.)
        c.element.remove(u"index"_s);
        out.append(c.id);
        copies.append(std::move(c));
    }
    const qsizetype at = originals.last() + 1;
    for (qsizetype k = 0; k < copies.size(); ++k)
        d.strokes.insert(at + k, copies.at(k));
    return out;
}

void deleteElements(Drawing &d, const QSet<QString> &ids, const EditStamp &s)
{
    const QHash<QString, qsizetype> index = indexById(d);
    QSet<QString> gone;
    for (const Stroke &e : d.strokes) {
        if (ids.contains(e.id) && !lockedIn(d, index, e))
            gone.insert(e.id);
    }
    for (const Stroke &e : d.strokes) {
        if (e.type == ElementType::Text && gone.contains(e.containerId))
            gone.insert(e.id);
    }
    if (gone.isEmpty())
        return;
    QList<Stroke> kept;
    kept.reserve(d.strokes.size());
    for (Stroke &e : d.strokes) {
        if (gone.contains(e.id))
            continue;
        bool touched = false;
        const qsizetype bound = e.boundIds.size();
        e.boundIds.removeIf([&gone](const QString &b) { return gone.contains(b); });
        touched = e.boundIds.size() != bound;
        if (!e.startBound.isEmpty() && gone.contains(e.startBound)) {
            e.startBound.clear();
            touched = true;
        }
        if (!e.endBound.isEmpty() && gone.contains(e.endBound)) {
            e.endBound.clear();
            touched = true;
        }
        if (touched)
            stamp(e, s);
        kept.append(std::move(e));
    }
    d.strokes = std::move(kept);
}

void recolorElements(Drawing &d, const QSet<QString> &ids, const QString &color, const EditStamp &s)
{
    const QHash<QString, qsizetype> index = indexById(d);
    QList<qsizetype> chosen;
    for (qsizetype i = 0; i < d.strokes.size(); ++i) {
        const Stroke &e = d.strokes.at(i);
        if (ids.contains(e.id) && e.color != color && !lockedIn(d, index, e))
            chosen.append(i);
    }
    for (const qsizetype i : std::as_const(chosen)) {
        d.strokes[i].color = color;
        stamp(d.strokes[i], s);
    }
}

void reorderElements(Drawing &d, const QSet<QString> &ids, Reorder how, const EditStamp &s)
{
    // Units: an element with its labels; blocks: neighbouring units of one outermost group.
    QList<QList<qsizetype>> units;
    QHash<QString, qsizetype> unitOf;
    for (qsizetype i = 0; i < d.strokes.size(); ++i) {
        const Stroke &e = d.strokes.at(i);
        if (e.type == ElementType::Text && !e.containerId.isEmpty() && unitOf.contains(e.containerId)) {
            const qsizetype u = unitOf.value(e.containerId);
            units[u].append(i);
            unitOf.insert(e.id, u);
            continue;
        }
        unitOf.insert(e.id, units.size());
        units.append(QList<qsizetype>{i});
    }
    const auto outerGroup = [&d](const QList<qsizetype> &unit) {
        const Stroke &e = d.strokes.at(unit.first());
        return e.groupIds.isEmpty() ? QString() : e.groupIds.last();
    };
    QList<QList<qsizetype>> blocks;
    QString lastGroup;
    for (const QList<qsizetype> &unit : std::as_const(units)) {
        const QString g = outerGroup(unit);
        if (!g.isEmpty() && g == lastGroup && !blocks.isEmpty())
            blocks.last().append(unit);
        else
            blocks.append(unit);
        lastGroup = g;
    }
    // The backdrop: the leading locked blocks (with their shapes' labels) stay at the bottom.
    const QHash<QString, qsizetype> index = indexById(d);
    qsizetype fixed = 0;
    while (fixed < blocks.size()
           && std::all_of(
                   blocks.at(fixed).cbegin(), blocks.at(fixed).cend(),
                   [&](qsizetype i) { return lockedIn(d, index, d.strokes.at(i)); }))
        ++fixed;
    const auto picked = [&](qsizetype i) {
        return ids.contains(d.strokes.at(i).id) && !lockedIn(d, index, d.strokes.at(i));
    };
    QList<bool> selected;
    for (qsizetype b = 0; b < blocks.size(); ++b) {
        const QList<qsizetype> &block = blocks.at(b);
        selected.append(b >= fixed && std::any_of(block.cbegin(), block.cend(), picked));
    }
    QList<qsizetype> order(blocks.size());
    for (qsizetype b = 0; b < blocks.size(); ++b)
        order[b] = b;
    switch (how) {
    case Reorder::ToFront:
    case Reorder::ToBack: {
        QList<qsizetype> rest, chosen;
        for (qsizetype b = fixed; b < blocks.size(); ++b)
            (selected.at(b) ? chosen : rest).append(b);
        order = order.mid(0, fixed);
        order += how == Reorder::ToFront ? rest + chosen : chosen + rest;
        break;
    }
    case Reorder::Forward:
        for (qsizetype k = blocks.size() - 2; k >= fixed; --k) {
            if (selected.at(order.at(k)) && !selected.at(order.at(k + 1)))
                order.swapItemsAt(k, k + 1);
        }
        break;
    case Reorder::Backward:
        for (qsizetype k = fixed + 1; k < blocks.size(); ++k) {
            if (selected.at(order.at(k)) && !selected.at(order.at(k - 1)))
                order.swapItemsAt(k, k - 1);
        }
        break;
    }
    QList<qsizetype> flat;
    flat.reserve(d.strokes.size());
    for (const qsizetype b : std::as_const(order))
        flat += blocks.at(b);
    bool same = true;
    for (qsizetype i = 0; i < flat.size() && same; ++i)
        same = flat.at(i) == i;
    if (same)
        return;
    QList<Stroke> out;
    out.reserve(d.strokes.size());
    for (qsizetype k = 0; k < flat.size(); ++k) {
        Stroke e = d.strokes.at(flat.at(k));
        if (picked(flat.at(k)))
            stamp(e, s);
        out.append(std::move(e));
    }
    d.strokes = std::move(out);
}

Stroke newShape(ElementType type, QPointF from, QPointF to, bool constrain, const ShapeStyle &st, const EditStamp &s)
{
    Stroke e;
    e.type = type;
    e.id = newIdOf(s);
    e.seed = randomOf(s);
    e.versionNonce = randomOf(s);
    e.version = 1;
    e.updated = s.now;
    e.color = st.color;
    e.width = st.width;
    e.roughness = 0;
    from = QPointF(round2(from.x()), round2(from.y()));
    QPointF end = to;
    if (isLinear(type)) {
        if (constrain) {
            const QPointF v = to - from;
            const double length = std::hypot(v.x(), v.y());
            const double step = std::numbers::pi / 12;
            const double angle = std::round(std::atan2(v.y(), v.x()) / step) * step;
            end = from + QPointF(std::cos(angle), std::sin(angle)) * length;
        }
        end = QPointF(round2(end.x()), round2(end.y()));
        e.points = {{from.x(), from.y(), 0.5}, {end.x(), end.y(), 0.5}};
        e.box = pointsRect(e.points);
        e.roundness = 2;
        if (type == ElementType::Arrow)
            e.endArrowhead = Arrowhead::Arrow;
        return e;
    }
    if (constrain) {
        const QPointF v = to - from;
        const double side = std::max(std::abs(v.x()), std::abs(v.y()));
        end = from + QPointF(v.x() < 0 ? -side : side, v.y() < 0 ? -side : side);
    }
    end = QPointF(round2(end.x()), round2(end.y()));
    e.box = QRectF(from, end).normalized();
    e.roundness = type == ElementType::Rectangle && st.rounded ? 3 : 0;
    return e;
}

namespace {

/// The corners stretched to span the unit square.
QList<QPointF> spanUnitSquare(const QList<QPointF> &corners)
{
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (const QPointF &p : corners) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    QList<QPointF> out;
    out.reserve(corners.size());
    for (const QPointF &p : corners)
        out.append(QPointF((p.x() - minX) / (maxX - minX), (p.y() - minY) / (maxY - minY)));
    return out;
}

/// `count` corners on a circle, clockwise from the top; every second one at `inner` of the radius
/// (a star).
QList<QPointF> ringCorners(int count, double inner = 1)
{
    QList<QPointF> out;
    for (int i = 0; i < count; ++i) {
        const double r = i % 2 == 1 ? inner : 1;
        const double a = -std::numbers::pi / 2 + 2 * std::numbers::pi * i / count;
        out.append(QPointF(r * std::cos(a), r * std::sin(a)));
    }
    return spanUnitSquare(out);
}

QList<QPointF> heartCorners()
{
    // The classic parametric heart, enough corners to read as a curve in every program.
    constexpr int count = 48;
    QList<QPointF> out;
    for (int i = 0; i < count; ++i) {
        const double t = 2 * std::numbers::pi * i / count;
        const double s = std::sin(t);
        out.append(QPointF(16 * s * s * s,
                           -(13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t))));
    }
    return spanUnitSquare(out);
}

QList<QPointF> cloudCorners()
{
    // The outline of some circles on a flat base, seen from a point inside all of it: along each
    // ray the farthest point any of them reaches.
    static constexpr double circles[][3] = {
            {0.22, 0.62, 0.18}, {0.42, 0.40, 0.24}, {0.68, 0.46, 0.19}, {0.82, 0.64, 0.16}};
    const QPointF centre(0.5, 0.6);
    const QRectF base(0.22, 0.5, 0.6, 0.3);
    constexpr int count = 96;
    QList<QPointF> out;
    for (int i = 0; i < count; ++i) {
        const double a = 2 * std::numbers::pi * i / count;
        const QPointF dir(std::cos(a), std::sin(a));
        double reach = 0;
        for (const auto &c : circles) {
            const QPointF to = QPointF(c[0], c[1]) - centre;
            const double along = QPointF::dotProduct(to, dir);
            const double disc = along * along - QPointF::dotProduct(to, to) + c[2] * c[2];
            if (disc >= 0)
                reach = std::max(reach, along + std::sqrt(disc));
        }
        // Where the ray leaves the base.
        const double tx = dir.x() > 0   ? (base.right() - centre.x()) / dir.x()
                          : dir.x() < 0 ? (base.left() - centre.x()) / dir.x()
                                        : 1e9;
        const double ty = dir.y() > 0   ? (base.bottom() - centre.y()) / dir.y()
                          : dir.y() < 0 ? (base.top() - centre.y()) / dir.y()
                                        : 1e9;
        reach = std::max(reach, std::min(tx, ty));
        out.append(centre + dir * reach);
    }
    return spanUnitSquare(out);
}

} // namespace

std::optional<PolygonKind> polygonKindFromName(QStringView name)
{
    if (name == u"triangle")
        return PolygonKind::Triangle;
    if (name == u"rightTriangle")
        return PolygonKind::RightTriangle;
    if (name == u"parallelogram")
        return PolygonKind::Parallelogram;
    if (name == u"trapezoid")
        return PolygonKind::Trapezoid;
    if (name == u"pentagon")
        return PolygonKind::Pentagon;
    if (name == u"hexagon")
        return PolygonKind::Hexagon;
    if (name == u"star")
        return PolygonKind::Star;
    if (name == u"blockArrow")
        return PolygonKind::BlockArrow;
    if (name == u"callout")
        return PolygonKind::Callout;
    if (name == u"heart")
        return PolygonKind::Heart;
    if (name == u"cloud")
        return PolygonKind::Cloud;
    return std::nullopt;
}

QList<QPointF> polygonTemplate(PolygonKind kind)
{
    switch (kind) {
    case PolygonKind::Triangle: return {{0.5, 0}, {1, 1}, {0, 1}};
    case PolygonKind::RightTriangle: return {{0, 0}, {1, 1}, {0, 1}};
    case PolygonKind::Parallelogram: return {{0.25, 0}, {1, 0}, {0.75, 1}, {0, 1}};
    case PolygonKind::Trapezoid: return {{0.25, 0}, {0.75, 0}, {1, 1}, {0, 1}};
    case PolygonKind::Pentagon: return ringCorners(5);
    case PolygonKind::Hexagon: return {{0.25, 0}, {0.75, 0}, {1, 0.5}, {0.75, 1}, {0.25, 1}, {0, 0.5}};
    case PolygonKind::Star: return ringCorners(10, 0.382);
    case PolygonKind::BlockArrow: return {{0, 0.3}, {0.6, 0.3}, {0.6, 0}, {1, 0.5}, {0.6, 1}, {0.6, 0.7}, {0, 0.7}};
    case PolygonKind::Callout: return {{0, 0}, {1, 0}, {1, 0.75}, {0.45, 0.75}, {0.25, 1}, {0.25, 0.75}, {0, 0.75}};
    case PolygonKind::Heart: return heartCorners();
    case PolygonKind::Cloud: return cloudCorners();
    }
    return {};
}

Stroke newPolygon(PolygonKind kind, QPointF from, QPointF to, bool constrain, const ShapeStyle &st, const EditStamp &s)
{
    // The box as newShape() drags it; the line's fields as a new line has them.
    Stroke box = newShape(ElementType::Rectangle, from, to, constrain, st, s);
    Stroke e = box;
    e.type = ElementType::Line;
    e.roundness = 0;
    e.polygon = true;
    for (const QPointF &c : polygonTemplate(kind)) {
        e.points.append(
                {round2(box.box.x() + c.x() * box.box.width()), round2(box.box.y() + c.y() * box.box.height()), 0.5});
    }
    e.points.append(e.points.first());
    e.box = pointsRect(e.points);
    return e;
}

QSizeF measureText(const QString &text, int fontFamily, double fontSize, double lineHeight, const TextMetrics &m)
{
    const QStringList lines = QString(text).remove(u'\r').split(u'\n');
    double w = 0;
    for (const QString &line : lines)
        w = std::max(w, lineWidth(line, fontFamily, fontSize, m));
    return QSizeF(round2(w), round2(double(lines.size()) * fontSize * lineHeight));
}

QString wrapText(const QString &text, double maxWidth, int fontFamily, double fontSize, const TextMetrics &m)
{
    if (!(maxWidth > 0))
        return text;
    const auto fits = [&](const QString &line) { return lineWidth(line, fontFamily, fontSize, m) <= maxWidth; };
    QStringList out;
    for (const QString &paragraph : QString(text).remove(u'\r').split(u'\n')) {
        QString line;
        for (const QString &word : paragraph.split(u' ')) {
            const QString candidate = line.isEmpty() ? word : line + u' ' + word;
            if (fits(candidate)) {
                line = candidate;
                continue;
            }
            if (!line.isEmpty())
                out.append(line);
            line.clear();
            if (fits(word)) {
                line = word;
                continue;
            }
            // A word wider than the box: broken at characters (never inside a surrogate pair).
            QString chunk;
            for (qsizetype i = 0; i < word.size(); ++i) {
                QString ch(word.at(i));
                if (word.at(i).isHighSurrogate() && i + 1 < word.size() && word.at(i + 1).isLowSurrogate())
                    ch += word.at(++i);
                if (!chunk.isEmpty() && !fits(chunk + ch)) {
                    out.append(chunk);
                    chunk = ch;
                } else {
                    chunk += ch;
                }
            }
            line = chunk;
        }
        out.append(line);
    }
    return out.join(u'\n');
}

Stroke newText(QPointF topLeft, const QString &text, const TextStyle &st, const TextMetrics &m, const EditStamp &s)
{
    Stroke e;
    e.type = ElementType::Text;
    e.id = newIdOf(s);
    e.seed = randomOf(s);
    e.versionNonce = randomOf(s);
    e.version = 1;
    e.updated = s.now;
    e.color = st.color;
    e.width = 2;
    e.roughness = 0;
    e.fontSize = std::clamp(st.fontSize, 1.0, 1000.0);
    e.fontFamily = st.fontFamily;
    e.lineHeight = NewTextLineHeight;
    e.text = QString(text).remove(u'\r');
    e.originalText = e.text;
    e.textAlign = TextAlign::Left;
    e.verticalAlign = VerticalAlign::Top;
    e.autoResize = true;
    e.box = QRectF(QPointF(round2(topLeft.x()), round2(topLeft.y())),
                   measureText(e.text, e.fontFamily, e.fontSize, e.lineHeight, m));
    return e;
}

QString editableText(const Stroke &e)
{
    return (e.containerId.isEmpty() && e.autoResize) || e.originalText.isEmpty() ? e.text : e.originalText;
}

namespace {

/// JavaScript's Math.round (halves up), as Excalidraw computes.
double jsRound(double v)
{
    return std::floor(v + 0.5);
}

/// The widest (tallest) text a shape holds: Excalidraw's getBoundTextMaxWidth / …MaxHeight of a
/// `dimension` of the shape.
double labelMax(ElementType type, double dimension)
{
    switch (type) {
    case ElementType::Ellipse: return jsRound(dimension / 2 * std::numbers::sqrt2) - 2 * LabelPadding;
    case ElementType::Diamond: return jsRound(dimension / 2) - 2 * LabelPadding;
    default: break;
    }
    return dimension - 2 * LabelPadding;
}

/// The shape's dimension that holds text `dimension` wide (high): Excalidraw's
/// computeContainerDimensionForBoundText.
double containerDimension(ElementType type, double dimension)
{
    dimension = std::ceil(dimension);
    const double padding = 2 * LabelPadding;
    switch (type) {
    case ElementType::Ellipse: return jsRound((dimension + padding) / std::numbers::sqrt2 * 2);
    case ElementType::Diamond: return 2 * (dimension + padding);
    default: break;
    }
    return dimension + padding;
}

/// How much of a polygon's height its label's band takes (labelCenter()).
constexpr double PolygonLabelBand = 0.4;

/// The intervals of the horizontal line at `y` inside a closed polygon (even-odd), left to right.
QList<std::pair<double, double>> chords(const QList<StrokePoint> &points, double y)
{
    QList<double> xs;
    const qsizetype n = points.size();
    for (qsizetype i = 0; i < n; ++i) {
        const StrokePoint &a = points.at(i);
        const StrokePoint &b = points.at((i + 1) % n);
        if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y))
            xs.append(a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y));
    }
    std::ranges::sort(xs);
    QList<std::pair<double, double>> out;
    for (qsizetype i = 0; i + 1 < xs.size(); i += 2)
        out.append({xs.at(i), xs.at(i + 1)});
    return out;
}

/// The room a polygon has for its label, unrotated: the middle of the band and its width and height.
struct LabelRoom {
    QPointF centre;
    double width = 0;
    double height = 0;
};

LabelRoom polygonRoom(const Stroke &e)
{
    const QRectF r = pointsRect(e.points);
    LabelRoom room{r.center(), r.width(), r.height()};
    if (!(r.height() > 0) || !(r.width() > 0))
        return room;
    const double half = PolygonLabelBand / 2 * r.height();
    double best = -1;
    // From the middle outwards: of bands as wide, the one nearest the middle.
    for (int k = 0; k <= 20; ++k) {
        const int step = (k + 1) / 2 * (k % 2 == 1 ? 1 : -1);
        const double y = r.top() + (0.5 + 0.02 * step) * r.height();
        for (const auto &[left, right] : chords(e.points, y)) {
            double lo = left, hi = right;
            // The band's top and bottom: the interval there overlapping this one most.
            for (const double edge : {y - half, y + half}) {
                double bestLo = 0, bestHi = -1;
                for (const auto &[a, b] : chords(e.points, edge)) {
                    const double l = std::max(a, lo), h = std::min(b, hi);
                    if (h - l > bestHi - bestLo) {
                        bestLo = l;
                        bestHi = h;
                    }
                }
                lo = bestLo;
                hi = bestHi;
            }
            if (hi - lo > best + 1e-6 * r.width()) {
                best = hi - lo;
                room.centre = QPointF((lo + hi) / 2, y);
            }
        }
    }
    if (best <= 0)
        return room;
    room.width = best;
    room.height = 2 * half;
    return room;
}

/// The Text element at `i` given the text `typed`: measured, wrapped, placed — its label's shape
/// grown to hold it — and stamped.
void layoutText(Drawing &d, qsizetype i, const QString &typed, const TextMetrics &m, const EditStamp &s)
{
    Stroke e = d.strokes.at(i);
    e.originalText = typed;
    const qsizetype c = indexOfElement(d, e.containerId);
    if (c >= 0 && isLinear(d.strokes.at(c).type) && holdsLabel(d.strokes.at(c))) {
        Stroke &container = d.strokes[c];
        const LabelRoom room = polygonRoom(container);
        const double maxWidth = room.width - 2 * LabelPadding;
        e.text = wrapText(typed, maxWidth, e.fontFamily, e.fontSize, m);
        const QSizeF size = measureText(e.text, e.fontFamily, e.fontSize, e.lineHeight, m);
        // (Grown from its top left, as Excalidraw grows a shape: stretched along the axis it needs.)
        const QRectF r = pointsRect(container.points);
        double sx = 1, sy = 1;
        if (size.height() > room.height - 2 * LabelPadding && room.height > 0)
            sy = (std::ceil(size.height()) + 2 * LabelPadding) / room.height;
        if (size.width() > maxWidth && room.width > 0)
            sx = (std::ceil(size.width()) + 2 * LabelPadding) / room.width;
        if (sx > 1 || sy > 1) {
            for (StrokePoint &p : container.points) {
                p.x = round2(r.left() + (p.x - r.left()) * sx);
                p.y = round2(r.top() + (p.y - r.top()) * sy);
            }
            container.box = pointsRect(container.points);
            stamp(container, s);
        }
        const QPointF centre = labelCenter(container);
        e.box = QRectF(QPointF(round2(centre.x() - size.width() / 2), round2(centre.y() - size.height() / 2)), size);
        e.angle = container.angle;
        e.textAlign = TextAlign::Center;
        e.verticalAlign = VerticalAlign::Middle;
    } else if (c >= 0 && isShape(d.strokes.at(c).type)) {
        Stroke &container = d.strokes[c];
        const double maxWidth = labelMaxWidth(container);
        e.text = wrapText(typed, maxWidth, e.fontFamily, e.fontSize, m);
        const QSizeF size = measureText(e.text, e.fontFamily, e.fontSize, e.lineHeight, m);
        // (Grown as Excalidraw grows it: its top left stays.)
        bool grown = false;
        if (size.height() > labelMax(container.type, container.box.height())) {
            container.box.setHeight(containerDimension(container.type, size.height()));
            grown = true;
        }
        if (size.width() > maxWidth) {
            container.box.setWidth(containerDimension(container.type, size.width()));
            grown = true;
        }
        if (grown)
            stamp(container, s);
        const QPointF centre = container.box.center();
        e.box = QRectF(QPointF(round2(centre.x() - size.width() / 2), round2(centre.y() - size.height() / 2)), size);
        e.angle = container.angle;
        e.textAlign = TextAlign::Center;
        e.verticalAlign = VerticalAlign::Middle;
    } else if (c >= 0 && isLinear(d.strokes.at(c).type)) {
        e.text = typed;
        const QSizeF size = measureText(e.text, e.fontFamily, e.fontSize, e.lineHeight, m);
        const QPointF middle = polylineMiddle(d.strokes.at(c).points);
        e.box = QRectF(QPointF(round2(middle.x() - size.width() / 2), round2(middle.y() - size.height() / 2)), size);
    } else if (e.autoResize) {
        e.text = typed;
        e.box.setSize(measureText(e.text, e.fontFamily, e.fontSize, e.lineHeight, m));
    } else {
        e.text = wrapText(typed, e.box.width(), e.fontFamily, e.fontSize, m);
        e.box.setHeight(measureText(e.text, e.fontFamily, e.fontSize, e.lineHeight, m).height());
    }
    stamp(e, s);
    d.strokes[i] = std::move(e);
}

} // namespace

bool holdsLabel(const Stroke &e)
{
    // (A closed line: its last point is its first, so a triangle has four.)
    return isShape(e.type) || (e.type == ElementType::Line && e.polygon && e.points.size() >= 4);
}

double labelMaxWidth(const Stroke &container)
{
    if (isLinear(container.type))
        return holdsLabel(container) ? polygonRoom(container).width - 2 * LabelPadding : 0;
    return labelMax(container.type, container.box.width());
}

QPointF labelCenter(const Stroke &container)
{
    if (!isLinear(container.type))
        return container.box.center();
    const QPointF at = holdsLabel(container) ? polygonRoom(container).centre : polylineMiddle(container.points);
    return rotatePoint(at, elementCenter(container), container.angle);
}

void setText(Drawing &d, const QString &id, const QString &text, const TextMetrics &m, const EditStamp &s)
{
    const qsizetype i = indexOfElement(d, id);
    if (i < 0 || d.strokes.at(i).type != ElementType::Text || isLocked(d, d.strokes.at(i)))
        return;
    const QString typed = QString(text).remove(u'\r');
    if (typed == QString(editableText(d.strokes.at(i))).remove(u'\r'))
        return; // (unchanged: kept as it is, wrapped as its program wrapped it)
    if (typed.trimmed().isEmpty()) {
        deleteElements(d, {id}, s);
        return;
    }
    layoutText(d, i, typed, m, s);
}

QString addLabel(Drawing &d, const QString &containerId, const QString &text, const TextStyle &st, const TextMetrics &m,
                 const EditStamp &s)
{
    const qsizetype c = indexOfElement(d, containerId);
    if (c < 0 || text.trimmed().isEmpty())
        return {};
    const ElementType type = d.strokes.at(c).type;
    if ((!isShape(type) && !isLinear(type)) || isLocked(d, d.strokes.at(c)))
        return {};
    // (One label a container: the one it has is edited instead.)
    for (const Stroke &t : std::as_const(d.strokes)) {
        if (t.type == ElementType::Text && t.containerId == containerId)
            return {};
    }
    Stroke t = newText(d.strokes.at(c).box.topLeft(), text, st, m, s);
    t.containerId = containerId;
    t.textAlign = TextAlign::Center;
    t.verticalAlign = VerticalAlign::Middle;
    Stroke &container = d.strokes[c];
    container.boundIds.append(t.id);
    stamp(container, s);
    const QString id = t.id;
    const QString typed = t.text;
    d.strokes.insert(c + 1, std::move(t));
    layoutText(d, c + 1, typed, m, s);
    return id;
}

namespace {

/// How a transformation carries one element.
struct Placement {
    /// Where a scene point goes.
    std::function<QPointF(QPointF)> map;
    /// Added to the element's angle.
    double turn = 0;
    /// Mirrored: left and right, top and bottom.
    bool mirrorX = false;
    bool mirrorY = false;
    /// Along the element's own axes.
    double scaleX = 1;
    double scaleY = 1;
    /// A text's size and box (1: only carried along).
    double fontScale = 1;
    /// Shapes' labels are wrapped again.
    bool layoutLabels = false;
};

double normalizedAngle(double angle)
{
    const double full = 2 * std::numbers::pi;
    double a = std::fmod(angle, full);
    if (a < 0)
        a += full;
    return std::abs(a) < 1e-9 || std::abs(a - full) < 1e-9 ? 0 : a;
}

void place(Stroke &e, const Placement &p)
{
    const QPointF centre = elementCenter(e);
    const QPointF to = p.map(centre);
    const bool text = e.type == ElementType::Text;
    const double sx = text ? p.fontScale : (p.mirrorX ? -p.scaleX : p.scaleX);
    const double sy = text ? p.fontScale : (p.mirrorY ? -p.scaleY : p.scaleY);
    if (hasPoints(e.type)) {
        for (StrokePoint &point : e.points) {
            point.x = round2(to.x() + (point.x - centre.x()) * sx);
            point.y = round2(to.y() + (point.y - centre.y()) * sy);
        }
        e.box = pointsRect(e.points);
    } else {
        const double w = round2(e.box.width() * std::abs(sx));
        const double h = round2(e.box.height() * std::abs(sy));
        e.box = QRectF(round2(to.x() - w / 2), round2(to.y() - h / 2), w, h);
    }
    if (text)
        e.fontSize = std::clamp(round2(e.fontSize * p.fontScale), 1.0, 1000.0);
    if (e.type == ElementType::Image) {
        e.flipX = e.flipX != p.mirrorX;
        e.flipY = e.flipY != p.mirrorY;
    }
    if (e.elbowed)
        return;
    e.angle = normalizedAngle(p.mirrorX != p.mirrorY ? -e.angle : e.angle + p.turn);
}

bool withinLimit(const Stroke &e)
{
    const QRectF r = hasPoints(e.type) ? pointsRect(e.points).adjusted(0, 0, 1e-9, 1e-9) : e.box;
    const double L = CoordinateLimit;
    return std::isfinite(r.left()) && std::isfinite(r.top()) && std::isfinite(r.right()) && std::isfinite(r.bottom())
           && r.left() >= -L && r.right() <= L && r.top() >= -L && r.bottom() <= L;
}

/// The elements `ids` placed by `p`, their labels and the ends of arrows bound to them with them
/// (see resizeElements(), rotateElements(), flipElements()).
void transformElements(Drawing &d, const QSet<QString> &ids, const Placement &p, const TextMetrics &m,
                       const EditStamp &s)
{
    const QHash<QString, qsizetype> index = indexById(d);
    QSet<QString> moved;
    for (const Stroke &e : std::as_const(d.strokes)) {
        if (ids.contains(e.id) && !lockedIn(d, index, e))
            moved.insert(e.id);
    }
    for (const Stroke &e : std::as_const(d.strokes)) {
        if (e.type == ElementType::Text && !e.locked && moved.contains(e.containerId))
            moved.insert(e.id);
    }
    if (moved.isEmpty())
        return;
    const QList<Stroke> before = d.strokes;
    const auto labelOfMoved = [&moved](const Stroke &e) {
        return e.type == ElementType::Text && moved.contains(e.containerId);
    };
    // The elements first: labels follow where their shapes and arrows went.
    for (Stroke &e : d.strokes) {
        if (moved.contains(e.id) && !labelOfMoved(e)) {
            place(e, p);
            stamp(e, s);
        }
    }
    for (qsizetype i = 0; i < d.strokes.size(); ++i) {
        if (!moved.contains(d.strokes.at(i).id) || !labelOfMoved(d.strokes.at(i)))
            continue;
        const qsizetype c = index.value(d.strokes.at(i).containerId, -1);
        if (c < 0)
            continue;
        const Stroke &container = d.strokes.at(c);
        if (isLinear(container.type) && !holdsLabel(container)) {
            // Level, at the arrow's middle as the arrow now lies.
            Stroke &label = d.strokes[i];
            const QPointF middle =
                    rotatePoint(polylineMiddle(container.points), elementCenter(container), container.angle);
            const double w = round2(label.box.width() * p.fontScale);
            const double h = round2(label.box.height() * p.fontScale);
            label.fontSize = std::clamp(round2(label.fontSize * p.fontScale), 1.0, 1000.0);
            label.box = QRectF(round2(middle.x() - w / 2), round2(middle.y() - h / 2), w, h);
            stamp(label, s);
        } else if (p.layoutLabels) {
            Stroke &label = d.strokes[i];
            label.fontSize = std::clamp(round2(label.fontSize * p.fontScale), 1.0, 1000.0);
            const QString typed = editableText(label);
            layoutText(d, i, typed, m, s);
        } else {
            place(d.strokes[i], p);
            stamp(d.strokes[i], s);
        }
    }
    // Arrows that stay, bound to what moved: their bound ends follow.
    QHash<QString, QPointF> labelShift;
    for (Stroke &e : d.strokes) {
        if (moved.contains(e.id) || !isLinear(e.type) || e.locked || e.points.size() < 2)
            continue;
        const bool start = !e.startBound.isEmpty() && moved.contains(e.startBound);
        const bool end = !e.endBound.isEmpty() && moved.contains(e.endBound);
        if (!start && !end)
            continue;
        const QPointF middle = polylineMiddle(e.points);
        if (start && end && !e.elbowed) {
            for (StrokePoint &point : e.points) {
                const QPointF to = p.map(QPointF(point.x, point.y));
                point.x = round2(to.x());
                point.y = round2(to.y());
            }
            e.box = pointsRect(e.points);
        } else if (start && end) {
            translate(e, p.map(elementCenter(e)) - elementCenter(e));
        } else {
            const qsizetype n = e.points.size();
            const qsizetype k = start ? 0 : n - 1;
            const QPointF tip(e.points.at(k).x, e.points.at(k).y);
            moveEnd(e, k, start ? 1 : n - 2, p.map(tip) - tip);
        }
        stamp(e, s);
        labelShift.insert(e.id, polylineMiddle(e.points) - middle);
    }
    for (Stroke &e : d.strokes) {
        if (e.type == ElementType::Text && !moved.contains(e.id) && labelShift.contains(e.containerId)) {
            translate(e, labelShift.value(e.containerId));
            stamp(e, s);
        }
    }
    for (const Stroke &e : std::as_const(d.strokes)) {
        if (!withinLimit(e)) {
            d.strokes = before; // (nothing leaves the limit: the edit is not made)
            return;
        }
    }
}

} // namespace

QRectF resizedFrame(const QRectF &from, Handle handle, QPointF delta, bool proportional, double minSize)
{
    const bool west = handle == Handle::TopLeft || handle == Handle::Left || handle == Handle::BottomLeft;
    const bool east = handle == Handle::TopRight || handle == Handle::Right || handle == Handle::BottomRight;
    const bool north = handle == Handle::TopLeft || handle == Handle::Top || handle == Handle::TopRight;
    const bool south = handle == Handle::BottomLeft || handle == Handle::Bottom || handle == Handle::BottomRight;
    const double w0 = from.width(), h0 = from.height();
    double w = w0, h = h0;
    if (w0 > 0 && (west || east))
        w = std::max(minSize, west ? w0 - delta.x() : w0 + delta.x());
    if (h0 > 0 && (north || south))
        h = std::max(minSize, north ? h0 - delta.y() : h0 + delta.y());
    if (proportional) {
        double scale = 1;
        const bool horizontal = (west || east) && w0 > 0;
        const bool vertical = (north || south) && h0 > 0;
        if (horizontal && vertical)
            scale = std::max(w / w0, h / h0);
        else if (horizontal)
            scale = w / w0;
        else if (vertical)
            scale = h / h0;
        // Not below minSize on the shorter side that has a length.
        const double shorter = w0 > 0 && h0 > 0 ? std::min(w0, h0) : std::max(w0, h0);
        if (shorter > 0)
            scale = std::max(scale, minSize / shorter);
        w = w0 * scale;
        h = h0 * scale;
    }
    const double left = west ? from.right() - w : east ? from.left() : from.center().x() - w / 2;
    const double top = north ? from.bottom() - h : south ? from.top() : from.center().y() - h / 2;
    return QRectF(left, top, w, h);
}

void resizeElements(Drawing &d, const QSet<QString> &ids, const QRectF &from, const QRectF &to, double angle,
                    const TextMetrics &m, const EditStamp &s)
{
    const auto finite = [](const QRectF &r) {
        return std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width()) && std::isfinite(r.height());
    };
    if (!finite(from) || !finite(to) || !std::isfinite(angle) || from.width() < 0 || from.height() < 0 || to.width() < 0
        || to.height() < 0 || from == to)
        return;
    const double sx = from.width() > 0 ? to.width() / from.width() : 1;
    const double sy = from.height() > 0 ? to.height() / from.height() : 1;
    if (!(sx > 0) || !(sy > 0))
        return;
    const QPointF pivot = from.center();
    Placement p;
    p.map = [=](QPointF point) {
        const QPointF local = rotatePoint(point, pivot, -angle);
        const QPointF scaled(to.left() + (local.x() - from.left()) * sx, to.top() + (local.y() - from.top()) * sy);
        return rotatePoint(scaled, pivot, angle);
    };
    p.scaleX = sx;
    p.scaleY = sy;
    p.fontScale = std::abs(sx - sy) < 1e-6 ? sx : 1;
    p.layoutLabels = true;
    transformElements(d, ids, p, m, s);
}

void rotateElements(Drawing &d, const QSet<QString> &ids, QPointF pivot, double angle, const EditStamp &s)
{
    if (!std::isfinite(angle) || !std::isfinite(pivot.x()) || !std::isfinite(pivot.y()) || normalizedAngle(angle) == 0)
        return;
    Placement p;
    p.map = [=](QPointF point) { return rotatePoint(point, pivot, angle); };
    p.turn = angle;
    transformElements(d, ids, p, TextMetrics{}, s);
}

void flipElements(Drawing &d, const QSet<QString> &ids, Flip flip, QPointF pivot, const EditStamp &s)
{
    if (!std::isfinite(pivot.x()) || !std::isfinite(pivot.y()))
        return;
    const bool horizontal = flip == Flip::Horizontal;
    Placement p;
    p.map = [=](QPointF point) {
        return horizontal ? QPointF(2 * pivot.x() - point.x(), point.y())
                          : QPointF(point.x(), 2 * pivot.y() - point.y());
    };
    p.mirrorX = horizontal;
    p.mirrorY = !horizontal;
    transformElements(d, ids, p, TextMetrics{}, s);
}

bool hasFill(const Stroke &e)
{
    return isShape(e.type) || (e.type == ElementType::Line && e.polygon);
}

bool hasThickness(const Stroke &e)
{
    return e.type != ElementType::Text && e.type != ElementType::Image;
}

bool hasDashes(const Stroke &e)
{
    return isShape(e.type) || isLinear(e.type);
}

bool canRound(const Stroke &e)
{
    return e.type == ElementType::Rectangle || e.type == ElementType::Diamond || (isLinear(e.type) && !e.elbowed);
}

bool hasHeads(const Stroke &e)
{
    return isLinear(e.type) && !e.polygon;
}

double thicknessWidth(const Stroke &e, int thickness)
{
    const int i = std::clamp(thickness, 0, 2);
    if (e.type == ElementType::Freedraw && e.tool == Tool::Highlighter)
        return std::array{3.0, 5.0, 8.0}.at(size_t(i));
    if (e.type == ElementType::Freedraw)
        return std::array{0.5, 1.0, 2.0}.at(size_t(i));
    return std::array{1.0, 2.0, 4.0}.at(size_t(i));
}

int thicknessOf(const Stroke &e)
{
    int best = 0;
    for (int i = 1; i < 3; ++i) {
        if (std::abs(thicknessWidth(e, i) - e.width) < std::abs(thicknessWidth(e, best) - e.width))
            best = i;
    }
    return best;
}

void styleElements(Drawing &d, const QSet<QString> &ids, const ElementStyle &style, const TextMetrics &m,
                   const EditStamp &s)
{
    const QHash<QString, qsizetype> index = indexById(d);
    for (qsizetype i = 0; i < d.strokes.size(); ++i) {
        if (!ids.contains(d.strokes.at(i).id) || lockedIn(d, index, d.strokes.at(i)))
            continue;
        Stroke e = d.strokes.at(i);
        bool changed = false;
        const auto set = [&changed](auto &field, const auto &value) {
            if (field != value) {
                field = value;
                changed = true;
            }
        };
        if (hasFill(e)) {
            if (style.fill)
                set(e.fill, *style.fill);
            if (style.fillStyle)
                set(e.fillStyle, *style.fillStyle);
        }
        if (hasThickness(e) && style.thickness)
            set(e.width, thicknessWidth(e, *style.thickness));
        if (hasDashes(e) && style.strokeStyle)
            set(e.strokeStyle, *style.strokeStyle);
        if (style.opacity)
            set(e.opacity, std::clamp(*style.opacity, 0, 100));
        if (canRound(e) && style.rounded) {
            // Corners as Excalidraw rounds what it draws: rectangles adaptive, the others proportional.
            set(e.roundness, !*style.rounded ? 0 : e.type == ElementType::Rectangle ? 3 : 2);
        }
        if (hasHeads(e)) {
            if (style.startArrowhead)
                set(e.startArrowhead, *style.startArrowhead);
            if (style.endArrowhead)
                set(e.endArrowhead, *style.endArrowhead);
        }
        const bool newSize =
                e.type == ElementType::Text && style.fontSize && std::abs(e.fontSize - *style.fontSize) > 1e-6;
        if (newSize) {
            e.fontSize = std::clamp(*style.fontSize, 1.0, 1000.0);
            changed = true;
        }
        if (!changed)
            continue;
        const QString typed = editableText(e);
        d.strokes[i] = std::move(e);
        if (newSize)
            layoutText(d, i, typed, m, s);
        else
            stamp(d.strokes[i], s);
    }
}

bool canGroup(const Drawing &d, const QSet<QString> &ids)
{
    int units = 0;
    bool oneGroup = true;
    QString group;
    bool first = true;
    for (const Stroke &e : d.strokes) {
        if (!ids.contains(e.id) || (e.type == ElementType::Text && ids.contains(e.containerId)))
            continue;
        ++units;
        const QString outer = e.groupIds.isEmpty() ? QString() : e.groupIds.last();
        if (first)
            group = outer;
        else if (outer != group)
            oneGroup = false;
        first = false;
    }
    return units >= 2 && (!oneGroup || group.isEmpty());
}

bool canUngroup(const Drawing &d, const QSet<QString> &ids)
{
    return std::any_of(d.strokes.cbegin(), d.strokes.cend(),
                       [&ids](const Stroke &e) { return ids.contains(e.id) && !e.groupIds.isEmpty(); });
}

QString groupElements(Drawing &d, const QSet<QString> &ids, const EditStamp &s)
{
    if (!canGroup(d, ids))
        return {};
    const QHash<QString, qsizetype> index = indexById(d);
    const QString group = newIdOf(s);
    for (qsizetype i = 0; i < d.strokes.size(); ++i) {
        if (!ids.contains(d.strokes.at(i).id) || lockedIn(d, index, d.strokes.at(i)))
            continue;
        d.strokes[i].groupIds.append(group);
        stamp(d.strokes[i], s);
    }
    return group;
}

void ungroupElements(Drawing &d, const QSet<QString> &ids, const EditStamp &s)
{
    const QHash<QString, qsizetype> index = indexById(d);
    for (qsizetype i = 0; i < d.strokes.size(); ++i) {
        const Stroke &e = d.strokes.at(i);
        if (!ids.contains(e.id) || e.groupIds.isEmpty() || lockedIn(d, index, e))
            continue;
        d.strokes[i].groupIds.removeLast();
        stamp(d.strokes[i], s);
    }
}

namespace {

/// A part of a selection that align and distribute move as a whole, and the box it covers.
struct ArrangeUnit {
    QSet<QString> ids;
    QRectF box;
};

QList<ArrangeUnit> arrangeUnitsOf(const Drawing &d, const QSet<QString> &ids)
{
    const QHash<QString, qsizetype> index = indexById(d);
    QHash<QString, qsizetype> unitOf;
    QList<ArrangeUnit> units;
    for (const Stroke &e : d.strokes) {
        if (e.id.isEmpty() || !ids.contains(e.id) || lockedIn(d, index, e))
            continue;
        // (A label goes with its shape or arrow, a member with its outermost group.)
        const Stroke *owner = &e;
        if (e.type == ElementType::Text && !e.containerId.isEmpty()) {
            const qsizetype c = index.value(e.containerId, -1);
            if (c >= 0)
                owner = &d.strokes.at(c);
        }
        const QString key = owner->groupIds.isEmpty() ? u"e:"_s + owner->id : u"g:"_s + owner->groupIds.last();
        qsizetype u = unitOf.value(key, -1);
        if (u < 0) {
            u = units.size();
            unitOf.insert(key, u);
            units.append(ArrangeUnit{});
        }
        units[u].ids.insert(e.id);
        const QRectF b = elementBounds(e);
        if (!b.isNull())
            units[u].box = units.at(u).box.isNull() ? b : units.at(u).box.united(b);
    }
    units.removeIf([](const ArrangeUnit &u) { return u.box.isNull(); });
    return units;
}

/// Every part moved by its own delta, computed before any moved (rounded as positions are: a part
/// already in place is not stamped).
void moveUnits(Drawing &d, const QList<ArrangeUnit> &units, const QList<QPointF> &deltas, const EditStamp &s)
{
    for (qsizetype k = 0; k < units.size(); ++k) {
        const QPointF delta(round2(deltas.at(k).x()), round2(deltas.at(k).y()));
        if (delta.x() != 0 || delta.y() != 0)
            moveElements(d, units.at(k).ids, delta, s);
    }
}

} // namespace

int arrangeUnits(const Drawing &d, const QSet<QString> &ids)
{
    return int(arrangeUnitsOf(d, ids).size());
}

void alignElements(Drawing &d, const QSet<QString> &ids, Align how, const EditStamp &s)
{
    const QList<ArrangeUnit> units = arrangeUnitsOf(d, ids);
    if (units.size() < 2)
        return;
    QRectF all;
    for (const ArrangeUnit &u : units)
        all = all.isNull() ? u.box : all.united(u.box);
    QList<QPointF> deltas;
    for (const ArrangeUnit &u : units) {
        const QRectF &b = u.box;
        switch (how) {
        case Align::Left: deltas.append({all.left() - b.left(), 0}); break;
        case Align::Center: deltas.append({all.center().x() - b.center().x(), 0}); break;
        case Align::Right: deltas.append({all.right() - b.right(), 0}); break;
        case Align::Top: deltas.append({0, all.top() - b.top()}); break;
        case Align::Middle: deltas.append({0, all.center().y() - b.center().y()}); break;
        case Align::Bottom: deltas.append({0, all.bottom() - b.bottom()}); break;
        }
    }
    moveUnits(d, units, deltas, s);
}

void distributeElements(Drawing &d, const QSet<QString> &ids, Distribute axis, const EditStamp &s)
{
    QList<ArrangeUnit> units = arrangeUnitsOf(d, ids);
    if (units.size() < 3)
        return;
    const bool x = axis == Distribute::Horizontal;
    const auto start = [x](const QRectF &r) { return x ? r.left() : r.top(); };
    const auto end = [x](const QRectF &r) { return x ? r.right() : r.bottom(); };
    const auto mid = [x](const QRectF &r) { return x ? r.center().x() : r.center().y(); };
    const auto extent = [x](const QRectF &r) { return x ? r.width() : r.height(); };
    std::ranges::stable_sort(units,
                             [&mid](const ArrangeUnit &a, const ArrangeUnit &b) { return mid(a.box) < mid(b.box); });
    QRectF all;
    double span = 0;
    for (const ArrangeUnit &u : units) {
        all = all.isNull() ? u.box : all.united(u.box);
        span += extent(u.box);
    }
    const qsizetype n = units.size();
    const auto shift = [x](double by) { return x ? QPointF(by, 0) : QPointF(0, by); };
    QList<QPointF> deltas(n);
    const double gap = (extent(all) - span) / double(n - 1);
    if (gap >= 0) {
        // Equal gaps between the boxes, from the first one's start.
        double pos = start(all);
        for (qsizetype k = 0; k < n; ++k) {
            deltas[k] = shift(pos - start(units.at(k).box));
            pos += extent(units.at(k).box) + gap;
        }
    } else {
        // Overlapping: equal steps between the middles of the parts at either end, which stay.
        qsizetype first = 0, last = n - 1;
        for (qsizetype k = 0; k < n; ++k) {
            if (start(units.at(k).box) == start(all)) {
                first = k;
                break;
            }
        }
        for (qsizetype k = 0; k < n; ++k) {
            if (end(units.at(k).box) == end(all)) {
                last = k;
                break;
            }
        }
        const double step = (mid(units.at(last).box) - mid(units.at(first).box)) / double(n - 1);
        double pos = mid(units.at(first).box);
        for (qsizetype k = 0; k < n; ++k) {
            if (k == first || k == last)
                continue;
            pos += step;
            deltas[k] = shift(pos - mid(units.at(k).box));
        }
    }
    moveUnits(d, units, deltas, s);
}

namespace {

/// The keys of the elements (mergeKey); false when one is there twice (a broken file).
bool keysOf(const QList<Stroke> &elements, QStringList &keys, QHash<QString, qsizetype> &index)
{
    keys.clear();
    index.clear();
    keys.reserve(elements.size());
    index.reserve(elements.size());
    bool unique = true;
    for (qsizetype i = 0; i < elements.size(); ++i) {
        const QString k = mergeKey(elements.at(i));
        if (index.contains(k))
            unique = false;
        index.insert(k, i);
        keys.append(k);
    }
    return unique;
}

} // namespace

ElementDiff diffElements(const QList<Stroke> &before, const QList<Stroke> &after)
{
    ElementDiff diff;
    QStringList kb, ka;
    QHash<QString, qsizetype> hb, ha;
    const bool uniqueBefore = keysOf(before, kb, hb);
    const bool uniqueAfter = keysOf(after, ka, ha);
    const bool unique = uniqueBefore && uniqueAfter;
    if (!unique) {
        // Elements without unique keys cannot be matched: everything replaced.
        for (qsizetype i = 0; i < before.size(); ++i)
            diff.removed.append({i, before.at(i)});
        for (qsizetype i = 0; i < after.size(); ++i)
            diff.added.append({i, after.at(i)});
        return diff;
    }
    QStringList commonBefore, commonAfter;
    for (qsizetype i = 0; i < before.size(); ++i) {
        const qsizetype j = ha.value(kb.at(i), -1);
        if (j < 0) {
            diff.removed.append({i, before.at(i)});
            continue;
        }
        commonBefore.append(kb.at(i));
        const Stroke &a = before.at(i);
        const Stroke &b = after.at(j);
        if (a.version != b.version || a.versionNonce != b.versionNonce || a.updated != b.updated)
            diff.changed.append({a, b});
    }
    for (qsizetype j = 0; j < after.size(); ++j) {
        if (!hb.contains(ka.at(j)))
            diff.added.append({j, after.at(j)});
        else
            commonAfter.append(ka.at(j));
    }
    if (commonBefore != commonAfter) {
        diff.orderBefore = commonBefore;
        diff.orderAfter = commonAfter;
    }
    return diff;
}

void applyDiff(QList<Stroke> &elements, const ElementDiff &diff, bool forward, const EditStamp *restamp)
{
    // (A version above `replaced` and above the element's own + 1 — see edit.h.)
    const auto stampBack = [restamp](Stroke &e, int replaced) {
        if (!restamp)
            return;
        const int own = e.version < INT_MAX - 1 ? e.version + 1 : 1;
        const int above = std::max(replaced, own);
        e.version = above < INT_MAX ? above + 1 : 2;
        e.versionNonce = randomOf(*restamp);
        e.updated = restamp->now;
    };
    const auto &drop = forward ? diff.removed : diff.added;
    const auto &insert = forward ? diff.added : diff.removed;
    QList<qsizetype> dropIndices;
    for (const auto &[i, e] : drop)
        dropIndices.append(i);
    std::ranges::sort(dropIndices);
    for (auto it = dropIndices.crbegin(); it != dropIndices.crend(); ++it) {
        if (*it >= 0 && *it < elements.size())
            elements.removeAt(*it);
    }
    if (!diff.changed.isEmpty()) {
        QHash<QString, qsizetype> index;
        for (qsizetype i = 0; i < elements.size(); ++i)
            index.insert(mergeKey(elements.at(i)), i);
        for (const auto &[a, b] : diff.changed) {
            const qsizetype i = index.value(mergeKey(forward ? a : b), -1);
            if (i < 0)
                continue;
            const int replaced = elements.at(i).version;
            elements[i] = forward ? b : a;
            stampBack(elements[i], replaced);
        }
    }
    const QStringList &order = forward ? diff.orderAfter : diff.orderBefore;
    if (!order.isEmpty()) {
        QHash<QString, qsizetype> index;
        for (qsizetype i = 0; i < elements.size(); ++i)
            index.insert(mergeKey(elements.at(i)), i);
        QList<Stroke> sorted;
        sorted.reserve(elements.size());
        QList<bool> used(elements.size(), false);
        for (const QString &k : order) {
            const qsizetype i = index.value(k, -1);
            if (i >= 0 && !used.at(i)) {
                sorted.append(elements.at(i));
                used[i] = true;
            }
        }
        for (qsizetype i = 0; i < elements.size(); ++i) {
            if (!used.at(i))
                sorted.append(elements.at(i));
        }
        elements = std::move(sorted);
    }
    QList<std::pair<qsizetype, Stroke>> adds = insert;
    std::ranges::stable_sort(adds, [](const auto &x, const auto &y) { return x.first < y.first; });
    for (auto &[i, e] : adds) {
        stampBack(e, 0);
        elements.insert(std::clamp<qsizetype>(i, 0, elements.size()), e);
    }
}

} // namespace brulion::drawing
