#include "sync/merge.h"

#include "sync/diff.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::sync {

namespace {

/// Lines one side only added around the region: `side` is `before` + base + `after`.
struct AddedLines {
    QStringList before, after;
};

/// `side` is `base` (not empty) with lines added only right before it and / or right after it:
/// those lines. Where base occurs in `side` more than once with lines on both sides, the
/// placement is ambiguous: std::nullopt.
std::optional<AddedLines> addedAround(const QStringList &base, const QStringList &side)
{
    if (base.isEmpty() || side.size() <= base.size())
        return std::nullopt;
    const qsizetype extra = side.size() - base.size();
    if (side.first(base.size()) == base)
        return AddedLines{{}, side.last(extra)};
    if (side.last(base.size()) == base)
        return AddedLines{side.first(extra), {}};
    std::optional<AddedLines> found;
    for (qsizetype at = 1; at < extra; ++at) {
        if (side.mid(at, base.size()) != base)
            continue;
        if (found)
            return std::nullopt;
        found = AddedLines{side.first(at), side.mid(at + base.size())};
    }
    return found;
}

/// `changed` (the lines one side made of the region `base`) with the lines the other side only
/// added around the region, or std::nullopt when they cannot be placed for sure. Nothing of
/// either side is dropped and nothing added twice:
///  * lines added after the region come after the changed lines; when the changed side added
///    lines at the end too (its lines beyond the region's length), the two additions are
///    unioned as concurrent insertions at one place (unionLines(): common lines once) — the
///    changed side's first;
///  * lines added before it come first, unioned the same way with the changed side's lines
///    before its last `base.size()` lines — the added side's first, so they never move behind a
///    line that was there before them;
///  * an addition the changed side already ends (starts) with is taken once;
///  * lines added on both sides only around lines changed in place (as many lines as before).
/// A region the changed side emptied stays a conflict: everything around it would go with it.
std::optional<QStringList> besideChange(const QStringList &base, const QStringList &changed, const AddedLines &added)
{
    if (changed.isEmpty())
        return std::nullopt;
    const qsizetype n = base.size();
    if (added.before.isEmpty()) {
        const qsizetype na = added.after.size();
        if (changed.size() >= na && changed.last(na) == added.after)
            return changed;
        const qsizetype core = std::min(n, changed.size());
        return changed.first(core) + unionLines(changed.mid(core), added.after);
    }
    if (added.after.isEmpty()) {
        const qsizetype nb = added.before.size();
        if (changed.size() >= nb && changed.first(nb) == added.before)
            return changed;
        const qsizetype head = std::max<qsizetype>(0, changed.size() - n);
        return unionLines(added.before, changed.first(head)) + changed.mid(head);
    }
    if (changed.size() != n)
        return std::nullopt; // where the changed side's own additions belong is not known
    return added.before + changed + added.after;
}

/// Resolve one diff3 conflict region, or std::nullopt when it is a real conflict.
std::optional<QStringList> resolveRegion(const QStringList &base, const QStringList &local, const QStringList &remote)
{
    if (local == remote)
        return local;
    if (local == base)
        return remote;
    if (remote == base)
        return local;
    if (base.isEmpty())
        return unionLines(local, remote);
    // One side only added lines next to lines the other side changed — diff3 calls adjacent
    // changes a conflict, but both apply: the last block of a journal edited in the app while an
    // agent (or another device) appended to the journal.
    if (const std::optional<AddedLines> added = addedAround(base, remote)) {
        if (const std::optional<QStringList> merged = besideChange(base, local, *added))
            return merged;
    }
    if (const std::optional<AddedLines> added = addedAround(base, local)) {
        if (const std::optional<QStringList> merged = besideChange(base, remote, *added))
            return merged;
    }
    return std::nullopt;
}

void appendOk(QList<MergeHunk> &hunks, const QStringList &lines)
{
    if (lines.isEmpty())
        return;
    if (!hunks.isEmpty() && hunks.last().kind == MergeHunk::Kind::Ok)
        hunks.last().lines += lines;
    else
        hunks.append(MergeHunk::ok(lines));
}

QList<MergeHunk> okHunks(const QString &text)
{
    const SplitText split = splitText(text);
    return split.lines.isEmpty() ? QList<MergeHunk>() : QList<MergeHunk>{MergeHunk::ok(split.lines)};
}

/// Three-way merge of the "ends with a line break" flag; an empty side has no opinion.
bool mergeFinalNewline(const SplitText &o, const SplitText &a, const SplitText &b)
{
    if (a.lines.isEmpty())
        return b.finalNewline;
    if (b.lines.isEmpty())
        return a.finalNewline;
    return a.finalNewline == o.finalNewline ? b.finalNewline : a.finalNewline;
}

std::optional<QJsonObject> parseObject(const QString &text)
{
    if (text.trimmed().isEmpty())
        return QJsonObject();
    QString json = text;
    if (json.startsWith(QChar(0xFEFF)))
        json.remove(0, 1);
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    return doc.object();
}

} // namespace

