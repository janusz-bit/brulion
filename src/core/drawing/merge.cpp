#include "drawing/merge.h"

#include "drawing/backdrop.h"
#include "drawing/drawingfile.h"
#include "drawing/excalidraw.h"

#include <QJsonDocument>
#include <QList>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

namespace {

/// An element's content as the scene stores it, but for what a writer changes without a new
/// version: Excalidraw's fractional `index` (left out when the order changes), the Excalidraw
/// plugin's `rawText` (its Markdown, kept in step with the file's text entries) and `updated`.
QJsonObject contentOf(const Stroke &s)
{
    QJsonObject e = elementJson(s);
    e.remove(u"index"_s);
    e.remove(u"rawText"_s);
    e.remove(u"updated"_s);
    return e;
}

/// The elements by mergeKey (the first of a key seen twice).
QHash<QString, const Stroke *> byKey(const Drawing &d)
{
    QHash<QString, const Stroke *> out;
    out.reserve(d.strokes.size());
    for (const Stroke &s : d.strokes) {
        const QString k = mergeKey(s);
        if (!out.contains(k))
            out.insert(k, &s);
    }
    return out;
}

QSet<QString> keysOf(const Drawing &d)
{
    QSet<QString> out;
    out.reserve(d.strokes.size());
    for (const Stroke &s : d.strokes)
        out.insert(mergeKey(s));
    return out;
}

bool samePaper(QStringView a, QStringView b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

/// An element of `side` has the version it had in `base` but other content: changed by hand (a
/// text editor), so versions say nothing about it.
bool changedWithoutVersion(const Drawing &base, const Drawing &side)
{
    const QHash<QString, const Stroke *> was = byKey(base);
    for (const Stroke &s : side.strokes) {
        const auto it = was.constFind(mergeKey(s));
        if (it != was.constEnd() && (*it)->version == s.version && contentOf(**it) != contentOf(s))
            return true;
    }
    return false;
}

/// An Excalidraw plugin file without its scene: the frontmatter, the back of the card, the text
/// entries, the links, the `%%` (empty for other formats).
QByteArray aroundScene(const DrawingFile &file)
{
    if (!file.markdown)
        return {};
    const QByteArray &b = file.markdown->bytes;
    return b.first(file.markdown->sceneFrom) + b.sliced(file.markdown->sceneTo);
}

/// aroundScene() without the "## Text Elements" entries (each from its text to its line breaks):
/// what follows no element of the scene.
QByteArray notesOf(const ExcalidrawMarkdown &m)
{
    struct Cut {
        qsizetype from = 0;
        qsizetype to = 0;
    };
    QList<Cut> cuts{ { m.sceneFrom, m.sceneTo } };
    for (const ExcalidrawMarkdown::TextEntry &e : m.textEntries)
        cuts.append({ e.from, e.end });
    std::sort(cuts.begin(), cuts.end(), [](const Cut &a, const Cut &b) { return a.from < b.from; });
    QByteArray out;
    qsizetype at = 0;
    for (const Cut &c : std::as_const(cuts)) {
        if (c.from < at || c.to < c.from || c.to > m.bytes.size())
            continue; // (overlapping: never for a layout read from a file)
        out += QByteArrayView(m.bytes).sliced(at, c.from - at);
        at = c.to;
    }
    out += QByteArrayView(m.bytes).sliced(at);
    return out;
}

/// The elements with an id, by id (the first of an id seen twice).
QHash<QString, const Stroke *> byId(const Drawing &d)
{
    QHash<QString, const Stroke *> out;
    out.reserve(d.strokes.size());
    for (const Stroke &s : d.strokes) {
        if (!s.id.isEmpty() && !out.contains(s.id))
            out.insert(s.id, &s);
    }
    return out;
}

} // namespace

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
    // Each side's backdrop (backdrop.h), by key: when both set one of their own, one stays.
    const QString ourBackdrop = backdropIndex(ours) == 0 ? mergeKey(ours.strokes.first()) : QString();
    const QString theirBackdrop = backdropIndex(theirs) == 0 ? mergeKey(theirs.strokes.first()) : QString();
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
    if (!ourBackdrop.isEmpty() && !theirBackdrop.isEmpty() && ourBackdrop != theirBackdrop) {
        // Two backdrops (each side set its own): the one set last stays at the bottom (a tie keeps
        // theirs), the other goes — it would lie on it, hidden from the backdrop's menu.
        qsizetype mine = -1, other = -1;
        for (qsizetype i = 0; i < merged.size() && (mine < 0 || other < 0); ++i) {
            const QString k = mergeKey(merged.at(i));
            if (k == ourBackdrop && mine < 0)
                mine = i;
            else if (k == theirBackdrop && other < 0)
                other = i;
        }
        if (mine >= 0 && other >= 0) {
            const bool oursWins = merged.at(mine).updated > merged.at(other).updated;
            Stroke kept = merged.at(oursWins ? mine : other);
            merged.removeAt(std::max(mine, other));
            merged.removeAt(std::min(mine, other));
            merged.prepend(std::move(kept));
        }
    }
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

MergeCounts mergeCounts(const Drawing &ours, const Drawing &theirs, const Drawing &merged)
{
    const QSet<QString> mine = keysOf(ours);
    const QSet<QString> other = keysOf(theirs);
    MergeCounts c;
    c.ours = int(ours.strokes.size());
    c.theirs = int(theirs.strokes.size());
    c.merged = int(merged.strokes.size());
    for (const QString &k : mine) {
        if (other.contains(k))
            ++c.common;
        else
            ++c.onlyOurs;
    }
    c.onlyTheirs = int(other.size()) - c.common;
    return c;
}

bool sameDrawing(const Drawing &a, const Drawing &b)
{
    if (a.strokes.size() != b.strokes.size() || a.background != b.background || a.theme != b.theme
        || !samePaper(a.paper, b.paper) || a.foreign != b.foreign)
        return false;
    for (qsizetype i = 0; i < a.strokes.size(); ++i) {
        const Stroke &x = a.strokes.at(i);
        const Stroke &y = b.strokes.at(i);
        if (mergeKey(x) != mergeKey(y) || x.version != y.version || contentOf(x) != contentOf(y))
            return false;
    }
    return true;
}

bool containsDrawing(const Drawing &outer, const Drawing &inner)
{
    if ((inner.background != outer.background && inner.background != Background::Plain)
        || (inner.theme != outer.theme && inner.theme != PaperTheme::Auto)
        || (!samePaper(inner.paper, outer.paper) && !samePaper(inner.paper, DefaultPaper)))
        return false;
    for (const QJsonValue &v : inner.foreign) {
        if (!outer.foreign.contains(v))
            return false;
    }
    const QHash<QString, const Stroke *> at = byKey(outer);
    for (const Stroke &s : inner.strokes) {
        const auto it = at.constFind(mergeKey(s));
        if (it == at.constEnd() || (*it)->version < s.version
            || ((*it)->version == s.version && contentOf(**it) != contentOf(s)))
            return false;
    }
    return true;
}

QByteArray markdownAroundScene(const DrawingFile &file)
{
    return aroundScene(file);
}

bool keepsMarkdownOf(const DrawingFile &into, const DrawingFile &other)
{
    if (!other.markdown)
        return true;
    if (!into.markdown || notesOf(*into.markdown) != notesOf(*other.markdown))
        return false;
    QHash<QString, QByteArrayView> entries;
    const QByteArray &mine = into.markdown->bytes;
    for (const ExcalidrawMarkdown::TextEntry &e : into.markdown->textEntries) {
        if (!entries.contains(e.id) && e.from <= e.to && e.to <= mine.size())
            entries.insert(e.id, QByteArrayView(mine).sliced(e.from, e.to - e.from));
    }
    const QHash<QString, const Stroke *> here = byId(into.drawing);
    const QHash<QString, const Stroke *> there = byId(other.drawing);
    const QByteArray &theirs = other.markdown->bytes;
    for (const ExcalidrawMarkdown::TextEntry &e : other.markdown->textEntries) {
        if (e.from > e.to || e.to > theirs.size())
            continue;
        const auto entry = entries.constFind(e.id);
        if (entry != entries.constEnd() && *entry == QByteArrayView(theirs).sliced(e.from, e.to - e.from))
            continue;
        const Stroke *m = here.value(e.id);
        const Stroke *t = there.value(e.id);
        if (!m || !t || m->version > t->version)
            continue; // only other has the element (its text in the scene), or into's is newer
        if (t->version > m->version && (t->text != m->text || t->originalText != m->originalText))
            continue; // other's is newer, with another text: the merge writes other's
        return false;
    }
    return true;
}

std::optional<QByteArray> mergeDrawingFiles(QByteArrayView base, QByteArrayView local, QByteArrayView remote,
                                            qint64 now)
{
    const std::optional<DrawingFile> was = readDrawingFile(base);
    const std::optional<DrawingFile> here = readDrawingFile(local);
    const std::optional<DrawingFile> there = readDrawingFile(remote);
    if (!was || !here || !there || here->format != there->format || was->format != there->format)
        return std::nullopt;
    if (!here->writable || !there->writable || !editable(here->drawing) || !editable(there->drawing))
        return std::nullopt;
    if (changedWithoutVersion(was->drawing, here->drawing) || changedWithoutVersion(was->drawing, there->drawing))
        return std::nullopt;
    // The plugin's Markdown around the scene: the remote's, or the local one's when only this side
    // changed it (its back of the card, its text entries) — both changed: no merge of notes here.
    const DrawingFile *into = &*there;
    if (there->format == FileFormat::ExcalidrawMarkdown) {
        const QByteArray before = aroundScene(*was);
        const QByteArray mine = aroundScene(*here);
        const QByteArray theirs = aroundScene(*there);
        if (mine != before && theirs != before && mine != theirs)
            return std::nullopt;
        if (theirs == before && mine != before)
            into = &*here;
    }
    const Drawing merged = merge(mergeBase(was->drawing), here->drawing, there->drawing);
    if (!readsBack(*into, merged, now))
        return std::nullopt; // (the pictures of both together: more than a file may hold)
    QByteArray out = writeDrawingFile(*into, merged, now);
    if (out.isEmpty())
        return std::nullopt;
    return out;
}

} // namespace brulion::drawing
