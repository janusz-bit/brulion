// Habits (habits/habits.h, habits/habitcache.h): schedules in Polish and English (the recurrence
// parser where it fits, counts per week / month / year, weekday lists), malformed ones refused,
// formatting that reads back; names (links as labels, tags dropped) and keys; definitions (blocks,
// a page's frontmatter) and checks (#nawyk / #habit tasks of daily notes only); streaks, rates and
// what counts as missed under each schedule — skipped days, a today not done yet, days without a
// note, partial first periods, year boundaries, DST (calendar days only), weekStart; the month
// grid; HabitCache following an index note by note.
#include "habits/habitcache.h"
#include "habits/habits.h"

#include "index/vaultindex.h"
#include "markdown/parse.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::habits;

Q_DECLARE_METATYPE(brulion::habits::Schedule)

namespace {

Schedule days(int interval = 1)
{
    Schedule s;
    s.kind = Schedule::Kind::Days;
    s.interval = interval;
    return s;
}

Schedule weekdays(QList<int> list, int interval = 1)
{
    Schedule s;
    s.kind = Schedule::Kind::Weekdays;
    s.weekdays = std::move(list);
    s.interval = interval;
    return s;
}

Schedule monthDay(int day, int interval = 1)
{
    Schedule s;
    s.kind = Schedule::Kind::MonthDay;
    s.monthDay = day;
    s.interval = interval;
    return s;
}

Schedule times(int n, Schedule::Period period, int interval = 1)
{
    Schedule s;
    s.kind = Schedule::Kind::Times;
    s.times = n;
    s.period = period;
    s.interval = interval;
    return s;
}

constexpr auto Week = Schedule::Period::Week;
constexpr auto Month = Schedule::Period::Month;
constexpr auto Year = Schedule::Period::Year;

/// "2026-11-01:x 2026-11-02:- 2026-11-03:o" -> marks (x done, - skipped, o open).
DayMarks marksOf(const QString &spec)
{
    DayMarks out;
    for (const QString &item : spec.split(u' ', Qt::SkipEmptyParts)) {
        const QDate d = QDate::fromString(item.section(u':', 0, 0), Qt::ISODate);
        const QString m = item.section(u':', 1, 1);
        out.insert(d, m == u"x"_s ? Mark::Done : m == u"-"_s ? Mark::Skipped : Mark::Open);
    }
    return out;
}

Habit habit(const QString &schedule, QDate start = {})
{
    Habit h;
    h.name = u"Medytacja"_s;
    h.key = habitKey(h.name);
    h.scheduleText = schedule;
    h.schedule = parseSchedule(schedule);
    h.start = start;
    return h;
}

QString statesText(const QList<DayState> &states)
{
    QString out;
    for (const DayState s : states) {
        switch (s) {
        case DayState::Outside: out += u'.'; break;
        case DayState::Future: out += u'f'; break;
        case DayState::Rest: out += u'_'; break;
        case DayState::Due: out += u'!'; break;
        case DayState::Done: out += u'x'; break;
        case DayState::Skipped: out += u'-'; break;
        case DayState::Missed: out += u'm'; break;
        }
    }
    return out;
}

} // namespace

class TestHabits : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void parse_data();
    void parse();
    void malformed_data();
    void malformed();
    void formatReadsBack_data();
    void formatReadsBack();
    void formatWords();
    void scheduled_data();
    void scheduled();
    void names_data();
    void names();
    void keys();
    void definitions();
    void checks();
    void checksOnlyInJournals();
    void stats_data();
    void stats();
    void grid_data();
    void grid();
    void gridBounded();
    void markNames();
    void cache();
    void cacheDuplicatesAndUndefined();
    void cacheManyNotes();
};

