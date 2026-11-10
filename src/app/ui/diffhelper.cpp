#include "diffhelper.h"

#include "text/text.h"

#include <QHash>

#include <algorithm>
#include <functional>
#include <vector>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

/// Myers' move into diagonal `k` at step `d`, given `furthest[k]`, the furthest x on each
/// diagonal after step d - 1: down from diagonal k + 1 (an insertion), else right from k - 1
/// (a deletion).
bool movesDown(const int *furthest, int k, int d)
{
    return k == -d || (k != d && furthest[k - 1] < furthest[k + 1]);
}

/// Myers' greedy O(ND) algorithm on a[0..n) / b[0..m) (no common prefix/suffix expected).
/// Appends ops with indices shifted by `offset`. Returns false when the edit distance exceeds
/// `maxCost` (nothing appended then).
bool myers(const int *a, int n, const int *b, int m, int offset, int maxCost, QList<DiffOp> &out)
{
    const int limit = std::min(n + m, maxCost);
    std::vector<int> v(size_t(limit) * 2 + 3, 0);
    // furthest[k]: the furthest x on diagonal k (-limit - 1 … limit + 1).
    int *const furthest = v.data() + limit + 1;
    std::vector<std::vector<int>> trace; // trace[d][k + d] = furthest[k] after step d
    int found = -1;
    for (int d = 0; d <= limit && found < 0; ++d) {
        for (int k = -d; k <= d; k += 2) {
            int x = movesDown(furthest, k, d) ? furthest[k + 1] : furthest[k - 1] + 1;
            int y = x - k;
            while (x < n && y < m && a[x] == b[y]) {
                ++x;
                ++y;
            }
            furthest[k] = x;
            if (x >= n && y >= m) {
                found = d;
                break;
            }
        }
        trace.emplace_back(furthest - d, furthest + d + 1);
    }
    if (found < 0)
        return false;

    QList<DiffOp> reversed;
    int x = n;
    int y = m;
    for (int d = found; d > 0; --d) {
        const int *const before = trace[size_t(d) - 1].data() + (d - 1); // furthest[] after step d - 1
        const int k = x - y;
        const bool down = movesDown(before, k, d);
        const int prevK = down ? k + 1 : k - 1;
        const int prevX = before[prevK];
        const int prevY = prevX - prevK;
        while (x > prevX && y > prevY) {
            reversed.append({DiffOp::Equal, offset + x - 1, offset + y - 1});
            --x;
            --y;
        }
        if (down)
            reversed.append({DiffOp::Insert, -1, offset + y - 1});
        else
            reversed.append({DiffOp::Delete, offset + x - 1, -1});
        x = prevX;
        y = prevY;
    }
    while (x > 0 && y > 0) {
        reversed.append({DiffOp::Equal, offset + x - 1, offset + y - 1});
        --x;
        --y;
    }
    std::ranges::reverse(reversed);
    out.append(reversed);
    return true;
}

/// Within each run of non-equal ops, put deletions before insertions (stable).
void orderRegions(QList<DiffOp> &ops)
{
    const auto isEqual = [](DiffOp op) { return op.kind == DiffOp::Equal; };
    for (auto it = ops.begin(); it != ops.end();) {
        const auto run = std::find_if_not(it, ops.end(), isEqual);
        it = std::find_if(run, ops.end(), isEqual);
        std::stable_partition(run, it, [](DiffOp op) { return op.kind == DiffOp::Delete; });
    }
}

/// Lines of a text; a final empty line without terminator (the "line" after the last
/// terminator, or the only line of an empty text) is dropped.
QList<text::Line> linesOf(QStringView s)
{
    QList<text::Line> lines = text::splitLines(s);
    if (!lines.isEmpty() && lines.last().text.isEmpty() && lines.last().eol.isEmpty())
        lines.removeLast();
    return lines;
}

/// A stable small integer for the string (`proj`) of each of `items`: its id in `ids`, a new
/// one (the count so far) for a string seen first.
template<typename List, typename Proj = std::identity>
QList<int> intern(const List &items, QHash<QString, int> &ids, Proj proj = {})
{
    QList<int> out;
    out.reserve(items.size());
    for (const auto &item : items)
        out.append(*ids.tryEmplace(std::invoke(proj, item), int(ids.size())).iterator);
    return out;
}

