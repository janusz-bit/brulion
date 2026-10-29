#include "drawing/edit.h"

#include "drawing/geometry.h"
#include "drawing/merge.h"

#include <QHash>

#include <algorithm>
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
            const qsizetype j = start ? 1 : n - 2;
            if (e.elbowed && n >= 3) {
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

/// The Text element at `i` given the text `typed`: measured, wrapped, placed — its label's shape
/// grown to hold it — and stamped.
void layoutText(Drawing &d, qsizetype i, const QString &typed, const TextMetrics &m, const EditStamp &s)
{
    Stroke e = d.strokes.at(i);
    e.originalText = typed;
    const qsizetype c = indexOfElement(d, e.containerId);
    if (c >= 0 && isShape(d.strokes.at(c).type)) {
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

double labelMaxWidth(const Stroke &container)
{
    return labelMax(container.type, container.box.width());
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

QString addLabel(Drawing &d, const QString &containerId, const QString &text, const TextStyle &st,
                 const TextMetrics &m, const EditStamp &s)
{
    const qsizetype c = indexOfElement(d, containerId);
    if (c < 0 || text.trimmed().isEmpty())
        return {};
    const ElementType type = d.strokes.at(c).type;
    if ((!isShape(type) && !isLinear(type)) || isLocked(d, d.strokes.at(c)))
        return {};
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

void applyDiff(QList<Stroke> &elements, const ElementDiff &diff, bool forward)
{
    const auto &drop = forward ? diff.removed : diff.added;
    const auto &insert = forward ? diff.added : diff.removed;
    QList<qsizetype> dropIndices;
    for (const auto &[i, e] : drop)
        dropIndices.append(i);
    std::sort(dropIndices.begin(), dropIndices.end());
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
            if (i >= 0)
                elements[i] = forward ? b : a;
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
    std::stable_sort(adds.begin(), adds.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
    for (const auto &[i, e] : adds)
        elements.insert(std::clamp<qsizetype>(i, 0, elements.size()), e);
}

} // namespace brulion::drawing