void TestHabits::parse_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<Schedule>("expected");

    QTest::newRow("empty") << QString() << days();
    QTest::newRow("tak") << u"tak"_s << days();
    QTest::newRow("codziennie") << u"codziennie"_s << days();
    QTest::newRow("Codziennie capitalised") << u"  Codziennie "_s << days();
    QTest::newRow("daily") << u"daily"_s << days();
    QTest::newRow("every day") << u"every day"_s << days();
    QTest::newRow("co 2 dni") << u"co 2 dni"_s << days(2);
    QTest::newRow("every 3 days") << u"every 3 days"_s << days(3);
    QTest::newRow("with repeat mark") << u"🔁 codziennie"_s << days();
    QTest::newRow("w dni robocze") << u"w dni robocze"_s << weekdays({1, 2, 3, 4, 5});
    QTest::newRow("every weekday") << u"every weekday"_s << weekdays({1, 2, 3, 4, 5});
    QTest::newRow("pn, śr, pt") << u"pn, śr, pt"_s << weekdays({1, 3, 5});
    QTest::newRow("without diacritics") << u"pn, sr, pt"_s << weekdays({1, 3, 5});
    QTest::newRow("poniedziałek i czwartek") << u"poniedziałek i czwartek"_s << weekdays({1, 4});
    QTest::newRow("mon, fri") << u"mon, fri"_s << weekdays({1, 5});
    QTest::newRow("weekend") << u"sobota, niedziela"_s << weekdays({6, 7});
    QTest::newRow("co tydzień w poniedziałek, piątek") << u"co tydzień w poniedziałek, piątek"_s << weekdays({1, 5});
    QTest::newRow("every 2 weeks on Monday") << u"every 2 weeks on Monday"_s << weekdays({1}, 2);
    QTest::newRow("w każdy piątek") << u"w każdy piątek"_s << weekdays({5});
    QTest::newRow("co tydzień = once a week") << u"co tydzień"_s << times(1, Week);
    QTest::newRow("every week") << u"every week"_s << times(1, Week);
    QTest::newRow("every 2 weeks") << u"every 2 weeks"_s << times(1, Week, 2);
    QTest::newRow("co miesiąc") << u"co miesiąc"_s << times(1, Month);
    QTest::newRow("every year") << u"every year"_s << times(1, Year);
    QTest::newRow("co miesiąc 15.") << u"co miesiąc 15."_s << monthDay(15);
    QTest::newRow("every month on the 15th") << u"every month on the 15th"_s << monthDay(15);
    QTest::newRow("co miesiąc ostatniego") << u"co miesiąc ostatniego"_s << monthDay(0);
    QTest::newRow("every month on the last") << u"every month on the last"_s << monthDay(0);
    QTest::newRow("when done ignored") << u"codziennie po wykonaniu"_s << days();
    QTest::newRow("3 razy w tygodniu") << u"3 razy w tygodniu"_s << times(3, Week);
    QTest::newRow("3 razy w tygodniu.") << u"3 razy w tygodniu."_s << times(3, Week);
    QTest::newRow("3x w tygodniu") << u"3x w tygodniu"_s << times(3, Week);
    QTest::newRow("3x/tydz") << u"3x/tydz"_s << times(3, Week);
    QTest::newRow("3/tydzień") << u"3/tydzień"_s << times(3, Week);
    QTest::newRow("3 dni w tygodniu") << u"3 dni w tygodniu"_s << times(3, Week);
    QTest::newRow("raz w tygodniu") << u"raz w tygodniu"_s << times(1, Week);
    QTest::newRow("dwa razy w miesiącu") << u"dwa razy w miesiącu"_s << times(2, Month);
    QTest::newRow("DWA RAZY W MIESIĄCU") << u"DWA RAZY W MIESIĄCU"_s << times(2, Month);
    QTest::newRow("5 razy na 2 tygodnie") << u"5 razy na 2 tygodnie"_s << times(5, Week, 2);
    QTest::newRow("raz w roku") << u"raz w roku"_s << times(1, Year);
    QTest::newRow("3 times a week") << u"3 times a week"_s << times(3, Week);
    QTest::newRow("3x a week") << u"3x a week"_s << times(3, Week);
    QTest::newRow("3 per week") << u"3 per week"_s << times(3, Week);
    QTest::newRow("3x/week") << u"3x/week"_s << times(3, Week);
    QTest::newRow("once a week") << u"once a week"_s << times(1, Week);
    QTest::newRow("twice a month") << u"twice a month"_s << times(2, Month);
    QTest::newRow("3 times every 2 weeks") << u"3 times every 2 weeks"_s << times(3, Week, 2);
    QTest::newRow("7 a week") << u"7 razy w tygodniu"_s << times(7, Week);
    QTest::newRow("31 a month") << u"31 razy w miesiącu"_s << times(31, Month);
    QTest::newRow("14 every 2 weeks") << u"14 razy na 2 tygodnie"_s << times(14, Week, 2);
}

void TestHabits::parse()
{
    QFETCH(QString, text);
    QFETCH(Schedule, expected);
    const auto s = parseSchedule(text);
    QVERIFY2(s.has_value(), qPrintable(text));
    QCOMPARE(s->kind, expected.kind);
    QCOMPARE(s->interval, expected.interval);
    QCOMPARE(s->weekdays, expected.weekdays);
    QCOMPARE(s->monthDay, expected.monthDay);
    QCOMPARE(s->times, expected.times);
    if (expected.kind == Schedule::Kind::Times)
        QCOMPARE(s->period, expected.period);
}

void TestHabits::malformed_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("words") << u"kiedy mi się zachce"_s;
    QTest::newRow("every other tuesday") << u"co drugi wtorek"_s;
    QTest::newRow("nth weekday of a month") << u"every month on the 2nd Tuesday"_s;
    QTest::newRow("8 a week") << u"8 razy w tygodniu"_s;
    QTest::newRow("0 a week") << u"0 razy w tygodniu"_s;
    QTest::newRow("32 a month") << u"32 razy w miesiącu"_s;
    QTest::newRow("no period") << u"3 razy"_s;
    QTest::newRow("unknown period") << u"3 razy w dekadzie"_s;
    QTest::newRow("huge interval") << u"3 razy na 5000 tygodni"_s;
    QTest::newRow("huge days interval") << u"co 5000 dni"_s;
    QTest::newRow("every 2nd business day") << u"every 2 weekdays"_s;
    QTest::newRow("only punctuation") << u"!!!"_s;
    QTest::newRow("negative") << u"-3 razy w tygodniu"_s;
    QTest::newRow("lone surrogate") << QString(QChar(0xD800)) + u" razy w tygodniu"_s;
}

void TestHabits::malformed()
{
    QFETCH(QString, text);
    QVERIFY2(!parseSchedule(text).has_value(), qPrintable(text));
}

void TestHabits::formatReadsBack_data()
{
    QTest::addColumn<Schedule>("schedule");
    QTest::newRow("daily") << days();
    QTest::newRow("every 2 days") << days(2);
    QTest::newRow("weekdays") << weekdays({1, 2, 3, 4, 5});
    QTest::newRow("mon wed fri") << weekdays({1, 3, 5});
    QTest::newRow("sat sun") << weekdays({6, 7});
    QTest::newRow("every 2 weeks on monday") << weekdays({1}, 2);
    QTest::newRow("15th") << monthDay(15);
    QTest::newRow("last") << monthDay(0);
    QTest::newRow("every 3 months on the 1st") << monthDay(1, 3);
    QTest::newRow("once a week") << times(1, Week);
    QTest::newRow("3 a week") << times(3, Week);
    QTest::newRow("2 a month") << times(2, Month);
    QTest::newRow("once a year") << times(1, Year);
    QTest::newRow("once every 2 weeks") << times(1, Week, 2);
    QTest::newRow("5 every 2 weeks") << times(5, Week, 2);
    QTest::newRow("3 every 2 months") << times(3, Month, 2);
    QTest::newRow("2 every 5 years") << times(2, Year, 5);
}

