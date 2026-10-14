// Line diff (Myers with budget) and diff3 regions.
#include "sync/diff.h"

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>
#include <random>
#include <vector>

using namespace brulion;
using namespace brulion::sync;
using namespace Qt::StringLiterals;

namespace {

/// Deterministic pseudo-random numbers (mt19937's raw output is specified by the standard).
class Random {
public:
    explicit Random(unsigned seed)
        : m_engine(seed)
    {
    }
    /// Uniform-ish integer in [0, n).
    int below(int n) { return n <= 0 ? 0 : static_cast<int>(m_engine() % static_cast<unsigned>(n)); }
    /// True with probability p (in percent).
    bool chance(int percent) { return below(100) < percent; }

private:
    std::mt19937 m_engine;
};

QStringList randomLines(Random &r, int n, const QStringList &alphabet)
{
    QStringList out;
    for (int i = 0; i < n; ++i)
        out.append(alphabet.at(r.below(static_cast<int>(alphabet.size()))));
    return out;
}

/// Matched index pairs implied by hunks (lines between hunks are equal).
QList<std::pair<qsizetype, qsizetype>> matchesOf(const QList<DiffHunk> &hunks, qsizetype aLength)
{
    QList<std::pair<qsizetype, qsizetype>> out;
    qsizetype pa = 0;
    qsizetype pb = 0;
    QList<DiffHunk> all = hunks;
    all.append(DiffHunk{aLength, 0, -1, 0});
    for (const DiffHunk &h : all) {
        while (pa < h.aStart)
            out.append({pa++, pb++});
        pa = h.aStart + h.aLength;
        pb = h.bStart + h.bLength;
    }
    return out;
}

/// Length of a longest common subsequence (dynamic programming reference).
qsizetype lcsLength(const QStringList &a, const QStringList &b)
{
    std::vector<std::vector<qsizetype>> dp(static_cast<size_t>(a.size() + 1),
                                           std::vector<qsizetype>(static_cast<size_t>(b.size() + 1), 0));
    for (qsizetype i = a.size() - 1; i >= 0; --i) {
        for (qsizetype j = b.size() - 1; j >= 0; --j) {
            const auto ui = static_cast<size_t>(i);
            const auto uj = static_cast<size_t>(j);
            dp[ui][uj] = a.at(i) == b.at(j) ? dp[ui + 1][uj + 1] + 1 : qMax(dp[ui + 1][uj], dp[ui][uj + 1]);
        }
    }
    return dp[0][0];
}

const QStringList kAlphabet = {QString(), u"- a"_s, u"- b"_s, u"# H"_s, u"ąę"_s, u"żółw"_s};

Diff3Region ok(const QStringList &lines)
{
    Diff3Region r;
    r.ok = lines;
    return r;
}

Diff3Region conflict(const QStringList &a, const QStringList &o, const QStringList &b)
{
    Diff3Region r;
    r.conflict = true;
    r.a = a;
    r.o = o;
    r.b = b;
    return r;
}

/// A version of `base` (unique lines) with some lines deleted, changed and inserted.
QStringList derive(Random &r, const QStringList &base, const QString &tag)
{
    QStringList out;
    for (qsizetype i = 0; i < base.size(); ++i) {
        const int p = r.below(100);
        if (p >= 10)
            out.append(p < 20 ? u"%1-zmiana-%2"_s.arg(tag).arg(i) : base.at(i));
        if (r.chance(10))
            out.append(u"%1-nowa-%2"_s.arg(tag).arg(i));
    }
    if (r.chance(30))
        out.prepend(tag + u"-na-początku"_s);
    return out;
}

/// Lines of the merged regions (conflicts: the local side).
QStringList mergedLines(const QList<Diff3Region> &regions, bool *clean)
{
    QStringList out;
    *clean = true;
    for (const Diff3Region &region : regions) {
        if (region.conflict) {
            *clean = false;
            out += region.a;
        } else {
            out += region.ok;
        }
    }
    return out;
}

/// list[start, end) like JavaScript's slice for non-negative indexes (empty when end <= start).
QStringList slice(const QStringList &list, qsizetype start, qsizetype end)
{
    return end > start ? list.mid(start, end - start) : QStringList();
}

/// node-diff3's diff3Merge (excludeFalseConflicts) as written there — regions bounded per side
/// by min/max over that side's hunks — on top of diffLines(). A reference for diff3Regions(),
/// which tracks running offsets instead.
QList<Diff3Region> referenceDiff3(const QStringList &a, const QStringList &o, const QStringList &b)
{
    struct Hunk {
        bool fromA;
        qsizetype oStart, oLength, abStart, abLength;
    };
    QList<Hunk> hunks;
    for (const DiffHunk &h : diffLines(o, a))
        hunks.append(Hunk{true, h.aStart, h.aLength, h.bStart, h.bLength});
    for (const DiffHunk &h : diffLines(o, b))
        hunks.append(Hunk{false, h.aStart, h.aLength, h.bStart, h.bLength});
    std::stable_sort(hunks.begin(), hunks.end(), [](const Hunk &x, const Hunk &y) { return x.oStart < y.oStart; });

    QList<Diff3Region> results;
    QStringList okBuffer;
    const auto flushOk = [&] {
        if (!okBuffer.isEmpty())
            results.append(ok(okBuffer));
        okBuffer.clear();
    };
    qsizetype current = 0;
    const auto advanceTo = [&](qsizetype end) {
        if (end > current) {
            okBuffer += slice(o, current, end);
            current = end;
        }
    };
    qsizetype i = 0;
    while (i < hunks.size()) {
        const Hunk first = hunks.at(i);
        const qsizetype regionStart = first.oStart;
        qsizetype regionEnd = first.oStart + first.oLength;
        advanceTo(regionStart);
        qsizetype j = i + 1;
        while (j < hunks.size() && hunks.at(j).oStart <= regionEnd) {
            regionEnd = qMax(regionEnd, hunks.at(j).oStart + hunks.at(j).oLength);
            ++j;
        }
        if (j - i == 1) {
            if (first.abLength > 0)
                okBuffer += slice(first.fromA ? a : b, first.abStart, first.abStart + first.abLength);
        } else {
            // bounds[side] = {min abStart, max abEnd, min oStart, max oEnd}
            qsizetype bounds[2][4] = {{a.size(), -1, o.size(), -1}, {b.size(), -1, o.size(), -1}};
            for (qsizetype k = i; k < j; ++k) {
                const Hunk &h = hunks.at(k);
                qsizetype *bd = bounds[h.fromA ? 0 : 1];
                bd[0] = qMin(h.abStart, bd[0]);
                bd[1] = qMax(h.abStart + h.abLength, bd[1]);
                bd[2] = qMin(h.oStart, bd[2]);
                bd[3] = qMax(h.oStart + h.oLength, bd[3]);
            }
            const QStringList aContent =
                slice(a, bounds[0][0] + (regionStart - bounds[0][2]), bounds[0][1] + (regionEnd - bounds[0][3]));
            const QStringList bContent =
                slice(b, bounds[1][0] + (regionStart - bounds[1][2]), bounds[1][1] + (regionEnd - bounds[1][3]));
            if (aContent == bContent) {
                okBuffer += aContent;
            } else {
                flushOk();
                results.append(conflict(aContent, slice(o, regionStart, regionEnd), bContent));
            }
        }
        current = regionEnd;
        i = j;
    }
    advanceTo(o.size());
    flushOk();
    return results;
}

} // namespace

