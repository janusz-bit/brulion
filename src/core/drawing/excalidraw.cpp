#include "drawing/excalidraw.h"

#include "drawing/stroke.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>

#include <miniz.h>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

namespace {

constexpr QByteArrayView PayloadType = "payload-type:application/vnd.excalidraw+json";
constexpr QByteArrayView PayloadVersion = "<!-- payload-version:";
constexpr QByteArrayView PayloadStart = "<!-- payload-start -->";
constexpr QByteArrayView PayloadEnd = "<!-- payload-end -->";

bool finite(const QJsonValue &v)
{
    return v.isDouble() && std::isfinite(v.toDouble());
}

/// JSON.stringify's escaping of a string whose characters are bytes (a "byte string").
void appendByteStringJson(QByteArray &out, QByteArrayView bytes)
{
    static constexpr char hex[] = "0123456789abcdef";
    out.reserve(out.size() + bytes.size() + bytes.size() / 8 + 2);
    out += '"';
    for (const char c : bytes) {
        const auto b = uchar(c);
        switch (b) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (b < 0x20) {
                out += "\\u00";
                out += hex[b >> 4];
                out += hex[b & 0xf];
            } else {
                out += char(b); // Latin-1: one byte per character, as btoa() reads it
            }
        }
    }
    out += '"';
}

/// The scene JSON of a payload's JSON (the wrapper, or a scene of old exports).
std::optional<QByteArray> unwrapPayload(const QByteArray &json)
{
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    const QJsonObject o = doc.object();
    if (!o.contains(u"encoded"_s)) {
        // Legacy: the scene JSON itself.
        if (o.value(u"type"_s).toString() == u"excalidraw")
            return json;
        return std::nullopt;
    }
    if (o.value(u"encoding"_s).toString() != u"bstring")
        return std::nullopt;
    const QString encoded = o.value(u"encoded"_s).toString();
    for (const QChar c : encoded) {
        if (c.unicode() > 0xff)
            return std::nullopt;
    }
    const QByteArray bytes = encoded.toLatin1();
    if (o.value(u"compressed"_s).toBool())
        return inflate(bytes);
    return bytes;
}

} // namespace

namespace {

double round2(double v)
{
    return std::round(v * 100) / 100;
}

bool inReach(double v)
{
    return std::abs(v) <= CoordinateLimit;
}

/// A number of the element, or `fallback` when it is missing or no finite number.
double number(const QJsonObject &e, QStringView key, double fallback)
{
    const QJsonValue v = e.value(key);
    return finite(v) ? v.toDouble() : fallback;
}

QString stringOr(const QJsonObject &e, QStringView key, const QString &fallback)
{
    const QJsonValue v = e.value(key);
    return v.isString() ? v.toString() : fallback;
}

FillStyle fillStyleFrom(QStringView name)
{
    if (name == u"hachure")
        return FillStyle::Hachure;
    if (name == u"cross-hatch")
        return FillStyle::CrossHatch;
    if (name == u"zigzag")
        return FillStyle::Zigzag;
    return FillStyle::Solid;
}

StrokeStyle strokeStyleFrom(QStringView name)
{
    if (name == u"dashed")
        return StrokeStyle::Dashed;
    if (name == u"dotted")
        return StrokeStyle::Dotted;
    return StrokeStyle::Solid;
}

Arrowhead arrowheadFrom(const QJsonValue &v)
{
    if (v.isNull() || v.isUndefined())
        return Arrowhead::None;
    const QString n = v.toString();
    if (n == u"arrow")
        return Arrowhead::Arrow;
    if (n == u"triangle")
        return Arrowhead::Triangle;
    if (n == u"triangle_outline")
        return Arrowhead::TriangleOutline;
    if (n == u"bar")
        return Arrowhead::Bar;
    if (n == u"dot")
        return Arrowhead::Dot;
    if (n == u"circle")
        return Arrowhead::Circle;
    if (n == u"circle_outline")
        return Arrowhead::CircleOutline;
    if (n == u"diamond")
        return Arrowhead::Diamond;
    if (n == u"diamond_outline")
        return Arrowhead::DiamondOutline;
    return Arrowhead::Other;
}

TextAlign textAlignFrom(QStringView name)
{
    if (name == u"center")
        return TextAlign::Center;
    if (name == u"right")
        return TextAlign::Right;
    return TextAlign::Left;
}

VerticalAlign verticalAlignFrom(QStringView name)
{
    if (name == u"middle")
        return VerticalAlign::Middle;
    if (name == u"bottom")
        return VerticalAlign::Bottom;
    return VerticalAlign::Top;
}

QStringList stringList(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &item : v.toArray()) {
        if (item.isString())
            out.append(item.toString());
    }
    return out;
}