void TestHabits::formatReadsBack()
{
    QFETCH(Schedule, schedule);
    for (const auto locale : {dates::UiLocale::Polish, dates::UiLocale::English}) {
        const QString text = formatSchedule(schedule, locale);
        QVERIFY(!text.isEmpty());
        const auto back = parseSchedule(text);
        QVERIFY2(back.has_value(), qPrintable(text));
        QVERIFY2(*back == schedule, qPrintable(text));
    }
}

void TestHabits::formatWords()
{
    QCOMPARE(formatSchedule(days(), dates::UiLocale::Polish), u"codziennie"_s);
    QCOMPARE(formatSchedule(days(), dates::UiLocale::English), u"every day"_s);
    QCOMPARE(formatSchedule(times(3, Week), dates::UiLocale::Polish), u"3 razy w tygodniu"_s);
    QCOMPARE(formatSchedule(times(1, Month), dates::UiLocale::Polish), u"raz w miesiącu"_s);
    QCOMPARE(formatSchedule(times(2, Month), dates::UiLocale::English), u"twice a month"_s);
    QCOMPARE(formatSchedule(times(5, Week, 2), dates::UiLocale::Polish), u"5 razy na 2 tygodnie"_s);
    QCOMPARE(formatSchedule(weekdays({1, 2, 3, 4, 5}), dates::UiLocale::Polish), u"w dni robocze"_s);
    QCOMPARE(formatSchedule(weekdays({1, 3, 5}), dates::UiLocale::English), u"Mon, Wed, Fri"_s);
}

void TestHabits::scheduled_data()
{
    QTest::addColumn<Schedule>("schedule");
    QTest::addColumn<QDate>("start");
    QTest::addColumn<QDate>("day");
    QTest::addColumn<int>("weekStart");
    QTest::addColumn<bool>("due");

    const QDate nov1(2026, 11, 1);
    QTest::newRow("daily on start") << days() << nov1 << nov1 << 1 << true;
    QTest::newRow("daily before start") << days() << nov1 << QDate(2026, 10, 31) << 1 << false;
    QTest::newRow("every 2 days, 2nd") << days(2) << nov1 << QDate(2026, 11, 2) << 1 << false;
    QTest::newRow("every 2 days, 3rd") << days(2) << nov1 << QDate(2026, 11, 3) << 1 << true;
    QTest::newRow("every 2 days across the year") << days(2) << QDate(2026, 12, 31) << QDate(2027, 1, 2) << 1 << true;
    QTest::newRow("mon/fri on a friday") << weekdays({1, 5}) << nov1 << QDate(2026, 11, 6) << 1 << true;
    QTest::newRow("mon/fri on a thursday") << weekdays({1, 5}) << nov1 << QDate(2026, 11, 5) << 1 << false;
    // Every 2 weeks from a Wednesday: weeks counted from the start's week (Monday-based).
    QTest::newRow("2 weeks: next week's monday")
            << weekdays({1}, 2) << QDate(2026, 11, 4) << QDate(2026, 11, 9) << 1 << false;
    QTest::newRow("2 weeks: the monday after")
            << weekdays({1}, 2) << QDate(2026, 11, 4) << QDate(2026, 11, 16) << 1 << true;
    QTest::newRow("2 weeks across the year")
            << weekdays({1}, 2) << QDate(2026, 12, 28) << QDate(2027, 1, 11) << 1 << true;
    QTest::newRow("2 weeks across the year, off")
            << weekdays({1}, 2) << QDate(2026, 12, 28) << QDate(2027, 1, 4) << 1 << false;
    // Weeks starting on Sunday: Sunday 8 Nov begins the week after the start's.
    QTest::newRow("2 weeks, Sunday weeks")
            << weekdays({7}, 2) << QDate(2026, 11, 2) << QDate(2026, 11, 8) << 7 << false;
    QTest::newRow("2 weeks, Sunday weeks, same week")
            << weekdays({7, 1}, 2) << QDate(2026, 11, 1) << QDate(2026, 11, 2) << 7 << true;
    QTest::newRow("31st in April is the 30th") << monthDay(31) << QDate(2026, 1, 1) << QDate(2026, 4, 30) << 1 << true;
    QTest::newRow("last day of a leap February") << monthDay(0) << QDate(2028, 1, 1) << QDate(2028, 2, 29) << 1 << true;
    QTest::newRow("last day, the 28th of a leap year")
            << monthDay(0) << QDate(2028, 1, 1) << QDate(2028, 2, 28) << 1 << false;
    QTest::newRow("every 3 months, off month")
            << monthDay(1, 3) << QDate(2026, 11, 1) << QDate(2026, 12, 1) << 1 << false;
    QTest::newRow("every 3 months across the year")
            << monthDay(1, 3) << QDate(2026, 11, 1) << QDate(2027, 2, 1) << 1 << true;
    QTest::newRow("times: no due days") << times(3, Week) << nov1 << QDate(2026, 11, 2) << 1 << false;
}

void TestHabits::scheduled()
{
    QFETCH(Schedule, schedule);
    QFETCH(QDate, start);
    QFETCH(QDate, day);
    QFETCH(int, weekStart);
    QFETCH(bool, due);
    QCOMPARE(isScheduled(schedule, start, day, Qt::DayOfWeek(weekStart)), due);
}