/// Word-level tokens: runs of word characters, runs of whitespace, single other characters.
QStringList tokenize(const QString &s)
{
    QStringList tokens;
    qsizetype i = 0;
    while (i < s.size()) {
        qsizetype j = i + 1;
        if (text::isWordChar(s.at(i)) || s.at(i).isSurrogate()) {
            while (j < s.size() && (text::isWordChar(s.at(j)) || s.at(j).isSurrogate()))
                ++j;
        } else if (s.at(i).isSpace()) {
            while (j < s.size() && s.at(j).isSpace())
                ++j;
        }
        tokens.append(s.mid(i, j - i));
        i = j;
    }
    return tokens;
}

struct Segments {
    QVariantList oldSegs;
    QVariantList newSegs;
};

QVariantList mergeSegments(const QStringList &tokens, QList<bool> changed)
{
    // A lone unchanged whitespace token between two changes reads better as part of the change.
    for (qsizetype i = 1; i + 1 < tokens.size(); ++i) {
        if (!changed[i] && changed[i - 1] && changed[i + 1] && tokens.at(i).trimmed().isEmpty())
            changed[i] = true;
    }
    QVariantList out;
    QString current;
    bool currentChanged = false;
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        if (i > 0 && changed[i] != currentChanged) {
            out.append(QVariantMap{{u"text"_s, current}, {u"changed"_s, currentChanged}});
            current.clear();
        }
        currentChanged = changed[i];
        current += tokens.at(i);
    }
    if (!current.isEmpty())
        out.append(QVariantMap{{u"text"_s, current}, {u"changed"_s, currentChanged}});
    return out;
}

/// Word-level segments of a changed line pair; empty lists when the lines share too little
/// (highlighting confetti over a rewritten line helps nobody).
Segments wordSegments(const QString &oldLine, const QString &newLine)
{
    const QStringList oldTokens = tokenize(oldLine);
    const QStringList newTokens = tokenize(newLine);
    QHash<QString, int> ids;
    const QList<int> a = intern(oldTokens, ids);
    const QList<int> b = intern(newTokens, ids);
    const QList<DiffOp> ops = diffSequences(a, b, 400);
    QList<bool> oldChanged(oldTokens.size(), true);
    QList<bool> newChanged(newTokens.size(), true);
    qsizetype same = 0;
    for (const DiffOp &op : ops) {
        if (op.kind == DiffOp::Equal) {
            oldChanged[op.oldIndex] = false;
            newChanged[op.newIndex] = false;
            if (!oldTokens.at(op.oldIndex).trimmed().isEmpty())
                same += oldTokens.at(op.oldIndex).size();
        }
    }
    const qsizetype longest = std::max(oldLine.trimmed().size(), newLine.trimmed().size());
    if (longest == 0 || same * 5 < longest * 2) // less than 40 % in common
        return {};
    return {mergeSegments(oldTokens, std::move(oldChanged)), mergeSegments(newTokens, std::move(newChanged))};
}

/// A changed region: indices of deleted old lines and inserted new lines.
struct Region {
    QList<int> deleted;
    QList<int> inserted;
};

/// Walk the ops, calling onEqual(oldIndex, newIndex) for unchanged lines and onRegion(region)
/// for each changed region.
template<typename EqualFn, typename RegionFn>
void walk(const QList<DiffOp> &ops, EqualFn onEqual, RegionFn onRegion)
{
    Region region;
    auto flush = [&] {
        if (!region.deleted.isEmpty() || !region.inserted.isEmpty())
            onRegion(region);
        region = {};
    };
    for (const DiffOp &op : ops) {
        switch (op.kind) {
        case DiffOp::Equal:
            flush();
            onEqual(op.oldIndex, op.newIndex);
            break;
        case DiffOp::Delete: region.deleted.append(op.oldIndex); break;
        case DiffOp::Insert: region.inserted.append(op.newIndex); break;
        }
    }
    flush();
}

/// Collapse long unchanged runs into "skip" rows, keeping `context` lines around changes.
QVariantList collapse(const QVariantList &rows, int context)
{
    if (context < 0)
        return rows;
    const auto isEqual = [](const QVariant &row) { return row.toMap().value(u"kind"_s).toString() == u"equal"; };
    QVariantList out;
    const qsizetype n = rows.size();
    for (qsizetype i = 0; i < n;) {
        if (!isEqual(rows.at(i))) {
            out.append(rows.at(i));
            ++i;
            continue;
        }
        qsizetype j = i;
        while (j < n && isEqual(rows.at(j)))
            ++j;
        const qsizetype keepBefore = i == 0 ? 0 : context; // after the previous change
        const qsizetype keepAfter = j == n ? 0 : context;  // before the next change
        const qsizetype hidden = (j - i) - keepBefore - keepAfter;
        if (hidden < 2) {
            out.append(rows.sliced(i, j - i));
        } else {
            out.append(rows.sliced(i, keepBefore));
            const QVariantMap first = rows.at(i + keepBefore).toMap();
            out.append(QVariantMap{
                    {u"kind"_s, u"skip"_s},
                    {u"count"_s, hidden},
                    {u"oldStart"_s, first.value(u"oldNo"_s)},
                    {u"newStart"_s, first.value(u"newNo"_s)},
                    {u"oldNo"_s, 0},
                    {u"newNo"_s, 0},
            });
            out.append(rows.sliced(j - keepAfter, keepAfter));
        }
        i = j;
    }
    return out;
}

