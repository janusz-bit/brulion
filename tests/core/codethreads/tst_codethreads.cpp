// A Code mode thread's place (codethreads/codethreads.h): the state its page's frontmatter gives —
// settled, snoozed, auto-settle never, malformed times, quotes, CRLF, no frontmatter —; settling,
// snoozing, waking and a new message's reactivation as content edits that keep everything else byte
// for byte; the sidebar's section of each mix of states; snooze presets and times in words (Polish and
// English); inactivity's settling and what holds it.
#include "codethreads/codethreads.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::codethreads;

namespace {

const QString Page = u"---\nproject: sklep\nbranch: brulion/rabat\n---\n- 🧑 Dodaj rabat 10% — żółw\n"_s;

QDateTime at(const QString &text)
{
    return QDateTime::fromString(text, u"yyyy-MM-dd HH:mm"_s);
}

} // namespace

class TestCodeThreads : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void stateRead_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("settled");
        QTest::addColumn<QString>("snoozed");
        QTest::addColumn<bool>("never");
        QTest::newRow("none") << Page << "" << "" << false;
        QTest::newRow("no frontmatter") << u"- 🧑 settled: 2026-11-28 10:00\n"_s << "" << "" << false;
        QTest::newRow("settled") << u"---\nproject: a\nsettled: 2026-11-28 10:15\n---\n- 🧑 x\n"_s
                                 << "2026-11-28 10:15" << "" << false;
        QTest::newRow("snoozed, quoted, T") << u"---\nsnoozed: \"2026-11-28T18:00:00\"\n---\n"_s << ""
                                            << "2026-11-28 18:00" << false;
        QTest::newRow("bare date") << u"---\nsnoozed: 2026-12-01\n---\n"_s << "" << "2026-12-01 00:00" << false;
        QTest::newRow("never") << u"---\nauto-settle: never\n---\n"_s << "" << "" << true;
        QTest::newRow("never, other case") << u"---\nAuto-Settle: Never\n---\n"_s << "" << "" << true;
        QTest::newRow("auto-settle other value") << u"---\nauto-settle: yes\n---\n"_s << "" << "" << false;
        QTest::newRow("malformed times") << u"---\nsettled: wczoraj\nsnoozed: 2026-13-40 25:00\n---\n"_s << ""
                                         << "" << false;
        QTest::newRow("CRLF") << u"---\r\nsettled: 2026-11-28 10:15\r\n---\r\n"_s << "2026-11-28 10:15" << ""
                              << false;
        QTest::newRow("after the frontmatter") << u"---\nproject: a\n---\nsettled: 2026-11-28 10:15\n"_s << ""
                                               << "" << false;
    }
    void stateRead()
    {
        QFETCH(QString, content);
        QFETCH(QString, settled);
        QFETCH(QString, snoozed);
        QFETCH(bool, never);
        const State s = brulion::codethreads::readState(content);
        QCOMPARE(s.settled.isValid() ? formatTime(s.settled) : QString(), settled);
        QCOMPARE(s.snoozedUntil.isValid() ? formatTime(s.snoozedUntil) : QString(), snoozed);
        QCOMPARE(s.neverAutoSettle, never);
    }

    void settleAndBack()
    {
        const QString settled = settle(Page, at(u"2026-11-28 10:15"_s));
        QCOMPARE(settled, u"---\nproject: sklep\nbranch: brulion/rabat\nsettled: 2026-11-28 10:15\n---\n"
                          u"- 🧑 Dodaj rabat 10% — żółw\n"_s);
        QCOMPARE(unsettle(settled), Page);
        // Settling twice: the time replaced in place, one line.
        QCOMPARE(readState(settle(settled, at(u"2026-11-29 08:00"_s))).settled, at(u"2026-11-29 08:00"_s));
        QCOMPARE(settle(settled, at(u"2026-11-29 08:00"_s)).count(u"settled:"_s), 1);
        QCOMPARE(unsettle(Page), Page);
    }
    void settleTakesTheSnoozeAway()
    {
        const QString snoozed = snooze(Page, at(u"2026-11-28 18:00"_s));
        QVERIFY(readState(snoozed).snoozedUntil.isValid());
        const State s = readState(settle(snoozed, at(u"2026-11-28 12:00"_s)));
        QVERIFY(s.settled.isValid());
        QVERIFY(!s.snoozedUntil.isValid());
    }
    void snoozeTakesTheSettlingAway()
    {
        const State s = readState(snooze(settle(Page, at(u"2026-11-28 10:00"_s)), at(u"2026-11-28 18:00"_s)));
        QVERIFY(!s.settled.isValid());
        QCOMPARE(s.snoozedUntil, at(u"2026-11-28 18:00"_s));
        QCOMPARE(wake(snooze(Page, at(u"2026-11-28 18:00"_s))), Page);
        QCOMPARE(snooze(snooze(Page, at(u"2026-11-28 18:00"_s)), QDateTime()), Page);
    }
    void pageWithoutFrontmatter()
    {
        const QString plain = u"- 🧑 Cześć\n"_s;
        const QString settled = settle(plain, at(u"2026-11-28 10:15"_s));
        QCOMPARE(readState(settled).settled, at(u"2026-11-28 10:15"_s));
        QVERIFY(settled.endsWith(plain));
    }
    void crlfKept()
    {
        const QString page = u"---\r\nproject: a\r\n---\r\n- 🧑 x\r\n"_s;
        const QString settled = settle(page, at(u"2026-11-28 10:15"_s));
        QCOMPARE(settled, u"---\r\nproject: a\r\nsettled: 2026-11-28 10:15\r\n---\r\n- 🧑 x\r\n"_s);
        QCOMPARE(unsettle(settled), page);
    }
    void neverAutoSettle()
    {
        const QString never = setNeverAutoSettle(Page, true);
        QVERIFY(readState(never).neverAutoSettle);
        QCOMPARE(setNeverAutoSettle(never, false), Page);
        QCOMPARE(setNeverAutoSettle(Page, false), Page);
    }
    void reactivatedByAMessage()
    {
        const QString both = u"---\nproject: a\nsettled: 2026-11-28 10:15\nsnoozed: 2026-11-28 18:00\n"
                             u"auto-settle: never\n---\n- 🧑 x\n"_s;
        const QString after = reactivated(both);
        QCOMPARE(after, u"---\nproject: a\nauto-settle: never\n---\n- 🧑 x\n"_s);
        QCOMPARE(reactivated(Page), Page);
    }

    void sections_data()
    {
        QTest::addColumn<QString>("settled");
        QTest::addColumn<QString>("snoozed");
        QTest::addColumn<bool>("pinned");
        QTest::addColumn<bool>("archived");
        QTest::addColumn<bool>("needsUser");
        QTest::addColumn<QString>("section");
        // (now: 2026-11-28 12:00)
        QTest::newRow("plain") << "" << "" << false << false << false << "active";
        QTest::newRow("pinned") << "" << "" << true << false << false << "pinned";
        QTest::newRow("archived wins") << "2026-11-28 10:00" << "" << true << true << false << "archived";
        QTest::newRow("settled") << "2026-11-28 10:00" << "" << false << false << false << "settled";
        QTest::newRow("settled and pinned") << "2026-11-28 10:00" << "" << true << false << false << "settled";
        QTest::newRow("snoozed ahead") << "" << "2026-11-28 18:00" << false << false << false << "snoozed";
        QTest::newRow("snooze over") << "" << "2026-11-28 11:59" << false << false << false << "active";
        QTest::newRow("snooze ends now") << "" << "2026-11-28 12:00" << false << false << false << "active";
        QTest::newRow("snoozed, pinned") << "" << "2026-11-28 18:00" << true << false << false << "snoozed";
        QTest::newRow("snooze over, pinned") << "" << "2026-11-28 11:00" << true << false << false << "pinned";
        QTest::newRow("snoozed but asks") << "" << "2026-11-28 18:00" << false << false << true << "active";
        QTest::newRow("settled, asks") << "2026-11-28 10:00" << "" << false << false << true << "active";
    }
    void sections()
    {
        QFETCH(QString, settled);
        QFETCH(QString, snoozed);
        QFETCH(bool, pinned);
        QFETCH(bool, archived);
        QFETCH(bool, needsUser);
        QFETCH(QString, section);
        State s;
        s.settled = at(settled);
        s.snoozedUntil = at(snoozed);
        QCOMPARE(sectionName(brulion::codethreads::sectionOf(s, {pinned, archived, needsUser},
                                                             at(u"2026-11-28 12:00"_s))),
                 section);
    }
    void sectionNames()
    {
        QCOMPARE(sectionName(Section::Pinned), u"pinned"_s);
        QCOMPARE(sectionName(Section::Active), u"active"_s);
        QCOMPARE(sectionName(Section::Snoozed), u"snoozed"_s);
        QCOMPARE(sectionName(Section::Settled), u"settled"_s);
        QCOMPARE(sectionName(Section::Archived), u"archived"_s);
    }

    void snoozeEnds_data()
    {
        QTest::addColumn<QString>("preset");
        QTest::addColumn<QString>("end");
        // (now: Saturday 2026-11-28 12:00)
        QTest::newRow("1h") << "1h" << "2026-11-28 13:00";
        QTest::newRow("3h") << "3h" << "2026-11-28 15:00";
        QTest::newRow("evening") << "evening" << "2026-11-28 18:00";
        QTest::newRow("tomorrow") << "tomorrow" << "2026-11-29 09:00";
        QTest::newRow("monday") << "monday" << "2026-11-30 09:00";
        QTest::newRow("Polish words") << "jutro o 18" << "2026-11-29 18:00";
        QTest::newRow("Polish offset") << "za 2 godziny" << "2026-11-28 14:00";
        QTest::newRow("English") << "in 30 minutes" << "2026-11-28 12:30";
        QTest::newRow("a day: 9:00") << "1 grudnia" << "2026-12-01 09:00";
        QTest::newRow("digits") << "2026-12-03 07:45" << "2026-12-03 07:45";
        QTest::newRow("past") << "wczoraj o 9" << "";
        QTest::newRow("now itself") << "2026-11-28 12:00" << "";
        QTest::newRow("today without time") << "dziś" << "";
        QTest::newRow("nothing") << "kiedyś tam" << "";
        QTest::newRow("empty") << "" << "";
    }
    void snoozeEnds()
    {
        QFETCH(QString, preset);
        QFETCH(QString, end);
        const std::optional<QDateTime> got = brulion::codethreads::snoozeEnd(preset, at(u"2026-11-28 12:00"_s));
        QCOMPARE(got ? formatTime(*got) : QString(), end);
    }
    void eveningAfterSix()
    {
        QCOMPARE(formatTime(*brulion::codethreads::snoozeEnd(u"evening", at(u"2026-11-28 19:30"_s))),
                 u"2026-11-29 18:00"_s);
        QCOMPARE(formatTime(*brulion::codethreads::snoozeEnd(u"monday", at(u"2026-11-30 08:00"_s))),
                 u"2026-12-07 09:00"_s);
        // At 18:00 itself: tomorrow's evening, never a snooze of nothing.
        QCOMPARE(formatTime(*brulion::codethreads::snoozeEnd(u"evening", at(u"2026-11-28 18:00"_s))),
                 u"2026-11-29 18:00"_s);
        // Hours to the second; no clock, no end.
        const QDateTime now = QDateTime(QDate(2026, 11, 28), QTime(12, 0, 30));
        QCOMPARE(*brulion::codethreads::snoozeEnd(u"1h", now), now.addSecs(3600));
        QCOMPARE(*brulion::codethreads::snoozeEnd(u"3h", now), now.addSecs(3 * 3600));
        QVERIFY(!brulion::codethreads::snoozeEnd(u"1h", QDateTime()));
    }

    void inactivity_data()
    {
        QTest::addColumn<QString>("last");
        QTest::addColumn<int>("days");
        QTest::addColumn<bool>("busy");
        QTest::addColumn<int>("state"); // 0 plain, 1 settled, 2 never, 3 archived
        QTest::addColumn<bool>("settles");
        // (now: 2026-11-28 12:00)
        QTest::newRow("8 days") << "2026-11-20 11:00" << 7 << false << 0 << true;
        QTest::newRow("exactly 7 days") << "2026-11-21 12:00" << 7 << false << 0 << true;
        QTest::newRow("6 days") << "2026-11-22 12:00" << 7 << false << 0 << false;
        QTest::newRow("off") << "2026-01-01 12:00" << 0 << false << 0 << false;
        QTest::newRow("a day") << "2026-11-26 12:00" << 1 << false << 0 << true;
        QTest::newRow("busy") << "2026-11-01 12:00" << 7 << true << 0 << false;
        QTest::newRow("settled already") << "2026-11-01 12:00" << 7 << false << 1 << false;
        QTest::newRow("never") << "2026-11-01 12:00" << 7 << false << 2 << false;
        QTest::newRow("archived") << "2026-11-01 12:00" << 7 << false << 3 << false;
        QTest::newRow("no time") << "" << 7 << false << 0 << false;
    }
    void inactivity()
    {
        QFETCH(QString, last);
        QFETCH(int, days);
        QFETCH(bool, busy);
        QFETCH(int, state);
        QFETCH(bool, settles);
        State s;
        Facts f;
        if (state == 1)
            s.settled = at(u"2026-11-02 10:00"_s);
        s.neverAutoSettle = state == 2;
        f.archived = state == 3;
        QCOMPARE(settlesFromInactivity(s, f, at(last), at(u"2026-11-28 12:00"_s), days, busy), settles);
    }

    void pullRequestStates()
    {
        QCOMPARE(pullRequestOf(u"open"), PullRequest::Open);
        QCOMPARE(pullRequestOf(u"draft"), PullRequest::Open);
        QCOMPARE(pullRequestOf(u"MERGED"), PullRequest::Merged);
        QCOMPARE(pullRequestOf(u"closed"), PullRequest::Closed);
        QCOMPARE(pullRequestOf(u""), PullRequest::None);
        QCOMPARE(pullRequestOf(u"zamknięty"), PullRequest::None);
    }

    void settlesFromPullRequest_data()
    {
        QTest::addColumn<int>("pr"); // PullRequest
        QTest::addColumn<int>("state"); // 0 plain, 1 settled, 2 never, 3 archived
        QTest::addColumn<bool>("busy");
        QTest::addColumn<bool>("settles");
        QTest::newRow("merged") << int(PullRequest::Merged) << 0 << false << true;
        QTest::newRow("closed") << int(PullRequest::Closed) << 0 << false << true;
        QTest::newRow("open") << int(PullRequest::Open) << 0 << false << false;
        QTest::newRow("none") << int(PullRequest::None) << 0 << false << false;
        QTest::newRow("merged, busy") << int(PullRequest::Merged) << 0 << true << false;
        QTest::newRow("merged, settled") << int(PullRequest::Merged) << 1 << false << false;
        QTest::newRow("merged, never") << int(PullRequest::Merged) << 2 << false << false;
        QTest::newRow("merged, archived") << int(PullRequest::Merged) << 3 << false << false;
    }
    void settlesFromPullRequest()
    {
        QFETCH(int, pr);
        QFETCH(int, state);
        QFETCH(bool, busy);
        QFETCH(bool, settles);
        State s;
        Facts f;
        if (state == 1)
            s.settled = at(u"2026-11-20 10:00"_s);
        s.neverAutoSettle = state == 2;
        f.archived = state == 3;
        QCOMPARE(brulion::codethreads::settlesFromPullRequest(s, f, PullRequest(pr), busy), settles);
    }

    void cleanupSetting()
    {
        QCOMPARE(int(cleanupOf(u"never").when), int(Cleanup::When::Never));
        QCOMPARE(int(cleanupOf(u"merged").when), int(Cleanup::When::Merged));
        QCOMPARE(int(cleanupOf(u"7").when), int(Cleanup::When::Days));
        QCOMPARE(cleanupOf(u"7").days, 7);
        QCOMPARE(cleanupOf(u"30").days, 30);
        QCOMPARE(int(cleanupOf(u"1").when), int(Cleanup::When::Days));
        QCOMPARE(cleanupOf(u"1").days, 1);
        QCOMPARE(int(cleanupOf(u"0").when), int(Cleanup::When::Never));
        QCOMPARE(int(cleanupOf(u"-3").when), int(Cleanup::When::Never));
        QCOMPARE(int(cleanupOf(u"jutro").when), int(Cleanup::When::Never));
        QCOMPARE(int(cleanupOf(u"").when), int(Cleanup::When::Never));
    }

    void removesWorktree_data()
    {
        QTest::addColumn<QString>("settled");
        QTest::addColumn<int>("pr");
        QTest::addColumn<QString>("setting");
        QTest::addColumn<int>("changes");
        QTest::addColumn<bool>("others");
        QTest::addColumn<bool>("removes");
        // (now: 2026-11-28 12:00)
        QTest::newRow("merged") << "2026-11-28 11:00" << int(PullRequest::Merged) << "merged" << 0 << false << true;
        QTest::newRow("merged, days") << "2026-11-28 11:00" << int(PullRequest::Merged) << "7" << 0 << false << true;
        QTest::newRow("open, merged only") << "2026-11-01 11:00" << int(PullRequest::Open) << "merged" << 0 << false << false;
        QTest::newRow("closed is no merge") << "2026-11-28 11:00" << int(PullRequest::Closed) << "merged" << 0 << false << false;
        QTest::newRow("7 days settled") << "2026-11-21 12:00" << int(PullRequest::None) << "7" << 0 << false << true;
        QTest::newRow("6 days settled") << "2026-11-22 12:00" << int(PullRequest::Open) << "7" << 0 << false << false;
        QTest::newRow("not settled") << "" << int(PullRequest::Merged) << "merged" << 0 << false << false;
        QTest::newRow("changes") << "2026-11-01 11:00" << int(PullRequest::Merged) << "7" << 1 << false << false;
        QTest::newRow("check failed") << "2026-11-01 11:00" << int(PullRequest::Merged) << "7" << -1 << false << false;
        QTest::newRow("another thread there") << "2026-11-01 11:00" << int(PullRequest::Merged) << "7" << 0 << true << false;
        QTest::newRow("never") << "2026-11-01 11:00" << int(PullRequest::Merged) << "never" << 0 << false << false;
    }
    void removesWorktree()
    {
        QFETCH(QString, settled);
        QFETCH(int, pr);
        QFETCH(QString, setting);
        QFETCH(int, changes);
        QFETCH(bool, others);
        QFETCH(bool, removes);
        State s;
        s.settled = at(settled);
        QCOMPARE(brulion::codethreads::removesWorktree(s, PullRequest(pr), cleanupOf(setting), at(u"2026-11-28 12:00"_s),
                                                       changes, others),
                 removes);
    }

    void timesRead_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("time");
        QTest::newRow("canonical") << "2026-11-28 10:15" << "2026-11-28 10:15";
        QTest::newRow("T, seconds") << "2026-11-28T10:15:30" << "2026-11-28 10:15";
        QTest::newRow("single quotes") << "'2026-11-28 10:15'" << "2026-11-28 10:15";
        QTest::newRow("spaces") << "  2026-11-28 10:15  " << "2026-11-28 10:15";
        QTest::newRow("date") << "2026-11-28" << "2026-11-28 00:00";
        QTest::newRow("words") << "jutro" << "";
        QTest::newRow("empty") << "" << "";
        QTest::newRow("one quote") << "\"2026-11-28 10:15" << "";
        QTest::newRow("double quotes") << "\"2026-11-28 10:15\"" << "2026-11-28 10:15";
        QTest::newRow("two kinds of quotes") << "\"2026-11-28 10:15'" << "";
    }
    void timesRead()
    {
        QFETCH(QString, value);
        QFETCH(QString, time);
        const QDateTime t = brulion::codethreads::parseTime(value);
        QCOMPARE(t.isValid() ? formatTime(t) : QString(), time);
    }
};

QTEST_GUILESS_MAIN(TestCodeThreads)
#include "tst_codethreads.moc"