/// The points of a freedraw, line or arrow element in scene coordinates (nullopt: broken or far
/// away).
std::optional<QList<StrokePoint>> pointsFrom(const QJsonObject &e, double x, double y, const QJsonArray &pressures,
                                             bool realPressure)
{
    const QJsonArray points = e.value(u"points"_s).toArray();
    QList<StrokePoint> out;
    out.reserve(points.size());
    for (qsizetype i = 0; i < points.size(); ++i) {
        const QJsonArray p = points.at(i).toArray();
        if (p.size() < 2 || !finite(p.at(0)) || !finite(p.at(1)))
            return std::nullopt;
        double pressure = 0.5;
        if (realPressure) {
            if (!finite(pressures.at(i)))
                return std::nullopt;
            pressure = std::clamp(pressures.at(i).toDouble(), 0.0, 1.0);
        }
        const StrokePoint point{x + p.at(0).toDouble(), y + p.at(1).toDouble(), pressure};
        if (!inReach(point.x) || !inReach(point.y))
            return std::nullopt; // far away (CoordinateLimit): kept as foreign
        out.append(point);
    }
    return out;
}

} // namespace

std::optional<Stroke> elementFromJson(const QJsonObject &e)
{
    const std::optional<ElementType> type = elementTypeFromName(e.value(u"type"_s).toString());
    if (!type)
        return std::nullopt;
    if (!finite(e.value(u"x"_s)) || !finite(e.value(u"y"_s)))
        return std::nullopt;
    if (e.contains(u"angle"_s) && !e.value(u"angle"_s).isNull() && !finite(e.value(u"angle"_s)))
        return std::nullopt;
    Stroke s;
    s.type = *type;
    s.id = e.value(u"id"_s).toString();
    const double x = e.value(u"x"_s).toDouble();
    const double y = e.value(u"y"_s).toDouble();
    double w = number(e, u"width", 0);
    double h = number(e, u"height", 0);
    double left = x, top = y;
    // A negative size (hand-made scenes): the box is the same, from its other corner.
    if (w < 0) {
        left += w;
        w = -w;
    }
    if (h < 0) {
        top += h;
        h = -h;
    }
    if (!inReach(left) || !inReach(top) || !inReach(left + w) || !inReach(top + h))
        return std::nullopt;
    s.box = QRectF(left, top, w, h);
    s.angle = number(e, u"angle", 0);
    s.color = stringOr(e, u"strokeColor", u"#1e1e1e"_s);
    s.fill = stringOr(e, u"backgroundColor", u"transparent"_s);
    s.fillStyle = fillStyleFrom(e.value(u"fillStyle"_s).toString());
    s.width = finite(e.value(u"strokeWidth"_s)) ? std::clamp(e.value(u"strokeWidth"_s).toDouble(), 0.05, 200.0) : 1;
    s.strokeStyle = strokeStyleFrom(e.value(u"strokeStyle"_s).toString());
    s.roughness = int(std::clamp(number(e, u"roughness", 1), 0.0, 10.0));
    s.opacity = finite(e.value(u"opacity"_s)) ? std::clamp(e.value(u"opacity"_s).toInt(), 0, 100) : 100;
    if (const QJsonValue r = e.value(u"roundness"_s); r.isObject())
        s.roundness = std::clamp(r.toObject().value(u"type"_s).toInt(3), 1, 3);
    s.locked = e.value(u"locked"_s).toBool();
    s.groupIds = stringList(e.value(u"groupIds"_s));
    for (const QJsonValue &b : e.value(u"boundElements"_s).toArray()) {
        const QString id = b.toObject().value(u"id"_s).toString();
        if (!id.isEmpty())
            s.boundIds.append(id);
    }
    switch (s.type) {
    case ElementType::Freedraw: {
        if (e.value(u"points"_s).toArray().isEmpty())
            return std::nullopt;
        s.simulatePressure = e.value(u"simulatePressure"_s).toBool(true);
        const QJsonArray pressures = e.value(u"pressures"_s).toArray();
        const bool realPressure = !s.simulatePressure && pressures.size() == e.value(u"points"_s).toArray().size();
        std::optional<QList<StrokePoint>> points = pointsFrom(e, x, y, pressures, realPressure);
        if (!points)
            return std::nullopt;
        s.points = std::move(*points);
        if (!realPressure)
            s.simulatePressure = true;
        s.tool = e.value(u"customData"_s).toObject().value(u"brulionTool"_s).toString() == u"highlighter"
                         ? Tool::Highlighter
                         : Tool::Pen;
        break;
    }
    case ElementType::Line:
    case ElementType::Arrow: {
        std::optional<QList<StrokePoint>> points = pointsFrom(e, x, y, {}, false);
        if (!points || points->size() < 2)
            return std::nullopt;
        s.points = std::move(*points);
        s.startArrowhead = arrowheadFrom(e.value(u"startArrowhead"_s));
        s.endArrowhead = arrowheadFrom(e.value(u"endArrowhead"_s));
        s.startBound = e.value(u"startBinding"_s).toObject().value(u"elementId"_s).toString();
        s.endBound = e.value(u"endBinding"_s).toObject().value(u"elementId"_s).toString();
        s.elbowed = e.value(u"elbowed"_s).toBool();
        s.polygon = e.value(u"polygon"_s).toBool();
        break;
    }
    case ElementType::Text: {
        if (!e.value(u"text"_s).isString())
            return std::nullopt;
        s.text = e.value(u"text"_s).toString();
        s.originalText = stringOr(e, u"originalText", s.text);
        s.fontSize = std::clamp(number(e, u"fontSize", 20), 1.0, 1000.0);
        s.fontFamily = int(std::clamp(number(e, u"fontFamily", 5), 0.0, 1000.0));
        const double lh = number(e, u"lineHeight", 1.25);
        s.lineHeight = lh >= 0.5 && lh <= 5 ? lh : 1.25;
        s.textAlign = textAlignFrom(e.value(u"textAlign"_s).toString());
        s.verticalAlign = verticalAlignFrom(e.value(u"verticalAlign"_s).toString());
        s.containerId = e.value(u"containerId"_s).toString();
        s.autoResize = e.value(u"autoResize"_s).toBool(true);
        break;
    }
    case ElementType::Image: s.fileId = e.value(u"fileId"_s).toString(); break;
    case ElementType::Rectangle:
    case ElementType::Ellipse:
    case ElementType::Diamond: break;
    }
    s.seed = e.value(u"seed"_s).toInt(1);
    s.version = e.value(u"version"_s).toInt(1);
    s.versionNonce = e.value(u"versionNonce"_s).toInt();
    s.updated = qint64(number(e, u"updated", 0));
    s.element = e;
    return s;
}