/// `text` with every line break ("\r\n", "\r" or "\n") written as `eol`.
QString withEol(QString text, const QString &eol)
{
    if (eol == u"\n" && !text.contains(u'\r'))
        return text;
    text.replace(u"\r\n"_s, u"\n"_s);
    text.replace(u'\r', u'\n');
    if (eol != u"\n")
        text.replace(u"\n"_s, eol);
    return text;
}

bool hasLineBreak(const QString &s)
{
    return s.contains(u'\n') || s.contains(u'\r');
}

void appendPiece(QString &out, const QString &piece, const QString &eol)
{
    if (piece.isEmpty())
        return;
    if (!out.isEmpty() && !out.endsWith(u'\n') && !out.endsWith(u'\r'))
        out += eol;
    out += piece;
}

/// The lines of `lines` at `indexes`, terminators included.
QString joinedLines(const QList<text::Line> &lines, const QList<int> &indexes)
{
    QString out;
    for (int i : indexes)
        out += lines.at(i).text + lines.at(i).eol;
    return out;
}

} // namespace

QList<DiffOp> diffSequences(const QList<int> &a, const QList<int> &b, int maxCost)
{
    QList<DiffOp> ops;
    const int n = int(a.size());
    const int m = int(b.size());
    int pre = 0;
    while (pre < n && pre < m && a.at(pre) == b.at(pre))
        ++pre;
    int suf = 0;
    while (suf < n - pre && suf < m - pre && a.at(n - 1 - suf) == b.at(m - 1 - suf))
        ++suf;
    ops.reserve(std::max(n, m) + 8);
    for (int i = 0; i < pre; ++i)
        ops.append({DiffOp::Equal, i, i});
    const int an = n - pre - suf;
    const int bm = m - pre - suf;
    // Only deletions or insertions in the middle, or more changes than maxCost: one block of each.
    if (an == 0 || bm == 0
        || !myers(a.constData() + pre, an, b.constData() + pre, bm, pre, std::max(1, maxCost), ops)) {
        for (int i = 0; i < an; ++i)
            ops.append({DiffOp::Delete, pre + i, -1});
        for (int j = 0; j < bm; ++j)
            ops.append({DiffOp::Insert, -1, pre + j});
    }
    for (int k = 0; k < suf; ++k)
        ops.append({DiffOp::Equal, n - suf + k, m - suf + k});
    orderRegions(ops);
    return ops;
}

QList<DiffOp> diffLines(QStringView oldText, QStringView newText)
{
    QHash<QString, int> ids;
    const QList<int> a = intern(linesOf(oldText), ids, &text::Line::text);
    const QList<int> b = intern(linesOf(newText), ids, &text::Line::text);
    return diffSequences(a, b);
}

DiffHelper::DiffHelper(QObject *parent) : QObject(parent) { }

QList<DiffOp> DiffHelper::cachedDiff(const QString &oldText, const QString &newText) const
{
    if (!m_cacheValid || oldText != m_cacheOld || newText != m_cacheNew) {
        m_cacheOps = diffLines(oldText, newText);
        m_cacheOld = oldText;
        m_cacheNew = newText;
        m_cacheValid = true;
    }
    return m_cacheOps;
}

