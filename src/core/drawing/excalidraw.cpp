#include "drawing/excalidraw.h"

#include "drawing/stroke.h"

#include <QJsonDocument>
#include <QJsonValue>

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

/// A stroke Brulion draws as Excalidraw does, or nullopt (the element then stays foreign).
std::optional<Stroke> strokeFromElement(const QJsonObject &e)
{
    if (e.value(u"type"_s).toString() != u"freedraw")
        return std::nullopt;
    if (!finite(e.value(u"x"_s)) || !finite(e.value(u"y"_s)))
        return std::nullopt;
    // Rotated or filled freehand lines: Brulion would draw them otherwise.
    if (e.contains(u"angle"_s) && (!finite(e.value(u"angle"_s)) || std::abs(e.value(u"angle"_s).toDouble()) > 1e-9))
        return std::nullopt;
    if (const QJsonValue bg = e.value(u"backgroundColor"_s);
        !bg.isUndefined() && !bg.isNull() && bg.toString() != u"transparent")
        return std::nullopt;
    const QJsonArray points = e.value(u"points"_s).toArray();
    if (points.isEmpty())
        return std::nullopt;
    Stroke s;
    s.id = e.value(u"id"_s).toString();
    s.simulatePressure = e.value(u"simulatePressure"_s).toBool(true);
    const QJsonArray pressures = e.value(u"pressures"_s).toArray();
    const bool realPressure = !s.simulatePressure && pressures.size() == points.size();
    const double x = e.value(u"x"_s).toDouble();
    const double y = e.value(u"y"_s).toDouble();
    s.points.reserve(points.size());
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
        s.points.append({x + p.at(0).toDouble(), y + p.at(1).toDouble(), pressure});
    }
    if (!realPressure)
        s.simulatePressure = true;
    s.color = e.value(u"strokeColor"_s).toString(u"#1e1e1e"_s);
    s.width = finite(e.value(u"strokeWidth"_s)) ? std::clamp(e.value(u"strokeWidth"_s).toDouble(), 0.05, 200.0) : 1;
    s.opacity = finite(e.value(u"opacity"_s)) ? std::clamp(e.value(u"opacity"_s).toInt(), 0, 100) : 100;
    s.tool = e.value(u"customData"_s).toObject().value(u"brulionTool"_s).toString() == u"highlighter"
                     ? Tool::Highlighter
                     : Tool::Pen;
    s.seed = e.value(u"seed"_s).toInt(1);
    s.version = e.value(u"version"_s).toInt(1);
    s.versionNonce = e.value(u"versionNonce"_s).toInt();
    s.updated = qint64(e.value(u"updated"_s).toDouble());
    s.element = e;
    return s;
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

std::optional<Drawing> parseScene(QByteArrayView json)
{
    if (json.size() > MaxSceneBytes)
        return std::nullopt;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toByteArray(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    const QJsonObject root = doc.object();
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
        if (std::optional<Stroke> s = strokeFromElement(e))
            d.strokes.append(std::move(*s));
        else
            d.foreign.append(v);
    }
    d.appState = root.value(u"appState"_s).toObject();
    d.files = root.value(u"files"_s).toObject();
    if (const QString paper = d.appState.value(u"viewBackgroundColor"_s).toString(); !paper.isEmpty())
        d.paper = paper;
    d.background = backgroundFromName(d.appState.value(u"brulionBackground"_s).toString());
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (it.key() != u"type" && it.key() != u"version" && it.key() != u"elements" && it.key() != u"appState"
            && it.key() != u"files")
            d.scene.insert(it.key(), it.value());
    }
    return d;
}

QByteArray sceneJson(const Drawing &drawing)
{
    QJsonObject root = drawing.scene;
    root.insert(u"type"_s, u"excalidraw"_s);
    root.insert(u"version"_s, 2);
    if (!root.value(u"source"_s).isString())
        root.insert(u"source"_s, SceneSource.toString());
    QJsonArray elements;
    for (const Stroke &s : drawing.strokes)
        elements.append(s.element.isEmpty() ? strokeElement(s) : s.element);
    for (const QJsonValue &v : drawing.foreign)
        elements.append(v);
    root.insert(u"elements"_s, elements);
    QJsonObject appState = drawing.appState;
    appState.insert(u"viewBackgroundColor"_s, drawing.paper);
    if (drawing.background == Background::Plain)
        appState.remove(u"brulionBackground"_s);
    else
        appState.insert(u"brulionBackground"_s, backgroundName(drawing.background));
    root.insert(u"appState"_s, appState);
    root.insert(u"files"_s, drawing.files);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
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