class TestSyncDiff : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void equalEmptyAndDisjoint()
    {
        QVERIFY(diffLines({u"a"_s, u"b"_s}, {u"a"_s, u"b"_s}).isEmpty());
        QVERIFY(diffLines({}, {}).isEmpty());
        QCOMPARE(diffLines({u"a"_s}, {u"b"_s}), (QList<DiffHunk>{{0, 1, 0, 1}}));
        QCOMPARE(diffLines({}, {u"x"_s, u"y"_s}), (QList<DiffHunk>{{0, 0, 0, 2}}));
        QCOMPARE(diffLines({u"x"_s}, {}), (QList<DiffHunk>{{0, 1, 0, 0}}));
    }

    void changesInTheMiddle()
    {
        QCOMPARE(diffLines({u"a"_s, u"b"_s, u"c"_s}, {u"a"_s, u"X"_s, u"b"_s, u"c"_s}), (QList<DiffHunk>{{1, 0, 1, 1}}));
        QCOMPARE(diffLines({u"a"_s, u"b"_s, u"c"_s}, {u"a"_s, u"c"_s}), (QList<DiffHunk>{{1, 1, 1, 0}}));
        QCOMPARE(diffLines({u"Zażółć"_s, u"gęślą"_s, u"jaźń"_s}, {u"Zażółć"_s, u"GĘŚLĄ"_s, u"jaźń"_s}),
                 (QList<DiffHunk>{{1, 1, 1, 1}}));
    }

    void specialStringsAreOrdinaryLines()
    {
        QCOMPARE(diffLines({u"__proto__"_s, u"constructor"_s}, {u"__proto__"_s, u"x"_s, u"constructor"_s}),
                 (QList<DiffHunk>{{1, 0, 1, 1}}));
        QCOMPARE(diffLines({u"\r"_s, QString()}, {QString(), u"\r"_s, QString()}), (QList<DiffHunk>{{0, 0, 0, 1}}));
    }

    void randomInputGivesALongestCommonSubsequence()
    {
        Random r(42);
        for (int round = 0; round < 300; ++round) {
            const QStringList a = randomLines(r, r.below(40), kAlphabet);
            const QStringList b = randomLines(r, r.below(40), kAlphabet);
            const auto pairs = matchesOf(diffLines(a, b), a.size());
            for (qsizetype i = 0; i < pairs.size(); ++i) {
                const auto [x, y] = pairs.at(i);
                QCOMPARE(a.at(x), b.at(y));
                if (i > 0) {
                    QVERIFY(x > pairs.at(i - 1).first);
                    QVERIFY(y > pairs.at(i - 1).second);
                }
            }
            QCOMPARE(pairs.size(), lcsLength(a, b));
        }
    }

    void exhaustedBudgetStaysCorrect()
    {
        Random r(7);
        const QStringList a = randomLines(r, 300, kAlphabet);
        const QStringList b = randomLines(r, 300, kAlphabet);
        const QList<DiffHunk> hunks = diffLines(a, b, 50);
        for (const auto &[x, y] : matchesOf(hunks, a.size()))
            QCOMPARE(a.at(x), b.at(y));
        // Hunks cover both sides completely and in order.
        qsizetype pa = 0, pb = 0;
        for (const DiffHunk &h : hunks) {
            QVERIFY(h.aStart >= pa && h.bStart >= pb);
            QCOMPARE(h.aStart - pa, h.bStart - pb);
            pa = h.aStart + h.aLength;
            pb = h.bStart + h.bLength;
        }
        QCOMPARE(a.size() - pa, b.size() - pb);
    }

    void fastOnLongNotesWithBlankLines()
    {
        QStringList lines;
        for (int i = 0; i < 8000; ++i)
            lines.append(i % 2 ? QString() : u"akapit %1"_s.arg(i % 37));
        QStringList changed = lines;
        for (int i = 100; i < 8000; i += 400)
            changed[i] = u"zmiana %1"_s.arg(i);
        changed.prepend(u"nowy początek"_s);
        changed.append(u"koniec"_s);
        QElapsedTimer timer;
        timer.start();
        const QList<DiffHunk> hunks = diffLines(lines, changed);
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(hunks.size(), 22);
    }

    void boundedTimeForCompletelyDifferentInputs()
    {
        QStringList a, b;
        for (int i = 0; i < 20000; ++i) {
            a.append(u"a%1"_s.arg(i % 97));
            b.append(u"b%1"_s.arg(i % 89));
        }
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(diffLines(a, b), (QList<DiffHunk>{{0, 20000, 0, 20000}}));
        QVERIFY(timer.elapsed() < 5000);
    }

    void diffComm()
    {
        CommPart common1;
        common1.common = true;
        common1.commonLines = {u"a"_s};
        CommPart changed;
        changed.a = {u"b"_s};
        changed.b = {u"x"_s};
        CommPart common2;
        common2.common = true;
        common2.commonLines = {u"c"_s};
        CommPart added;
        added.b = {u"d"_s};
        QCOMPARE(sync::diffComm({u"a"_s, u"b"_s, u"c"_s}, {u"a"_s, u"x"_s, u"c"_s, u"d"_s}),
                 (QList<CommPart>{common1, changed, common2, added}));
        QVERIFY(sync::diffComm({}, {}).isEmpty());
    }

    void diff3HandWrittenCases_data()
    {
        QTest::addColumn<QStringList>("a");
        QTest::addColumn<QStringList>("o");
        QTest::addColumn<QStringList>("b");
        QTest::addColumn<QList<Diff3Region>>("expected");
        QTest::newRow("adjacent edits conflict")
            << QStringList{u"a"_s, u"B"_s, u"c"_s} << QStringList{u"a"_s, u"b"_s, u"c"_s} << QStringList{u"a"_s, u"b"_s, u"C"_s}
            << QList<Diff3Region>{ok({u"a"_s}), conflict({u"B"_s, u"c"_s}, {u"b"_s, u"c"_s}, {u"b"_s, u"C"_s})};
        QTest::newRow("appends at the same place")
            << QStringList{u"a"_s, u"b"_s, u"c"_s, u"x"_s} << QStringList{u"a"_s, u"b"_s, u"c"_s}
            << QStringList{u"a"_s, u"b"_s, u"c"_s, u"y"_s}
            << QList<Diff3Region>{ok({u"a"_s, u"b"_s, u"c"_s}), conflict({u"x"_s}, {}, {u"y"_s})};
        QTest::newRow("both lines changed") << QStringList{u"A"_s, u"b"_s} << QStringList{u"a"_s, u"b"_s}
                                            << QStringList{u"a"_s, u"B"_s}
                                            << QList<Diff3Region>{conflict({u"A"_s, u"b"_s}, {u"a"_s, u"b"_s}, {u"a"_s, u"B"_s})};
        QTest::newRow("delete vs modify")
            << QStringList{u"a"_s, u"c"_s} << QStringList{u"a"_s, u"b"_s, u"c"_s} << QStringList{u"a"_s, u"B"_s, u"c"_s}
            << QList<Diff3Region>{ok({u"a"_s}), conflict({}, {u"b"_s}, {u"B"_s}), ok({u"c"_s})};
        QTest::newRow("emptied vs appended") << QStringList{} << QStringList{u"a"_s} << QStringList{u"a"_s, u"b"_s}
                                             << QList<Diff3Region>{conflict({}, {u"a"_s}, {u"a"_s, u"b"_s})};
        QTest::newRow("empty base") << QStringList{u"x"_s, u"y"_s} << QStringList{} << QStringList{u"y"_s, u"z"_s}
                                    << QList<Diff3Region>{conflict({u"x"_s, u"y"_s}, {}, {u"y"_s, u"z"_s})};
        QTest::newRow("same change on both sides")
            << QStringList{u"a"_s, u"B"_s, u"c"_s} << QStringList{u"a"_s, u"b"_s, u"c"_s} << QStringList{u"a"_s, u"B"_s, u"c"_s}
            << QList<Diff3Region>{ok({u"a"_s, u"B"_s, u"c"_s})};
        QTest::newRow("one-sided append") << QStringList{u"a"_s, u"b"_s} << QStringList{u"a"_s, u"b"_s}
                                          << QStringList{u"a"_s, u"b"_s, u"c"_s}
                                          << QList<Diff3Region>{ok({u"a"_s, u"b"_s, u"c"_s})};
        QTest::newRow("single line, both changed")
            << QStringList{u"L"_s} << QStringList{u"o"_s} << QStringList{u"R"_s}
            << QList<Diff3Region>{conflict({u"L"_s}, {u"o"_s}, {u"R"_s})};
        QTest::newRow("far apart edits") << QStringList{u"A"_s, u"b"_s, u"c"_s, u"d"_s}
                                         << QStringList{u"a"_s, u"b"_s, u"c"_s, u"d"_s}
                                         << QStringList{u"a"_s, u"b"_s, u"c"_s, u"D"_s}
                                         << QList<Diff3Region>{ok({u"A"_s, u"b"_s, u"c"_s, u"D"_s})};
        QTest::newRow("all empty") << QStringList{} << QStringList{} << QStringList{} << QList<Diff3Region>{};
    }
    void diff3HandWrittenCases()
    {
        QFETCH(QStringList, a);
        QFETCH(QStringList, o);
        QFETCH(QStringList, b);
        QFETCH(QList<Diff3Region>, expected);
        QCOMPARE(diff3Regions(a, o, b), expected);
    }

    void diff3MatchesNodeDiff3OnRandomEdits()
    {
        // The archive compared with node-diff3's diff3Merge on 200 random rounds of unique lines;
        // here the same rounds against its algorithm (referenceDiff3), plus repeated lines.
        Random r(2026);
        for (int round = 0; round < 400; ++round) {
            QStringList base;
            const int n = r.below(30);
            for (int i = 0; i < n; ++i)
                base.append(round % 2 == 0 ? u"linia %1 — źdźbło"_s.arg(i) : kAlphabet.at(r.below(int(kAlphabet.size()))));
            const QStringList a = derive(r, base, u"L"_s);
            const QStringList b = round % 3 == 0 ? derive(r, base, u"L"_s) : derive(r, base, u"R"_s);
            QCOMPARE(diff3Regions(a, base, b), referenceDiff3(a, base, b));
        }
    }

    void diff3OneSidedEditsAreTakenCompletely()
    {
        Random r(2026);
        for (int round = 0; round < 200; ++round) {
            QStringList base;
            const int n = r.below(30);
            for (int i = 0; i < n; ++i)
                base.append(u"linia %1 — źdźbło"_s.arg(i));
            const QStringList a = derive(r, base, u"L"_s);
            const QStringList b = derive(r, base, u"R"_s);
            bool clean = false;
            QCOMPARE(mergedLines(diff3Regions(a, base, base), &clean), a);
            QVERIFY(clean);
            QCOMPARE(mergedLines(diff3Regions(base, base, b), &clean), b);
            QVERIFY(clean);
            QCOMPARE(mergedLines(diff3Regions(a, base, a), &clean), a);
            QVERIFY(clean);
        }
    }

    void diff3DisjointEditsMergeCleanly()
    {
        Random r(1410);
        for (int round = 0; round < 200; ++round) {
            QStringList base;
            const int n = 6 + r.below(30);
            for (int i = 0; i < n; ++i)
                base.append(u"wiersz %1 ąćę"_s.arg(i));
            // Local edits the first part, remote the second; two untouched lines in between.
            const qsizetype mid = n / 2;
            const QStringList head = base.mid(0, mid - 1);
            const QStringList gap = base.mid(mid - 1, 2);
            const QStringList tail = base.mid(mid + 1);
            const QStringList localHead = derive(r, head, u"L"_s);
            QStringList remoteTail = derive(r, tail, u"R"_s);
            remoteTail.removeIf([](const QString &line) { return line.endsWith(u"-na-początku"_s); });
            const QStringList a = localHead + gap + tail;
            const QStringList b = head + gap + remoteTail;
            bool clean = false;
            const QStringList merged = mergedLines(diff3Regions(a, base, b), &clean);
            QVERIFY(clean);
            QCOMPARE(merged, localHead + gap + remoteTail);
        }
    }
};

QTEST_GUILESS_MAIN(TestSyncDiff)
#include "tst_sync_diff.moc"