void TestHabits::names_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("name");
    QTest::newRow("plain") << u"Medytacja"_s << u"Medytacja"_s;
    QTest::newRow("tag dropped") << u"Medytacja #nawyk"_s << u"Medytacja"_s;
    QTest::newRow("tags anywhere") << u"#habit Czytanie  książek   #zdrowie"_s << u"Czytanie książek"_s;
    QTest::newRow("link as its page") << u"[[Bieganie]] #nawyk"_s << u"Bieganie"_s;
    QTest::newRow("link with folder") << u"[[Zdrowie/Bieganie]]"_s << u"Bieganie"_s;
    QTest::newRow("link alias") << u"[[Zdrowie/Bieganie|Biegać]]"_s << u"Biegać"_s;
    QTest::newRow("link heading") << u"[[Bieganie#Plan]]"_s << u"Bieganie"_s;
    QTest::newRow("bracket tag") << u"#[[Jan Kowalski]] spacer"_s << u"spacer"_s;
    QTest::newRow("only a tag") << u"#nawyk"_s << QString();
    QTest::newRow("first line only") << u"Joga\nz Anią"_s << u"Joga"_s;
    QTest::newRow("code keeps its hash") << u"Ćwiczenia `#nie-tag`"_s << u"Ćwiczenia `#nie-tag`"_s;
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("unclosed link") << u"[[Bieganie #nawyk"_s << u"[[Bieganie"_s;
}

void TestHabits::names()
{
    QFETCH(QString, text);
    QFETCH(QString, name);
    QCOMPARE(habitName(text), name);
}

void TestHabits::keys()
{
    QCOMPARE(habitKey(u"  ŻÓŁW   biegnie "_s), habitKey(u"żółw biegnie"_s));
    QCOMPARE(habitKey(u"żółw"_s), habitKey(u"żółw"_s));  // decomposed = composed
    QVERIFY(habitKey(u"Łódź"_s) != habitKey(u"Lodz"_s)); // diacritics matter
    QVERIFY(habitKey(u"   "_s).isEmpty());
    QVERIFY(isCheckTag(u"nawyk"));
    QVERIFY(isCheckTag(u"habit"));
    QVERIFY(!isCheckTag(u"nawyki"));
    QCOMPARE(checkTag(dates::UiLocale::Polish), u"nawyk"_s);
    QCOMPARE(checkTag(dates::UiLocale::English), u"habit"_s);
}

void TestHabits::definitions()
{
    const QString page = u"---\nhabit: 3 razy w tygodniu\nstart: 2026-10-01\n---\n"
                         "- Medytacja\n  habit:: codziennie\n"
                         "- Czytanie #zdrowie\n  nawyk:: pn, śr, pt\n  start:: [[2026-10-05]]\n"
                         "- Coś dziwnego\n  habit:: kiedy mi się zachce\n"
                         "- Zwykły punkt\n  kolor:: zielony\n"
                         "- #tylko-tag\n  habit:: codziennie\n"
                         "\nAkapit z nawykiem\nhabit:: co miesiąc 15.\n"_s;
    const ParsedNote note = markdown::parseNote(u"Zdrowie/Bieganie.md"_s, page);
    const QList<Habit> list = definitionsOf(u"Zdrowie/Bieganie.md"_s, note);
    QCOMPARE(list.size(), 5);
    QCOMPARE(list[0].name, u"Bieganie"_s);
    QCOMPARE(list[0].blockIndex, -1);
    QVERIFY(list[0].schedule && *list[0].schedule == times(3, Week));
    QCOMPARE(list[0].start, QDate(2026, 10, 1));
    QCOMPARE(list[1].name, u"Medytacja"_s);
    QCOMPARE(list[1].line, 4);
    QVERIFY(list[1].schedule && *list[1].schedule == days());
    QVERIFY(!list[1].start.isValid());
    QCOMPARE(list[2].name, u"Czytanie"_s);
    QCOMPARE(list[2].scheduleText, u"pn, śr, pt"_s);
    QCOMPARE(list[2].start, QDate(2026, 10, 5));
    QCOMPARE(list[3].name, u"Coś dziwnego"_s);
    QVERIFY(!list[3].schedule);
    QVERIFY(!list[3].isValid());
    QCOMPARE(list[4].name, u"Akapit z nawykiem"_s);
    QVERIFY(list[4].schedule && *list[4].schedule == monthDay(15));

    // Leading `habit::` lines of a Logseq page: the page is the habit, once.
    const ParsedNote logseq = markdown::parseNote(u"Joga.md"_s, u"habit:: pn, czw\n\n- notatki\n"_s);
    const QList<Habit> one = definitionsOf(u"Joga.md"_s, logseq);
    QCOMPARE(one.size(), 1);
    QCOMPARE(one[0].name, u"Joga"_s);
    QVERIFY(one[0].schedule && *one[0].schedule == weekdays({1, 4}));

    // A malformed start is no start.
    const ParsedNote bad = markdown::parseNote(u"a.md"_s, u"- Spacer\n  habit:: codziennie\n  start:: wczoraj\n"_s);
    QVERIFY(!definitionsOf(u"a.md"_s, bad).value(0).start.isValid());
}

