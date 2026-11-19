// The built-in habits extension (src/core/extensions/builtin/nawyki.luau, docs/SYNTAX.md §5
// "Habits") on a real notebook: a vault, its index and the NotebookHost writing to disk.
//   * schedules in Polish and English through the `schedule` command (the recurrence parser where
//     it fits, counts per week / month / year, weekday lists), malformed ones refused, the words
//     reading back in both interface languages;
//   * names (links as labels, tags dropped), keys (NFC, case folded, not diacritic-insensitive),
//     definitions (blocks, a page's frontmatter, a Logseq page), checks (#nawyk / #habit tasks of
//     daily notes only), duplicates and undefined names — through the `list` command;
//   * streaks, rates and what counts as missed under each schedule (skipped days, a today not done
//     yet, days without a note, partial first periods, year boundaries, DST, weekStart), the grid;
//   * writing a check (`set`): a new `- [x] Name #nawyk ✅ today` line next to the other checks or
//     as the note's last block, an existing check ticked, skipped or opened again — every other
//     byte kept (CRLF, a BOM, a last line without a break), Logseq keywords, the English tag, a
//     page habit's link, names that cannot be written;
//   * the month view's node tree, the `set` action with its Undo, `add` with its form (asked again
//     when not understood or taken), `tick`, both interface languages, the empty state;
//   * a notebook of 2000 daily notes × 8 habits drawn within the time limit.

#include "extensions/builtin.h"
#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "vault/vault.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::extensions;

namespace {

/// A schedule as the `schedule` command gives it.
struct Sched
{
    QString kind = u"days"_s;
    int interval = 1;
    QList<int> weekdays;
    int monthDay = 0;
    int times = 1;
    QString period = u"week"_s;
};

Sched days(int interval = 1) { return {u"days"_s, interval, {}, 0, 1, u"week"_s}; }
Sched weekdays(QList<int> list, int interval = 1) { return {u"weekdays"_s, interval, std::move(list), 0, 1, u"week"_s}; }
Sched monthDay(int day, int interval = 1) { return {u"monthDay"_s, interval, {}, day, 1, u"week"_s}; }
Sched times(int n, const QString &period, int interval = 1) { return {u"times"_s, interval, {}, 0, n, period}; }

const QString Week = u"week"_s;
const QString Month = u"month"_s;
const QString Year = u"year"_s;

QList<int> intList(const QVariant &v)
{
    QList<int> out;
    for (const QVariant &x : v.toList())
        out << x.toInt();
    return out;
}

/// "2026-11-01:x 2026-11-02:- 2026-11-03:o" -> daily notes with a check of Medytacja each.
QHash<QString, QString> journalsOf(const QString &spec, const QString &name = u"Medytacja"_s)
{
    QHash<QString, QString> out;
    for (const QString &item : spec.split(u' ', Qt::SkipEmptyParts)) {
        const QString date = item.section(u':', 0, 0);
        const QString m = item.section(u':', 1, 1);
        const QString box = m == u"x"_s ? u"x"_s : m == u"-"_s ? u"-"_s : u" "_s;
        out.insert(u"journals/"_s + date + u".md"_s, u"- [%1] %2 #nawyk\n"_s.arg(box, name));
    }
    return out;
}

QString statesText(const QVariantList &days)
{
    QString out;
    for (const QVariant &d : days) {
        const QString s = d.toMap().value(u"state"_s).toString();
        out += s == u"outside"_s ? u"."_s
             : s == u"future"_s  ? u"f"_s
             : s == u"rest"_s    ? u"_"_s
             : s == u"due"_s     ? u"!"_s
             : s == u"done"_s    ? u"x"_s
             : s == u"skipped"_s ? u"-"_s
             : s == u"missed"_s  ? u"m"_s
                                 : u"?"_s;
    }
    return out;
}

/// How a test notebook is opened.
struct Options
{
    QString language = u"pl"_s;
    QString uiLanguage = u"pl"_s;
    Qt::DayOfWeek weekStart = Qt::Monday;
    QString journalFormat;
};

} // namespace

Q_DECLARE_METATYPE(Sched)

