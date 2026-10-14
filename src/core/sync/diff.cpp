#include "sync/diff.h"

#include <QHash>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace brulion::sync {

namespace {

using Ids = std::vector<int>;

/// Map lines to integer ids; one shared map so equal lines get equal ids in every sequence.
Ids intern(const QStringList &lines, QHash<QString, int> &ids)
{
    Ids out;
    out.reserve(static_cast<size_t>(lines.size()));
    for (const QString &line : lines) {
        const auto it = ids.constFind(line);
        if (it != ids.cend()) {
            out.push_back(*it);
        } else {
            const int id = static_cast<int>(ids.size());
            ids.insert(line, id);
            out.push_back(id);
        }
    }
    return out;
}

struct DiffContext {
    const Ids &a;
    const Ids &b;
    std::vector<qsizetype> ma; ///< matched indexes in a, ascending
    std::vector<qsizetype> mb; ///< matched indexes in b, ascending
    qint64 budget;
};

using Split = std::pair<qsizetype, qsizetype>;

/// A split point on an optimal edit path of a[aLo,aHi) / b[bLo,bHi), found by running Myers'
/// search from both ends until the paths overlap (the "middle snake"). Absolute indexes, or
/// std::nullopt when the budget runs out (the range then counts as fully changed).
std::optional<Split> bisect(DiffContext &ctx, qsizetype aLo, qsizetype aHi, qsizetype bLo, qsizetype bHi)
{
    const Ids &a = ctx.a;
    const Ids &b = ctx.b;
    const qsizetype n = aHi - aLo;
    const qsizetype m = bHi - bLo;
    const qsizetype maxD = (n + m + 1) / 2;
    const qsizetype offset = maxD;
    const qsizetype size = 2 * maxD + 2;
    const auto at = [](std::vector<qsizetype> &v, qsizetype i) -> qsizetype & { return v[static_cast<size_t>(i)]; };
    std::vector<qsizetype> v1(static_cast<size_t>(size), -1);
    std::vector<qsizetype> v2(static_cast<size_t>(size), -1);
    at(v1, offset + 1) = 0;
    at(v2, offset + 1) = 0;
    const qsizetype delta = n - m;
    const bool front = delta % 2 != 0;
    qsizetype k1start = 0, k1end = 0, k2start = 0, k2end = 0;
    for (qsizetype d = 0; d < maxD; ++d) {
        if (ctx.budget <= 0)
            return std::nullopt;
        // Forward search.
        for (qsizetype k1 = -d + k1start; k1 <= d - k1end; k1 += 2) {
            const qsizetype k1o = offset + k1;
            qsizetype x1 = (k1 == -d || (k1 != d && at(v1, k1o - 1) < at(v1, k1o + 1))) ? at(v1, k1o + 1)
                                                                                      : at(v1, k1o - 1) + 1;
            qsizetype y1 = x1 - k1;
            const qsizetype snakeStart = x1;
            while (x1 < n && y1 < m && a[static_cast<size_t>(aLo + x1)] == b[static_cast<size_t>(bLo + y1)]) {
                ++x1;
                ++y1;
            }
            ctx.budget -= 1 + x1 - snakeStart;
            at(v1, k1o) = x1;
            if (x1 > n) {
                k1end += 2;
            } else if (y1 > m) {
                k1start += 2;
            } else if (front) {
                const qsizetype k2o = offset + delta - k1;
                if (k2o >= 0 && k2o < size && at(v2, k2o) != -1 && x1 >= n - at(v2, k2o))
                    return Split{aLo + x1, bLo + y1};
            }
        }
        // Reverse search.
        for (qsizetype k2 = -d + k2start; k2 <= d - k2end; k2 += 2) {
            const qsizetype k2o = offset + k2;
            qsizetype x2 = (k2 == -d || (k2 != d && at(v2, k2o - 1) < at(v2, k2o + 1))) ? at(v2, k2o + 1)
                                                                                      : at(v2, k2o - 1) + 1;
            qsizetype y2 = x2 - k2;
            const qsizetype snakeStart = x2;
            while (x2 < n && y2 < m
                   && a[static_cast<size_t>(aHi - x2 - 1)] == b[static_cast<size_t>(bHi - y2 - 1)]) {
                ++x2;
                ++y2;
            }
            ctx.budget -= 1 + x2 - snakeStart;
            at(v2, k2o) = x2;
            if (x2 > n) {
                k2end += 2;
            } else if (y2 > m) {
                k2start += 2;
            } else if (!front) {
                const qsizetype k1o = offset + delta - k2;
                if (k1o >= 0 && k1o < size && at(v1, k1o) != -1) {
                    const qsizetype x1 = at(v1, k1o);
                    const qsizetype y1 = offset + x1 - k1o;
                    if (x1 >= n - x2)
                        return Split{aLo + x1, bLo + y1};
                }
            }
        }
    }
    return std::nullopt;
}

/// Collect the matching lines of a[aLo,aHi) and b[bLo,bHi) into ctx.ma / ctx.mb, in order.
/// Iterative: a stack of pending ranges and pending suffix matches.
void diffRange(DiffContext &ctx, qsizetype aLo0, qsizetype aHi0, qsizetype bLo0, qsizetype bHi0)
{
    struct Task {
        bool emit;   ///< true: record `count` matches starting at (aLo, bLo)
        qsizetype aLo, aHi, bLo, bHi;
        qsizetype count;
    };
    const Ids &a = ctx.a;
    const Ids &b = ctx.b;
    const auto same = [&](qsizetype i, qsizetype j) { return a[static_cast<size_t>(i)] == b[static_cast<size_t>(j)]; };
    std::vector<Task> stack;
    stack.push_back(Task{false, aLo0, aHi0, bLo0, bHi0, 0});
    while (!stack.empty()) {
        const Task task = stack.back();
        stack.pop_back();
        if (task.emit) {
            for (qsizetype i = 0; i < task.count; ++i) {
                ctx.ma.push_back(task.aLo + i);
                ctx.mb.push_back(task.bLo + i);
            }
            continue;
        }
        qsizetype aLo = task.aLo, bLo = task.bLo;
        const qsizetype aHi = task.aHi, bHi = task.bHi;
        while (aLo < aHi && bLo < bHi && same(aLo, bLo)) {
            ctx.ma.push_back(aLo++);
            ctx.mb.push_back(bLo++);
        }
        qsizetype suffix = 0;
        while (aLo < aHi - suffix && bLo < bHi - suffix && same(aHi - 1 - suffix, bHi - 1 - suffix))
            ++suffix;
        const qsizetype aEnd = aHi - suffix;
        const qsizetype bEnd = bHi - suffix;
        // Processed in reverse push order: left part, right part, then the common suffix.
        if (suffix > 0)
            stack.push_back(Task{true, aEnd, 0, bEnd, 0, suffix});
        if (aLo < aEnd && bLo < bEnd) {
            const std::optional<Split> split = bisect(ctx, aLo, aEnd, bLo, bEnd);
            // A split at either corner would not shrink the problem: treat as fully changed.
            if (split && !(split->first == aLo && split->second == bLo) && !(split->first == aEnd && split->second == bEnd)) {
                stack.push_back(Task{false, split->first, aEnd, split->second, bEnd, 0});
                stack.push_back(Task{false, aLo, split->first, bLo, split->second, 0});
            }
        }
    }
}

struct Matches {
    std::vector<qsizetype> ma, mb;
};

Matches matchIds(const Ids &a, const Ids &b, qint64 budget)
{
    DiffContext ctx{a, b, {}, {}, budget};
    diffRange(ctx, 0, static_cast<qsizetype>(a.size()), 0, static_cast<qsizetype>(b.size()));
    return Matches{std::move(ctx.ma), std::move(ctx.mb)};
}

QList<DiffHunk> hunksFromMatches(const Matches &m, qsizetype aLength, qsizetype bLength)
{
    QList<DiffHunk> out;
    qsizetype pa = 0;
    qsizetype pb = 0;
    const size_t count = m.ma.size();
    for (size_t i = 0; i <= count; ++i) {
        const qsizetype ia = i < count ? m.ma[i] : aLength;
        const qsizetype ib = i < count ? m.mb[i] : bLength;
        if (ia > pa || ib > pb)
            out.append(DiffHunk{pa, ia - pa, pb, ib - pb});
        pa = ia + 1;
        pb = ib + 1;
    }
    return out;
}

/// list[start, end) — empty when end <= start (like JavaScript's slice).
QStringList slice(const QStringList &list, qsizetype start, qsizetype end)
{
    return end > start ? list.mid(start, end - start) : QStringList();
}

struct SideHunk {
    bool fromA;
    qsizetype oStart, oLength, sStart, sLength;
};

} // namespace