void TestHabits::checks()
{
    const QString journal = u"- [x] Medytacja #nawyk ✅ 2026-11-03\n"
                            "- [-] Czytanie #habit\n"
                            "- [ ] [[Bieganie]] #nawyk\n"
                            "- [x] Zakupy\n"
                            "- Medytacja #nawyk\n"
                            "- DONE Joga #nawyk\n"
                            "- [/] Pływanie #nawyk\n"
                            "- [x] #nawyk\n"
                            "- [x] Definicja #nawyk\n  habit:: codziennie\n"
                            "- Rano\n  - [x] Rozciąganie #nawyk\n"_s;
    const ParsedNote note = markdown::parseNote(u"journals/2026-11-03.md"_s, journal);
    QVERIFY(note.journalDate);
    const QList<Check> list = checksOf(note);
    QCOMPARE(list.size(), 6);
    QCOMPARE(list[0].name, u"Medytacja"_s);
    QCOMPARE(list[0].mark, Mark::Done);
    QCOMPARE(list[0].date, QDate(2026, 11, 3));
    QCOMPARE(list[0].line, 0);
    QCOMPARE(list[1].name, u"Czytanie"_s);
    QCOMPARE(list[1].mark, Mark::Skipped);
    QCOMPARE(list[2].name, u"Bieganie"_s);
    QCOMPARE(list[2].mark, Mark::Open);
    QCOMPARE(list[3].name, u"Joga"_s);
    QCOMPARE(list[3].mark, Mark::Done);
    QCOMPARE(list[4].name, u"Pływanie"_s);
    QCOMPARE(list[4].mark, Mark::Open);
    QCOMPARE(list[5].name, u"Rozciąganie"_s);
    QCOMPARE(list[5].line, 11);

    QCOMPARE(strongerMark(Mark::Open, Mark::Skipped), Mark::Skipped);
    QCOMPARE(strongerMark(Mark::Done, Mark::Skipped), Mark::Done);
}

void TestHabits::checksOnlyInJournals()
{
    const QString text = u"- [x] Medytacja #nawyk\n"_s;
    QVERIFY(checksOf(markdown::parseNote(u"Projekty/Plan.md"_s, text)).isEmpty());
    // Another file name format of the journal folder.
    VaultConfig config;
    config.journalFileFormat = u"dd.MM.yyyy"_s;
    const ParsedNote note = markdown::parseNote(u"journals/03.11.2026.md"_s, text, config);
    QCOMPARE(checksOf(note).value(0).date, QDate(2026, 11, 3));
}

