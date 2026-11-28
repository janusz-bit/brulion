// The patch parsers of gitcli.h: one file's unified diff into hunks, the hunks that change a range
// of lines, a patch of some of them for `git apply`, git's conflict markers.
#include "gitcli/gitcli.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::gitcli {

namespace {

/// "@@ -a[,b] +c[,d] @@[ section]": false when it does not read.
bool parseHunkHeader(QByteArrayView line, Hunk &h)
{
    if (!line.startsWith("@@ -"))
        return false;
    const qsizetype close = line.indexOf(" @@", 4);
    if (close < 0)
        return false;
    const QByteArrayView ranges = line.sliced(4, close - 4);
    const qsizetype plus = ranges.indexOf(" +");
    if (plus < 0)
        return false;
    const auto range = [](QByteArrayView text, int *start, int *count) {
        const qsizetype comma = text.indexOf(',');
        bool ok = false;
        *start = (comma < 0 ? text : text.first(comma)).toInt(&ok);
        if (!ok || *start < 0)
            return false;
        if (comma < 0) {
            *count = 1;
            return true;
        }
        *count = text.sliced(comma + 1).toInt(&ok);
        return ok && *count >= 0;
    };
    if (!range(ranges.first(plus), &h.oldStart, &h.oldLines) || !range(ranges.sliced(plus + 2), &h.newStart, &h.newLines))
        return false;
    QByteArrayView section = line.sliced(close + 3);
    if (section.startsWith(' '))
        section = section.sliced(1);
    h.section = section.toByteArray();
    return true;
}

/// The lines the hunk changes on each side (Hunk::oldFirst …).
void markChanged(Hunk &h)
{
    int o = h.oldLines == 0 ? h.oldStart + 1 : h.oldStart;
    int n = h.newLines == 0 ? h.newStart + 1 : h.newStart;
    const auto mark = [](int line, int *first, int *last) {
        if (*first == 0 || line < *first)
            *first = line;
        if (line > *last)
            *last = line;
    };
    // (A line replaced — deleted, then inserted — counts at the deleted line on the old side and
    // at the inserted one on the new side.)
    char previous = ' ';
    for (const QByteArray &line : std::as_const(h.lines)) {
        const char kind = line.isEmpty() ? ' ' : line.at(0);
        if (kind == '-') {
            mark(o, &h.oldFirst, &h.oldLast);
            mark(previous == '+' ? n - 1 : n, &h.newFirst, &h.newLast);
            ++o;
        } else if (kind == '+') {
            mark(n, &h.newFirst, &h.newLast);
            mark(previous == '-' ? o - 1 : o, &h.oldFirst, &h.oldLast);
            ++n;
        } else if (kind == ' ') {
            ++o;
            ++n;
        }
        if (kind != '\\')
            previous = kind == '+' || kind == '-' ? (previous == '-' && kind == '+' ? '-' : kind) : ' ';
    }
}

} // namespace