QList<DiffHunk> diffLines(const QStringList &a, const QStringList &b, qint64 budget)
{
    QHash<QString, int> ids;
    const Ids ia = intern(a, ids);
    const Ids ib = intern(b, ids);
    return hunksFromMatches(matchIds(ia, ib, budget), a.size(), b.size());
}

QList<CommPart> diffComm(const QStringList &a, const QStringList &b, qint64 budget)
{
    QList<CommPart> out;
    qsizetype pa = 0;
    for (const DiffHunk &h : diffLines(a, b, budget)) {
        if (h.aStart > pa) {
            CommPart part;
            part.common = true;
            part.commonLines = a.mid(pa, h.aStart - pa);
            out.append(part);
        }
        CommPart part;
        part.a = a.mid(h.aStart, h.aLength);
        part.b = b.mid(h.bStart, h.bLength);
        out.append(part);
        pa = h.aStart + h.aLength;
    }
    if (pa < a.size()) {
        CommPart part;
        part.common = true;
        part.commonLines = a.mid(pa);
        out.append(part);
    }
    return out;
}

QList<Diff3Region> diff3Regions(const QStringList &a, const QStringList &o, const QStringList &b, qint64 budget)
{
    QHash<QString, int> ids;
    const Ids io = intern(o, ids);
    const Ids ia = intern(a, ids);
    const Ids ib = intern(b, ids);
    const auto sideHunks = [&](bool fromA, const Ids &side, qsizetype length) {
        std::vector<SideHunk> out;
        for (const DiffHunk &h : hunksFromMatches(matchIds(io, side, budget), o.size(), length))
            out.push_back(SideHunk{fromA, h.aStart, h.aLength, h.bStart, h.bLength});
        return out;
    };
    std::vector<SideHunk> hunks = sideHunks(true, ia, a.size());
    const std::vector<SideHunk> remote = sideHunks(false, ib, b.size());
    hunks.insert(hunks.end(), remote.begin(), remote.end());
    // Stable: for equal starts the local hunk comes first (like node-diff3).
    std::stable_sort(hunks.begin(), hunks.end(),
                     [](const SideHunk &x, const SideHunk &y) { return x.oStart < y.oStart; });

    QList<Diff3Region> regions;
    QStringList okBuffer;
    const auto flushOk = [&] {
        if (!okBuffer.isEmpty()) {
            Diff3Region region;
            region.ok = std::move(okBuffer);
            regions.append(std::move(region));
        }
        okBuffer = QStringList();
    };
    qsizetype current = 0;
    // Running (side index - base index) offsets after the hunks processed so far.
    qsizetype dA = 0;
    qsizetype dB = 0;
    size_t i = 0;
    while (i < hunks.size()) {
        const SideHunk &first = hunks[i];
        const qsizetype regionStart = first.oStart;
        qsizetype regionEnd = first.oStart + first.oLength;
        size_t j = i + 1;
        while (j < hunks.size() && hunks[j].oStart <= regionEnd) {
            regionEnd = std::max(regionEnd, hunks[j].oStart + hunks[j].oLength);
            ++j;
        }
        okBuffer += slice(o, current, regionStart);
        const qsizetype aStart = regionStart + dA;
        const qsizetype bStart = regionStart + dB;
        for (size_t k = i; k < j; ++k) {
            const SideHunk &h = hunks[k];
            (h.fromA ? dA : dB) += h.sLength - h.oLength;
        }
        if (j - i == 1) {
            const QStringList &source = first.fromA ? a : b;
            okBuffer += source.mid(first.sStart, first.sLength);
        } else {
            const QStringList aContent = slice(a, aStart, regionEnd + dA);
            const QStringList bContent = slice(b, bStart, regionEnd + dB);
            if (aContent == bContent) {
                okBuffer += aContent;
            } else {
                flushOk();
                Diff3Region region;
                region.conflict = true;
                region.a = aContent;
                region.o = slice(o, regionStart, regionEnd);
                region.b = bContent;
                regions.append(std::move(region));
            }
        }
        current = regionEnd;
        i = j;
    }
    if (current < o.size())
        okBuffer += o.mid(current);
    flushOk();
    return regions;
}

} // namespace brulion::sync