void TestHabits::stats_data()
{
    QTest::addColumn<QString>("schedule");
    QTest::addColumn<QDate>("start");
    QTest::addColumn<QString>("marks");
    QTest::addColumn<QDate>("today");
    QTest::addColumn<int>("weekStart");
    QTest::addColumn<int>("current");
    QTest::addColumn<int>("longest");
    QTest::addColumn<int>("met");
    QTest::addColumn<int>("expected");
    QTest::addColumn<bool>("dueToday");

    const QDate none;
    QTest::newRow("daily, three in a row") << u"codziennie"_s << none << u"2026-11-01:x 2026-11-02:x 2026-11-03:x"_s
                                           << QDate(2026, 11, 3) << 1 << 3 << 3 << 3 << 3 << false;
    QTest::newRow("daily, today not done yet keeps the streak")
            << u"codziennie"_s << none << u"2026-11-01:x 2026-11-02:x"_s << QDate(2026, 11, 3) << 1 << 2 << 2 << 2 << 2
            << true;
    QTest::newRow("daily, an open check today is pending too")
            << u"codziennie"_s << none << u"2026-11-01:x 2026-11-02:x 2026-11-03:o"_s << QDate(2026, 11, 3) << 1 << 2
            << 2 << 2 << 2 << true;
    QTest::newRow("daily, a day without a note breaks it") << u"codziennie"_s << none << u"2026-11-01:x 2026-11-03:x"_s
                                                           << QDate(2026, 11, 3) << 1 << 1 << 1 << 2 << 3 << false;
    QTest::newRow("daily, an open check yesterday is missed")
            << u"codziennie"_s << none << u"2026-11-01:x 2026-11-02:o 2026-11-03:x"_s << QDate(2026, 11, 3) << 1 << 1
            << 1 << 2 << 3 << false;
    QTest::newRow("daily, a skipped day is excused")
            << u"codziennie"_s << none << u"2026-11-01:x 2026-11-02:- 2026-11-03:x"_s << QDate(2026, 11, 3) << 1 << 2
            << 2 << 2 << 2 << false;
    QTest::newRow("daily, longest earlier")
            << u"codziennie"_s << none << u"2026-10-01:x 2026-10-02:x 2026-10-03:x 2026-10-05:x"_s << QDate(2026, 10, 5)
            << 1 << 1 << 3 << 4 << 5 << false;
    QTest::newRow("daily, explicit start before the first check")
            << u"codziennie"_s << QDate(2026, 10, 30) << u"2026-11-02:x 2026-11-03:x"_s << QDate(2026, 11, 3) << 1 << 2
            << 2 << 2 << 5 << false;
    QTest::newRow("daily, checks before the start are not counted")
            << u"codziennie"_s << QDate(2026, 11, 2) << u"2026-10-20:x 2026-11-02:x"_s << QDate(2026, 11, 2) << 1 << 1
            << 1 << 1 << 1 << false;
    QTest::newRow("daily, across the year") << u"codziennie"_s << none << u"2026-12-30:x 2026-12-31:x 2027-01-01:x"_s
                                            << QDate(2027, 1, 1) << 1 << 3 << 3 << 3 << 3 << false;
    QTest::newRow("daily, across the DST change")
            << u"codziennie"_s << none << u"2026-10-24:x 2026-10-25:x 2026-10-26:x"_s << QDate(2026, 10, 26) << 1 << 3
            << 3 << 3 << 3 << false;
    QTest::newRow("daily, start after today")
            << u"codziennie"_s << QDate(2026, 12, 1) << u""_s << QDate(2026, 11, 3) << 1 << 0 << 0 << 0 << 0 << false;
    QTest::newRow("daily, nothing yet: starts today")
            << u"codziennie"_s << none << u""_s << QDate(2026, 11, 3) << 1 << 0 << 0 << 0 << 0 << true;
    QTest::newRow("every 2 days, today due") << u"co 2 dni"_s << none << u"2026-11-01:x 2026-11-03:x"_s
                                             << QDate(2026, 11, 5) << 1 << 2 << 2 << 2 << 2 << true;
    QTest::newRow("every 2 days, off-day extra does not count")
            << u"co 2 dni"_s << none << u"2026-11-01:x 2026-11-04:x"_s << QDate(2026, 11, 5) << 1 << 0 << 1 << 1 << 2
            << true;
    QTest::newRow("mon/wed/fri, done twice, thursday")
            << u"pn, śr, pt"_s << QDate(2026, 11, 2) << u"2026-11-02:x 2026-11-03:x 2026-11-04:x"_s
            << QDate(2026, 11, 5) << 1 << 2 << 2 << 2 << 2 << false;
    QTest::newRow("mon/wed/fri, wednesday missed, friday due")
            << u"pn, śr, pt"_s << QDate(2026, 11, 2) << u"2026-11-02:x"_s << QDate(2026, 11, 6) << 1 << 0 << 1 << 1 << 2
            << true;
    QTest::newRow("weekdays across the weekend")
            << u"w dni robocze"_s << none << u"2026-11-05:x 2026-11-06:x 2026-11-09:x"_s << QDate(2026, 11, 9) << 1 << 3
            << 3 << 3 << 3 << false;
    QTest::newRow("15th monthly") << u"co miesiąc 15."_s << QDate(2026, 9, 1) << u"2026-09-15:x 2026-10-15:x"_s
                                  << QDate(2026, 11, 3) << 1 << 2 << 2 << 2 << 2 << false;
    QTest::newRow("15th monthly, today") << u"co miesiąc 15."_s << QDate(2026, 9, 1) << u"2026-09-15:x 2026-10-15:x"_s
                                         << QDate(2026, 11, 15) << 1 << 2 << 2 << 2 << 2 << true;
    QTest::newRow("3 a week: met, then pending")
            << u"3 razy w tygodniu"_s << QDate(2026, 10, 26) << u"2026-10-26:x 2026-10-28:x 2026-10-30:x 2026-11-02:x"_s
            << QDate(2026, 11, 4) << 1 << 1 << 1 << 1 << 1 << true;
    QTest::newRow("3 a week: missed, then met this week")
            << u"3 razy w tygodniu"_s << QDate(2026, 10, 26)
            << u"2026-10-26:x 2026-10-28:x 2026-11-02:x 2026-11-03:x 2026-11-04:x"_s << QDate(2026, 11, 4) << 1 << 1
            << 1 << 1 << 2 << false;
    QTest::newRow("3 a week: a skip lowers the need")
            << u"3 razy w tygodniu"_s << QDate(2026, 10, 26) << u"2026-10-26:x 2026-10-27:- 2026-10-28:x"_s
            << QDate(2026, 11, 2) << 1 << 1 << 1 << 1 << 1 << true;
    QTest::newRow("3 a week: a week all skipped is excused")
            << u"3 razy w tygodniu"_s << QDate(2026, 10, 19)
            << u"2026-10-19:x 2026-10-20:x 2026-10-21:x 2026-10-26:- 2026-10-27:- 2026-10-28:- 2026-11-02:x "
               "2026-11-03:x 2026-11-04:x"_s
            << QDate(2026, 11, 4) << 1 << 2 << 2 << 2 << 2 << false;
    QTest::newRow("3 a week: a partial first week counts only when met")
            << u"3 razy w tygodniu"_s << none << u"2026-10-31:x"_s << QDate(2026, 11, 4) << 1 << 0 << 0 << 0 << 0
            << true;
    QTest::newRow("once a week, Sunday weeks across the year")
            << u"raz w tygodniu"_s << QDate(2026, 12, 27) << u"2026-12-27:x 2027-01-05:x"_s << QDate(2027, 1, 10) << 7
            << 2 << 2 << 2 << 2 << true;
    // Monday weeks: 27 Dec ends its week (met), 28 Dec – 3 Jan has nothing (missed).
    QTest::newRow("once a week, Monday weeks: a week missed")
            << u"raz w tygodniu"_s << QDate(2026, 12, 27) << u"2026-12-27:x 2027-01-05:x"_s << QDate(2027, 1, 10) << 1
            << 1 << 1 << 2 << 3 << false;
    QTest::newRow("twice a month, last month missed")
            << u"dwa razy w miesiącu"_s << QDate(2026, 9, 1) << u"2026-09-03:x 2026-09-20:x 2026-10-10:x 2026-11-02:x"_s
            << QDate(2026, 11, 3) << 1 << 0 << 1 << 1 << 2 << true;
    QTest::newRow("once every 2 weeks") << u"every 2 weeks"_s << QDate(2026, 10, 19) << u"2026-10-20:x 2026-11-03:x"_s
                                        << QDate(2026, 11, 3) << 1 << 2 << 2 << 2 << 2 << false;
}

void TestHabits::stats()
{
    QFETCH(QString, schedule);
    QFETCH(QDate, start);
    QFETCH(QString, marks);
    QFETCH(QDate, today);
    QFETCH(int, weekStart);
    QFETCH(int, current);
    QFETCH(int, longest);
    QFETCH(int, met);
    QFETCH(int, expected);
    QFETCH(bool, dueToday);
    const Habit h = habit(schedule, start);
    QVERIFY(h.isValid());
    const Stats st = statsOf(h, marksOf(marks), today, Qt::DayOfWeek(weekStart));
    QCOMPARE(st.currentStreak, current);
    QCOMPARE(st.longestStreak, longest);
    QCOMPARE(st.met, met);
    QCOMPARE(st.expected, expected);
    QCOMPARE(st.dueToday, dueToday);
    QVERIFY(st.currentStreak <= st.longestStreak);
    if (expected == 0)
        QCOMPARE(st.rate(), -1.0);
    else
        QCOMPARE(st.rate(), double(met) / expected);
}

