#include "drawing/merge.h"

#include "drawing/excalidraw.h"

#include <QJsonDocument>
#include <QSet>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

QString mergeKey(const Stroke &s)
{
    if (!s.id.isEmpty())
        return s.id;
    const QByteArray json =
            QJsonDocument(s.element.isEmpty() ? elementJson(s) : s.element).toJson(QJsonDocument::Compact);
    quint64 h = 0xcbf29ce484222325ull;
    for (const char c : json) {
        h ^= quint64(uchar(c));
        h *= 0x100000001b3ull;
    }
    return u"~"_s + QString::number(h, 16);
}

MergeBase mergeBase(const Drawing &base)
{
    MergeBase b;
    b.known = true;
    b.versions.reserve(base.strokes.size());
    for (const Stroke &s : base.strokes)
        b.versions.insert(mergeKey(s), s.version);
    b.background = base.background;
    b.theme = base.theme;
    return b;
}

Drawing merge(const MergeBase &base, const Drawing &ours, Drawing theirs)
{
    QHash<QString, qsizetype> oursIndex;
    QStringList oursKeys;
    oursKeys.reserve(ours.strokes.size());
    for (qsizetype i = 0; i < ours.strokes.size(); ++i) {
        const QString k = mergeKey(ours.strokes.at(i));
        oursKeys.append(k);
        if (!oursIndex.contains(k))
            oursIndex.insert(k, i);
    }
    QSet<QString> theirKeys;
    QList<Stroke> merged;
    merged.reserve(theirs.strokes.size() + ours.strokes.size());
    for (Stroke &t : theirs.strokes) {
        const QString k = mergeKey(t);
        theirKeys.insert(k);
        const qsizetype o = oursIndex.value(k, -1);
        if (base.known) {
            const bool inBase = base.versions.contains(k);
            if (inBase && o < 0)
                continue; // deleted here
            if (o >= 0) {
                const Stroke &mine = ours.strokes.at(o);
                if (inBase ? mine.version != base.versions.value(k) : mine.version > t.version) {
                    merged.append(mine); // changed here (or new on both sides, ours newer)
                    continue;
                }
            }
        } else if (o >= 0 && ours.strokes.at(o).version > t.version) {
            merged.append(ours.strokes.at(o));
            continue;
        }
        merged.append(std::move(t));
    }
    // New here: on top, in ours' order — the leading locked run (a backdrop) at the bottom.
    QList<Stroke> bottom;
    bool leading = true;
    for (qsizetype i = 0; i < ours.strokes.size(); ++i) {
        const Stroke &mine = ours.strokes.at(i);
        leading = leading && mine.locked;
        const QString &k = oursKeys.at(i);
        if (theirKeys.contains(k) || oursIndex.value(k) != i)
            continue;
        if (base.known && base.versions.contains(k))
            continue; // deleted there
        if (leading)
            bottom.append(mine);
        else
            merged.append(mine);
    }
    if (!bottom.isEmpty())
        merged = bottom + merged;
    theirs.strokes = std::move(merged);
    if (base.known && ours.background != base.background)
        theirs.background = ours.background;
    if (base.known && ours.theme != base.theme)
        theirs.theme = ours.theme;
    for (auto it = ours.files.constBegin(); it != ours.files.constEnd(); ++it) {
        if (!theirs.files.contains(it.key()))
            theirs.files.insert(it.key(), it.value());
    }
    return theirs;
}

} // namespace brulion::drawing