QVariantList DiffHelper::unifiedRows(const QString &oldText, const QString &newText, int context) const
{
    const QList<text::Line> oldLines = linesOf(oldText);
    const QList<text::Line> newLines = linesOf(newText);
    QVariantList rows;
    walk(
            cachedDiff(oldText, newText),
            [&](int o, int n) {
                rows.append(QVariantMap{{u"kind"_s, u"equal"_s},
                                        {u"oldNo"_s, o + 1},
                                        {u"newNo"_s, n + 1},
                                        {u"text"_s, oldLines.at(o).text}});
            },
            [&](const Region &r) {
                const qsizetype pairs = std::min(r.deleted.size(), r.inserted.size());
                QList<Segments> segs;
                for (qsizetype p = 0; p < pairs; ++p)
                    segs.append(wordSegments(oldLines.at(r.deleted.at(p)).text, newLines.at(r.inserted.at(p)).text));
                for (qsizetype p = 0; p < r.deleted.size(); ++p) {
                    const int o = r.deleted.at(p);
                    rows.append(QVariantMap{{u"kind"_s, u"delete"_s},
                                            {u"oldNo"_s, o + 1},
                                            {u"newNo"_s, 0},
                                            {u"text"_s, oldLines.at(o).text},
                                            {u"segments"_s, p < pairs ? segs.at(p).oldSegs : QVariantList()}});
                }
                for (qsizetype p = 0; p < r.inserted.size(); ++p) {
                    const int n = r.inserted.at(p);
                    rows.append(QVariantMap{{u"kind"_s, u"insert"_s},
                                            {u"oldNo"_s, 0},
                                            {u"newNo"_s, n + 1},
                                            {u"text"_s, newLines.at(n).text},
                                            {u"segments"_s, p < pairs ? segs.at(p).newSegs : QVariantList()}});
                }
            });
    return collapse(rows, context);
}

QVariantList DiffHelper::splitRows(const QString &oldText, const QString &newText, int context) const
{
    const QList<text::Line> oldLines = linesOf(oldText);
    const QList<text::Line> newLines = linesOf(newText);
    QVariantList rows;
    walk(
            cachedDiff(oldText, newText),
            [&](int o, int n) {
                rows.append(QVariantMap{{u"kind"_s, u"equal"_s},
                                        {u"oldNo"_s, o + 1},
                                        {u"newNo"_s, n + 1},
                                        {u"left"_s, oldLines.at(o).text},
                                        {u"right"_s, newLines.at(n).text},
                                        {u"leftText"_s, oldLines.at(o).text},
                                        {u"rightText"_s, newLines.at(n).text}});
            },
            [&](const Region &r) {
                const qsizetype count = std::max(r.deleted.size(), r.inserted.size());
                for (qsizetype p = 0; p < count; ++p) {
                    const bool hasOld = p < r.deleted.size();
                    const bool hasNew = p < r.inserted.size();
                    const int o = hasOld ? r.deleted.at(p) : -1;
                    const int n = hasNew ? r.inserted.at(p) : -1;
                    const QString left = hasOld ? oldLines.at(o).text : QString();
                    const QString right = hasNew ? newLines.at(n).text : QString();
                    const Segments s = hasOld && hasNew ? wordSegments(left, right) : Segments();
                    rows.append(QVariantMap{
                            {u"kind"_s, hasOld && hasNew ? u"change"_s
                                        : hasOld         ? u"delete"_s
                                                         : u"insert"_s},
                            {u"oldNo"_s, o + 1},
                            {u"newNo"_s, n + 1},
                            {u"left"_s, left},
                            {u"right"_s, right},
                            {u"leftText"_s, left},
                            {u"rightText"_s, right},
                            {u"leftSegments"_s, s.oldSegs},
                            {u"rightSegments"_s, s.newSegs},
                    });
                }
            });
    return collapse(rows, context);
}

QVariantList DiffHelper::viewRows(const QString &oldText, const QString &newText, bool split, int context) const
{
    QVariantList rows = split ? splitRows(oldText, newText, context) : unifiedRows(oldText, newText, context);
    static const std::pair<QString, QVariant> defaults[] = {
            {u"kind"_s, QString()},
            {u"oldNo"_s, 0},
            {u"newNo"_s, 0},
            {u"count"_s, 0},
            {u"text"_s, QString()},
            {u"leftText"_s, QString()},
            {u"rightText"_s, QString()},
            {u"segments"_s, QVariantList()},
            {u"leftSegments"_s, QVariantList()},
            {u"rightSegments"_s, QVariantList()},
    };
    // Every row is a map: completed in place (no copy of it).
    for (QVariant &r : rows) {
        QVariantMap *row = get_if<QVariantMap>(&r);
        for (const auto &[key, value] : defaults) {
            if (row && !row->contains(key))
                row->insert(key, value);
        }
    }
    return rows;
}

QVariantMap DiffHelper::stats(const QString &oldText, const QString &newText) const
{
    const QList<DiffOp> ops = cachedDiff(oldText, newText);
    const auto count = [&ops](DiffOp::Kind kind) { return int(std::ranges::count(ops, kind, &DiffOp::kind)); };
    return {{u"added"_s, count(DiffOp::Insert)},
            {u"removed"_s, count(DiffOp::Delete)},
            {u"unchanged"_s, count(DiffOp::Equal)}};
}