void TestHabits::grid_data()
{
    QTest::addColumn<QString>("schedule");
    QTest::addColumn<QDate>("start");
    QTest::addColumn<QString>("marks");
    QTest::addColumn<QDate>("from");
    QTest::addColumn<QDate>("to");
    QTest::addColumn<QDate>("today");
    QTest::addColumn<QString>("states");

    // Mon 26 Oct … Sun 8 Nov 2026, today Thursday 5 Nov.
    const QDate from(2026, 10, 26);
    const QDate to(2026, 11, 8);
    const QDate today(2026, 11, 5);
    QTest::newRow("mon/wed/fri") << u"pn, śr, pt"_s << QDate(2026, 10, 28)
                                 << u"2026-10-28:x 2026-10-29:x 2026-11-02:- 2026-11-05:o"_s << from << to << today
                                 << u"..xxm__-_m_fff"_s;
    QTest::newRow("daily, today due") << u"codziennie"_s << QDate(2026, 11, 1) << u"2026-11-01:x 2026-11-03:x"_s << from
                                      << to << today << u"......xmxm!fff"_s;
    QTest::newRow("daily, today done") << u"codziennie"_s << QDate(2026, 11, 4) << u"2026-11-04:x 2026-11-05:x"_s
                                       << from << to << today << u".........xxfff"_s;
    QTest::newRow("3 a week, period open")
            << u"3 razy w tygodniu"_s << QDate(2026, 10, 26) << u"2026-10-26:x 2026-11-03:x"_s << from << to << today
            << u"x_______x_!fff"_s;
    QTest::newRow("3 a week, period met")
            << u"3 razy w tygodniu"_s << QDate(2026, 10, 26) << u"2026-11-02:x 2026-11-03:x 2026-11-04:x"_s << from
            << to << today << u"_______xxx_fff"_s;
    QTest::newRow("malformed: outside") << u"kiedy mi się zachce"_s << QDate(2026, 11, 1) << u"2026-11-02:x"_s << from
                                        << to << today << u".......x...fff"_s;
}

void TestHabits::grid()
{
    QFETCH(QString, schedule);
    QFETCH(QDate, start);
    QFETCH(QString, marks);
    QFETCH(QDate, from);
    QFETCH(QDate, to);
    QFETCH(QDate, today);
    QFETCH(QString, states);
    QCOMPARE(statesText(daysOf(habit(schedule, start), marksOf(marks), from, to, today)), states);
}

void TestHabits::gridBounded()
{
    const Habit h = habit(u"codziennie"_s, QDate(2026, 1, 1));
    QCOMPARE(daysOf(h, {}, QDate(2026, 1, 1), QDate(2030, 1, 1), QDate(2026, 6, 1)).size(), 400);
    QVERIFY(daysOf(h, {}, QDate(2026, 2, 1), QDate(2026, 1, 1), QDate(2026, 6, 1)).isEmpty());
    QVERIFY(daysOf(h, {}, QDate(), QDate(2026, 1, 1), QDate(2026, 6, 1)).isEmpty());
    // A start a century ago: only the last MaxCountedDays are walked.
    const Habit old = habit(u"codziennie"_s, QDate(1926, 1, 1));
    const Stats st = statsOf(old, marksOf(u"2026-11-03:x"_s), QDate(2026, 11, 3));
    QCOMPARE(st.expected, MaxCountedDays + 1);
    QCOMPARE(st.currentStreak, 1);
    // An invalid schedule has no statistics.
    const Stats none = statsOf(habit(u"bla bla"_s), marksOf(u"2026-11-03:x"_s), QDate(2026, 11, 3));
    QCOMPARE(none.expected, 0);
    QCOMPARE(none.done, 0);
}

void TestHabits::markNames()
{
    QCOMPARE(markName(Mark::Done), u"done"_s);
    QCOMPARE(markFromName(u"Zrobione"), Mark::Done);
    QCOMPARE(markFromName(u"POMINIĘTE"), Mark::Skipped);
    QCOMPARE(markFromName(u"skip"), Mark::Skipped);
    QCOMPARE(markFromName(u"none"), Mark::Open);
    QCOMPARE(markFromName(u"brak"), Mark::Open);
    QVERIFY(!markFromName(u"może"));
    QCOMPARE(dayStateName(DayState::Missed), u"missed"_s);
    QCOMPARE(streakUnitName(StreakUnit::Weeks), u"weeks"_s);
}