FilePatch parsePatch(QByteArrayView data)
{
    FilePatch patch;
    QList<QByteArrayView> lines;
    qsizetype start = 0;
    while (start < data.size()) {
        const qsizetype end = data.indexOf('\n', start);
        if (end < 0) {
            lines.append(data.sliced(start));
            break;
        }
        lines.append(data.sliced(start, end - start));
        start = end + 1;
    }
    qsizetype i = 0;
    bool seenDiff = false;
    for (; i < lines.size(); ++i) {
        const QByteArrayView line = lines.at(i);
        if (line.startsWith("@@ "))
            break;
        if (line.startsWith("diff --git ")) {
            if (seenDiff)
                return patch; // (a second file: only the first one's)
            seenDiff = true;
        }
        if (line.startsWith("Binary files ") || line.startsWith("GIT binary patch"))
            patch.binary = true;
        patch.header += line.toByteArray() + '\n';
    }
    while (i < lines.size()) {
        const QByteArrayView line = lines.at(i);
        if (line.startsWith("diff --git "))
            break;
        Hunk h;
        if (!parseHunkHeader(line, h)) {
            ++i;
            continue;
        }
        ++i;
        int oldLeft = h.oldLines;
        int newLeft = h.newLines;
        bool fits = true;
        while (i < lines.size() && (oldLeft > 0 || newLeft > 0)) {
            const QByteArrayView l = lines.at(i);
            const char kind = l.isEmpty() ? ' ' : l.at(0);
            if (kind == ' ' && oldLeft > 0 && newLeft > 0) {
                --oldLeft;
                --newLeft;
            } else if (kind == '-' && oldLeft > 0) {
                --oldLeft;
            } else if (kind == '+' && newLeft > 0) {
                --newLeft;
            } else if (kind != '\\') {
                fits = false;
                break;
            }
            h.lines.append(l.isEmpty() ? QByteArray(" ") : l.toByteArray());
            ++i;
        }
        // "\ No newline at end of file" after the last line.
        while (i < lines.size() && lines.at(i).startsWith('\\')) {
            h.lines.append(lines.at(i).toByteArray());
            ++i;
        }
        if (!fits || oldLeft > 0 || newLeft > 0)
            continue; // (counts that do not add up: a truncated or garbled hunk)
        markChanged(h);
        patch.hunks.append(h);
    }
    return patch;
}

QList<int> hunksTouching(const FilePatch &patch, int first, int last, bool newSide)
{
    QList<int> out;
    if (last < first)
        std::swap(first, last);
    for (int i = 0; i < patch.hunks.size(); ++i) {
        const Hunk &h = patch.hunks.at(i);
        const int a = newSide ? h.newFirst : h.oldFirst;
        const int b = newSide ? h.newLast : h.oldLast;
        if (a > 0 && a <= last && first <= b)
            out.append(i);
    }
    return out;
}

namespace {

/// A line of a hunk being cut: as written, chosen or not ("\\ No newline" markers ride with theirs).
struct CutLine {
    QByteArray line;
    bool chosen = false;
    QList<QByteArray> markers;
};

/// One block of changes (its deletions and insertions) written to `out`, pair by pair — the k-th
/// deletion, then the k-th insertion — so a change not chosen keeps its place beside the chosen ones:
/// forward a deletion not chosen is context and an insertion not chosen goes, reversed the other way.
void flushBlock(QList<CutLine> &dels, QList<CutLine> &ins, bool reverse, QList<QByteArray> &out, bool *changed)
{
    const auto put = [&out](const CutLine &l, const QByteArray &line) {
        out << line;
        out += l.markers;
    };
    for (qsizetype k = 0; k < std::max(dels.size(), ins.size()); ++k) {
        if (k < dels.size()) {
            const CutLine &d = dels.at(k);
            if (d.chosen)
                put(d, d.line);
            else if (!reverse)
                put(d, ' ' + d.line.mid(1));
            *changed = *changed || d.chosen;
        }
        if (k < ins.size()) {
            const CutLine &i = ins.at(k);
            if (i.chosen)
                put(i, i.line);
            else if (reverse)
                put(i, ' ' + i.line.mid(1));
            *changed = *changed || i.chosen;
        }
    }
    dels.clear();
    ins.clear();
}

/// One hunk cut to its chosen changes (linesOnly()); `changed` false when none is left.
Hunk cutHunk(const Hunk &hunk, int oldFirst, int oldLast, int newFirst, int newLast, bool reverse, bool *changed)
{
    Hunk out = hunk;
    out.lines.clear();
    int oldNo = hunk.oldStart, newNo = hunk.newStart;
    QList<CutLine> dels, ins;
    QList<CutLine> *last = nullptr; // (the list whose last line a "\\ No newline" belongs to)
    *changed = false;
    for (const QByteArray &line : hunk.lines) {
        const char kind = line.isEmpty() ? ' ' : line.at(0);
        if (kind == '\\') {
            if (last && !last->isEmpty())
                last->last().markers << line;
            else
                out.lines << line;
            continue;
        }
        if (kind == '-') {
            dels.append({line, oldFirst > 0 && oldNo >= oldFirst && oldNo <= oldLast, {}});
            last = &dels;
        } else if (kind == '+') {
            ins.append({line, newFirst > 0 && newNo >= newFirst && newNo <= newLast, {}});
            last = &ins;
        } else {
            flushBlock(dels, ins, reverse, out.lines, changed);
            out.lines << line;
            last = nullptr;
        }
        oldNo += kind != '+';
        newNo += kind != '-';
    }
    flushBlock(dels, ins, reverse, out.lines, changed);
    const auto counts = [&out](char side) {
        return int(std::ranges::count_if(out.lines, [side](const QByteArray &l) {
            return l.isEmpty() || l.startsWith(' ') || l.startsWith(side);
        }));
    };
    out.oldLines = counts('-');
    out.newLines = counts('+');
    return out;
}

} // namespace