class TestExtensionsHabits : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QDateTime m_now{QDate(2026, 11, 3), QTime(9, 0)};
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    std::unique_ptr<NotebookHost> m_host;
    std::unique_ptr<Engine> m_engine;
    QString m_dir;

    bool open(const QHash<QString, QString> &files, const Options &o = Options())
    {
        m_engine.reset();
        m_host.reset();
        m_index.reset();
        m_vault.reset();
        m_dir = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        QDir().mkpath(m_dir);
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            if (!writeFile(it.key(), it.value()))
                return false;
        m_vault = std::make_unique<Vault>(m_dir, m_tmp.filePath(u"s%1"_s.arg(m_n)));
        if (!m_vault->open())
            return false;
        VaultConfig config = m_vault->config();
        config.weekStartsOn = o.weekStart;
        if (!o.journalFormat.isEmpty())
            config.journalFileFormat = o.journalFormat;
        m_index = std::make_unique<VaultIndex>(config);
        {
            VaultIndex::Batch batch(*m_index);
            for (auto it = files.cbegin(); it != files.cend(); ++it)
                if (it.key().endsWith(u".md"_s))
                    m_index->update(it.key(), m_vault->readText(it.key()));
        }
        NotebookHost::Options options;
        options.now = [this] { return m_now; };
        options.language = o.language;
        options.uiLanguage = o.uiLanguage;
        options.updateIndex = true;
        m_host = std::make_unique<NotebookHost>(*m_vault, *m_index, options);
        m_engine = std::make_unique<Engine>();
        if (!m_engine->start(m_host.get(), Limits{2000 * BRULION_TEST_SLOWDOWN, 64ll << 20}))
            return false;
        const Result loaded = m_engine->load(builtinExtension(u"nawyki"_s));
        if (!loaded.ok())
            qWarning("%s", qPrintable(loaded.error));
        return loaded.ok();
    }

    bool writeFile(const QString &path, const QString &content)
    {
        const QString file = m_dir + u'/' + path;
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(content.toUtf8());
        return true;
    }

    /// A note changed as the editor would change it: on disk and in the index.
    void edit(const QString &path, const QString &content)
    {
        QVERIFY(writeFile(path, content));
        m_index->update(path, content);
    }

    QByteArray onDisk(const QString &path) const
    {
        QFile f(m_dir + u'/' + path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
    }

    Result run(const QString &name, const QVariantMap &args = {})
    {
        return m_engine->runCommand(u"nawyki"_s, name, {{u"page"_s, QString()}, {u"selection"_s, QString()}, {u"args"_s, args}});
    }

    QVariantMap command(const QString &name, const QVariantMap &args = {})
    {
        const Result r = run(name, args);
        if (!r.ok())
            qWarning("%s: %s", qPrintable(name), qPrintable(r.error));
        return r.value.toMap();
    }

    QVariantMap schedule(const QString &text) { return command(u"schedule"_s, {{u"text"_s, text}}); }

    QVariantMap list(const QString &date = QString(), int days = 7)
    {
        QVariantMap args{{u"days"_s, days}};
        if (!date.isEmpty())
            args.insert(u"date"_s, date);
        return command(u"list"_s, args);
    }

    static QVariantMap habitIn(const QVariantMap &list, const QString &name)
    {
        for (const QVariant &h : list.value(u"habits"_s).toList())
            if (h.toMap().value(u"name"_s) == name)
                return h.toMap();
        return {};
    }

    QVariantMap set(const QString &name, const QString &state, const QString &date = QString())
    {
        QVariantMap args{{u"name"_s, name}, {u"state"_s, state}};
        if (!date.isEmpty())
            args.insert(u"date"_s, date);
        return command(u"set"_s, args);
    }

    QVariantMap render(const QVariantMap &params = {})
    {
        const Result r = m_engine->render(u"nawyki"_s, u"month"_s, params);
        if (!r.ok())
            qWarning("%s", qPrintable(r.error));
        return r.value.toMap();
    }

    Result act(const QVariantMap &args) { return m_engine->act(u"nawyki"_s, u"month"_s, u"set"_s, args); }
    Result act(const QString &action, const QVariantMap &args) { return m_engine->act(u"nawyki"_s, u"month"_s, action, args); }

    static QVariantList children(const QVariantMap &node) { return node.value(u"children"_s).toList(); }

    /// The cards of the month view.
    static QList<QVariantMap> cards(const QVariantMap &view)
    {
        QList<QVariantMap> out;
        for (const QVariant &n : children(view))
            if (n.toMap().value(u"type"_s) == u"card"_s)
                out << n.toMap();
        return out;
    }

    static QVariantMap cardOf(const QVariantMap &view, const QString &name)
    {
        for (const QVariantMap &card : cards(view)) {
            const QVariantList c = children(card);
            if (c.isEmpty())
                continue;
            const QVariantMap first = c[0].toMap();
            if (first.value(u"type"_s) == u"text"_s && first.value(u"text"_s) == name)
                return card;
            if (first.value(u"type"_s) == u"row"_s && children(first).value(1).toMap().value(u"text"_s) == name)
                return card;
        }
        return {};
    }

    static QVariantMap monthOf(const QVariantMap &card)
    {
        for (const QVariant &n : children(card))
            if (n.toMap().value(u"type"_s) == u"month"_s)
                return n.toMap();
        return {};
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    void init()
    {
        m_now = QDateTime(QDate(2026, 11, 3), QTime(9, 0));
    }

    // ---- schedules ------------------------------------------------------------------------

    void parse_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<Sched>("expected");

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
        QTest::newRow("3 times in a week") << u"3 times in a week"_s << times(3, Week);
        QTest::newRow("7 a week") << u"7 razy w tygodniu"_s << times(7, Week);
        QTest::newRow("31 a month") << u"31 razy w miesiącu"_s << times(31, Month);
        QTest::newRow("14 every 2 weeks") << u"14 razy na 2 tygodnie"_s << times(14, Week, 2);
    }

    void parse()
    {
        QFETCH(QString, text);
        QFETCH(Sched, expected);
        if (!m_engine)
            QVERIFY(open({}));
        const QVariantMap s = schedule(text);
        QVERIFY2(!s.contains(u"error"_s), qPrintable(text));
        QCOMPARE(s.value(u"kind"_s).toString(), expected.kind);
        QCOMPARE(s.value(u"interval"_s).toInt(), expected.interval);
        QCOMPARE(intList(s.value(u"weekdays"_s)), expected.weekdays);
        QCOMPARE(s.value(u"monthDay"_s).toInt(), expected.monthDay);
        QCOMPARE(s.value(u"times"_s).toInt(), expected.times);
        if (expected.kind == u"times"_s)
            QCOMPARE(s.value(u"period"_s).toString(), expected.period);
    }

    void malformed_data()
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
        QTest::newRow("huge count") << u"99999999999 razy w roku"_s;
        QTest::newRow("lone surrogate") << QString(QChar(0xD800)) + u" razy w tygodniu"_s;
    }

    void malformed()
    {
        QFETCH(QString, text);
        if (!m_engine)
            QVERIFY(open({}));
        QCOMPARE(schedule(text).value(u"error"_s).toString(), u"not_understood"_s);
    }

    void formatReadsBack_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<Sched>("expected");
        QTest::newRow("daily") << u"codziennie"_s << days();
        QTest::newRow("every 2 days") << u"co 2 dni"_s << days(2);
        QTest::newRow("weekdays") << u"w dni robocze"_s << weekdays({1, 2, 3, 4, 5});
        QTest::newRow("mon wed fri") << u"pn, śr, pt"_s << weekdays({1, 3, 5});
        QTest::newRow("sat sun") << u"sob, nd"_s << weekdays({6, 7});
        QTest::newRow("every 2 weeks on monday") << u"every 2 weeks on Monday"_s << weekdays({1}, 2);
        QTest::newRow("15th") << u"co miesiąc 15."_s << monthDay(15);
        QTest::newRow("last") << u"co miesiąc ostatniego"_s << monthDay(0);
        QTest::newRow("every 3 months on the 1st") << u"every 3 months on the 1st"_s << monthDay(1, 3);
        QTest::newRow("once a week") << u"raz w tygodniu"_s << times(1, Week);
        QTest::newRow("3 a week") << u"3 razy w tygodniu"_s << times(3, Week);
        QTest::newRow("2 a month") << u"2 razy w miesiącu"_s << times(2, Month);
        QTest::newRow("once a year") << u"raz w roku"_s << times(1, Year);
        QTest::newRow("once every 2 weeks") << u"every 2 weeks"_s << times(1, Week, 2);
        QTest::newRow("5 every 2 weeks") << u"5 razy na 2 tygodnie"_s << times(5, Week, 2);
        QTest::newRow("3 every 2 months") << u"3 razy na 2 miesiące"_s << times(3, Month, 2);
        QTest::newRow("2 every 5 years") << u"2 razy na 5 lat"_s << times(2, Year, 5);
    }

    void formatReadsBack()
    {
        QFETCH(QString, text);
        QFETCH(Sched, expected);
        if (!m_engine)
            QVERIFY(open({}));
        for (const QString &lang : {u"pl"_s, u"en"_s}) {
            m_host->setLanguages(u"pl"_s, lang);
            const QVariantMap first = schedule(text);
            QCOMPARE(first.value(u"kind"_s).toString(), expected.kind);
            const QString words = first.value(u"words"_s).toString();
            QVERIFY(!words.isEmpty());
            const QVariantMap back = schedule(words);
            QVERIFY2(!back.contains(u"error"_s), qPrintable(words));
            QCOMPARE(back.value(u"kind"_s).toString(), expected.kind);
            QCOMPARE(back.value(u"interval"_s).toInt(), expected.interval);
            QCOMPARE(intList(back.value(u"weekdays"_s)), expected.weekdays);
            QCOMPARE(back.value(u"monthDay"_s).toInt(), expected.monthDay);
            QCOMPARE(back.value(u"times"_s).toInt(), expected.times);
            QCOMPARE(back.value(u"period"_s).toString(), expected.period);
        }
        m_host->setLanguages(u"pl"_s, u"pl"_s);
    }

    void formatWords_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("lang");
        QTest::addColumn<QString>("words");
        QTest::newRow("codziennie") << u"tak"_s << u"pl"_s << u"codziennie"_s;
        QTest::newRow("every day") << u"codziennie"_s << u"en"_s << u"every day"_s;
        QTest::newRow("3 razy w tygodniu") << u"3x/tydz"_s << u"pl"_s << u"3 razy w tygodniu"_s;
        QTest::newRow("raz w miesiącu") << u"once a month"_s << u"pl"_s << u"raz w miesiącu"_s;
        QTest::newRow("twice a month") << u"dwa razy w miesiącu"_s << u"en"_s << u"twice a month"_s;
        QTest::newRow("5 razy na 2 tygodnie") << u"5 times every 2 weeks"_s << u"pl"_s << u"5 razy na 2 tygodnie"_s;
        QTest::newRow("3 razy na 5 lat") << u"3 razy na 5 lat"_s << u"pl"_s << u"3 razy na 5 lat"_s;
        QTest::newRow("3 times every 2 months") << u"3 razy na 2 miesiące"_s << u"en"_s << u"3 times every 2 months"_s;
        QTest::newRow("w dni robocze") << u"every weekday"_s << u"pl"_s << u"w dni robocze"_s;
        QTest::newRow("Mon, Wed, Fri") << u"pn, śr, pt"_s << u"en"_s << u"Mon, Wed, Fri"_s;
        QTest::newRow("pn, śr, pt") << u"mon, wed, fri"_s << u"pl"_s << u"pn, śr, pt"_s;
    }

    void formatWords()
    {
        QFETCH(QString, text);
        QFETCH(QString, lang);
        QFETCH(QString, words);
        if (!m_engine)
            QVERIFY(open({}));
        m_host->setLanguages(u"pl"_s, lang);
        QCOMPARE(schedule(text).value(u"words"_s).toString(), words);
        m_host->setLanguages(u"pl"_s, u"pl"_s);
    }

    /// Due days (isScheduled): a day of the habit, the day after it today, no check — missed when
    /// it was due, a rest day (or before the start) when not.
    void scheduled_data()
    {
        QTest::addColumn<QString>("schedule");
        QTest::addColumn<Sched>("expected");
        QTest::addColumn<QDate>("start");
        QTest::addColumn<QDate>("day");
        QTest::addColumn<int>("weekStart");
        QTest::addColumn<bool>("due");

        const QDate nov1(2026, 11, 1);
        QTest::newRow("daily on start") << u"codziennie"_s << days() << nov1 << nov1 << 1 << true;
        QTest::newRow("daily before start") << u"codziennie"_s << days() << nov1 << QDate(2026, 10, 31) << 1 << false;
        QTest::newRow("every 2 days, 2nd") << u"co 2 dni"_s << days(2) << nov1 << QDate(2026, 11, 2) << 1 << false;
        QTest::newRow("every 2 days, 3rd") << u"co 2 dni"_s << days(2) << nov1 << QDate(2026, 11, 3) << 1 << true;
        QTest::newRow("every 2 days across the year")
                << u"co 2 dni"_s << days(2) << QDate(2026, 12, 31) << QDate(2027, 1, 2) << 1 << true;
        QTest::newRow("mon/fri on a friday") << u"pn, pt"_s << weekdays({1, 5}) << nov1 << QDate(2026, 11, 6) << 1 << true;
        QTest::newRow("mon/fri on a thursday")
                << u"pn, pt"_s << weekdays({1, 5}) << nov1 << QDate(2026, 11, 5) << 1 << false;
        // Every 2 weeks from a Wednesday: weeks counted from the start's week (Monday-based).
        QTest::newRow("2 weeks: next week's monday") << u"co 2 tygodnie w poniedziałek"_s << weekdays({1}, 2)
                                                     << QDate(2026, 11, 4) << QDate(2026, 11, 9) << 1 << false;
        QTest::newRow("2 weeks: the monday after") << u"co 2 tygodnie w poniedziałek"_s << weekdays({1}, 2)
                                                   << QDate(2026, 11, 4) << QDate(2026, 11, 16) << 1 << true;
        QTest::newRow("2 weeks across the year") << u"every 2 weeks on Monday"_s << weekdays({1}, 2)
                                                 << QDate(2026, 12, 28) << QDate(2027, 1, 11) << 1 << true;
        QTest::newRow("2 weeks across the year, off") << u"every 2 weeks on Monday"_s << weekdays({1}, 2)
                                                      << QDate(2026, 12, 28) << QDate(2027, 1, 4) << 1 << false;
        // Weeks starting on Sunday: Sunday 8 Nov begins the week after the start's.
        QTest::newRow("2 weeks, Sunday weeks") << u"every 2 weeks on Sunday"_s << weekdays({7}, 2)
                                               << QDate(2026, 11, 2) << QDate(2026, 11, 8) << 7 << false;
        QTest::newRow("2 weeks, Sunday weeks, same week") << u"every 2 weeks on Monday, Sunday"_s << weekdays({1, 7}, 2)
                                                          << QDate(2026, 11, 1) << QDate(2026, 11, 2) << 7 << true;
        QTest::newRow("31st in April is the 30th")
                << u"every month on the 31st"_s << monthDay(31) << QDate(2026, 1, 1) << QDate(2026, 4, 30) << 1 << true;
        QTest::newRow("last day of a leap February")
                << u"co miesiąc ostatniego"_s << monthDay(0) << QDate(2028, 1, 1) << QDate(2028, 2, 29) << 1 << true;
        QTest::newRow("last day, the 28th of a leap year")
                << u"co miesiąc ostatniego"_s << monthDay(0) << QDate(2028, 1, 1) << QDate(2028, 2, 28) << 1 << false;
        QTest::newRow("every 3 months, off month") << u"every 3 months on the 1st"_s << monthDay(1, 3)
                                                   << QDate(2026, 11, 1) << QDate(2026, 12, 1) << 1 << false;
        QTest::newRow("every 3 months across the year") << u"every 3 months on the 1st"_s << monthDay(1, 3)
                                                        << QDate(2026, 11, 1) << QDate(2027, 2, 1) << 1 << true;
        QTest::newRow("times: no due days")
                << u"3 razy w tygodniu"_s << times(3, Week) << nov1 << QDate(2026, 11, 2) << 1 << false;
    }

    void scheduled()
    {
        QFETCH(QString, schedule);
        QFETCH(Sched, expected);
        QFETCH(QDate, start);
        QFETCH(QDate, day);
        QFETCH(int, weekStart);
        QFETCH(bool, due);
        Options o;
        o.weekStart = Qt::DayOfWeek(weekStart);
        QVERIFY(open({{u"Nawyki.md"_s, u"- Medytacja\n  habit:: %1\n  start:: %2\n"_s.arg(schedule, start.toString(Qt::ISODate))}}, o));
        const QVariantMap s = this->schedule(schedule);
        QCOMPARE(s.value(u"kind"_s).toString(), expected.kind);
        QCOMPARE(s.value(u"interval"_s).toInt(), expected.interval);
        QCOMPARE(intList(s.value(u"weekdays"_s)), expected.weekdays);
        QCOMPARE(s.value(u"monthDay"_s).toInt(), expected.monthDay);
        const QVariantMap h = habitIn(list(day.addDays(1).toString(Qt::ISODate), 2), u"Medytacja"_s);
        const QString state = h.value(u"days"_s).toList().value(0).toMap().value(u"state"_s).toString();
        QCOMPARE(state == u"missed"_s, due);
    }

    // ---- names, definitions, checks -------------------------------------------------------

    void names_data()
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
        QTest::newRow("code keeps its hash") << u"Ćwiczenia `#nie-tag`"_s << u"Ćwiczenia `#nie-tag`"_s;
        QTest::newRow("unclosed link") << u"[[Bieganie #nawyk"_s << u"[[Bieganie"_s;
    }

    void names()
    {
        QFETCH(QString, text);
        QFETCH(QString, name);
        QVERIFY(open({{u"Nawyki.md"_s, u"- %1\n  habit:: codziennie\n"_s.arg(text)}}));
        const QVariantList habits = list().value(u"habits"_s).toList();
        if (name.isEmpty()) {
            QVERIFY(habits.isEmpty());
            return;
        }
        QCOMPARE(habits.size(), 1);
        QCOMPARE(habits[0].toMap().value(u"name"_s).toString(), name);
    }

    void keys()
    {
        QVERIFY(open({{u"Nawyki.md"_s, u"- żółw biegnie\n  habit:: codziennie\n- Łódź\n  habit:: codziennie\n- Joga\n  habit:: codziennie\n"_s},
                      {u"journals/2026-11-03.md"_s, u"- [x] Joga #nawyki\n"_s}}));
        // Case and white space do not matter; NFC: decomposed = composed.
        QCOMPARE(set(u"  ŻÓŁW   biegnie "_s, u"done"_s).value(u"habit"_s).toString(), u"żółw biegnie"_s);
        QCOMPARE(set(u"żółw biegnie"_s, u"done"_s).value(u"changed"_s).toBool(), false);
        // Diacritics do: "Lodz" is no "Łódź"; nothing is no name.
        QCOMPARE(set(u"Lodz"_s, u"done"_s).value(u"error"_s).toString(), u"habit_not_found"_s);
        QCOMPARE(set(u"   "_s, u"done"_s).value(u"error"_s).toString(), u"habit_not_found"_s);
        // The first line of a name only.
        QCOMPARE(set(u"Joga\nz Anią"_s, u"skipped"_s).value(u"habit"_s).toString(), u"Joga"_s);
        // #nawyki is no check tag; #nawyk and #habit are, and Polish notes get #nawyk.
        QCOMPARE(onDisk(u"journals/2026-11-03.md"_s),
                 "- [x] Joga #nawyki\n- [x] żółw biegnie #nawyk ✅ 2026-11-03\n- [-] Joga #nawyk ❌ 2026-11-03\n"_ba);
    }

    void definitions()
    {
        const QString page = u"---\nhabit: 3 razy w tygodniu\nstart: 2026-10-01\n---\n"
                             "- Medytacja\n  habit:: codziennie\n"
                             "- Czytanie #zdrowie\n  nawyk:: pn, śr, pt\n  start:: [[2026-10-05]]\n"
                             "- Coś dziwnego\n  habit:: kiedy mi się zachce\n"
                             "- Zwykły punkt\n  kolor:: zielony\n"
                             "- #tylko-tag\n  habit:: codziennie\n"
                             "\nAkapit z nawykiem\nhabit:: co miesiąc 15.\n"_s;
        QVERIFY(open({{u"Zdrowie/Bieganie.md"_s, page},
                      {u"Joga.md"_s, u"habit:: pn, czw\n\n- notatki\n"_s},
                      {u"Spacer.md"_s, u"- Spacer\n  habit:: codziennie\n  start:: wczoraj\n"_s}}));
        const QVariantMap result = list(u"2026-11-03"_s);
        const QVariantList habits = result.value(u"habits"_s).toList();
        QCOMPARE(habits.size(), 7);
        // Notes by path (Joga.md, Spacer.md, Zdrowie/…), the page's own definition first.
        QStringList names;
        for (const QVariant &h : habits)
            names << h.toMap().value(u"name"_s).toString();
        QCOMPARE(names, (QStringList{u"Joga"_s, u"Spacer"_s, u"Bieganie"_s, u"Medytacja"_s, u"Czytanie"_s,
                                     u"Coś dziwnego"_s, u"Akapit z nawykiem"_s}));
        const QVariantMap joga = habits[0].toMap();
        QCOMPARE(joga.value(u"scheduleText"_s).toString(), u"pn, czw"_s);
        QCOMPARE(joga.value(u"ref"_s).toString(), u"Joga.md"_s);
        // A malformed start is no start: it starts today (nothing recorded).
        QCOMPARE(habits[1].toMap().value(u"start"_s).toString(), u"2026-11-03"_s);
        const QVariantMap run = habits[2].toMap();
        QCOMPARE(run.value(u"line"_s).toInt(), 0);
        QCOMPARE(run.value(u"path"_s).toString(), u"Zdrowie/Bieganie.md"_s);
        QCOMPARE(run.value(u"scheduleText"_s).toString(), u"3 razy w tygodniu"_s);
        QCOMPARE(run.value(u"start"_s).toString(), u"2026-10-01"_s);
        QCOMPARE(run.value(u"period"_s).toMap().value(u"target"_s).toInt(), 3);
        const QVariantMap med = habits[3].toMap();
        QCOMPARE(med.value(u"line"_s).toInt(), 4);
        QVERIFY(med.value(u"ref"_s).toString().startsWith(u"Zdrowie/Bieganie.md#L5@"_s));
        QCOMPARE(med.value(u"scheduleText"_s).toString(), u"codziennie"_s);
        QCOMPARE(med.value(u"start"_s).toString(), u"2026-11-03"_s);
        const QVariantMap read = habits[4].toMap();
        QCOMPARE(read.value(u"schedule"_s).toString(), u"pn, śr, pt"_s);
        QCOMPARE(read.value(u"start"_s).toString(), u"2026-10-05"_s);
        const QVariantMap odd = habits[5].toMap();
        QCOMPARE(odd.value(u"problem"_s).toString(), u"schedule_not_understood"_s);
        QCOMPARE(odd.value(u"valid"_s).toBool(), false);
        QVERIFY(!odd.contains(u"streak"_s));
        QCOMPARE(habits[6].toMap().value(u"scheduleText"_s).toString(), u"co miesiąc 15."_s);
    }

    void checks()
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
        QVERIFY(open({{u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n- Czytanie\n  habit:: codziennie\n"
                                       "- Bieganie\n  habit:: codziennie\n- Zakupy\n  habit:: codziennie\n"
                                       "- Joga\n  habit:: codziennie\n- Pływanie\n  habit:: codziennie\n"_s},
                      {u"journals/2026-11-03.md"_s, journal}}));
        const QVariantMap result = list(u"2026-11-03"_s);
        const auto mark = [&](const QString &name) {
            return habitIn(result, name).value(u"today"_s).toMap().value(u"mark"_s).toString();
        };
        QCOMPARE(mark(u"Medytacja"_s), u"done"_s);
        QCOMPARE(mark(u"Czytanie"_s), u"skipped"_s);
        QCOMPARE(mark(u"Bieganie"_s), u"open"_s);
        QCOMPARE(mark(u"Zakupy"_s), QString()); // an untagged task is no check
        QCOMPARE(mark(u"Joga"_s), u"done"_s);  // a Logseq keyword
        QCOMPARE(mark(u"Pływanie"_s), u"open"_s);
        // A block defining a habit is no check, even tagged; the defined name is a habit.
        QCOMPARE(mark(u"Definicja"_s), QString());
        // Nested checks count; names no habit has are listed by their key.
        QCOMPARE(result.value(u"undefined"_s).toStringList(), QStringList{u"rozciąganie"_s});
        QCOMPARE(habitIn(result, u"Medytacja"_s).value(u"lastDone"_s).toString(), u"2026-11-03"_s);
    }

    void checksOnlyInJournals()
    {
        QVERIFY(open({{u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n"_s},
                      {u"Projekty/Plan.md"_s, u"- [x] Medytacja #nawyk\n"_s}}));
        QCOMPARE(habitIn(list(u"2026-11-03"_s), u"Medytacja"_s).value(u"done"_s).toInt(), 0);
        // Another file name format of the journal folder.
        Options o;
        o.journalFormat = u"dd.MM.yyyy"_s;
        QVERIFY(open({{u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n"_s},
                      {u"journals/03.11.2026.md"_s, u"- [x] Medytacja #nawyk\n"_s}}, o));
        QCOMPARE(habitIn(list(u"2026-11-03"_s), u"Medytacja"_s).value(u"lastDone"_s).toString(), u"2026-11-03"_s);
    }

    // ---- statistics -----------------------------------------------------------------------

    void stats_data()
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
                << u"codziennie"_s << none << u"2026-11-01:x 2026-11-02:x"_s << QDate(2026, 11, 3) << 1 << 2 << 2 << 2
                << 2 << true;
        QTest::newRow("daily, an open check today is pending too")
                << u"codziennie"_s << none << u"2026-11-01:x 2026-11-02:x 2026-11-03:o"_s << QDate(2026, 11, 3) << 1 << 2
                << 2 << 2 << 2 << true;
        QTest::newRow("daily, a day without a note breaks it")
                << u"codziennie"_s << none << u"2026-11-01:x 2026-11-03:x"_s << QDate(2026, 11, 3) << 1 << 1 << 1 << 2
                << 3 << false;
        QTest::newRow("daily, an open check yesterday is missed")
                << u"codziennie"_s << none << u"2026-11-01:x 2026-11-02:o 2026-11-03:x"_s << QDate(2026, 11, 3) << 1 << 1
                << 1 << 2 << 3 << false;
        QTest::newRow("daily, a skipped day is excused")
                << u"codziennie"_s << none << u"2026-11-01:x 2026-11-02:- 2026-11-03:x"_s << QDate(2026, 11, 3) << 1 << 2
                << 2 << 2 << 2 << false;
        QTest::newRow("daily, longest earlier")
                << u"codziennie"_s << none << u"2026-10-01:x 2026-10-02:x 2026-10-03:x 2026-10-05:x"_s
                << QDate(2026, 10, 5) << 1 << 1 << 3 << 4 << 5 << false;
        QTest::newRow("daily, explicit start before the first check")
                << u"codziennie"_s << QDate(2026, 10, 30) << u"2026-11-02:x 2026-11-03:x"_s << QDate(2026, 11, 3) << 1
                << 2 << 2 << 2 << 5 << false;
        QTest::newRow("daily, checks before the start are not counted")
                << u"codziennie"_s << QDate(2026, 11, 2) << u"2026-10-20:x 2026-11-02:x"_s << QDate(2026, 11, 2) << 1
                << 1 << 1 << 1 << 1 << false;
        QTest::newRow("daily, across the year")
                << u"codziennie"_s << none << u"2026-12-30:x 2026-12-31:x 2027-01-01:x"_s << QDate(2027, 1, 1) << 1 << 3
                << 3 << 3 << 3 << false;
        QTest::newRow("daily, across the DST change")
                << u"codziennie"_s << none << u"2026-10-24:x 2026-10-25:x 2026-10-26:x"_s << QDate(2026, 10, 26) << 1
                << 3 << 3 << 3 << 3 << false;
        QTest::newRow("daily, start after today")
                << u"codziennie"_s << QDate(2026, 12, 1) << u""_s << QDate(2026, 11, 3) << 1 << 0 << 0 << 0 << 0 << false;
        QTest::newRow("daily, nothing yet: starts today")
                << u"codziennie"_s << none << u""_s << QDate(2026, 11, 3) << 1 << 0 << 0 << 0 << 0 << true;
        QTest::newRow("every 2 days, today due") << u"co 2 dni"_s << none << u"2026-11-01:x 2026-11-03:x"_s
                                                 << QDate(2026, 11, 5) << 1 << 2 << 2 << 2 << 2 << true;
        QTest::newRow("every 2 days, off-day extra does not count")
                << u"co 2 dni"_s << none << u"2026-11-01:x 2026-11-04:x"_s << QDate(2026, 11, 5) << 1 << 0 << 1 << 1
                << 2 << true;
        QTest::newRow("mon/wed/fri, done twice, thursday")
                << u"pn, śr, pt"_s << QDate(2026, 11, 2) << u"2026-11-02:x 2026-11-03:x 2026-11-04:x"_s
                << QDate(2026, 11, 5) << 1 << 2 << 2 << 2 << 2 << false;
        QTest::newRow("mon/wed/fri, wednesday missed, friday due")
                << u"pn, śr, pt"_s << QDate(2026, 11, 2) << u"2026-11-02:x"_s << QDate(2026, 11, 6) << 1 << 0 << 1 << 1
                << 2 << true;
        QTest::newRow("weekdays across the weekend")
                << u"w dni robocze"_s << none << u"2026-11-05:x 2026-11-06:x 2026-11-09:x"_s << QDate(2026, 11, 9) << 1
                << 3 << 3 << 3 << 3 << false;
        QTest::newRow("15th monthly") << u"co miesiąc 15."_s << QDate(2026, 9, 1) << u"2026-09-15:x 2026-10-15:x"_s
                                      << QDate(2026, 11, 3) << 1 << 2 << 2 << 2 << 2 << false;
        QTest::newRow("15th monthly, today") << u"co miesiąc 15."_s << QDate(2026, 9, 1)
                                             << u"2026-09-15:x 2026-10-15:x"_s << QDate(2026, 11, 15) << 1 << 2 << 2
                                             << 2 << 2 << true;
        QTest::newRow("3 a week: met, then pending")
                << u"3 razy w tygodniu"_s << QDate(2026, 10, 26)
                << u"2026-10-26:x 2026-10-28:x 2026-10-30:x 2026-11-02:x"_s << QDate(2026, 11, 4) << 1 << 1 << 1 << 1
                << 1 << true;
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
                << u"raz w tygodniu"_s << QDate(2026, 12, 27) << u"2026-12-27:x 2027-01-05:x"_s << QDate(2027, 1, 10)
                << 7 << 2 << 2 << 2 << 2 << true;
        // Monday weeks: 27 Dec ends its week (met), 28 Dec – 3 Jan has nothing (missed).
        QTest::newRow("once a week, Monday weeks: a week missed")
                << u"raz w tygodniu"_s << QDate(2026, 12, 27) << u"2026-12-27:x 2027-01-05:x"_s << QDate(2027, 1, 10)
                << 1 << 1 << 1 << 2 << 3 << false;
        QTest::newRow("twice a month, last month missed")
                << u"dwa razy w miesiącu"_s << QDate(2026, 9, 1)
                << u"2026-09-03:x 2026-09-20:x 2026-10-10:x 2026-11-02:x"_s << QDate(2026, 11, 3) << 1 << 0 << 1 << 1
                << 2 << true;
        QTest::newRow("once every 2 weeks") << u"every 2 weeks"_s << QDate(2026, 10, 19)
                                            << u"2026-10-20:x 2026-11-03:x"_s << QDate(2026, 11, 3) << 1 << 2 << 2 << 2
                                            << 2 << false;
    }

    void stats()
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
        QHash<QString, QString> files = journalsOf(marks);
        files.insert(u"Nawyki.md"_s, u"- Medytacja\n  habit:: %1\n%2"_s.arg(
                                             schedule, start.isValid() ? u"  start:: %1\n"_s.arg(start.toString(Qt::ISODate)) : QString()));
        Options o;
        o.weekStart = Qt::DayOfWeek(weekStart);
        QVERIFY(open(files, o));
        const QVariantMap h = habitIn(list(today.toString(Qt::ISODate)), u"Medytacja"_s);
        QVERIFY(h.value(u"valid"_s).toBool());
        const QVariantMap streak = h.value(u"streak"_s).toMap();
        QCOMPARE(streak.value(u"current"_s).toInt(), current);
        QCOMPARE(streak.value(u"longest"_s).toInt(), longest);
        QCOMPARE(h.value(u"met"_s).toInt(), met);
        QCOMPARE(h.value(u"expected"_s).toInt(), expected);
        QCOMPARE(h.value(u"today"_s).toMap().value(u"due"_s).toBool(), dueToday);
        QVERIFY(current <= longest);
        if (expected == 0)
            QVERIFY(!h.contains(u"rate"_s));
        else
            QCOMPARE(h.value(u"rate"_s).toDouble(), std::round(double(met) / expected * 1000) / 1000);
    }

    void grid_data()
    {
        QTest::addColumn<QString>("schedule");
        QTest::addColumn<QDate>("start");
        QTest::addColumn<QString>("marks");
        QTest::addColumn<QString>("states");

        // Mon 26 Oct … Sun 8 Nov 2026, today Thursday 5 Nov.
        QTest::newRow("mon/wed/fri") << u"pn, śr, pt"_s << QDate(2026, 10, 28)
                                     << u"2026-10-28:x 2026-10-29:x 2026-11-02:- 2026-11-05:o"_s << u"..xxm__-_m_fff"_s;
        QTest::newRow("daily, today due") << u"codziennie"_s << QDate(2026, 11, 1) << u"2026-11-01:x 2026-11-03:x"_s
                                          << u"......xmxm!fff"_s;
        QTest::newRow("daily, today done") << u"codziennie"_s << QDate(2026, 11, 4) << u"2026-11-04:x 2026-11-05:x"_s
                                           << u".........xxfff"_s;
        QTest::newRow("3 a week, period open") << u"3 razy w tygodniu"_s << QDate(2026, 10, 26)
                                               << u"2026-10-26:x 2026-11-03:x"_s << u"x_______x_!fff"_s;
        QTest::newRow("3 a week, period met") << u"3 razy w tygodniu"_s << QDate(2026, 10, 26)
                                              << u"2026-11-02:x 2026-11-03:x 2026-11-04:x"_s << u"_______xxx_fff"_s;
    }

    void grid()
    {
        QFETCH(QString, schedule);
        QFETCH(QDate, start);
        QFETCH(QString, marks);
        QFETCH(QString, states);
        m_now = QDateTime(QDate(2026, 11, 5), QTime(20, 0));
        QHash<QString, QString> files = journalsOf(marks);
        files.insert(u"Nawyki.md"_s, u"- Medytacja\n  habit:: %1\n  start:: %2\n"_s.arg(schedule, start.toString(Qt::ISODate)));
        QVERIFY(open(files));
        // Up to today from the list: 26 Oct … 5 Nov.
        const QVariantMap h = habitIn(list(u"2026-11-05"_s, 11), u"Medytacja"_s);
        QCOMPARE(statesText(h.value(u"days"_s).toList()), states.first(11));
        // The month view: its days in November, the future ones not tappable.
        const QVariantMap month = monthOf(cardOf(render({{u"month"_s, u"2026-11"_s}}), u"Medytacja"_s));
        QCOMPARE(month.value(u"month"_s).toString(), u"2026-11"_s);
        const QVariantMap days = month.value(u"days"_s).toMap();
        QCOMPARE(days.size(), 30);
        QVariantList november;
        for (int d = 1; d <= 8; ++d)
            november << days.value(QDate(2026, 11, d).toString(Qt::ISODate));
        QCOMPARE(statesText(november), states.last(8));
        QVERIFY(!days.value(u"2026-11-06"_s).toMap().contains(u"action"_s));
        QCOMPARE(days.value(u"2026-11-05"_s).toMap().value(u"action"_s).toString(), u"set"_s);
    }

    void gridBounded()
    {
        QVERIFY(open({{u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n  start:: 1926-01-01\n- Bla\n  habit:: bla bla\n"_s},
                      {u"journals/2026-11-03.md"_s, u"- [x] Medytacja #nawyk\n- [x] Bla #nawyk\n"_s}}));
        const QVariantMap result = list(u"2026-11-03"_s, 1000);
        const QVariantMap h = habitIn(result, u"Medytacja"_s);
        QCOMPARE(h.value(u"days"_s).toList().size(), 400);
        // A start a century ago: only the last 3660 days are walked.
        QCOMPARE(h.value(u"expected"_s).toInt(), 3660 + 1);
        QCOMPARE(h.value(u"streak"_s).toMap().value(u"current"_s).toInt(), 1);
        QCOMPARE(h.value(u"start"_s).toString(), u"1926-01-01"_s);
        // An invalid schedule has no statistics.
        const QVariantMap bla = habitIn(result, u"Bla"_s);
        QCOMPARE(bla.value(u"problem"_s).toString(), u"schedule_not_understood"_s);
        QVERIFY(!bla.contains(u"done"_s));
        QVERIFY(!bla.contains(u"days"_s));
        // No days asked for: none.
        QVERIFY(!habitIn(list(u"2026-11-03"_s, 0), u"Medytacja"_s).contains(u"days"_s));
        QCOMPARE(list(u"2026-11-31"_s).value(u"error"_s).toString(), u"bad_argument"_s);
    }

    void markNames_data()
    {
        QTest::addColumn<QString>("word");
        QTest::addColumn<QString>("mark");
        QTest::newRow("Zrobione") << u"Zrobione"_s << u"done"_s;
        QTest::newRow("POMINIĘTE") << u"POMINIĘTE"_s << u"skipped"_s;
        QTest::newRow("skip") << u"skip"_s << u"skipped"_s;
        QTest::newRow("pomiń") << u" pomiń "_s << u"skipped"_s;
        QTest::newRow("none") << u"none"_s << u"open"_s;
        QTest::newRow("brak") << u"brak"_s << u"open"_s;
        QTest::newRow("x") << u"x"_s << u"done"_s;
        QTest::newRow("może") << u"może"_s << QString();
    }

    void markNames()
    {
        QFETCH(QString, word);
        QFETCH(QString, mark);
        QVERIFY(open({{u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n"_s},
                      {u"journals/2026-11-03.md"_s, u"- [x] Medytacja #nawyk\n"_s}}));
        const QVariantMap r = set(u"Medytacja"_s, word);
        if (mark.isEmpty()) {
            QCOMPARE(r.value(u"error"_s).toString(), u"bad_argument"_s);
            QCOMPARE(onDisk(u"journals/2026-11-03.md"_s), "- [x] Medytacja #nawyk\n"_ba);
            return;
        }
        QCOMPARE(r.value(u"mark"_s).toString(), mark);
        QCOMPARE(r.value(u"before"_s).toString(), u"done"_s);
    }

    // ---- the notebook changing --------------------------------------------------------------

    void cache()
    {
        QVERIFY(open({{u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n- Czytanie\n  habit:: pn, śr, pt\n"_s},
                      {u"journals/2026-11-02.md"_s, u"- [x] Medytacja #nawyk\n- [x] Czytanie #nawyk\n"_s},
                      {u"journals/2026-11-03.md"_s, u"- [ ] Medytacja #nawyk\n"_s},
                      {u"Projekty/Dom.md"_s, u"- [x] Medytacja #nawyk\n- zwykła strona\n"_s}}));
        QVariantMap r = list(u"2026-11-03"_s, 2);
        QCOMPARE(r.value(u"habits"_s).toList().size(), 2);
        QCOMPARE(statesText(habitIn(r, u"Medytacja"_s).value(u"days"_s).toList()), u"x!"_s);
        // Nothing changed: the same answer (from what was kept).
        QCOMPARE(list(u"2026-11-03"_s, 2), r);
        QCOMPARE(render(), render());
        // Ticking today's check in the editor: it shows.
        edit(u"journals/2026-11-03.md"_s, u"- [x] Medytacja #nawyk ✅ 2026-11-03\n"_s);
        r = list(u"2026-11-03"_s, 2);
        QCOMPARE(statesText(habitIn(r, u"Medytacja"_s).value(u"days"_s).toList()), u"xx"_s);
        // Two daily notes of one day: done wins over skipped.
        edit(u"journals/2026_11_02.md"_s, u"- [-] Czytanie #habit\n"_s);
        QCOMPARE(statesText(habitIn(list(u"2026-11-03"_s, 2), u"Czytanie"_s).value(u"days"_s).toList()), u"x_"_s);
        edit(u"journals/2026-11-02.md"_s, u"- [x] Medytacja #nawyk\n"_s);
        QCOMPARE(statesText(habitIn(list(u"2026-11-03"_s, 2), u"Czytanie"_s).value(u"days"_s).toList()), u"-_"_s);
        // A schedule changed.
        edit(u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n- Czytanie\n  habit:: 3 razy w tygodniu\n"_s);
        QCOMPARE(habitIn(list(u"2026-11-03"_s), u"Czytanie"_s).value(u"scheduleText"_s).toString(), u"3 razy w tygodniu"_s);
        // A journal removed: its checks go.
        m_index->remove(u"journals/2026-11-03.md"_s);
        QCOMPARE(statesText(habitIn(list(u"2026-11-03"_s, 2), u"Medytacja"_s).value(u"days"_s).toList()), u"x!"_s);
    }

    void duplicatesAndUndefined()
    {
        QVERIFY(open({{u"A.md"_s, u"- Joga\n  habit:: codziennie\n"_s},
                      {u"B.md"_s, u"- joga\n  habit:: pn, pt\n"_s},
                      {u"journals/2026-11-03.md"_s, u"- [x] Spacer #nawyk\n- [x] JOGA #nawyk\n"_s}}));
        QVariantMap r = list(u"2026-11-03"_s);
        QVariantList habits = r.value(u"habits"_s).toList();
        QCOMPARE(habits.size(), 2);
        QCOMPARE(habits[0].toMap().value(u"path"_s).toString(), u"A.md"_s);
        QVERIFY(habits[0].toMap().value(u"valid"_s).toBool());
        QCOMPARE(habits[1].toMap().value(u"problem"_s).toString(), u"duplicate"_s);
        QVERIFY(!habits[1].toMap().value(u"valid"_s).toBool());
        QCOMPARE(habits[0].toMap().value(u"done"_s).toInt(), 1);
        QCOMPARE(r.value(u"undefined"_s).toStringList(), QStringList{u"spacer"_s});
        // `set` names the first; the duplicate is not offered.
        QCOMPARE(set(u"Pływanie"_s, u"done"_s).value(u"habits"_s).toStringList(), QStringList{u"Joga"_s});
        // The month view shows the duplicate as a problem.
        const QVariantMap dup = cards(render()).value(1);
        QCOMPARE(children(dup).value(1).toMap().value(u"text"_s).toString(), u"Zapisany dwa razy — ta definicja nie jest używana"_s);
        QCOMPARE(children(dup).value(1).toMap().value(u"tone"_s).toString(), u"danger"_s);
        // The first one goes: the other takes over.
        m_index->remove(u"A.md"_s);
        habits = list(u"2026-11-03"_s).value(u"habits"_s).toList();
        QCOMPARE(habits.size(), 1);
        QVERIFY(habits[0].toMap().value(u"valid"_s).toBool());
        QCOMPARE(habits[0].toMap().value(u"path"_s).toString(), u"B.md"_s);
    }

    // ---- writing a check (notebook/habitchecks.h before) ------------------------------------

    void edit_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("mark");
        QTest::addColumn<QString>("after");
        QTest::addColumn<bool>("created");

        QTest::newRow("empty note") << QString() << u"Medytacja"_s << u"done"_s
                                    << u"- [x] Medytacja #nawyk ✅ 2026-11-03\n"_s << true;
        QTest::newRow("template's empty bullet filled")
                << u"# Dziennik\n\n- \n"_s << u"Medytacja"_s << u"done"_s
                << u"# Dziennik\n\n- [x] Medytacja #nawyk ✅ 2026-11-03\n"_s << true;
        QTest::newRow("appended after other notes")
                << u"- Rano kawa\n- Spotkanie z Łucją\n"_s << u"Ćwiczenia"_s << u"done"_s
                << u"- Rano kawa\n- Spotkanie z Łucją\n- [x] Ćwiczenia #nawyk ✅ 2026-11-03\n"_s << true;
        QTest::newRow("next to the other checks, CRLF kept")
                << u"- [x] Joga #nawyk\r\n- Notatka\r\n"_s << u"Medytacja"_s << u"done"_s
                << u"- [x] Joga #nawyk\r\n- [x] Medytacja #nawyk ✅ 2026-11-03\r\n- Notatka\r\n"_s << true;
        QTest::newRow("indented like the other checks")
                << u"- Nawyki\n  - [x] Joga #nawyk\n    - z Anią\n- Inne\n"_s << u"Spacer"_s << u"done"_s
                << u"- Nawyki\n  - [x] Joga #nawyk\n    - z Anią\n  - [x] Spacer #nawyk ✅ 2026-11-03\n- Inne\n"_s << true;
        QTest::newRow("last line without a line ending")
                << u"- [x] Joga #nawyk"_s << u"Medytacja"_s << u"done"_s
                << u"- [x] Joga #nawyk\n- [x] Medytacja #nawyk ✅ 2026-11-03"_s << true;
        QTest::newRow("BOM kept") << u"\uFEFF- notatka\n"_s << u"Medytacja"_s << u"done"_s
                                  << u"\uFEFF- notatka\n- [x] Medytacja #nawyk ✅ 2026-11-03\n"_s << true;
        QTest::newRow("open check ticked, the rest untouched")
                << u"# Wtorek  \n- [ ] Medytacja #nawyk\n- [ ] Zakupy 📅 2026-11-04\n"_s << u"medytacja"_s << u"done"_s
                << u"# Wtorek  \n- [x] Medytacja #nawyk ✅ 2026-11-03\n- [ ] Zakupy 📅 2026-11-04\n"_s << false;
        QTest::newRow("already done: nothing changes")
                << u"- [x] Medytacja #nawyk\n"_s << u"Medytacja"_s << u"done"_s << u"- [x] Medytacja #nawyk\n"_s << false;
        QTest::newRow("done opened again") << u"- [x] Medytacja #nawyk ✅ 2026-11-03\n- dalej\n"_s << u"Medytacja"_s
                                           << u"open"_s << u"- [ ] Medytacja #nawyk\n- dalej\n"_s << false;
        QTest::newRow("every done check opened again")
                << u"- [x] Medytacja #nawyk\n- [-] medytacja #habit\n"_s << u"Medytacja"_s << u"open"_s
                << u"- [ ] Medytacja #nawyk\n- [ ] medytacja #habit\n"_s << false;
        QTest::newRow("open without a check: nothing written")
                << u"- notatka\n"_s << u"Medytacja"_s << u"open"_s << u"- notatka\n"_s << false;
        QTest::newRow("skipped") << u"- [ ] Medytacja #nawyk\n"_s << u"Medytacja"_s << u"skipped"_s
                                 << u"- [-] Medytacja #nawyk ❌ 2026-11-03\n"_s << false;
        QTest::newRow("skipped, new line")
                << QString() << u"Bieganie"_s << u"skipped"_s << u"- [-] Bieganie #nawyk ❌ 2026-11-03\n"_s << true;
        QTest::newRow("done over skipped") << u"- [-] Medytacja #nawyk ❌ 2026-11-02\n"_s << u"Medytacja"_s
                                           << u"done"_s << u"- [x] Medytacja #nawyk ✅ 2026-11-03\n"_s << false;
        QTest::newRow("a check of another habit is not touched")
                << u"- [ ] Joga #nawyk\n"_s << u"Medytacja"_s << u"done"_s
                << u"- [ ] Joga #nawyk\n- [x] Medytacja #nawyk ✅ 2026-11-03\n"_s << true;
        QTest::newRow("a plain task of that name is no check")
                << u"- [ ] Medytacja\n"_s << u"Medytacja"_s << u"done"_s
                << u"- [ ] Medytacja\n- [x] Medytacja #nawyk ✅ 2026-11-03\n"_s << true;
        QTest::newRow("name with diacritics and spaces") << QString() << u"  Zażółć   gęślą jaźń "_s << u"done"_s
                                                         << u"- [x] Zażółć gęślą jaźń #nawyk ✅ 2026-11-03\n"_s << true;
    }

    void edit()
    {
        QFETCH(QString, before);
        QFETCH(QString, name);
        QFETCH(QString, mark);
        QFETCH(QString, after);
        QFETCH(bool, created);
        const QString path = u"journals/2026-11-03.md"_s;
        const QString clean = name.simplified();
        QVERIFY(open({{u"Nawyki.md"_s, u"- %1\n  habit:: codziennie\n"_s.arg(clean)}, {path, before}}));
        const Result r = act({{u"name"_s, name}, {u"page"_s, false}, {u"date"_s, u"2026-11-03"_s}, {u"state"_s, mark}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(onDisk(path), after.toUtf8());
        // Nothing changed: no message; else what was done, with its undo.
        if (after == before) {
            QVERIFY(r.value.isNull());
        } else {
            const QVariantMap result = r.value.toMap();
            QVERIFY(!result.value(u"message"_s).toString().isEmpty());
            QCOMPARE(result.value(u"button"_s).toMap().value(u"action"_s).toString(), u"set"_s);
        }
        QCOMPARE(after.count(u"#nawyk"_s) + after.count(u"#habit"_s) > before.count(u"#nawyk"_s) + before.count(u"#habit"_s),
                 created);
        // The day reads back with that mark.
        const QString read = habitIn(list(u"2026-11-03"_s), clean).value(u"today"_s).toMap().value(u"mark"_s).toString();
        QCOMPARE(read.isEmpty() ? u"open"_s : read, mark);
    }

    void lineAndBefore()
    {
        QVERIFY(open({{u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n- Joga\n  habit:: codziennie\n"_s},
                      {u"journals/2026-11-03.md"_s, u"- notatka\n- [-] Medytacja #nawyk\n"_s},
                      {u"journals/2026-11-02.md"_s, u"- notatka\n"_s}}));
        QVariantMap r = set(u"Medytacja"_s, u"done"_s);
        QCOMPARE(r.value(u"line"_s).toInt(), 1);
        QCOMPARE(r.value(u"before"_s).toString(), u"skipped"_s);
        QCOMPARE(r.value(u"changed"_s).toBool(), true);
        r = set(u"Joga"_s, u"done"_s);
        QCOMPARE(r.value(u"line"_s).toInt(), 2); // after the check of Medytacja
        QCOMPARE(r.value(u"before"_s).toString(), QString());
        QCOMPARE(r.value(u"created"_s).toBool(), true);
        r = set(u"Joga"_s, u"done"_s, u"2026-11-02"_s);
        QCOMPARE(r.value(u"line"_s).toInt(), 1); // the note's last block
        QCOMPARE(onDisk(u"journals/2026-11-02.md"_s), "- notatka\n- [x] Joga #nawyk ✅ 2026-11-03\n"_ba);
        // A missing daily note is made (from the journal template when there is one).
        r = set(u"Joga"_s, u"skipped"_s, u"2026-11-01"_s);
        QCOMPARE(r.value(u"changed"_s).toBool(), true);
        QVERIFY(onDisk(u"journals/2026-11-01.md"_s).contains("- [-] Joga #nawyk ❌ 2026-11-03"));
        // Open on a day without a note writes nothing.
        r = set(u"Joga"_s, u"open"_s, u"2026-10-01"_s);
        QCOMPARE(r.value(u"changed"_s).toBool(), false);
        QCOMPARE(onDisk(u"journals/2026-10-01.md"_s), "<missing>"_ba);
    }

    void logseqKeywords()
    {
        const QString path = u"journals/2026-11-03.md"_s;
        QVERIFY(open({{u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n- Joga\n  habit:: codziennie\n"_s},
                      {path, u"- TODO Medytacja #nawyk\n- DONE Joga #nawyk\n"_s}}));
        QVERIFY(set(u"Medytacja"_s, u"done"_s).value(u"changed"_s).toBool());
        QVERIFY(onDisk(path).startsWith("- DONE Medytacja #nawyk"));
        QVERIFY(onDisk(path).endsWith("\n- DONE Joga #nawyk\n"));
        edit(path, u"- TODO Medytacja #nawyk\n- DONE Joga #nawyk\n"_s);
        QVERIFY(set(u"Joga"_s, u"open"_s).value(u"changed"_s).toBool());
        QCOMPARE(onDisk(path), "- TODO Medytacja #nawyk\n- TODO Joga #nawyk\n"_ba);
    }

    void tagAndPageLink()
    {
        const QString path = u"journals/2026-11-03.md"_s;
        Options english;
        english.language = u"en"_s;
        QVERIFY(open({{u"Habits.md"_s, u"- Reading\n  habit:: every day\n"_s}, {path, QString()}}, english));
        QVERIFY(set(u"reading"_s, u"done"_s).value(u"changed"_s).toBool());
        QCOMPARE(onDisk(path), "- [x] Reading #habit ✅ 2026-11-03\n"_ba);
        // A page habit: its check links to the page; written either way, it is one habit.
        QVERIFY(open({{u"Bieganie.md"_s, u"---\nhabit: codziennie\n---\n# Bieganie\n"_s}, {path, QString()}}));
        QVariantMap r = set(u"Bieganie"_s, u"done"_s);
        QCOMPARE(r.value(u"isPage"_s).toBool(), true);
        QCOMPARE(onDisk(path), "- [x] [[Bieganie]] #nawyk ✅ 2026-11-03\n"_ba);
        QCOMPARE(set(u"Bieganie"_s, u"done"_s).value(u"changed"_s).toBool(), false);
        QVERIFY(act({{u"name"_s, u"Bieganie"_s}, {u"page"_s, false}, {u"date"_s, u"2026-11-03"_s}, {u"state"_s, u"done"_s}})
                        .value.isNull());
        QCOMPARE(onDisk(path), "- [x] [[Bieganie]] #nawyk ✅ 2026-11-03\n"_ba);
    }

    void refused_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("date");
        QTest::newRow("empty") << QString() << u"2026-11-03"_s;
        QTest::newRow("only a tag") << u"#nawyk"_s << u"2026-11-03"_s;
        QTest::newRow("invalid date") << u"Medytacja"_s << u"2026-02-30"_s;
        QTest::newRow("no date") << u"Medytacja"_s << u"wczoraj"_s;
        // Reads back as another name: the link's label.
        QTest::newRow("a link with an alias") << u"[[Bieganie|Bieg]] rano"_s << u"2026-11-03"_s;
    }

    void refused()
    {
        QFETCH(QString, name);
        QFETCH(QString, date);
        const QString path = u"journals/2026-11-03.md"_s;
        QVERIFY(open({{path, u"- notatka\r\n"_s}}));
        const Result r = act({{u"name"_s, name}, {u"page"_s, false}, {u"date"_s, date}, {u"state"_s, u"done"_s}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        if (QTest::currentDataTag() == QByteArrayLiteral("a link with an alias")) {
            // b.plain("[[Bieganie|Bieg]] rano") is "Bieg rano": the written line reads back so.
            QCOMPARE(onDisk(path), "- notatka\r\n- [x] [[Bieganie|Bieg]] rano #nawyk ✅ 2026-11-03\r\n"_ba);
            return;
        }
        QCOMPARE(r.value.toMap().value(u"tone"_s).toString(), u"error"_s);
        QCOMPARE(r.value.toMap().value(u"message"_s).toString(), u"Nie udało się zapisać dziennika."_s);
        QCOMPARE(onDisk(path), "- notatka\r\n"_ba);
    }

    // ---- the view, its action, the commands -----------------------------------------------------

    void monthView()
    {
        // Tuesday 3 Nov 2026.
        QVERIFY(open({{u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n  start:: 2026-11-01\n"
                                       "- Czytanie\n  habit:: 3 razy w tygodniu\n  start:: 2026-11-02\n"
                                       "- Coś\n  habit:: kiedy mi się zachce\n"_s},
                      {u"journals/2026-11-01.md"_s, u"- [x] Medytacja #nawyk\n"_s},
                      {u"journals/2026-11-02.md"_s, u"- [-] Medytacja #nawyk\n- [x] Czytanie #nawyk\n"_s},
                      {u"journals/2026-11-03.md"_s, u"- [x] Czytanie #nawyk\n"_s}}));
        const QVariantMap view = render();
        QCOMPARE(view.value(u"type"_s).toString(), u"column"_s);
        const QVariantList nodes = children(view);
        const QVariantMap summary = nodes.value(0).toMap();
        QCOMPARE(summary.value(u"type"_s).toString(), u"text"_s);
        QCOMPARE(summary.value(u"muted"_s).toBool(), true);
        QCOMPARE(summary.value(u"text"_s).toString(), u"1 z 2 zrobione dziś · 1 z problemem"_s);
        QCOMPARE(cards(view).size(), 3);

        // A habit's card: today's check, its name, its schedule, its menu; streaks; the month.
        const QVariantMap med = cardOf(view, u"Medytacja"_s);
        const QVariantMap row = children(med).value(0).toMap();
        QCOMPARE(row.value(u"type"_s).toString(), u"row"_s);
        const QVariantList controls = children(row);
        const QVariantMap check = controls.value(0).toMap();
        QCOMPARE(check.value(u"type"_s).toString(), u"check"_s);
        QCOMPARE(check.value(u"state"_s).toString(), u"open"_s);
        QCOMPARE(check.value(u"label"_s).toString(), u"Medytacja"_s);
        QCOMPARE(check.value(u"action"_s).toString(), u"set"_s);
        const QVariantMap args = check.value(u"args"_s).toMap();
        QCOMPARE(args.value(u"name"_s).toString(), u"Medytacja"_s);
        QCOMPARE(args.value(u"date"_s).toString(), u"2026-11-03"_s);
        QCOMPARE(args.value(u"state"_s).toString(), u"done"_s);
        QCOMPARE(args.value(u"page"_s).toBool(), false);
        QCOMPARE(check.value(u"menu"_s).toList().size(), 5);
        QCOMPARE(controls.value(1).toMap().value(u"text"_s).toString(), u"Medytacja"_s);
        QCOMPARE(controls.value(2).toMap().value(u"text"_s).toString(), u"codziennie"_s);
        QCOMPARE(controls.value(2).toMap().value(u"muted"_s).toBool(), true);
        const QVariantMap menu = controls.value(3).toMap();
        QCOMPARE(menu.value(u"type"_s).toString(), u"menu"_s);
        const QVariantList items = menu.value(u"items"_s).toList();
        QCOMPARE(items.value(0).toMap().value(u"label"_s).toString(), u"Zrobione dziś"_s);
        QCOMPARE(items.value(1).toMap().value(u"args"_s).toMap().value(u"state"_s).toString(), u"skipped"_s);
        QCOMPARE(items.value(2).toMap().value(u"args"_s).toMap().value(u"state"_s).toString(), u"open"_s);
        QCOMPARE(items.value(3).toMap().value(u"page"_s).toString(), u"Nawyki.md"_s);
        QCOMPARE(items.value(3).toMap().value(u"line"_s).toInt(), 1);
        QCOMPARE(items.value(4).toMap().value(u"journal"_s).toString(), u"2026-11-03"_s);
        const QVariantMap streak = children(med).value(1).toMap();
        QCOMPARE(streak.value(u"text"_s).toString(), u"Seria: 1 dzień · najdłuższa: 1 dzień · wykonano: 100%"_s);
        QCOMPARE(streak.value(u"icon"_s).toString(), u"flame"_s);
        const QVariantMap month = monthOf(med);
        QVERIFY(!month.contains(u"label"_s)); // the view's header names the month
        const QVariantMap days = month.value(u"days"_s).toMap();
        QCOMPARE(days.size(), 30);
        QCOMPARE(days.value(u"2026-11-01"_s).toMap().value(u"state"_s).toString(), u"done"_s);
        QCOMPARE(days.value(u"2026-11-01"_s).toMap().value(u"args"_s).toMap().value(u"state"_s).toString(), u"open"_s);
        QCOMPARE(days.value(u"2026-11-02"_s).toMap().value(u"state"_s).toString(), u"skipped"_s);
        const QVariantMap today = days.value(u"2026-11-03"_s).toMap();
        QCOMPARE(today.value(u"state"_s).toString(), u"due"_s);
        QCOMPARE(today.value(u"args"_s).toMap().value(u"state"_s).toString(), u"done"_s);
        const QVariantList dayMenu = today.value(u"menu"_s).toList();
        QCOMPARE(dayMenu.value(0).toMap().value(u"label"_s).toString(), u"Zrobione"_s);
        QCOMPARE(dayMenu.value(1).toMap().value(u"label"_s).toString(), u"Pominięte (seria trwa)"_s);
        QCOMPARE(dayMenu.value(3).toMap().value(u"journal"_s).toString(), u"2026-11-03"_s);
        QCOMPARE(days.value(u"2026-11-04"_s).toMap(), (QVariantMap{{u"state"_s, u"future"_s}}));

        // A times habit: its period's progress; today done.
        const QVariantMap read = cardOf(view, u"Czytanie"_s);
        QCOMPARE(children(children(read).value(0).toMap()).value(0).toMap().value(u"state"_s).toString(), u"done"_s);
        QCOMPARE(children(children(read).value(0).toMap()).value(0).toMap().value(u"checked"_s).toBool(), true);
        QVERIFY(children(read).value(1).toMap().value(u"text"_s).toString().endsWith(u" · 2 z 3 w tym tygodniu"_s));
        QCOMPARE(monthOf(read).value(u"days"_s).toMap().value(u"2026-11-01"_s).toMap().value(u"state"_s).toString(),
                 u"outside"_s);

        // A problem: the name, the reason, a link to the definition; no statistics.
        const QVariantMap odd = cardOf(view, u"Coś"_s);
        QCOMPARE(children(odd).size(), 3);
        QCOMPARE(children(odd).value(1).toMap().value(u"text"_s).toString(),
                 u"Niezrozumiały harmonogram: „kiedy mi się zachce”"_s);
        const QVariantMap link = children(odd).value(2).toMap();
        QCOMPARE(link.value(u"type"_s).toString(), u"link"_s);
        QCOMPARE(link.value(u"page"_s).toString(), u"Nawyki.md"_s);
        QCOMPARE(link.value(u"line"_s).toInt(), 7);

        // Another month's grid (the arrows are the view's header, not nodes).
        QCOMPARE(nodes.last().toMap().value(u"type"_s).toString(), u"card"_s);
        const QVariantMap december = render({{u"month"_s, u"2026-12"_s}});
        QCOMPARE(monthOf(cardOf(december, u"Medytacja"_s)).value(u"days"_s).toMap().size(), 31);
        QCOMPARE(monthOf(cardOf(render({{u"month"_s, u"2026-13"_s}}), u"Medytacja"_s)).value(u"month"_s).toString(),
                 u"2026-11"_s);
    }

    void monthViewInEnglish()
    {
        Options o;
        o.language = u"en"_s;
        o.uiLanguage = u"en"_s;
        QVERIFY(open({{u"Habits.md"_s, u"- Reading\n  habit:: mon, wed, fri\n  start:: 2026-10-26\n- Yoga\n  habit:: twice a week\n"_s},
                      {u"journals/2026-10-28.md"_s, u"- [x] Reading #habit\n"_s},
                      {u"journals/2026-10-30.md"_s, u"- [x] Reading #habit\n"_s},
                      {u"journals/2026-11-02.md"_s, u"- [x] Reading #habit\n"_s}}, o));
        const QVariantMap view = render();
        // Tuesday: Reading is not due, Yoga's week still needs two days.
        QCOMPARE(children(view).value(0).toMap().value(u"text"_s).toString(), u"0 of 1 done today"_s);
        const QVariantMap reading = cardOf(view, u"Reading"_s);
        QCOMPARE(children(children(reading).value(0).toMap()).value(2).toMap().value(u"text"_s).toString(), u"Mon, Wed, Fri"_s);
        QCOMPARE(children(reading).value(1).toMap().value(u"text"_s).toString(),
                 u"Streak: 3 times · longest: 3 times · completed: 75%"_s);
        QVERIFY(!monthOf(reading).contains(u"label"_s));
        QCOMPARE(children(cardOf(view, u"Yoga"_s)).value(1).toMap().value(u"text"_s).toString(),
                 u"Streak: 0 weeks · longest: 0 weeks · 0 of 2 this week"_s);
        // The action's message and its undo, in English.
        Result r = act({{u"name"_s, u"Reading"_s}, {u"page"_s, false}, {u"date"_s, u"2026-11-03"_s}, {u"state"_s, u"done"_s}});
        QCOMPARE(r.value.toMap().value(u"message"_s).toString(), u"Done for today: Reading"_s);
        QCOMPARE(r.value.toMap().value(u"tone"_s).toString(), u"success"_s);
        QCOMPARE(r.value.toMap().value(u"button"_s).toMap().value(u"label"_s).toString(), u"Undo"_s);
        QCOMPARE(onDisk(u"journals/2026-11-03.md"_s).contains("- [x] Reading #habit ✅ 2026-11-03"), true);
        QCOMPARE(children(render()).value(0).toMap().value(u"text"_s).toString(), u"1 of 2 done today"_s);
    }

    void setActionAndUndo()
    {
        const QString path = u"journals/2026-11-02.md"_s;
        QVERIFY(open({{u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n"_s}, {path, u"- [-] Medytacja #nawyk ❌ 2026-11-02\n"_s}}));
        // A past day's cell: done ("Marked done"), the undo brings the skipped day back.
        const QVariantMap cell =
                monthOf(cardOf(render(), u"Medytacja"_s)).value(u"days"_s).toMap().value(u"2026-11-02"_s).toMap();
        QCOMPARE(cell.value(u"args"_s).toMap().value(u"state"_s).toString(), u"open"_s);
        Result r = act(cell.value(u"menu"_s).toList().value(0).toMap().value(u"args"_s).toMap());
        QVariantMap result = r.value.toMap();
        QCOMPARE(result.value(u"message"_s).toString(), u"Odhaczono: Medytacja"_s);
        QCOMPARE(result.value(u"tone"_s).toString(), u"success"_s);
        QCOMPARE(onDisk(path), "- [x] Medytacja #nawyk ✅ 2026-11-03\n"_ba);
        const QVariantMap button = result.value(u"button"_s).toMap();
        QCOMPARE(button.value(u"label"_s).toString(), u"Cofnij"_s);
        QCOMPARE(button.value(u"args"_s).toMap().value(u"state"_s).toString(), u"skipped"_s);
        r = act(button.value(u"args"_s).toMap());
        QCOMPARE(r.value.toMap().value(u"message"_s).toString(), u"Pominięto dzień: Medytacja"_s);
        QCOMPARE(r.value.toMap().value(u"tone"_s).toString(), u"info"_s);
        QCOMPARE(onDisk(path), "- [-] Medytacja #nawyk ❌ 2026-11-03\n"_ba);
        // Today: a new check, its undo opens it again ("Check removed").
        r = act({{u"name"_s, u"Medytacja"_s}, {u"page"_s, false}, {u"date"_s, u"2026-11-03"_s}, {u"state"_s, u"done"_s}});
        QCOMPARE(r.value.toMap().value(u"message"_s).toString(), u"Zrobione na dziś: Medytacja"_s);
        QCOMPARE(r.value.toMap().value(u"button"_s).toMap().value(u"args"_s).toMap().value(u"state"_s).toString(), u"open"_s);
        r = act(r.value.toMap().value(u"button"_s).toMap().value(u"args"_s).toMap());
        QCOMPARE(r.value.toMap().value(u"message"_s).toString(), u"Cofnięto odhaczenie: Medytacja"_s);
        QCOMPARE(onDisk(u"journals/2026-11-03.md"_s), "- [ ] Medytacja #nawyk\n"_ba);
        // An action the view does not have.
        QCOMPARE(act(u"nothing"_s, {}).status, Status::NotFound);
    }

    void emptyState()
    {
        QVERIFY(open({{u"Strona.md"_s, u"- zwykła strona\n"_s}}));
        QVariantMap view = render();
        QVariantMap empty = children(view).value(0).toMap();
        QCOMPARE(empty.value(u"type"_s).toString(), u"empty"_s);
        QCOMPARE(empty.value(u"title"_s).toString(), u"Nie ma jeszcze nawyków"_s);
        QCOMPARE(empty.value(u"icon"_s).toString(), u"flame"_s);
        QCOMPARE(children(view).value(1).toMap().value(u"command"_s).toString(), u"add"_s);
        Options o;
        o.uiLanguage = u"en"_s;
        QVERIFY(open({}, o));
        empty = children(render()).value(0).toMap();
        QCOMPARE(empty.value(u"title"_s).toString(), u"No habits yet"_s);
        QVERIFY(empty.value(u"text"_s).toString().contains(u"habit:: every day"_s));
        QCOMPARE(list().value(u"habits"_s).toList().size(), 0);
    }

    void addHabit()
    {
        QVERIFY(open({{u"Strona.md"_s, u"- zwykła strona\n"_s}}));
        Result r = run(u"add"_s);
        QCOMPARE(r.status, Status::Suspended);
        QVariantMap question = r.value.toMap();
        QCOMPARE(question.value(u"kind"_s).toString(), u"form"_s);
        QCOMPARE(question.value(u"title"_s).toString(), u"Nowy nawyk"_s);
        const QVariantList fields = question.value(u"fields"_s).toList();
        QCOMPARE(fields.size(), 2);
        QCOMPARE(fields[0].toMap().value(u"kind"_s).toString(), u"text"_s);
        QCOMPARE(fields[0].toMap().value(u"label"_s).toString(), u"Nazwa"_s);
        QCOMPARE(fields[1].toMap().value(u"placeholder"_s).toString(), u"codziennie, pn śr pt, 3 razy w tygodniu…"_s);
        // A schedule not understood: asked again, saying why, with what was typed.
        r = m_engine->resume(QVariantMap{{u"name"_s, u" Medytacja "_s}, {u"schedule"_s, u"kiedy mi się zachce"_s}});
        QCOMPARE(r.status, Status::Suspended);
        question = r.value.toMap();
        QVERIFY(question.value(u"title"_s).toString().startsWith(u"Nowy nawyk\nNie rozumiem"_s));
        QCOMPARE(question.value(u"fields"_s).toList()[1].toMap().value(u"value"_s).toString(), u"kiedy mi się zachce"_s);
        // No name: asked again.
        r = m_engine->resume(QVariantMap{{u"name"_s, u"#tag"_s}, {u"schedule"_s, QString()}});
        QCOMPARE(r.status, Status::Suspended);
        QVERIFY(r.value.toMap().value(u"title"_s).toString().endsWith(u"Podaj nazwę nawyku"_s));
        r = m_engine->resume(QVariantMap{{u"name"_s, u" Medytacja "_s}, {u"schedule"_s, u"pn, śr, pt"_s}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVariantMap result = r.value.toMap();
        QCOMPARE(result.value(u"message"_s).toString(), u"Dodano nawyk na stronie „Nawyki”"_s);
        QCOMPARE(result.value(u"tone"_s).toString(), u"success"_s);
        QCOMPARE(result.value(u"button"_s).toMap().value(u"label"_s).toString(), u"Otwórz"_s);
        QCOMPARE(result.value(u"button"_s).toMap().value(u"page"_s).toString(), u"Nawyki"_s);
        QVERIFY(QString::fromUtf8(onDisk(u"Nawyki.md"_s)).contains(u"- Medytacja\n  habit:: pn, śr, pt"_s));
        // The next goes to the page of the first habit; an empty schedule is every day; a name in
        // use is asked again; cancelled: nothing written.
        r = run(u"add"_s);
        r = m_engine->resume(QVariantMap{{u"name"_s, u"MEDYTACJA"_s}, {u"schedule"_s, QString()}});
        QCOMPARE(r.status, Status::Suspended);
        QVERIFY(r.value.toMap().value(u"title"_s).toString().endsWith(u"Nawyk o tej nazwie już istnieje"_s));
        r = m_engine->resume(QVariantMap{{u"name"_s, u"Joga"_s}, {u"schedule"_s, QString()}});
        QVERIFY(r.ok());
        QVERIFY(QString::fromUtf8(onDisk(u"Nawyki.md"_s)).endsWith(u"- Joga\n  habit:: codziennie\n"_s));
        const QByteArray before = onDisk(u"Nawyki.md"_s);
        r = run(u"add"_s);
        r = m_engine->resume(QVariant());
        QVERIFY(r.ok());
        QVERIFY(r.value.isNull());
        QCOMPARE(onDisk(u"Nawyki.md"_s), before);
        // Both are habits now.
        QCOMPARE(list().value(u"habits"_s).toList().size(), 2);

        // English notes: "Habits" and "every day"; a page whose habits are blocks gets the next.
        Options o;
        o.language = u"en"_s;
        o.uiLanguage = u"en"_s;
        QVERIFY(open({{u"Health/Routine.md"_s, u"- Yoga\n  habit:: every day\n"_s}}, o));
        r = run(u"add"_s);
        QCOMPARE(r.value.toMap().value(u"title"_s).toString(), u"New habit"_s);
        r = m_engine->resume(QVariantMap{{u"name"_s, u"Reading"_s}, {u"schedule"_s, QString()}});
        QVERIFY(r.ok());
        QCOMPARE(r.value.toMap().value(u"message"_s).toString(), u"Habit added to “Routine”"_s);
        QCOMPARE(onDisk(u"Health/Routine.md"_s), "- Yoga\n  habit:: every day\n- Reading\n  habit:: every day\n"_ba);
        QVERIFY(open({}, o));
        r = run(u"add"_s);
        r = m_engine->resume(QVariantMap{{u"name"_s, u"Yoga"_s}, {u"schedule"_s, QString()}});
        QVERIFY(r.ok());
        QVERIFY(QString::fromUtf8(onDisk(u"Habits.md"_s)).contains(u"- Yoga\n  habit:: every day"_s));

        // The habits' own code as a page ("Edit the code": Rozszerzenia/Nawyki.md) is never where
        // habits go: a page of its own.
        const QByteArray code = "---\ntags: [rozszerzenie]\nzastępuje: nawyki\n---\n```luau\n-- kod\n```\n";
        QVERIFY(open({{u"Rozszerzenia/Nawyki.md"_s, QString::fromUtf8(code)}}));
        r = run(u"add"_s);
        r = m_engine->resume(QVariantMap{{u"name"_s, u"Medytacja"_s}, {u"schedule"_s, QString()}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(onDisk(u"Rozszerzenia/Nawyki.md"_s), code);
        QVERIFY(QString::fromUtf8(onDisk(u"Lista nawyków.md"_s)).contains(u"- Medytacja\n  habit:: codziennie"_s));
    }

    void tick()
    {
        QVERIFY(open({{u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n- Joga\n  habit:: codziennie\n- Coś\n  habit:: bla\n"_s},
                      {u"journals/2026-11-03.md"_s, u"- [x] Medytacja #nawyk\n"_s}}));
        Result r = run(u"tick"_s);
        QCOMPARE(r.status, Status::Suspended);
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
        QCOMPARE(r.value.toMap().value(u"options"_s).toStringList(), QStringList{u"Joga"_s});
        r = m_engine->resume(u"Joga"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toMap().value(u"message"_s).toString(), u"Zrobione na dziś: Joga"_s);
        QCOMPARE(onDisk(u"journals/2026-11-03.md"_s), "- [x] Medytacja #nawyk\n- [x] Joga #nawyk ✅ 2026-11-03\n"_ba);
        r = run(u"tick"_s);
        QCOMPARE(r.value.toString(), u"Wszystkie nawyki na dziś zrobione"_s);
        // The palette's labels in the interface's language; list, set, schedule are not offered.
        QStringList labels;
        for (const CommandInfo &c : m_engine->commands())
            if (c.extension == u"nawyki"_s && c.palette)
                labels << c.label;
        labels.sort();
        QCOMPARE(labels, (QStringList{u"Nawyki: dodaj nawyk"_s, u"Nawyki: odhacz nawyk"_s}));
    }

    void bigNotebook()
    {
        // 2000 daily notes of 8 habits and 1000 other pages.
        QHash<QString, QString> files;
        QString defs;
        const QStringList habits{u"Medytacja"_s, u"Czytanie"_s, u"Spacer"_s, u"Joga"_s,
                                 u"Woda"_s,      u"Języki"_s,   u"Sen"_s,    u"Ćwiczenia"_s};
        const QStringList schedules{u"codziennie"_s, u"3 razy w tygodniu"_s, u"w dni robocze"_s, u"pn, śr, pt"_s,
                                    u"co 2 dni"_s,   u"co miesiąc 15."_s,    u"raz w tygodniu"_s, u"5 razy na 2 tygodnie"_s};
        for (int i = 0; i < habits.size(); ++i)
            defs += u"- %1\n  habit:: %2\n"_s.arg(habits[i], schedules[i]);
        files.insert(u"Nawyki.md"_s, defs);
        const QDate first(2021, 5, 13);
        for (int i = 0; i < 2000; ++i) {
            QString text;
            for (int h = 0; h < habits.size(); ++h)
                text += u"- [%1] %2 #nawyk\n"_s.arg((i + h) % 3 ? u"x"_s : u" "_s, habits[h]);
            text += u"- notatka dnia %1\n"_s.arg(i);
            files.insert(u"journals/"_s + first.addDays(i).toString(Qt::ISODate) + u".md"_s, text);
        }
        for (int i = 0; i < 1000; ++i)
            files.insert(u"Strony/Strona %1.md"_s.arg(i), u"- treść %1 #praca\n"_s.arg(i));
        const QDate last = first.addDays(1999);
        m_now = QDateTime(last, QTime(21, 0));
        QVERIFY(open(files));
        QElapsedTimer timer;
        timer.start();
        const QVariantMap view = render();
        const qint64 firstRender = timer.restart();
        QCOMPARE(cards(view).size(), 8);
        const QVariantMap again = render();
        const qint64 cached = timer.restart();
        QCOMPARE(again, view);
        // A tick: written, then drawn again from the notes.
        const Result r = act({{u"name"_s, u"Spacer"_s}, {u"page"_s, false}, {u"date"_s, last.toString(Qt::ISODate)},
                              {u"state"_s, u"done"_s}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        const qint64 action = timer.restart();
        const QVariantMap after = render();
        const qint64 redraw = timer.restart();
        QVERIFY(after != view);
        const QVariantMap listed = list(last.toString(Qt::ISODate), 30);
        const qint64 listing = timer.elapsed();
        QCOMPARE(habitIn(listed, u"Spacer"_s).value(u"done"_s).toInt(), 1334);
        qInfo("2000 daily notes × 8 habits: first render %lld ms, cached %lld ms, action %lld ms, render after it "
              "%lld ms, list %lld ms",
              firstRender, cached, action, redraw, listing);
        QVERIFY2(firstRender < 1500 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(firstRender)));
        QVERIFY2(redraw < 1500 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(redraw)));
        QVERIFY2(cached < 200 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(cached)));
    }
};

QTEST_GUILESS_MAIN(TestExtensionsHabits)
#include "tst_extensions_habits.moc"
