#include "drawing/clipboard.h"

#include "drawing/excalidraw.h"
#include "drawing/geometry.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>
#include <climits>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

QByteArray clipboardJson(const Drawing &d, const QStringList &ids)
{
    const QSet<QString> want(ids.cbegin(), ids.cend());
    QSet<QString> taken;
    for (const Stroke &e : d.strokes) {
        if (!e.id.isEmpty() && want.contains(e.id) && !isLocked(d, e))
            taken.insert(e.id);
    }
    if (taken.isEmpty())
        return {};
    Drawing out;
    out.files = d.files; // (sceneObject() keeps the pictures' files only)
    QSet<QString> written;
    for (const Stroke &original : d.strokes) {
        if (!taken.contains(original.id) || written.contains(original.id))
            continue;
        written.insert(original.id);
        // References to elements not copied are dropped: written from the model, not as read.
        Stroke e = original;
        bool cut = e.boundIds.removeIf([&taken](const QString &b) { return !taken.contains(b); }) > 0;
        for (QString *ref : {&e.startBound, &e.endBound, &e.containerId}) {
            if (!ref->isEmpty() && !taken.contains(*ref)) {
                ref->clear();
                cut = true;
            }
        }
        if (cut)
            e.version = e.version < INT_MAX ? e.version + 1 : 2;
        out.strokes.append(std::move(e));
    }
    const QJsonObject scene = sceneObject(out);
    const QJsonObject clip{{u"type"_s, ClipboardType.toString()},
                           {u"elements"_s, scene.value(u"elements"_s)},
                           {u"files"_s, scene.value(u"files"_s)}};
    return QJsonDocument(clip).toJson(QJsonDocument::Compact);
}

std::optional<ClipboardContent> parseClipboard(QByteArrayView text)
{
    if (text.size() > MaxSceneBytes)
        return std::nullopt;
    // (Plain text — the usual clipboard — is no object: refused before any parsing.)
    const QByteArrayView trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.front() != '{'
        || std::ranges::count_if(trimmed, [](char c) { return c == '[' || c == '{'; }) > MaxSceneContainers)
        return std::nullopt;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toByteArray(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    const QJsonObject root = doc.object();
    const QString type = root.value(u"type"_s).toString();
    if (type != ClipboardType && type != u"excalidraw-api/clipboard" && type != u"excalidraw")
        return std::nullopt;
    if (!root.value(u"elements"_s).isArray())
        return std::nullopt;
    ClipboardContent out;
    QJsonArray elements;
    // (A const array: iterating the root's own one would copy it.)
    for (const QJsonArray given = root.value(u"elements"_s).toArray(); const auto &v : given) {
        if (v.isObject() && v.toObject().value(u"isDeleted"_s).toBool())
            continue;
        if (!v.isObject() || elements.size() >= MaxPastedElements)
            ++out.skipped;
        else
            elements.append(v);
    }
    std::optional<Drawing> d = sceneFromObject(QJsonObject{
            {u"type"_s, u"excalidraw"_s}, {u"elements"_s, elements}, {u"files"_s, root.value(u"files"_s).toObject()}});
    if (!d)
        return std::nullopt;
    out.elements = std::move(d->strokes);
    out.files = std::move(d->files);
    out.skipped += int(d->foreign.size());
    return out;
}

QStringList pasteElements(Drawing &d, const ClipboardContent &content, QPointF at, const EditStamp &s)
{
    if (content.elements.isEmpty() || !std::isfinite(at.x()) || !std::isfinite(at.y()))
        return {};
    Drawing tmp;
    tmp.strokes = content.elements;
    QStringList all;
    QSet<QString> seen;
    for (qsizetype i = 0; i < tmp.strokes.size(); ++i) {
        Stroke &e = tmp.strokes[i];
        e.locked = false;
        // (Copied by id: an element without one, or a second of an id, gets one of its own.)
        if (e.id.isEmpty() || seen.contains(e.id)) {
            e.id = u"\x1fpaste"_s + QString::number(i);
            while (seen.contains(e.id)) // (the clipboard may hold that one too)
                e.id += u'\x1f';
        }
        seen.insert(e.id);
        all.append(e.id);
    }
    const QStringList copies = duplicateElements(tmp, all, QPointF(0, 0), s);
    if (copies.isEmpty())
        return {};
    const QSet<QString> copied(copies.cbegin(), copies.cend());
    // The middle of what they cover at `at` (moveElements() keeps them within CoordinateLimit).
    QRectF box;
    for (const Stroke &e : std::as_const(tmp.strokes)) {
        if (copied.contains(e.id))
            box |= elementBounds(e);
    }
    if (!box.isNull()) {
        const QPointF delta = at - box.center();
        moveElements(tmp, copied, QPointF(std::round(delta.x() * 100) / 100, std::round(delta.y() * 100) / 100), s);
    }
    QList<Stroke> pasted;
    for (Stroke &e : tmp.strokes) {
        if (!copied.contains(e.id))
            continue;
        // (Not in a frame of the drawing it came from.)
        if (e.element.contains(u"frameId"_s))
            e.element.insert(u"frameId"_s, QJsonValue::Null);
        if (!e.fileId.isEmpty() && !d.files.contains(e.fileId) && content.files.contains(e.fileId))
            d.files.insert(e.fileId, content.files.value(e.fileId));
        pasted.append(std::move(e));
    }
    d.strokes.append(std::move(pasted));
    return copies;
}

} // namespace brulion::drawing