QString fillStyleName(FillStyle style)
{
    switch (style) {
    case FillStyle::Hachure: return u"hachure"_s;
    case FillStyle::CrossHatch: return u"cross-hatch"_s;
    case FillStyle::Zigzag: return u"zigzag"_s;
    case FillStyle::Solid: break;
    }
    return u"solid"_s;
}

QString strokeStyleName(StrokeStyle style)
{
    switch (style) {
    case StrokeStyle::Dashed: return u"dashed"_s;
    case StrokeStyle::Dotted: return u"dotted"_s;
    case StrokeStyle::Solid: break;
    }
    return u"solid"_s;
}

QString arrowheadName(Arrowhead head)
{
    switch (head) {
    case Arrowhead::Arrow:
    case Arrowhead::Other: return u"arrow"_s;
    case Arrowhead::Triangle: return u"triangle"_s;
    case Arrowhead::TriangleOutline: return u"triangle_outline"_s;
    case Arrowhead::Bar: return u"bar"_s;
    case Arrowhead::Dot: return u"dot"_s;
    case Arrowhead::Circle: return u"circle"_s;
    case Arrowhead::CircleOutline: return u"circle_outline"_s;
    case Arrowhead::Diamond: return u"diamond"_s;
    case Arrowhead::DiamondOutline: return u"diamond_outline"_s;
    case Arrowhead::None: break;
    }
    return {};
}