MergeHunk MergeHunk::ok(const QStringList &lines)
{
    MergeHunk h;
    h.kind = Kind::Ok;
    h.lines = lines;
    return h;
}

MergeHunk MergeHunk::conflict(const QStringList &base, const QStringList &local, const QStringList &remote)
{
    MergeHunk h;
    h.kind = Kind::Conflict;
    h.base = base;
    h.local = local;
    h.remote = remote;
    return h;
}

SplitText splitText(const QString &text)
{
    SplitText out;
    if (text.isEmpty())
        return out;
    const qsizetype lf = text.count(u'\n');
    const qsizetype crlf = text.count(u"\r\n"_s);
    if (lf > 0)
        out.eol = crlf * 2 >= lf ? u"\r\n"_s : u"\n"_s;
    QString normalized = text;
    normalized.replace(u"\r\n"_s, u"\n"_s);
    out.finalNewline = normalized.endsWith(u'\n');
    if (out.finalNewline)
        normalized.chop(1);
    out.lines = normalized.split(u'\n');
    return out;
}

QString joinLines(const QStringList &lines, const QString &eol, bool finalNewline)
{
    if (lines.isEmpty())
        return {};
    QString out = lines.join(eol);
    if (finalNewline)
        out += eol;
    return out;
}

QStringList unionLines(const QStringList &local, const QStringList &remote)
{
    const QList<CommPart> parts = diffComm(local, remote);
    // Local lines outside the common subsequence: a remote line equal to one of them is a duplicate.
    QHash<QString, int> localOnly;
    for (const CommPart &part : parts) {
        if (!part.common) {
            for (const QString &line : part.a)
                ++localOnly[line];
        }
    }
    QStringList out;
    for (const CommPart &part : parts) {
        if (part.common) {
            out += part.commonLines;
            continue;
        }
        out += part.a;
        for (const QString &line : part.b) {
            const auto it = localOnly.find(line);
            if (it != localOnly.end() && *it > 0)
                --*it;
            else
                out.append(line);
        }
    }
    return out;
}

MergeResult merge3(const QString &base, const QString &local, const QString &remote)
{
    if (local == remote || base == remote)
        return MergeResult{local, true, okHunks(local)};
    if (base == local)
        return MergeResult{remote, true, okHunks(remote)};

    const SplitText o = splitText(base);
    const SplitText a = splitText(local);
    const SplitText b = splitText(remote);
    MergeResult result;
    for (const Diff3Region &region : diff3Regions(a.lines, o.lines, b.lines)) {
        if (!region.conflict) {
            appendOk(result.hunks, region.ok);
            continue;
        }
        if (const std::optional<QStringList> resolved = resolveRegion(region.o, region.a, region.b)) {
            appendOk(result.hunks, *resolved);
        } else {
            result.clean = false;
            result.hunks.append(MergeHunk::conflict(region.o, region.a, region.b));
        }
    }
    QStringList lines;
    for (const MergeHunk &h : std::as_const(result.hunks))
        lines += h.kind == MergeHunk::Kind::Ok ? h.lines : h.local;
    QString eol = !a.eol.isEmpty() ? a.eol : !b.eol.isEmpty() ? b.eol : !o.eol.isEmpty() ? o.eol : u"\n"_s;
    result.text = joinLines(lines, eol, mergeFinalNewline(o, a, b));
    return result;
}

std::optional<QString> mergeJson3(const QString &base, const QString &local, const QString &remote)
{
    const std::optional<QJsonObject> l = parseObject(local);
    const std::optional<QJsonObject> r = parseObject(remote);
    if (!l || !r)
        return std::nullopt;
    const QJsonObject o = parseObject(base).value_or(QJsonObject());
    QStringList keys = l->keys();
    for (const QString &key : r->keys()) {
        if (!l->contains(key))
            keys.append(key);
    }
    QJsonObject out;
    for (const QString &key : std::as_const(keys)) {
        const bool inL = l->contains(key);
        const bool inR = r->contains(key);
        const bool inO = o.contains(key);
        const bool localUnchanged = inL == inO && (!inL || l->value(key) == o.value(key));
        const bool present = localUnchanged ? inR : inL;
        if (present)
            out.insert(key, localUnchanged ? r->value(key) : l->value(key));
    }
    QString json = QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Indented));
    if (!json.endsWith(u'\n'))
        json += u'\n';
    return json;
}

} // namespace brulion::sync