void TestHabits::cache()
{
    VaultIndex index;
    index.update(u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n- Czytanie\n  habit:: pn, śr, pt\n"_s);
    index.update(u"journals/2026-11-02.md"_s, u"- [x] Medytacja #nawyk\n- [x] Czytanie #nawyk\n"_s);
    index.update(u"journals/2026-11-03.md"_s, u"- [ ] Medytacja #nawyk\n"_s);
    index.update(u"Projekty/Dom.md"_s, u"- [x] Medytacja #nawyk\n- zwykła strona\n"_s);
    HabitCache cache;
    QVERIFY(cache.sync(index));
    QVERIFY(cache.lastSync().full);
    QCOMPARE(cache.habits().size(), 2);
    QCOMPARE(cache.habits()[0].name, u"Medytacja"_s);
    QVERIFY(cache.find(u"MEDYTACJA"));
    QVERIFY(!cache.find(u"Pływanie"));
    QCOMPARE(cache.days(habitKey(u"Medytacja")), marksOf(u"2026-11-02:x 2026-11-03:o"_s));
    QCOMPARE(cache.checksOn(QDate(2026, 11, 2)).size(), 2);
    const int medytacja = cache.revision(habitKey(u"Medytacja"));
    const int czytanie = cache.revision(habitKey(u"Czytanie"));

    // Nothing changed: no work.
    QVERIFY(!cache.sync(index));
    QVERIFY(!cache.lastSync().any());

    // Ticking today's check (as the editor would): only that note is read again; only
    // Medytacja's revision moves.
    index.update(u"journals/2026-11-03.md"_s, u"- [x] Medytacja #nawyk ✅ 2026-11-03\n"_s);
    QVERIFY(cache.sync(index));
    QVERIFY(!cache.lastSync().full);
    QCOMPARE(cache.lastSync().changed, QList<VaultPath>{u"journals/2026-11-03.md"_s});
    QCOMPARE(cache.days(habitKey(u"Medytacja")).value(QDate(2026, 11, 3)), Mark::Done);
    QVERIFY(cache.revision(habitKey(u"Medytacja")) > medytacja);
    QCOMPARE(cache.revision(habitKey(u"Czytanie")), czytanie);

    // An edit of an unrelated page: read again, nothing differs.
    index.update(u"Projekty/Dom.md"_s, u"- zwykła strona, zmieniona\n"_s);
    QVERIFY(!cache.sync(index));
    QCOMPARE(cache.lastSync().changed, QList<VaultPath>{u"Projekty/Dom.md"_s});

    // Two daily notes of one day (another name the folder accepts): Done wins.
    index.update(u"journals/2026_11_02.md"_s, u"- [-] Czytanie #habit\n"_s);
    QVERIFY(!cache.sync(index)); // Done (other note) still wins: nothing differs
    index.update(u"journals/2026-11-02.md"_s, u"- [x] Medytacja #nawyk\n"_s);
    QVERIFY(cache.sync(index));
    QCOMPARE(cache.days(habitKey(u"Czytanie")).value(QDate(2026, 11, 2)), Mark::Skipped);

    // A schedule changed: the habit's revision moves.
    const int before = cache.revision(habitKey(u"Czytanie"));
    index.update(u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n- Czytanie\n  habit:: 3 razy w tygodniu\n"_s);
    QVERIFY(cache.sync(index));
    QVERIFY(cache.revision(habitKey(u"Czytanie")) > before);
    QVERIFY(*cache.find(u"Czytanie")->schedule == times(3, Week));

    // A journal removed: its checks go.
    index.remove(u"journals/2026-11-03.md"_s);
    QVERIFY(cache.sync(index));
    QVERIFY(!cache.days(habitKey(u"Medytacja")).contains(QDate(2026, 11, 3)));

    // Another index: a full sync.
    VaultIndex other;
    QVERIFY(cache.sync(other));
    QVERIFY(cache.lastSync().full);
    QVERIFY(cache.habits().isEmpty());
    cache.clear();
    QVERIFY(cache.sync(other));
}

void TestHabits::cacheDuplicatesAndUndefined()
{
    VaultIndex index;
    index.update(u"A.md"_s, u"- Joga\n  habit:: codziennie\n"_s);
    index.update(u"B.md"_s, u"- joga\n  habit:: pn, pt\n"_s);
    index.update(u"journals/2026-11-03.md"_s, u"- [x] Spacer #nawyk\n- [x] JOGA #nawyk\n"_s);
    HabitCache cache;
    cache.sync(index);
    QCOMPARE(cache.habits().size(), 2);
    QVERIFY(!cache.habits()[0].duplicate);
    QVERIFY(cache.habits()[1].duplicate);
    QVERIFY(!cache.habits()[1].isValid());
    QCOMPARE(cache.find(u"joga")->path, u"A.md"_s);
    QCOMPARE(cache.days(habitKey(u"Joga")).size(), 1);
    QCOMPARE(cache.undefinedKeys(), QStringList{habitKey(u"Spacer")});
    // The first one goes: the other takes over.
    index.remove(u"A.md"_s);
    cache.sync(index);
    QCOMPARE(cache.habits().size(), 1);
    QVERIFY(cache.habits()[0].isValid());
    QCOMPARE(cache.find(u"joga")->path, u"B.md"_s);
}

void TestHabits::cacheManyNotes()
{
    // 2000 daily notes of 3 habits and 3000 other pages: one full sync, then one edit reads
    // one note.
    VaultIndex index;
    {
        VaultIndex::Batch batch(index);
        index.update(
                u"Nawyki.md"_s,
                u"- Medytacja\n  habit:: codziennie\n- Czytanie\n  habit:: 3 razy w tygodniu\n- Spacer\n  habit:: w dni robocze\n"_s);
        for (int i = 0; i < 2000; ++i) {
            const QDate d = QDate(2021, 5, 1).addDays(i);
            index.update(u"journals/"_s + d.toString(Qt::ISODate) + u".md"_s,
                         u"- [x] Medytacja #nawyk\n- [%1] Czytanie #nawyk\n- notatka dnia %2\n"_s
                                 .arg(i % 2 ? u"x"_s : u" "_s)
                                 .arg(i));
        }
        for (int i = 0; i < 3000; ++i)
            index.update(u"Strony/Strona %1.md"_s.arg(i), u"- treść %1 #praca\n"_s.arg(i));
    }
    HabitCache cache;
    cache.sync(index);
    QCOMPARE(cache.days(habitKey(u"Medytacja")).size(), 2000);
    const QDate last = QDate(2021, 5, 1).addDays(1999);
    const Stats st = statsOf(*cache.find(u"Medytacja"), cache.days(habitKey(u"Medytacja")), last);
    QCOMPARE(st.currentStreak, 2000);
    index.update(u"journals/"_s + last.toString(Qt::ISODate) + u".md"_s, u"- [ ] Medytacja #nawyk\n"_s);
    QVERIFY(cache.sync(index));
    QCOMPARE(cache.lastSync().changed.size(), 1);
    QCOMPARE(statsOf(*cache.find(u"Medytacja"), cache.days(habitKey(u"Medytacja")), last).currentStreak, 1999);
}

QTEST_GUILESS_MAIN(TestHabits)
#include "tst_habits.moc"