QVariantList DiffHelper::conflictHunks(const QString &ours, const QString &theirs) const
{
    const QList<text::Line> oursLines = linesOf(ours);
    const QList<text::Line> theirsLines = linesOf(theirs);
    QVariantList hunks;
    QList<int> same; // the unchanged lines (of ours) since the last conflict
    auto flushSame = [&] {
        if (!same.isEmpty())
            hunks.append(QVariantMap{
                    {u"kind"_s, u"same"_s}, {u"text"_s, joinedLines(oursLines, same)}, {u"lines"_s, int(same.size())}});
        same.clear();
    };
    walk(
            cachedDiff(ours, theirs), [&](int o, int) { same.append(o); },
            [&](const Region &r) {
                flushSame();
                hunks.append(QVariantMap{
                        {u"kind"_s, u"conflict"_s},
                        {u"ours"_s, joinedLines(oursLines, r.deleted)},
                        {u"theirs"_s, joinedLines(theirsLines, r.inserted)},
                        {u"oursLine"_s, r.deleted.isEmpty() ? 0 : r.deleted.first() + 1},
                        {u"theirsLine"_s, r.inserted.isEmpty() ? 0 : r.inserted.first() + 1},
                });
            });
    flushSame();
    return hunks;
}

QString DiffHelper::resolveHunks(const QVariantList &hunks, const QVariantList &choices) const
{
    QString all;
    for (const QVariant &h : hunks) {
        const QVariantMap m = h.toMap();
        all += m.value(u"text"_s).toString() + m.value(u"ours"_s).toString();
    }
    const QString eol = text::detectEol(all);
    QString out;
    qsizetype conflict = 0;
    for (const QVariant &h : hunks) {
        const QVariantMap m = h.toMap();
        if (m.value(u"kind"_s).toString() != u"conflict") {
            appendPiece(out, m.value(u"text"_s).toString(), eol);
            continue;
        }
        const QVariantMap choice = conflict < choices.size() ? choices.at(conflict).toMap() : QVariantMap();
        ++conflict;
        const QString kind = choice.value(u"choice"_s, u"ours"_s).toString();
        const QString o = m.value(u"ours"_s).toString();
        const QString t = m.value(u"theirs"_s).toString();
        if (kind == u"theirs") {
            appendPiece(out, t, eol);
        } else if (kind == u"both") {
            appendPiece(out, o, eol);
            appendPiece(out, t, eol);
        } else if (kind == u"manual") {
            const QString sides = o + t;
            const QString pieceEol = hasLineBreak(sides) ? text::detectEol(sides) : eol;
            appendPiece(out, withEol(choice.value(u"text"_s).toString(), pieceEol), eol);
        } else {
            appendPiece(out, o, eol);
        }
    }
    return out;
}

QList<int> DiffHelper::conflictIndexes(const QVariantList &hunks) const
{
    QList<int> out;
    for (int i = 0; i < hunks.size(); ++i) {
        if (hunks.at(i).toMap().value(u"kind"_s).toString() == u"conflict")
            out.append(i);
    }
    return out;
}

QString DiffHelper::resolveHunksWith(const QVariantList &hunks, const QStringList &choices,
                                     const QStringList &texts) const
{
    QVariantList list;
    list.reserve(choices.size());
    for (qsizetype i = 0; i < choices.size(); ++i) {
        const QString &choice = choices.at(i);
        list.append(QVariantMap{{u"choice"_s, choice.isEmpty() ? u"ours"_s : choice},
                                {u"text"_s, i < texts.size() ? texts.at(i) : QString()}});
    }
    return resolveHunks(hunks, list);
}

QString DiffHelper::segmentsHtml(const QVariantList &segments, const QString &highlight) const
{
    QString out = u"<span style=\"white-space:pre-wrap\">"_s;
    const QString open = u"<span style=\"background-color:%1\">"_s.arg(highlight.toHtmlEscaped());
    for (const QVariant &s : segments) {
        const QVariantMap seg = s.toMap();
        const QString escaped = seg.value(u"text"_s).toString().toHtmlEscaped();
        if (seg.value(u"changed"_s).toBool())
            out += open + escaped + u"</span>"_s;
        else
            out += escaped;
    }
    out += u"</span>"_s;
    return out;
}

} // namespace brulion::ui
