// Recurrence rules: parsing, formatting, Logseq repeaters and occurrences. Test cases ported
// from archive/web-v1 (recurrence.test.ts); weekdays use Qt numbering (1 = Monday … 7 = Sunday).
#include "dates/local.h"
#include "dates/recurrence.h"

#include <QElapsedTimer>
#include <QTest>

#include <limits>

using namespace brulion;
using namespace brulion::dates;
using namespace Qt::StringLiterals;

namespace {

using Freq = RecurrenceRule::Freq;

/// Fluent rule builder for compact test tables.
struct R {
    RecurrenceRule rule;
    explicit R(Freq f, int n = 1)
    {
        rule.freq = f;
        rule.interval = n;
    }
    R &on(const QList<int> &days)
    {
        rule.weekdays = days;
        return *this;
    }
    R &day(int d)
    {
        rule.monthDay = d;
        return *this;
    }
    R &last()
    {
        rule.lastDayOfMonth = true;
        return *this;
    }
    R &workdays()
    {
        rule.weekdaysOnly = true;
        return *this;
    }
    R &done()
    {
        rule.whenDone = true;
        return *this;
    }
    operator RecurrenceRule() const { return rule; }
};

QString freqName(Freq f)
{
    switch (f) {
    case Freq::Day:
        return u"day"_s;
    case Freq::Week:
        return u"week"_s;
    case Freq::Month:
        return u"month"_s;
    case Freq::Year:
        return u"year"_s;
    }
    return u"?"_s;
}

/// "week/2 on 1,5 md 15 last workdays done" or "null".
QString describe(const std::optional<RecurrenceRule> &rule)
{
    if (!rule)
        return u"null"_s;
    QString s = freqName(rule->freq) + u'/' + QString::number(rule->interval);
    if (!rule->weekdays.isEmpty()) {
        QStringList days;
        for (const int d : rule->weekdays)
            days.append(QString::number(d));
        s += u" on "_s + days.join(u',');
    }
    if (rule->monthDay)
        s += u" md "_s + QString::number(*rule->monthDay);
    if (rule->lastDayOfMonth)
        s += u" last"_s;
    if (rule->weekdaysOnly)
        s += u" workdays"_s;
    if (rule->whenDone)
        s += u" done"_s;
    return s;
}

QString describe(const std::optional<DateSpec> &spec)
{
    return spec ? spec->toString() : u"null"_s;
}

QStringList describe(const QList<DateSpec> &specs)
{
    QStringList out;
    for (const DateSpec &s : specs)
        out.append(s.toString());
    return out;
}

DateSpec spec(QStringView text)
{
    return parseDateSpec(text).value_or(DateSpec{});
}

QDate d(QStringView iso)
{
    return parseIsoDate(iso).value_or(QDate());
}

RecurrenceRule rule(QStringView text)
{
    const auto r = parseRecurrence(text);
    if (!r)
        qFatal("cannot parse rule %s", qUtf8Printable(text.toString()));
    return *r;
}

QString next(QStringView ruleText, QStringView from, std::optional<QDate> completedOn = std::nullopt)
{
    return describe(nextOccurrence(rule(ruleText), spec(from), completedOn));
}

QStringList between(const RecurrenceRule &r, QStringView anchor, QStringView from, QStringView to, int max = 500)
{
    return describe(occurrencesBetween(r, spec(anchor), d(from), d(to), max));
}

void parseColumns()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
}

void addParseRows(std::initializer_list<std::pair<QString, RecurrenceRule>> rows)
{
    for (const auto &[input, expected] : rows)
        QTest::newRow(input.toUtf8().constData()) << input << describe(expected);
}

const RecurrenceRule BadFreq = [] {
    RecurrenceRule r;
    r.freq = static_cast<Freq>(42);
    return r;
}();

} // namespace