QString textAlignName(TextAlign align)
{
    switch (align) {
    case TextAlign::Center: return u"center"_s;
    case TextAlign::Right: return u"right"_s;
    case TextAlign::Left: break;
    }
    return u"left"_s;
}

QString verticalAlignName(VerticalAlign align)
{
    switch (align) {
    case VerticalAlign::Middle: return u"middle"_s;
    case VerticalAlign::Bottom: return u"bottom"_s;
    case VerticalAlign::Top: break;
    }
    return u"top"_s;
}

QJsonObject strokeElement(const Stroke &stroke)
{
    const double x0 = stroke.points.isEmpty() ? 0 : stroke.points.first().x;
    const double y0 = stroke.points.isEmpty() ? 0 : stroke.points.first().y;
    const auto round2 = [](double v) { return std::round(v * 100) / 100; };
    QJsonArray points;
    QJsonArray pressures;
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    for (const StrokePoint &p : stroke.points) {
        const double dx = round2(p.x - x0);
        const double dy = round2(p.y - y0);
        minX = std::min(minX, dx);
        maxX = std::max(maxX, dx);
        minY = std::min(minY, dy);
        maxY = std::max(maxY, dy);
        points.append(QJsonArray{dx, dy});
        if (!stroke.simulatePressure)
            pressures.append(std::round(std::clamp(p.pressure, 0.0, 1.0) * 1000) / 1000);
    }
    QJsonObject e{
            {u"id"_s, stroke.id},
            {u"type"_s, u"freedraw"_s},
            {u"x"_s, round2(x0)},
            {u"y"_s, round2(y0)},
            {u"width"_s, round2(maxX - minX)},
            {u"height"_s, round2(maxY - minY)},
            {u"angle"_s, 0},
            {u"strokeColor"_s, stroke.color},
            {u"backgroundColor"_s, u"transparent"_s},
            {u"fillStyle"_s, u"solid"_s},
            {u"strokeWidth"_s, stroke.width},
            {u"strokeStyle"_s, u"solid"_s},
            {u"roughness"_s, 0},
            {u"opacity"_s, stroke.opacity},
            {u"groupIds"_s, QJsonArray()},
            {u"frameId"_s, QJsonValue::Null},
            {u"roundness"_s, QJsonValue::Null},
            {u"seed"_s, stroke.seed},
            {u"version"_s, stroke.version},
            {u"versionNonce"_s, stroke.versionNonce},
            {u"isDeleted"_s, false},
            {u"boundElements"_s, QJsonValue::Null},
            {u"updated"_s, double(stroke.updated)},
            {u"link"_s, QJsonValue::Null},
            {u"locked"_s, false},
            {u"points"_s, points},
            {u"pressures"_s, pressures},
            {u"simulatePressure"_s, stroke.simulatePressure},
            {u"lastCommittedPoint"_s, points.isEmpty() ? QJsonValue(QJsonValue::Null) : points.last()},
    };
    if (stroke.tool == Tool::Highlighter)
        e.insert(u"customData"_s, QJsonObject{{u"brulionTool"_s, u"highlighter"_s}});
    return e;
}

namespace {

/// The relative points of a freedraw, line or arrow element (from the first point), and the
/// extent they span.
QJsonArray relativePoints(const QList<StrokePoint> &points, QSizeF *extent)
{
    const double x0 = points.isEmpty() ? 0 : points.first().x;
    const double y0 = points.isEmpty() ? 0 : points.first().y;
    QJsonArray out;
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    for (const StrokePoint &p : points) {
        const double dx = round2(p.x - x0);
        const double dy = round2(p.y - y0);
        minX = std::min(minX, dx);
        maxX = std::max(maxX, dx);
        minY = std::min(minY, dy);
        maxY = std::max(maxY, dy);
        out.append(QJsonArray{dx, dy});
    }
    if (extent)
        *extent = QSizeF(round2(maxX - minX), round2(maxY - minY));
    return out;
}

QJsonValue nullOr(const QString &s)
{
    return s.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(s);
}

/// A binding as written: the one read with its elementId replaced (a copy's), or a new one.
QJsonValue bindingJson(const QJsonValue &read, const QString &id)
{
    if (id.isEmpty())
        return QJsonValue::Null;
    QJsonObject b = read.isObject() ? read.toObject() : QJsonObject{{u"focus"_s, 0}, {u"gap"_s, 1}};
    b.insert(u"elementId"_s, id);
    return b;
}

} // namespace

