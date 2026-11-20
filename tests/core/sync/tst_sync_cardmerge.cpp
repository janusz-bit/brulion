// Flashcards reviewed on two devices (sync/cardmerge.h, docs/SYNTAX.md "Flashcards"): merge3 of a
// deck note where both sides changed only cards' scheduling state — the Spaced Repetition plugin's
// comments (FSRS and SM-2 segments, siblings), Logseq's card-* properties — merges by itself, line
// by line, the same card keeping the later review; anything else is still a conflict.
#include "sync/cardmerge.h"
#include "sync/merge.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::sync;

namespace {

/// An FSRS segment reviewed at `last` (UTC), due at `due`.
QString fsrs(const QString &due, const QString &last, int reps = 2)
{
    return u"fsrs,%1T08:00:00.000Z,3,3.17,5.28,2,%2,0,0,%3T08:00:00.000Z"_s.arg(due).arg(reps).arg(last);
}

QString comment(const QStringList &segments)
{
    return u"<!--SR:!"_s + segments.join(u'!') + u"-->"_s;
}

} // namespace

class TestSyncCardMerge : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void key_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("key");
        QTest::newRow("comment at the end") << u"Stolica Peru::Lima <!--SR:!2026-11-20,3,250-->"_s << u"Stolica Peru::Lima"_s;
        QTest::newRow("before a block id") << u"- A::B <!--SR:!2026-11-20,3,250--> ^abc"_s << u"- A::B ^abc"_s;
        QTest::newRow("on its own line") << u"<!--SR:!2026-11-20,3,250-->"_s << QString();
        QTest::newRow("Logseq property") << u"  card-repeats:: 4"_s << u"  card-repeats::"_s;
        QTest::newRow("other property") << u"  id:: 64a1"_s << u"  id:: 64a1"_s;
        QTest::newRow("unclosed comment") << u"A::B <!--SR:!2026-11-20,3,250"_s << u"A::B <!--SR:!2026-11-20,3,250"_s;
        QTest::newRow("no comment") << u"Zażółć gęślą jaźń"_s << u"Zażółć gęślą jaźń"_s;
    }
    void key()
    {
        QFETCH(QString, line);
        QFETCH(QString, key);
        QCOMPARE(cardStateKey(line), key);
    }

    // Two devices review different cards on neighbouring lines (diff3 calls that a conflict).
    void differentCards()
    {
        const QString base = u"#fiszki\n\npies::dog\nkot::cat\nkoń::horse\n"_s;
        const QString local = u"#fiszki\n\npies::dog "_s + comment({fsrs(u"2026-11-23"_s, u"2026-11-20"_s)})
                              + u"\nkot::cat\nkoń::horse\n"_s;
        const QString remote = u"#fiszki\n\npies::dog\nkot::cat "_s + comment({u"2026-11-25,4,250"_s}) + u"\nkoń::horse\n"_s;
        const MergeResult r = merge3(base, local, remote);
        QVERIFY(r.clean);
        QCOMPARE(r.text, u"#fiszki\n\npies::dog "_s + comment({fsrs(u"2026-11-23"_s, u"2026-11-20"_s)}) + u"\nkot::cat "_s
                                 + comment({u"2026-11-25,4,250"_s}) + u"\nkoń::horse\n"_s);
    }

    // The same card on both: the later review stays, whichever side it is on — SM-2 or FSRS.
    void sameCard_data()
    {
        QTest::addColumn<QString>("local");
        QTest::addColumn<QString>("remote");
        QTest::addColumn<QString>("expected");
        const QString early = fsrs(u"2026-11-23"_s, u"2026-11-19"_s);
        const QString late = fsrs(u"2026-11-30"_s, u"2026-11-20"_s);
        QTest::newRow("remote later") << early << late << late;
        QTest::newRow("local later") << late << early << late;
        // SM-2: reviewed on its due minus its interval — 2026-11-21 against FSRS's 2026-11-20.
        QTest::newRow("SM-2 later") << late << u"2026-11-25,4,250"_s << u"2026-11-25,4,250"_s;
        QTest::newRow("FSRS later") << u"2026-11-25,10,250"_s << late << late;
        QTest::newRow("equal: the local side") << fsrs(u"2026-11-23"_s, u"2026-11-20"_s, 3) << late
                                               << fsrs(u"2026-11-23"_s, u"2026-11-20"_s, 3);
    }
    void sameCard()
    {
        QFETCH(QString, local);
        QFETCH(QString, remote);
        QFETCH(QString, expected);
        const QString base = u"A\n\nżółw::turtle <!--SR:!2026-11-18,3,250-->\n\nZ\n"_s;
        const MergeResult r = merge3(base, u"A\n\nżółw::turtle "_s + comment({local}) + u"\n\nZ\n"_s,
                                     u"A\n\nżółw::turtle "_s + comment({remote}) + u"\n\nZ\n"_s);
        QVERIFY(r.clean);
        QCOMPARE(r.text, u"A\n\nżółw::turtle "_s + comment({expected}) + u"\n\nZ\n"_s);
    }

    // A cloze's siblings, one reviewed on each device: both kept.
    void siblings()
    {
        const QString base = u"==Rysy== w ==Tatrach== <!--SR:!2000-01-01,1,250!2000-01-01,1,250-->\n"_s;
        const QString a = fsrs(u"2026-11-23"_s, u"2026-11-20"_s);
        const QString b = fsrs(u"2026-11-24"_s, u"2026-11-20"_s);
        const MergeResult r = merge3(base, u"==Rysy== w ==Tatrach== "_s + comment({a, u"2000-01-01,1,250"_s}) + u"\n"_s,
                                     u"==Rysy== w ==Tatrach== "_s + comment({u"2000-01-01,1,250"_s, b}) + u"\n"_s);
        QVERIFY(r.clean);
        QCOMPARE(r.text, u"==Rysy== w ==Tatrach== "_s + comment({a, b}) + u"\n"_s);
    }

    // A comment on its own line and the first review of a new card on both devices.
    void firstReviewOnBoth()
    {
        const QString base = u"Pytanie\n?\nOdpowiedź\n"_s;
        const QString early = comment({fsrs(u"2026-11-21"_s, u"2026-11-19"_s)});
        const QString late = comment({fsrs(u"2026-11-22"_s, u"2026-11-20"_s)});
        // (Brulion writes on the answer's line: one line changed on both.)
        MergeResult r = merge3(base, u"Pytanie\n?\nOdpowiedź "_s + late + u"\n"_s, u"Pytanie\n?\nOdpowiedź "_s + early + u"\n"_s);
        QVERIFY(r.clean);
        QCOMPARE(r.text, u"Pytanie\n?\nOdpowiedź "_s + late + u"\n"_s);
        // (The plugin writes a line of its own: both added one at the same place — one kept.)
        r = merge3(base, base + early + u"\n"_s, base + late + u"\n"_s);
        QVERIFY(r.clean);
        QCOMPARE(r.text, base + late + u"\n"_s);
    }

    // Logseq: the run of card-* properties of the later card-last-reviewed, as a whole.
    void logseq()
    {
        const auto props = [](const QString &interval, const QString &reviewed) {
            return u"- Co znaczy „brulion”? #card\n  card-last-interval:: "_s + interval
                   + u"\n  card-repeats:: 2\n  card-last-reviewed:: "_s + reviewed + u"\n  - zeszyt\n"_s;
        };
        const QString base = props(u"4"_s, u"2026-11-10T08:00:00.000Z"_s);
        MergeResult r = merge3(base, props(u"9"_s, u"2026-11-20T08:00:00.000Z"_s), props(u"11"_s, u"2026-11-19T08:00:00.000Z"_s));
        QVERIFY(r.clean);
        QCOMPARE(r.text, props(u"9"_s, u"2026-11-20T08:00:00.000Z"_s));
        r = merge3(base, props(u"9"_s, u"2026-11-18T08:00:00.000Z"_s), props(u"11"_s, u"2026-11-19T08:00:00.000Z"_s));
        QVERIFY(r.clean);
        QCOMPARE(r.text, props(u"11"_s, u"2026-11-19T08:00:00.000Z"_s));
        // The first review on both devices: both added the properties at the same place.
        const QString fresh = u"- Pytanie #card\n  - Odpowiedź\n"_s;
        const auto added = [](const QString &reviewed) {
            return u"- Pytanie #card\n  card-repeats:: 1\n  card-last-reviewed:: "_s + reviewed + u"\n  - Odpowiedź\n"_s;
        };
        r = merge3(fresh, added(u"2026-11-20T09:00:00.000Z"_s), added(u"2026-11-20T10:00:00.000Z"_s));
        QVERIFY(r.clean);
        QCOMPARE(r.text, added(u"2026-11-20T10:00:00.000Z"_s));
    }

    // What is no card state stays a conflict (or merges by merge3's own rules).
    void notCardState()
    {
        // The card's text changed on one side, its state on the other: one line, two changes.
        MergeResult r = merge3(u"A::B\n"_s, u"A::Bb\n"_s, u"A::B <!--SR:!2026-11-25,4,250-->\n"_s);
        QVERIFY(!r.clean);
        // Text on both sides.
        r = merge3(u"x\ny\n"_s, u"x1\ny\n"_s, u"x2\ny\n"_s);
        QVERIFY(!r.clean);
        // Two Logseq property runs nobody can order (no review time on either side).
        QCOMPARE(mergeCardState({u"  card-repeats:: 1"_s}, {u"  card-repeats:: 2"_s}, {u"  card-repeats:: 3"_s}),
                 std::nullopt);
        // Additions that are no state: merge3's union, untouched.
        r = merge3(u"a\n"_s, u"a\nb\n"_s, u"a\nc\n"_s);
        QVERIFY(r.clean);
        QCOMPARE(r.text, u"a\nb\nc\n"_s);
        // Malformed comments are text.
        QCOMPARE(mergeCardState({u"A <!--SR:!x"_s}, {u"A <!--SR:!y"_s}, {u"A <!--SR:!z"_s}), std::nullopt);
    }
};

QTEST_GUILESS_MAIN(TestSyncCardMerge)
#include "tst_sync_cardmerge.moc"
