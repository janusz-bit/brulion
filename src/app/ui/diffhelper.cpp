#include "diffhelper.h"

#include "text/text.h"

#include <QHash>

#include <algorithm>
#include <vector>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

/// Myers' greedy O(ND) algorithm on a[0..n) / b[0..m) (no common prefix/suffix expected).
/// Appends ops with indices shifted by `aOff`/`bOff`. Returns false when the edit distance
/// exceeds `maxCost` (nothing appended then).
bool myers(const int *a, int n, const int *b, int m, int aOff, int bOff, int maxCost, QList<DiffOp> &out)
{
    const int max = n + m;
    const int limit = std::min(max, maxCost);
    const int offset = limit + 1;
    std::vector<int> v(size_t(2 * limit + 3), 0);
    std::vector<std::vector<int>> trace; // trace[d][k + d] = furthest x on diagonal k after step d
    int found = -1;
    for (int d = 0; d <= limit && found < 0; ++d) {
        for (int k = -d; k <= d; k += 2) {
            int x;
            if (k == -d || (k != d && v[size_t(offset + k - 1)] < v[size_t(offset + k + 1)]))
                x = v[size_t(offset + k + 1)]; // down: insertion
            else
                x = v[size_t(offset + k - 1)] + 1; // right: deletion
            int y = x - k;
            while (x < n && y < m && a[x] == b[y]) {
                ++x;
                ++y;
            }
            v[size_t(offset + k)] = x;
            if (x >= n && y >= m) {
                found = d;
                break;
            }
        }
        trace.emplace_back(v.begin() + (offset - d), v.begin() + (offset + d + 1));
    }
    if (found < 0)
        return false;

    QList<DiffOp> reversed;
    int x = n;
    int y = m;
    for (int d = found; d > 0; --d) {
        const std::vector<int> &prev = trace[size_t(d - 1)];
        auto at = [&prev, d](int k) { return prev[size_t(k + d - 1)]; };
        const int k = x - y;
        const bool down = k == -d || (k != d && at(k - 1) < at(k + 1));
        const int prevK = down ? k + 1 : k - 1;
        const int prevX = at(prevK);
        const int prevY = prevX - prevK;
        while (x > prevX && y > prevY) {
            reversed.append({DiffOp::Equal, aOff + x - 1, bOff + y - 1});
            --x;
            --y;
        }
        if (down)
            reversed.append({DiffOp::Insert, -1, bOff + y - 1});
        else
            reversed.append({DiffOp::Delete, aOff + x - 1, -1});
        x = prevX;
        y = prevY;
    }
    while (x > 0 && y > 0) {
        reversed.append({DiffOp::Equal, aOff + x - 1, bOff + y - 1});
        --x;
        --y;
    }
    std::reverse(reversed.begin(), reversed.end());
    out.append(reversed);
    return true;
}