QJsonObject elementJson(const Stroke &s, const QHash<QString, ElementType> *types)
{
    if (!s.element.isEmpty() && s.version == s.element.value(u"version"_s).toInt(1)
        && s.id == s.element.value(u"id"_s).toString())
        return s.element; // unchanged since it was read: as it was
    QJsonObject e;
    if (!s.element.isEmpty()) {
        e = s.element;
    } else if (s.type == ElementType::Freedraw) {
        e = strokeElement(s);
    } else {
        e = QJsonObject{{u"frameId"_s, QJsonValue::Null}, {u"link"_s, QJsonValue::Null}};
        switch (s.type) {
        case ElementType::Line:
        case ElementType::Arrow:
            e.insert(u"lastCommittedPoint"_s, QJsonValue::Null);
            if (s.type == ElementType::Arrow)
                e.insert(u"elbowed"_s, false);
            else
                e.insert(u"polygon"_s, false);
            break;
        case ElementType::Image:
            e.insert(u"status"_s, u"saved"_s);
            e.insert(u"scale"_s, QJsonArray{1, 1});
            break;
        default: break;
        }
    }
    e.insert(u"id"_s, s.id);
    e.insert(u"type"_s, elementTypeName(s.type));
    e.insert(u"angle"_s, s.angle);
    e.insert(u"strokeColor"_s, s.color);
    e.insert(u"backgroundColor"_s, s.fill);
    e.insert(u"fillStyle"_s, fillStyleName(s.fillStyle));
    e.insert(u"strokeWidth"_s, s.width);
    e.insert(u"strokeStyle"_s, strokeStyleName(s.strokeStyle));
    e.insert(u"roughness"_s, s.roughness);
    e.insert(u"opacity"_s, s.opacity);
    if (s.roundness == 0) {
        e.insert(u"roundness"_s, QJsonValue::Null);
    } else if (e.value(u"roundness"_s).toObject().value(u"type"_s).toInt() != s.roundness) {
        e.insert(u"roundness"_s, QJsonObject{{u"type"_s, s.roundness}});
    }
    e.insert(u"locked"_s, s.locked);
    e.insert(u"groupIds"_s, QJsonArray::fromStringList(s.groupIds));
    {
        // boundElements: [{id, type}] — an entry read kept as it was; bound elements not in the
        // drawing (any more) left out.
        QJsonArray bound;
        const QJsonArray read = e.value(u"boundElements"_s).toArray();
        for (const QString &id : s.boundIds) {
            QJsonObject entry;
            for (const QJsonValue &v : read) {
                if (v.toObject().value(u"id"_s).toString() == id)
                    entry = v.toObject();
            }
            if (types) {
                const auto it = types->constFind(id);
                if (it == types->constEnd())
                    continue;
                entry.insert(u"type"_s, *it == ElementType::Text ? u"text"_s : u"arrow"_s);
            } else if (!entry.contains(u"type"_s)) {
                entry.insert(u"type"_s, u"arrow"_s);
            }
            entry.insert(u"id"_s, id);
            bound.append(entry);
        }
        e.insert(u"boundElements"_s, bound.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(bound));
    }
    e.insert(u"seed"_s, s.seed);
    e.insert(u"version"_s, s.version);
    e.insert(u"versionNonce"_s, s.versionNonce);
    e.insert(u"isDeleted"_s, false);
    e.insert(u"updated"_s, double(s.updated));
    if (hasPoints(s.type)) {
        QSizeF extent;
        const QJsonArray points = relativePoints(s.points, &extent);
        e.insert(u"x"_s, s.points.isEmpty() ? 0 : round2(s.points.first().x));
        e.insert(u"y"_s, s.points.isEmpty() ? 0 : round2(s.points.first().y));
        e.insert(u"width"_s, extent.width());
        e.insert(u"height"_s, extent.height());
        e.insert(u"points"_s, points);
        if (s.type == ElementType::Freedraw) {
            QJsonArray pressures;
            if (!s.simulatePressure) {
                for (const StrokePoint &p : s.points)
                    pressures.append(std::round(std::clamp(p.pressure, 0.0, 1.0) * 1000) / 1000);
            }
            e.insert(u"pressures"_s, pressures);
            e.insert(u"simulatePressure"_s, s.simulatePressure);
            if (e.contains(u"lastCommittedPoint"_s))
                e.insert(u"lastCommittedPoint"_s,
                         points.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(points.last()));
            QJsonObject custom = e.value(u"customData"_s).toObject();
            if (s.tool == Tool::Highlighter)
                custom.insert(u"brulionTool"_s, u"highlighter"_s);
            else
                custom.remove(u"brulionTool"_s);
            if (custom.isEmpty())
                e.remove(u"customData"_s);
            else
                e.insert(u"customData"_s, custom);
        } else {
            if (s.startArrowhead != Arrowhead::Other)
                e.insert(u"startArrowhead"_s, nullOr(arrowheadName(s.startArrowhead)));
            if (s.endArrowhead != Arrowhead::Other)
                e.insert(u"endArrowhead"_s, nullOr(arrowheadName(s.endArrowhead)));
            e.insert(u"startBinding"_s, bindingJson(e.value(u"startBinding"_s), s.startBound));
            e.insert(u"endBinding"_s, bindingJson(e.value(u"endBinding"_s), s.endBound));
            if (s.type == ElementType::Arrow)
                e.insert(u"elbowed"_s, s.elbowed);
            else if (s.polygon || e.contains(u"polygon"_s))
                e.insert(u"polygon"_s, s.polygon);
        }
    } else {
        e.insert(u"x"_s, round2(s.box.x()));
        e.insert(u"y"_s, round2(s.box.y()));
        e.insert(u"width"_s, round2(s.box.width()));
        e.insert(u"height"_s, round2(s.box.height()));
    }
    if (s.type == ElementType::Text) {
        e.insert(u"text"_s, s.text);
        e.insert(u"originalText"_s, s.originalText);
        e.insert(u"fontSize"_s, s.fontSize);
        e.insert(u"fontFamily"_s, s.fontFamily);
        e.insert(u"textAlign"_s, textAlignName(s.textAlign));
        e.insert(u"verticalAlign"_s, verticalAlignName(s.verticalAlign));
        e.insert(u"lineHeight"_s, s.lineHeight);
        e.insert(u"containerId"_s, nullOr(s.containerId));
        e.insert(u"autoResize"_s, s.autoResize);
    } else if (s.type == ElementType::Image) {
        e.insert(u"fileId"_s, nullOr(s.fileId));
    }
    return e;
}