class TestDatesRecurrence : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ---- parsing -------------------------------------------------------------------------
    void parseCanonicalEnglish_data()
    {
        parseColumns();
        addParseRows({
            {u"every day"_s, R(Freq::Day)},
            {u"every 3 days"_s, R(Freq::Day, 3)},
            {u"every weekday"_s, R(Freq::Day).workdays()},
            {u"every week"_s, R(Freq::Week)},
            {u"every 2 weeks"_s, R(Freq::Week, 2)},
            {u"every week on Monday, Friday"_s, R(Freq::Week).on({1, 5})},
            {u"every month"_s, R(Freq::Month)},
            {u"every month on the 15th"_s, R(Freq::Month).day(15)},
            {u"every month on the last"_s, R(Freq::Month).last()},
            {u"every year"_s, R(Freq::Year)},
            {u"every 2 years"_s, R(Freq::Year, 2)},
            {u"every day when done"_s, R(Freq::Day).done()},
            {u"every week on Monday, Friday when done"_s, R(Freq::Week).on({1, 5}).done()},
        });
    }
    void parseCanonicalEnglish()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(describe(parseRecurrence(input)), expected);
    }

    void parsePolish_data()
    {
        parseColumns();
        addParseRows({
            {u"codziennie"_s, R(Freq::Day)},
            {u"co 3 dni"_s, R(Freq::Day, 3)},
            {u"w dni robocze"_s, R(Freq::Day).workdays()},
            {u"co tydzień"_s, R(Freq::Week)},
            {u"co 2 tygodnie"_s, R(Freq::Week, 2)},
            {u"co tydzień w poniedziałek, piątek"_s, R(Freq::Week).on({1, 5})},
            {u"co miesiąc"_s, R(Freq::Month)},
            {u"co miesiąc 15."_s, R(Freq::Month).day(15)},
            {u"co miesiąc ostatniego"_s, R(Freq::Month).last()},
            {u"co rok"_s, R(Freq::Year)},
            {u"co 2 lata"_s, R(Freq::Year, 2)},
            {u"codziennie po wykonaniu"_s, R(Freq::Day).done()},
        });
    }
    void parsePolish()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(describe(parseRecurrence(input)), expected);
    }

    void parseVariants_data()
    {
        parseColumns();
        addParseRows({
            {u"co tydzien w poniedzialek, piatek"_s, R(Freq::Week).on({1, 5})},
            {u"CO TYDZIEŃ"_s, R(Freq::Week)},
            {u"🔁 co tydzień"_s, R(Freq::Week)},
            {u"🔁 every week"_s, R(Freq::Week)},
            {u"🔁️ every week"_s, R(Freq::Week)},
            {u"  every   2   weeks  "_s, R(Freq::Week, 2)},
            {u"Every Week On Monday"_s, R(Freq::Week).on({1})},
            {u"co 5 lat"_s, R(Freq::Year, 5)},
            {u"co roku"_s, R(Freq::Year)},
            {u"corocznie"_s, R(Freq::Year)},
            {u"co 5 tygodni"_s, R(Freq::Week, 5)},
            {u"co 2 miesiące"_s, R(Freq::Month, 2)},
            {u"co 6 miesięcy"_s, R(Freq::Month, 6)},
            {u"co dzień"_s, R(Freq::Day)},
            {u"każdego dnia"_s, R(Freq::Day)},
            {u"co drugi dzień"_s, R(Freq::Day, 2)},
            {u"co dwa tygodnie"_s, R(Freq::Week, 2)},
            {u"co trzeci miesiąc"_s, R(Freq::Month, 3)},
            {u"co poniedziałek"_s, R(Freq::Week).on({1})},
            {u"co środę"_s, R(Freq::Week).on({3})},
            {u"w każdy piątek"_s, R(Freq::Week).on({5})},
            {u"w każdą sobotę i niedzielę"_s, R(Freq::Week).on({6, 7})},
            {u"w poniedziałki i czwartki"_s, R(Freq::Week).on({1, 4})},
            {u"we wtorek i czwartek"_s, R(Freq::Week).on({2, 4})},
            {u"co tydzień we wtorek oraz czwartek"_s, R(Freq::Week).on({2, 4})},
            {u"co 2 tygodnie w pn, pt"_s, R(Freq::Week, 2).on({1, 5})},
            {u"co miesiąc ostatniego dnia"_s, R(Freq::Month).last()},
            {u"co miesiąc w ostatni dzień"_s, R(Freq::Month).last()},
            {u"co miesiąc 1-go"_s, R(Freq::Month).day(1)},
            {u"co miesiąc dnia 15"_s, R(Freq::Month).day(15)},
            {u"co miesiac 15"_s, R(Freq::Month).day(15)},
            {u"w dni powszednie"_s, R(Freq::Day).workdays()},
            {u"od poniedziałku do piątku"_s, R(Freq::Day).workdays()},
            {u"co 2 dni robocze"_s, R(Freq::Day, 2).workdays()},
            {u"w dni robocze, po wykonaniu"_s, R(Freq::Day).workdays().done()},
            {u"daily"_s, R(Freq::Day)},
            {u"weekly"_s, R(Freq::Week)},
            {u"weekly on tuesday"_s, R(Freq::Week).on({2})},
            {u"monthly"_s, R(Freq::Month)},
            {u"yearly"_s, R(Freq::Year)},
            {u"annually"_s, R(Freq::Year)},
            {u"fortnightly"_s, R(Freq::Week, 2)},
            {u"every other week"_s, R(Freq::Week, 2)},
            {u"every two days"_s, R(Freq::Day, 2)},
            {u"every monday"_s, R(Freq::Week).on({1})},
            {u"every mon, fri"_s, R(Freq::Week).on({1, 5})},
            {u"every friday and monday"_s, R(Freq::Week).on({1, 5})},
            {u"every 2 weeks on tuesday and thursday"_s, R(Freq::Week, 2).on({2, 4})},
            {u"every week on sunday, monday"_s, R(Freq::Week).on({1, 7})},
            {u"every month on the 1st"_s, R(Freq::Month).day(1)},
            {u"every month on the 31st"_s, R(Freq::Month).day(31)},
            {u"every month on the last day"_s, R(Freq::Month).last()},
            {u"every 3 months on the 2nd"_s, R(Freq::Month, 3).day(2)},
            {u"every weekday when done"_s, R(Freq::Day).workdays().done()},
            {u"every 3 days, when done"_s, R(Freq::Day, 3).done()},
            {u"every business day"_s, R(Freq::Day).workdays()},
            {u"pn, pt"_s, R(Freq::Week).on({1, 5})},
            {u"every 9999 days"_s, R(Freq::Day, 9999)},
        });
    }
    void parseVariants()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(describe(parseRecurrence(input)), expected);
    }

    void parseRejects_data()
    {
        QTest::addColumn<QString>("input");
        for (const QString &s : {u"   "_s, u"🔁"_s, u"every"_s, u"co"_s, u"every 0 days"_s, u"co 0 dni"_s,
                                 u"every month on the 32nd"_s, u"every month on the 0th"_s,
                                 u"every month on the 2nd Tuesday"_s, u"every year on October 15"_s,
                                 u"every day on monday"_s, u"every week on funday"_s, u"hello world"_s,
                                 u"every blah"_s, u"constructor"_s, u"toString"_s, u"when done"_s,
                                 u"every week on"_s, u"jutro"_s, u"every 99999999999999999999 days"_s,
                                 u"every 100000 years"_s, u"weekly on funday"_s, u"xwhen done"_s})
            QTest::newRow(s.toUtf8().constData()) << s;
        QTest::newRow("empty") << QString();
    }
    void parseRejects()
    {
        QFETCH(QString, input);
        QCOMPARE(describe(parseRecurrence(input)), u"null"_s);
    }

    // ---- formatting ----------------------------------------------------------------------
    void format_data()
    {
        QTest::addColumn<RecurrenceRule>("rule");
        QTest::addColumn<QString>("english");
        QTest::addColumn<QString>("polish");
        const auto row = [](const RecurrenceRule &r, const QString &en, const QString &pl) {
            QTest::newRow(en.toUtf8().constData()) << r << en << pl;
        };
        row(R(Freq::Day), u"every day"_s, u"codziennie"_s);
        row(R(Freq::Day, 3), u"every 3 days"_s, u"co 3 dni"_s);
        row(R(Freq::Day).workdays(), u"every weekday"_s, u"w dni robocze"_s);
        row(R(Freq::Day, 2).workdays(), u"every 2 weekdays"_s, u"co 2 dni robocze"_s);
        row(R(Freq::Week), u"every week"_s, u"co tydzień"_s);
        row(R(Freq::Week, 2), u"every 2 weeks"_s, u"co 2 tygodnie"_s);
        row(R(Freq::Week, 5), u"every 5 weeks"_s, u"co 5 tygodni"_s);
        row(R(Freq::Week, 12), u"every 12 weeks"_s, u"co 12 tygodni"_s);
        row(R(Freq::Week, 22), u"every 22 weeks"_s, u"co 22 tygodnie"_s);
        row(R(Freq::Week).on({1, 5}), u"every week on Monday, Friday"_s, u"co tydzień w poniedziałek, piątek"_s);
        row(R(Freq::Week).on({2, 4}), u"every week on Tuesday, Thursday"_s, u"co tydzień we wtorek, czwartek"_s);
        row(R(Freq::Week, 2).on({3, 6, 7}), u"every 2 weeks on Wednesday, Saturday, Sunday"_s,
            u"co 2 tygodnie w środę, sobotę, niedzielę"_s);
        row(R(Freq::Month), u"every month"_s, u"co miesiąc"_s);
        row(R(Freq::Month, 2), u"every 2 months"_s, u"co 2 miesiące"_s);
        row(R(Freq::Month, 5), u"every 5 months"_s, u"co 5 miesięcy"_s);
        row(R(Freq::Month).day(15), u"every month on the 15th"_s, u"co miesiąc 15."_s);
        row(R(Freq::Month).day(1), u"every month on the 1st"_s, u"co miesiąc 1."_s);
        row(R(Freq::Month).day(22), u"every month on the 22nd"_s, u"co miesiąc 22."_s);
        row(R(Freq::Month).last(), u"every month on the last"_s, u"co miesiąc ostatniego"_s);
        row(R(Freq::Year), u"every year"_s, u"co rok"_s);
        row(R(Freq::Year, 2), u"every 2 years"_s, u"co 2 lata"_s);
        row(R(Freq::Year, 5), u"every 5 years"_s, u"co 5 lat"_s);
        row(R(Freq::Year, 22), u"every 22 years"_s, u"co 22 lata"_s);
        row(R(Freq::Day).done(), u"every day when done"_s, u"codziennie po wykonaniu"_s);
    }
    void format()
    {
        QFETCH(RecurrenceRule, rule);
        QFETCH(QString, english);
        QFETCH(QString, polish);
        QCOMPARE(formatRecurrence(rule, UiLocale::English), english);
        QCOMPARE(formatRecurrence(rule, UiLocale::Polish), polish);
    }
    void formatSanitises()
    {
        QCOMPARE(formatRecurrence(R(Freq::Day, 0)), u"every day"_s);
        QCOMPARE(formatRecurrence(R(Freq::Day, -5)), u"every day"_s);
        QCOMPARE(formatRecurrence(R(Freq::Week).on({})), u"every week"_s);
        QCOMPARE(formatRecurrence(R(Freq::Week).on({9, 1, 0})), u"every week on Monday"_s);
        QCOMPARE(formatRecurrence(R(Freq::Week).on({5, 1, 5})), u"every week on Monday, Friday"_s);
        QCOMPARE(formatRecurrence(R(Freq::Month).day(40)), u"every month"_s);
        QCOMPARE(formatRecurrence(R(Freq::Day).day(3)), u"every day"_s);
        QCOMPARE(formatRecurrence(R(Freq::Year).day(3)), u"every year"_s);
        QCOMPARE(formatRecurrence(R(Freq::Year).last()), u"every year"_s);
        QCOMPARE(formatRecurrence(R(Freq::Month).day(15).last()), u"every month on the last"_s);
        QCOMPARE(formatRecurrence(R(Freq::Week).workdays()),
                 u"every week on Monday, Tuesday, Wednesday, Thursday, Friday"_s);
        QCOMPARE(formatRecurrence(R(Freq::Week).workdays(), UiLocale::Polish),
                 u"co tydzień w poniedziałek, wtorek, środę, czwartek, piątek"_s);
        QCOMPARE(formatRecurrence(R(Freq::Day, std::numeric_limits<int>::max())), u"every 99999 days"_s);
        QCOMPARE(formatRecurrence(BadFreq), QString());
        RecurrenceRule badDone = BadFreq;
        badDone.whenDone = true;
        QCOMPARE(formatRecurrence(badDone, UiLocale::Polish), QString());
    }
    void roundTrip_data()
    {
        QTest::addColumn<RecurrenceRule>("rule");
        const QList<RecurrenceRule> rules = {
            R(Freq::Day),
            R(Freq::Day, 7).done(),
            R(Freq::Day).workdays(),
            R(Freq::Day, 3).workdays().done(),
            R(Freq::Week),
            R(Freq::Week, 3),
            R(Freq::Week).on({1, 2, 3, 4, 5, 6, 7}),
            R(Freq::Week, 2).on({2}).done(),
            R(Freq::Week).on({6, 7}),
            R(Freq::Month),
            R(Freq::Month, 12).day(31),
            R(Freq::Month).last().done(),
            R(Freq::Year),
            R(Freq::Year, 25),
            R(Freq::Month, 3).day(1),
        };
        for (const RecurrenceRule &r : rules)
            QTest::newRow(formatRecurrence(r).toUtf8().constData()) << r;
    }
    void roundTrip()
    {
        QFETCH(RecurrenceRule, rule);
        QCOMPARE(describe(parseRecurrence(formatRecurrence(rule, UiLocale::English))), describe(rule));
        QCOMPARE(describe(parseRecurrence(formatRecurrence(rule, UiLocale::Polish))), describe(rule));
        QVERIFY(parseRecurrence(formatRecurrence(rule)) == std::optional<RecurrenceRule>(rule));
    }

    // ---- canonical text ------------------------------------------------------------------
    void canonical_data()
    {
        parseColumns();
        const auto row = [](const QString &in, const QString &out) {
            QTest::newRow(in.toUtf8().constData()) << in << out;
        };
        row(u"co tydzień w poniedziałek, piątek"_s, u"every week on Monday, Friday"_s);
        row(u"w dni robocze po wykonaniu"_s, u"every weekday when done"_s);
        row(u"co miesiąc ostatniego"_s, u"every month on the last"_s);
        row(u"🔁 co 2 lata"_s, u"every 2 years"_s);
        row(u"every friday and monday"_s, u"every week on Monday, Friday"_s);
        row(u"Every Day"_s, u"every day"_s);
        row(u"sometimes"_s, QString());
        // Trailing separators.
        row(u"every week on Monday, Friday,"_s, u"every week on Monday, Friday"_s);
        row(u"co tydzień w poniedziałek;"_s, u"every week on Monday"_s);
        row(u"every day, when done,"_s, u"every day when done"_s);
        // Quarters, half-years, ordinal intervals.
        row(u"co kwartał"_s, u"every 3 months"_s);
        row(u"co kwartal"_s, u"every 3 months"_s);
        row(u"kwartalnie"_s, u"every 3 months"_s);
        row(u"every quarter"_s, u"every 3 months"_s);
        row(u"quarterly"_s, u"every 3 months"_s);
        row(u"co 2 kwartały"_s, u"every 6 months"_s);
        row(u"co pół roku"_s, u"every 6 months"_s);
        row(u"co pol roku po wykonaniu"_s, u"every 6 months when done"_s);
        row(u"every half year"_s, u"every 6 months"_s);
        row(u"półrocznie"_s, u"every 6 months"_s);
        row(u"every second week"_s, u"every 2 weeks"_s);
        row(u"every 2nd week"_s, u"every 2 weeks"_s);
        row(u"every third day"_s, u"every 3 days"_s);
        row(u"every 3rd month on the 15th"_s, u"every 3 months on the 15th"_s);
    }
    void canonical()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(canonicalRecurrence(input), expected);
    }

    // ---- helpers -------------------------------------------------------------------------
    void parseWeekdayList()
    {
        QCOMPARE(dates::parseWeekdayList(u"poniedziałek i piątek"), QList<int>({1, 5}));
        QCOMPARE(dates::parseWeekdayList(u"w środę, sobotę"), QList<int>({3, 6}));
        QCOMPARE(dates::parseWeekdayList(u"monday, monday"), QList<int>({1}));
        QCOMPARE(dates::parseWeekdayList(u"Mondays and Fridays"), QList<int>({1, 5}));
        QCOMPARE(dates::parseWeekdayList(u"pn, pt"), QList<int>({1, 5}));
        QCOMPARE(dates::parseWeekdayList(u"niedziela/sobota"), QList<int>({6, 7}));
        QCOMPARE(dates::parseWeekdayList(u"monday, "), QList<int>());
        QCOMPARE(dates::parseWeekdayList(u""), QList<int>());
        QCOMPARE(dates::parseWeekdayList(u"monday, funday"), QList<int>());
    }
    void normalizeRule()
    {
        RecurrenceRule messy = R(Freq::Day, 2).on({1}).day(3);
        QCOMPARE(describe(dates::normalizeRule(messy)), u"day/2"_s);
        QCOMPARE(describe(dates::normalizeRule(R(Freq::Year).last())), u"year/1"_s);
        QCOMPARE(describe(dates::normalizeRule(R(Freq::Week).on({1}).workdays())), u"week/1 on 1"_s);
        QCOMPARE(describe(dates::normalizeRule(R(Freq::Week).on({7, 3, 3, 0, 8}))), u"week/1 on 3,7"_s);
        QCOMPARE(dates::normalizeRule(R(Freq::Day, std::numeric_limits<int>::max())).interval, MaxRecurrenceInterval);
        QCOMPARE(describe(dates::normalizeRule(R(Freq::Month).day(15).last())), u"month/1 last"_s);
    }

    // ---- Logseq repeaters ----------------------------------------------------------------
    void repeaters_data()
    {
        parseColumns();
        const auto row = [](const QString &in, const std::optional<RecurrenceRule> &r) {
            QTest::newRow(QString(u"'"_s + in + u"'"_s).toUtf8().constData()) << in << describe(r);
        };
        row(u"+1w"_s, R(Freq::Week));
        row(u"++2d"_s, R(Freq::Day, 2));
        row(u".+1m"_s, R(Freq::Month).done());
        row(u"+1y"_s, R(Freq::Year));
        row(u"+1W"_s, R(Freq::Week));
        row(u" +3d "_s, R(Freq::Day, 3));
        row(u"+3h"_s, std::nullopt);
        row(u"+0d"_s, std::nullopt);
        row(u"+1x"_s, std::nullopt);
        row(u"1w"_s, std::nullopt);
        row(QString(), std::nullopt);
        row(u"+-1d"_s, std::nullopt);
        row(u"+99999999999999999999d"_s, std::nullopt);
        row(u"+100000d"_s, std::nullopt);
    }
    void repeaters()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(describe(repeaterToRule(input)), expected);
    }
    void ruleToRepeater()
    {
        QCOMPARE(dates::ruleToRepeater(R(Freq::Week)), u"+1w"_s);
        QCOMPARE(dates::ruleToRepeater(R(Freq::Month, 2).done()), u".+2m"_s);
        QCOMPARE(dates::ruleToRepeater(R(Freq::Year)), u"+1y"_s);
        QCOMPARE(dates::ruleToRepeater(R(Freq::Day, 3)), u"+3d"_s);
        QCOMPARE(dates::ruleToRepeater(R(Freq::Week).on({1})), QString());
        QCOMPARE(dates::ruleToRepeater(R(Freq::Month).last()), QString());
        QCOMPARE(dates::ruleToRepeater(R(Freq::Month).day(15)), QString());
        QCOMPARE(dates::ruleToRepeater(R(Freq::Day).workdays()), QString());
        QCOMPARE(dates::ruleToRepeater(BadFreq), QString());
    }

    // ---- nextOccurrence ------------------------------------------------------------------
    void nextOccurrence_data()
    {
        QTest::addColumn<QString>("ruleText");
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("expected");
        const auto row = [](const QString &r, const QString &from, const QString &expected) {
            QTest::newRow(QString(r + u" from "_s + from).toUtf8().constData()) << r << from << expected;
        };
        row(u"every day"_s, u"2026-10-14"_s, u"2026-10-15"_s);
        row(u"every 3 days"_s, u"2026-10-14"_s, u"2026-10-17"_s);
        row(u"every day"_s, u"2026-12-31"_s, u"2027-01-01"_s);
        row(u"every weekday"_s, u"2026-10-14"_s, u"2026-10-15"_s);
        row(u"every weekday"_s, u"2026-10-16"_s, u"2026-10-19"_s);
        row(u"every weekday"_s, u"2026-10-17"_s, u"2026-10-19"_s);
        row(u"every weekday"_s, u"2026-10-18"_s, u"2026-10-19"_s);
        row(u"co 2 dni robocze"_s, u"2026-10-15"_s, u"2026-10-19"_s);
        row(u"co 5 dni robocze"_s, u"2026-10-12"_s, u"2026-10-19"_s);
        row(u"co 6 dni robocze"_s, u"2026-10-17"_s, u"2026-10-26"_s);
        row(u"every week"_s, u"2026-10-14"_s, u"2026-10-21"_s);
        row(u"every 2 weeks"_s, u"2026-10-14"_s, u"2026-10-28"_s);
        row(u"every week on Monday, Friday"_s, u"2026-10-14"_s, u"2026-10-16"_s);
        row(u"every week on Monday, Friday"_s, u"2026-10-16"_s, u"2026-10-19"_s);
        row(u"every week on Monday, Friday"_s, u"2026-10-19"_s, u"2026-10-23"_s);
        row(u"every week on Wednesday"_s, u"2026-10-14"_s, u"2026-10-21"_s);
        row(u"every week on Sunday"_s, u"2026-10-14"_s, u"2026-10-18"_s);
        row(u"every week on Sunday"_s, u"2026-10-18"_s, u"2026-10-25"_s);
        row(u"every 2 weeks on Monday, Friday"_s, u"2026-10-14"_s, u"2026-10-16"_s);
        row(u"every 2 weeks on Monday, Friday"_s, u"2026-10-16"_s, u"2026-10-26"_s);
        row(u"every month"_s, u"2026-10-14"_s, u"2026-11-14"_s);
        row(u"every month"_s, u"2026-01-31"_s, u"2026-02-28"_s);
        row(u"every month"_s, u"2028-01-31"_s, u"2028-02-29"_s);
        row(u"every month"_s, u"2026-12-15"_s, u"2027-01-15"_s);
        row(u"every 3 months"_s, u"2026-11-30"_s, u"2027-02-28"_s);
        row(u"every month on the 15th"_s, u"2026-10-14"_s, u"2026-10-15"_s);
        row(u"every month on the 15th"_s, u"2026-10-15"_s, u"2026-11-15"_s);
        row(u"every month on the 15th"_s, u"2026-10-20"_s, u"2026-11-15"_s);
        row(u"every 2 months on the 15th"_s, u"2026-10-20"_s, u"2026-12-15"_s);
        row(u"every month on the 31st"_s, u"2026-10-31"_s, u"2026-11-30"_s);
        row(u"every month on the 31st"_s, u"2026-11-30"_s, u"2026-12-31"_s);
        row(u"every month on the last"_s, u"2026-10-14"_s, u"2026-10-31"_s);
        row(u"every month on the last"_s, u"2026-10-31"_s, u"2026-11-30"_s);
        row(u"every month on the last"_s, u"2027-01-31"_s, u"2027-02-28"_s);
        row(u"every 2 months on the last"_s, u"2026-10-31"_s, u"2026-12-31"_s);
        row(u"every year"_s, u"2026-10-14"_s, u"2027-10-14"_s);
        row(u"every year"_s, u"2028-02-29"_s, u"2029-02-28"_s);
        row(u"every 2 years"_s, u"2026-10-14"_s, u"2028-10-14"_s);
        row(u"every day"_s, u"2026-10-14T09:00"_s, u"2026-10-15 09:00"_s);
        row(u"every day"_s, u"2026-10-14 09:00"_s, u"2026-10-15 09:00"_s);
        row(u"every week on Friday"_s, u"2026-10-14T18:30"_s, u"2026-10-16 18:30"_s);
        row(u"co tydzień w niedzielę"_s, u"2026-10-25 07:00"_s, u"2026-11-01 07:00"_s);
        // End of the supported range: no next occurrence.
        row(u"every year"_s, u"9999-06-01"_s, u"null"_s);
        row(u"every 10 days"_s, u"9999-12-25 09:00"_s, u"null"_s);
        row(u"every month"_s, u"9999-11-30"_s, u"9999-12-30"_s);
    }
    void nextOccurrence()
    {
        QFETCH(QString, ruleText);
        QFETCH(QString, from);
        QFETCH(QString, expected);
        QCOMPARE(next(ruleText, from), expected);
    }
    void nextOccurrenceWhenDone()
    {
        QCOMPARE(next(u"every week when done", u"2026-10-01", d(u"2026-10-14")), u"2026-10-21"_s);
        QCOMPARE(next(u"every day when done", u"2026-10-01 08:00", d(u"2026-10-14")), u"2026-10-15 08:00"_s);
        QCOMPARE(next(u"every month on the 15th when done", u"2026-09-15", d(u"2026-10-20")), u"2026-11-15"_s);
        // Without completedOn, or for normal rules, the original date is used.
        QCOMPARE(next(u"every week when done", u"2026-10-01"), u"2026-10-08"_s);
        QCOMPARE(next(u"every week", u"2026-10-01", d(u"2026-10-14")), u"2026-10-08"_s);
        QCOMPARE(next(u"every week when done", u"2026-10-01", QDate()), u"2026-10-08"_s);
    }
    void nextOccurrenceInvalid()
    {
        QCOMPARE(describe(dates::nextOccurrence(rule(u"every day"), DateSpec{})), u"null"_s);
        QCOMPARE(describe(dates::nextOccurrence(R(Freq::Day, 0), spec(u"2026-10-14"))), u"2026-10-15"_s);
        QCOMPARE(describe(dates::nextOccurrence(R(Freq::Week).on({}), spec(u"2026-10-14"))), u"2026-10-21"_s);
        QCOMPARE(describe(dates::nextOccurrence(R(Freq::Year, std::numeric_limits<int>::max()), spec(u"2026-10-14"))),
                 u"null"_s);
        QCOMPARE(describe(dates::nextOccurrence(BadFreq, spec(u"2026-10-14"))), u"null"_s);
        QCOMPARE(describe(dates::nextOccurrence(BadFreq, spec(u"2026-10-14 09:00"))), u"null"_s);
    }
    void nextOccurrenceStrictlyLater()
    {
        const QList<RecurrenceRule> rules = {rule(u"every day"), rule(u"every weekday"),
                                             rule(u"every week on Monday, Friday"), rule(u"every month on the last"),
                                             rule(u"every year"), rule(u"every 2 months on the 31st")};
        QDate day(2026, 1, 1);
        for (int i = 0; i < 400; ++i, day = day.addDays(1)) {
            for (const RecurrenceRule &r : rules) {
                const auto n = dates::nextOccurrence(r, DateSpec{day, std::nullopt});
                QVERIFY(n);
                QVERIFY2(n->date > day, qPrintable(formatRecurrence(r) + u' ' + day.toString(Qt::ISODate)));
            }
        }
    }

    // ---- occurrencesBetween --------------------------------------------------------------
    void occurrencesBasics()
    {
        QCOMPARE(between(rule(u"every day"), u"2026-10-14", u"2026-10-14", u"2026-10-17"),
                 QStringList({u"2026-10-14"_s, u"2026-10-15"_s, u"2026-10-16"_s, u"2026-10-17"_s}));
        QCOMPARE(between(rule(u"every 3 days"), u"2026-10-01", u"2026-10-14", u"2026-10-20"),
                 QStringList({u"2026-10-16"_s, u"2026-10-19"_s}));
        // Empty ranges, late anchors, invalid input, max <= 0.
        QCOMPARE(between(rule(u"every day"), u"2026-11-01", u"2026-10-01", u"2026-10-31"), QStringList());
        QCOMPARE(between(rule(u"every day"), u"2026-10-01", u"2026-10-31", u"2026-10-01"), QStringList());
        QCOMPARE(between(rule(u"every day"), u"bad", u"2026-10-01", u"2026-10-31"), QStringList());
        QCOMPARE(between(rule(u"every day"), u"2026-10-01", u"2026-10-01", u"2026-10-31", 0), QStringList());
        QCOMPARE(between(rule(u"every day"), u"2026-10-01", u"2026-10-01", u"2026-10-31", -3), QStringList());
        QCOMPARE(between(BadFreq, u"2026-10-14", u"2026-10-01", u"2026-10-31"), QStringList());
    }
    void occurrencesAnchorNotOnWeekday()
    {
        QCOMPARE(between(rule(u"every week on Monday, Friday"), u"2026-10-14", u"2026-10-01", u"2026-10-31"),
                 QStringList({u"2026-10-14"_s, u"2026-10-16"_s, u"2026-10-19"_s, u"2026-10-23"_s, u"2026-10-26"_s,
                              u"2026-10-30"_s}));
    }
    void occurrencesRememberMonthDay()
    {
        QCOMPARE(between(rule(u"every month"), u"2026-01-31", u"2026-01-01", u"2026-06-30"),
                 QStringList({u"2026-01-31"_s, u"2026-02-28"_s, u"2026-03-31"_s, u"2026-04-30"_s, u"2026-05-31"_s,
                              u"2026-06-30"_s}));
        QCOMPARE(between(rule(u"every year"), u"2028-02-29", u"2028-01-01", u"2032-12-31"),
                 QStringList({u"2028-02-29"_s, u"2029-02-28"_s, u"2030-02-28"_s, u"2031-02-28"_s, u"2032-02-29"_s}));
    }
    void occurrencesKeepTime()
    {
        QCOMPARE(between(rule(u"every week"), u"2026-10-14T07:30", u"2026-10-15", u"2026-10-31"),
                 QStringList({u"2026-10-21 07:30"_s, u"2026-10-28 07:30"_s}));
    }
    void occurrencesBusinessDays()
    {
        QCOMPARE(between(rule(u"every weekday"), u"2026-10-16", u"2026-10-16", u"2026-10-23"),
                 QStringList({u"2026-10-16"_s, u"2026-10-19"_s, u"2026-10-20"_s, u"2026-10-21"_s, u"2026-10-22"_s,
                              u"2026-10-23"_s}));
    }
    void occurrencesCap()
    {
        QCOMPARE(between(rule(u"every day"), u"2026-01-01", u"2026-01-01", u"2026-12-31", 10).size(), 10);
        QCOMPARE(between(rule(u"every day"), u"2020-01-01", u"2020-01-01", u"2030-12-31").size(), 500);
        QCOMPARE(between(rule(u"every day"), u"2026-10-01", u"2026-10-01", u"2026-10-31", 2),
                 QStringList({u"2026-10-01"_s, u"2026-10-02"_s}));
        QCOMPARE(between(rule(u"every day"), u"2026-10-01", u"2026-10-01", u"2026-10-03",
                         std::numeric_limits<int>::max())
                     .size(),
                 3);
    }
    void occurrencesFastForward_data()
    {
        QTest::addColumn<QString>("ruleText");
        QTest::addColumn<QString>("anchor");
        const auto row = [](const QString &r, const QString &a) {
            QTest::newRow(QString(r + u" @ "_s + a).toUtf8().constData()) << r << a;
        };
        row(u"every day"_s, u"1990-01-01"_s);
        row(u"every 3 days"_s, u"2001-02-03"_s);
        row(u"every 2 weeks"_s, u"2015-06-07"_s);
        row(u"every 2 weeks on Monday, Thursday"_s, u"2019-03-06"_s);
        row(u"every week on Sunday"_s, u"2010-01-01"_s);
        row(u"every month"_s, u"2004-01-31"_s);
        row(u"every 5 months on the last"_s, u"2003-08-12"_s);
        row(u"every month on the 30th"_s, u"2012-02-10"_s);
        row(u"every year"_s, u"2000-02-29"_s);
        row(u"every 3 years"_s, u"1999-12-31"_s);
        row(u"every weekday"_s, u"2024-05-05"_s);
        row(u"co 4 dni robocze"_s, u"2001-06-02"_s); // a Saturday anchor
    }
    void occurrencesFastForward()
    {
        QFETCH(QString, ruleText);
        QFETCH(QString, anchor);
        const RecurrenceRule r = rule(ruleText);
        const QStringList fast = between(r, anchor, u"2026-10-01", u"2027-03-31");
        QStringList slow = between(r, anchor, anchor, u"2027-03-31", 1'000'000);
        slow.removeIf([](const QString &s) { return s < u"2026-10-01"_s; });
        QCOMPARE(fast, slow);
        QVERIFY(!fast.isEmpty());
    }
    void occurrencesBusinessDaysOldAnchors_data()
    {
        QTest::addColumn<int>("interval");
        QTest::addColumn<QString>("anchor");
        QTest::newRow("1 @ 1400") << 1 << u"1400-03-03"_s;
        QTest::newRow("3 @ 1500") << 3 << u"1500-01-01"_s;
        QTest::newRow("7 @ 1800") << 7 << u"1800-06-30"_s;
    }
    void occurrencesBusinessDaysOldAnchors()
    {
        QFETCH(int, interval);
        QFETCH(QString, anchor);
        const QDate a = d(anchor);
        QVERIFY(a.dayOfWeek() <= Qt::Friday);
        // Independent reference: every n-th business day counted from a weekday anchor.
        const QDate lo(2026, 10, 1);
        const QDate hi(2026, 11, 15);
        QStringList reference;
        qint64 count = 0;
        for (QDate day = a; day <= hi; day = day.addDays(1)) {
            if (day.dayOfWeek() > Qt::Friday)
                continue;
            if (count % interval == 0 && day >= lo)
                reference.append(day.toString(u"yyyy-MM-dd"_s));
            ++count;
        }
        QElapsedTimer t;
        t.start();
        const QStringList got = between(R(Freq::Day, interval).workdays(), anchor, u"2026-10-01", u"2026-11-15");
        QVERIFY(t.elapsed() < 2000);
        QVERIFY(!got.isEmpty());
        QCOMPARE(got, reference);
    }
    void occurrencesEndOfRange()
    {
        QCOMPARE(between(rule(u"every year"), u"9997-03-01", u"9990-01-01", u"9999-12-31"),
                 QStringList({u"9997-03-01"_s, u"9998-03-01"_s, u"9999-03-01"_s}));
        // A huge interval is clamped to MaxRecurrenceInterval years: only the anchor fits.
        QCOMPARE(between(R(Freq::Year, std::numeric_limits<int>::max()), u"2026-10-14", u"2026-10-01", u"9999-12-31"),
                 QStringList({u"2026-10-14"_s}));
        // Clamped days still step normally (99 999 days ≈ 274 years) and stop before year 10000.
        const QStringList huge =
            between(R(Freq::Day, std::numeric_limits<int>::max()), u"2026-10-14", u"2026-10-01", u"9999-12-31");
        QVERIFY(huge.size() >= 2);
        QCOMPARE(huge.mid(0, 2), QStringList({u"2026-10-14"_s, u"2300-07-29"_s}));
        for (const QString &s : huge)
            QVERIFY(s <= u"9999-12-31"_s && s.size() == 10);
    }

    // ---- robustness ----------------------------------------------------------------------
    void longSeparatorRuns()
    {
        QElapsedTimer t;
        t.start();
        QCOMPARE(describe(parseRecurrence(QString(u" ,"_s.repeated(20'000) + u"x when done"_s))), u"null"_s);
        QCOMPARE(describe(parseRecurrence(QString(u"every week on monday"_s + u", "_s.repeated(20'000) + u"x"_s))),
                 u"null"_s);
        QCOMPARE(dates::parseWeekdayList(QString(u"monday"_s + QString(40'000, u' ') + u"x"_s)), QList<int>());
        QCOMPARE(dates::parseWeekdayList(QString(u"monday"_s + QString(40'000, u' ') + u", friday"_s)),
                 QList<int>({1, 5}));
        QVERIFY2(t.elapsed() < 5000, qPrintable(QString::number(t.elapsed())));
    }
};

QTEST_GUILESS_MAIN(TestDatesRecurrence)
#include "tst_dates_recurrence.moc"