FilePatch linesOnly(const FilePatch &patch, int oldFirst, int oldLast, int newFirst, int newLast, bool reverse)
{
    FilePatch out = patch;
    out.hunks.clear();
    for (const Hunk &hunk : patch.hunks) {
        bool changed = false;
        const Hunk cut = cutHunk(hunk, oldFirst, oldLast, newFirst, newLast, reverse, &changed);
        if (changed)
            out.hunks << cut;
    }
    return out;
}

QByteArray patchOf(const FilePatch &patch, const QList<int> &indexes, bool reverse)
{
    QList<int> chosen;
    for (const int i : indexes) {
        if (i >= 0 && i < patch.hunks.size() && !chosen.contains(i))
            chosen.append(i);
    }
    if (chosen.isEmpty() || patch.header.isEmpty())
        return {};
    std::sort(chosen.begin(), chosen.end());
    QByteArray out = patch.header;
    // The side the patch is applied to keeps its starts (`from`); the other side's start is the
    // hunk's own offset moved by the chosen hunks before it only (and by one for a side with no
    // lines, which git counts from the line before) — the hunks left out move nothing.
    int movedByAll = 0;
    int movedByChosen = 0;
    int next = 0;
    for (int i = 0; i < patch.hunks.size(); ++i) {
        const Hunk &h = patch.hunks.at(i);
        const int fromStart = reverse ? h.newStart : h.oldStart;
        const int toStart = reverse ? h.oldStart : h.newStart;
        const int grows = reverse ? h.oldLines - h.newLines : h.newLines - h.oldLines;
        const int own = (toStart - fromStart) - movedByAll;
        movedByAll += grows;
        if (next >= chosen.size() || chosen.at(next) != i)
            continue;
        ++next;
        const int start = fromStart + own + movedByChosen;
        movedByChosen += grows;
        const int oldStart = reverse ? start : h.oldStart;
        const int newStart = reverse ? h.newStart : start;
        out += "@@ -" + QByteArray::number(oldStart) + ',' + QByteArray::number(h.oldLines) + " +"
               + QByteArray::number(newStart) + ',' + QByteArray::number(h.newLines) + " @@";
        if (!h.section.isEmpty())
            out += ' ' + h.section;
        out += '\n';
        for (const QByteArray &line : h.lines)
            out += line + '\n';
    }
    return out;
}

bool hasConflictMarkers(QByteArrayView data)
{
    qsizetype start = 0;
    while (start < data.size()) {
        qsizetype end = data.indexOf('\n', start);
        if (end < 0)
            end = data.size();
        QByteArrayView line = data.sliced(start, end - start);
        if (line.endsWith('\r'))
            line.chop(1);
        if ((line.startsWith("<<<<<<<") || line.startsWith(">>>>>>>"))
            && (line.size() == 7 || line.at(7) == ' '))
            return true;
        start = end + 1;
    }
    return false;
}

} // namespace brulion::gitcli