/// Within each run of non-equal ops, put deletions before insertions (stable).
void orderRegions(QList<DiffOp> &ops)
{
    qsizetype i = 0;
    while (i < ops.size()) {
        if (ops.at(i).kind == DiffOp::Equal) {
            ++i;
            continue;
        }
        qsizetype j = i;
        while (j < ops.size() && ops.at(j).kind != DiffOp::Equal)
            ++j;
        std::stable_partition(ops.begin() + i, ops.begin() + j,
                              [](const DiffOp &op) { return op.kind == DiffOp::Delete; });
        i = j;
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

/// Stable small integer for `key` (QHash::emplace would overwrite existing values).
int internId(QHash<QString, int> &ids, const QString &key)
{
    auto it = ids.constFind(key);
    if (it == ids.cend())
        it = ids.insert(key, int(ids.size()));
    return it.value();
}

QList<int> intern(const QList<text::Line> &lines, QHash<QString, int> &ids)
{
    QList<int> out;
    out.reserve(lines.size());
    for (const text::Line &l : lines)
        out.append(internId(ids, l.text));
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
    QList<int> a;
    QList<int> b;
    for (const QString &t : oldTokens)
        a.append(internId(ids, t));
    for (const QString &t : newTokens)
        b.append(internId(ids, t));
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
    return {mergeSegments(oldTokens, oldChanged), mergeSegments(newTokens, newChanged)};
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
    QVariantList out;
    qsizetype i = 0;
    const qsizetype n = rows.size();
    while (i < n) {
        const QVariantMap row = rows.at(i).toMap();
        if (row.value(u"kind"_s).toString() != u"equal") {
            out.append(row);
            ++i;
            continue;
        }
        qsizetype j = i;
        while (j < n && rows.at(j).toMap().value(u"kind"_s).toString() == u"equal")
            ++j;
        const qsizetype keepBefore = i == 0 ? 0 : context; // after the previous change
        const qsizetype keepAfter = j == n ? 0 : context;  // before the next change
        const qsizetype hidden = (j - i) - keepBefore - keepAfter;
        if (hidden < 2) {
            for (qsizetype k = i; k < j; ++k)
                out.append(rows.at(k));
        } else {
            for (qsizetype k = i; k < i + keepBefore; ++k)
                out.append(rows.at(k));
            const QVariantMap first = rows.at(i + keepBefore).toMap();
            out.append(QVariantMap{
                    {u"kind"_s, u"skip"_s},
                    {u"count"_s, hidden},
                    {u"oldStart"_s, first.value(u"oldNo"_s)},
                    {u"newStart"_s, first.value(u"newNo"_s)},
                    {u"oldNo"_s, 0},
                    {u"newNo"_s, 0},
            });
            for (qsizetype k = j - keepAfter; k < j; ++k)
                out.append(rows.at(k));
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
    if (an == 0) {
        for (int j = 0; j < bm; ++j)
            ops.append({DiffOp::Insert, -1, pre + j});
    } else if (bm == 0) {
        for (int i = 0; i < an; ++i)
            ops.append({DiffOp::Delete, pre + i, -1});
    } else if (!myers(a.constData() + pre, an, b.constData() + pre, bm, pre, pre, std::max(1, maxCost), ops)) {
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
    const QList<int> a = intern(linesOf(oldText), ids);
    const QList<int> b = intern(linesOf(newText), ids);
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
                                        {u"right"_s, newLines.at(n).text}});
            },
            [&](const Region &r) {
                const qsizetype count = std::max(r.deleted.size(), r.inserted.size());
                for (qsizetype p = 0; p < count; ++p) {
                    const bool hasOld = p < r.deleted.size();
                    const bool hasNew = p < r.inserted.size();
                    const int o = hasOld ? r.deleted.at(p) : -1;
                    const int n = hasNew ? r.inserted.at(p) : -1;
                    QVariantMap row{
                            {u"kind"_s, hasOld && hasNew ? u"change"_s
                                        : hasOld         ? u"delete"_s
                                                         : u"insert"_s},
                            {u"oldNo"_s, o + 1},
                            {u"newNo"_s, n + 1},
                            {u"left"_s, hasOld ? oldLines.at(o).text : QString()},
                            {u"right"_s, hasNew ? newLines.at(n).text : QString()},
                            {u"leftSegments"_s, QVariantList()},
                            {u"rightSegments"_s, QVariantList()},
                    };
                    if (hasOld && hasNew) {
                        const Segments s = wordSegments(oldLines.at(o).text, newLines.at(n).text);
                        row.insert(u"leftSegments"_s, s.oldSegs);
                        row.insert(u"rightSegments"_s, s.newSegs);
                    }
                    rows.append(row);
                }
            });
    return collapse(rows, context);
}

QVariantMap DiffHelper::stats(const QString &oldText, const QString &newText) const
{
    int added = 0;
    int removed = 0;
    int unchanged = 0;
    for (const DiffOp &op : cachedDiff(oldText, newText)) {
        if (op.kind == DiffOp::Insert)
            ++added;
        else if (op.kind == DiffOp::Delete)
            ++removed;
        else
            ++unchanged;
    }
    return {{u"added"_s, added}, {u"removed"_s, removed}, {u"unchanged"_s, unchanged}};
}

QVariantList DiffHelper::conflictHunks(const QString &ours, const QString &theirs) const
{
    const QList<text::Line> oursLines = linesOf(ours);
    const QList<text::Line> theirsLines = linesOf(theirs);
    QVariantList hunks;
    QString same;
    int sameLines = 0;
    auto flushSame = [&] {
        if (sameLines > 0)
            hunks.append(QVariantMap{{u"kind"_s, u"same"_s}, {u"text"_s, same}, {u"lines"_s, sameLines}});
        same.clear();
        sameLines = 0;
    };
    walk(
            cachedDiff(ours, theirs),
            [&](int o, int) {
                same += oursLines.at(o).text + oursLines.at(o).eol;
                ++sameLines;
            },
            [&](const Region &r) {
                flushSame();
                QString o;
                QString t;
                for (int i : r.deleted)
                    o += oursLines.at(i).text + oursLines.at(i).eol;
                for (int i : r.inserted)
                    t += theirsLines.at(i).text + theirsLines.at(i).eol;
                hunks.append(QVariantMap{
                        {u"kind"_s, u"conflict"_s},
                        {u"ours"_s, o},
                        {u"theirs"_s, t},
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