std::optional<QJsonObject> sceneRoot(QByteArrayView json)
{
    if (json.size() > MaxSceneBytes)
        return std::nullopt;
    // Counted before parsing (brackets inside strings too: an upper bound): what the parse would
    // allocate grows with the arrays and objects, not with the bytes.
    qsizetype containers = 0;
    for (const char c : json) {
        if ((c == '[' || c == '{') && ++containers > MaxSceneContainers)
            return std::nullopt;
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toByteArray(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    QJsonObject root = doc.object();
    if (root.value(u"type"_s).toString() != u"excalidraw")
        return std::nullopt;
    if (root.contains(u"elements"_s) && !root.value(u"elements"_s).isArray())
        return std::nullopt;
    return root;
}

std::optional<Drawing> sceneFromObject(const QJsonObject &root)
{
    if (root.value(u"type"_s).toString() != u"excalidraw")
        return std::nullopt;
    if (root.contains(u"elements"_s) && !root.value(u"elements"_s).isArray())
        return std::nullopt;
    Drawing d;
    const QJsonArray elements = root.value(u"elements"_s).toArray();
    for (const QJsonValue &v : elements) {
        const QJsonObject e = v.toObject();
        if (e.value(u"isDeleted"_s).toBool())
            continue;
        if (std::optional<Stroke> s = elementFromJson(e))
            d.strokes.append(std::move(*s));
        else
            d.foreign.append(v);
    }
    d.appState = root.value(u"appState"_s).toObject();
    d.files = root.value(u"files"_s).toObject();
    if (const QString paper = d.appState.value(u"viewBackgroundColor"_s).toString(); !paper.isEmpty())
        d.paper = paper;
    d.background = backgroundFromName(d.appState.value(u"brulionBackground"_s).toString());
    // (A number or null reads as "": Auto.)
    d.theme = paperThemeFromName(d.appState.value(u"brulionTheme"_s).toString());
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (it.key() != u"type" && it.key() != u"version" && it.key() != u"elements" && it.key() != u"appState"
            && it.key() != u"files")
            d.scene.insert(it.key(), it.value());
    }
    return d;
}

std::optional<Drawing> parseScene(QByteArrayView json)
{
    const std::optional<QJsonObject> root = sceneRoot(json);
    return root ? sceneFromObject(*root) : std::nullopt;
}

QJsonObject sceneObject(const Drawing &drawing)
{
    QJsonObject root = drawing.scene;
    root.insert(u"type"_s, u"excalidraw"_s);
    root.insert(u"version"_s, 2);
    if (!root.value(u"source"_s).isString())
        root.insert(u"source"_s, SceneSource.toString());
    QHash<QString, ElementType> types;
    types.reserve(drawing.strokes.size());
    for (const Stroke &s : drawing.strokes)
        types.insert(s.id, s.type);
    QList<QJsonObject> written;
    written.reserve(drawing.strokes.size() + drawing.foreign.size());
    for (const Stroke &s : drawing.strokes)
        written.append(elementJson(s, &types));
    for (const QJsonValue &v : drawing.foreign)
        written.append(v.toObject());
    // Fractional indices that no longer rise (an element moved up or down, a copy): left out of
    // every element — Excalidraw makes new ones (syncInvalidIndices).
    bool rising = true;
    QString last;
    bool any = false;
    for (const QJsonObject &e : std::as_const(written)) {
        const QJsonValue index = e.value(u"index"_s);
        if (!index.isString())
            continue;
        if (any && !(last < index.toString())) {
            rising = false;
            break;
        }
        last = index.toString();
        any = true;
    }
    QJsonArray elements;
    QSet<QString> fileIds;
    for (qsizetype i = 0; i < written.size(); ++i) {
        QJsonObject e = written.at(i);
        if (!rising)
            e.remove(u"index"_s);
        if (e.value(u"type"_s).toString() == u"image")
            fileIds.insert(e.value(u"fileId"_s).toString());
        // (A foreign value that is no object is written as it was.)
        if (i >= drawing.strokes.size() && !drawing.foreign.at(i - drawing.strokes.size()).isObject())
            elements.append(drawing.foreign.at(i - drawing.strokes.size()));
        else
            elements.append(e);
    }
    root.insert(u"elements"_s, elements);
    QJsonObject appState = drawing.appState;
    appState.insert(u"viewBackgroundColor"_s, drawing.paper);
    if (drawing.background == Background::Plain)
        appState.remove(u"brulionBackground"_s);
    else
        appState.insert(u"brulionBackground"_s, backgroundName(drawing.background));
    if (drawing.theme == PaperTheme::Auto)
        appState.remove(u"brulionTheme"_s);
    else
        appState.insert(u"brulionTheme"_s, paperThemeName(drawing.theme));
    root.insert(u"appState"_s, appState);
    // The pictures of image elements (a deleted picture's file goes with it).
    QJsonObject files;
    for (auto it = drawing.files.constBegin(); it != drawing.files.constEnd(); ++it) {
        if (fileIds.contains(it.key()))
            files.insert(it.key(), it.value());
    }
    root.insert(u"files"_s, files);
    return root;
}

QByteArray sceneJson(const Drawing &drawing)
{
    return QJsonDocument(sceneObject(drawing)).toJson(QJsonDocument::Compact);
}

QByteArray encodePayload(QByteArrayView sceneJson)
{
    QByteArray wrapper = R"({"version":"1","encoding":"bstring","compressed":true,"encoded":)";
    appendByteStringJson(wrapper, deflate(sceneJson));
    wrapper += '}';
    return wrapper.toBase64();
}

std::optional<QByteArray> decodePayload(QByteArrayView base64, int version)
{
    QByteArray b64;
    b64.reserve(base64.size());
    for (const char c : base64) {
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
            b64 += c;
    }
    if (b64.isEmpty() || b64.size() > MaxSceneBytes)
        return std::nullopt;
    const QByteArray::FromBase64Result decoded =
            QByteArray::fromBase64Encoding(b64, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded)
        return std::nullopt;
    // Version 2: atob() — the bytes are characters (Latin-1); version 1: UTF-8 text.
    const QByteArray json = version >= 2 ? QString::fromLatin1(*decoded).toUtf8() : *decoded;
    return unwrapPayload(json);
}

std::optional<QByteArray> svgPayload(QByteArrayView svg, bool *corrupt)
{
    if (corrupt)
        *corrupt = false;
    const qsizetype type = svg.indexOf(PayloadType);
    if (type < 0)
        return std::nullopt;
    const auto fail = [corrupt]() -> std::optional<QByteArray> {
        if (corrupt)
            *corrupt = true;
        return std::nullopt;
    };
    const qsizetype start = svg.indexOf(PayloadStart, type);
    if (start < 0)
        return fail();
    const qsizetype from = start + PayloadStart.size();
    const qsizetype end = svg.indexOf(PayloadEnd, from);
    if (end < 0)
        return fail();
    int version = 1;
    if (const qsizetype v = svg.indexOf(PayloadVersion); v >= 0 && v < start) {
        qsizetype i = v + PayloadVersion.size();
        int n = 0;
        bool digits = false;
        while (i < svg.size() && svg.at(i) >= '0' && svg.at(i) <= '9' && n < 1000) {
            n = n * 10 + (svg.at(i) - '0');
            digits = true;
            ++i;
        }
        if (digits)
            version = n;
    }
    std::optional<QByteArray> scene = decodePayload(svg.sliced(from, end - from), version);
    if (!scene)
        return fail();
    return scene;
}

bool looksLikeExcalidrawSvg(QByteArrayView head)
{
    return head.indexOf(PayloadType) >= 0;
}

QByteArray deflate(QByteArrayView data)
{
    mz_ulong size = mz_compressBound(mz_ulong(data.size()));
    QByteArray out(qsizetype(size), Qt::Uninitialized);
    if (mz_compress2(reinterpret_cast<unsigned char *>(out.data()), &size,
                     reinterpret_cast<const unsigned char *>(data.data()), mz_ulong(data.size()), MZ_DEFAULT_LEVEL)
        != MZ_OK)
        return {};
    out.truncate(qsizetype(size));
    return out;
}

std::optional<QByteArray> inflate(QByteArrayView data, qsizetype limit)
{
    if (data.isEmpty())
        return std::nullopt;
    mz_stream stream{};
    stream.next_in = reinterpret_cast<const unsigned char *>(data.data());
    stream.avail_in = static_cast<unsigned int>(std::min<qsizetype>(data.size(), MaxSceneBytes));
    if (mz_inflateInit(&stream) != MZ_OK)
        return std::nullopt;
    QByteArray out;
    constexpr qsizetype Chunk = 64 * 1024;
    bool ok = false;
    for (;;) {
        const qsizetype before = out.size();
        out.resize(before + Chunk);
        stream.next_out = reinterpret_cast<unsigned char *>(out.data() + before);
        stream.avail_out = static_cast<unsigned int>(Chunk);
        const int result = mz_inflate(&stream, MZ_NO_FLUSH);
        out.resize(before + (Chunk - qsizetype(stream.avail_out)));
        if (result == MZ_STREAM_END) {
            ok = true;
            break;
        }
        if (out.size() > limit)
            break;
        if (result != MZ_OK)
            break; // broken data, or truncated (no progress without more input)
        if (out.size() == before && stream.avail_in == 0)
            break;
    }
    mz_inflateEnd(&stream);
    if (!ok || out.size() > limit)
        return std::nullopt;
    return out;
}

} // namespace brulion::drawing
