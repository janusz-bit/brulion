// A habit's check in a daily note (notebook/habitchecks.h): a new `- [x] Name #nawyk ✅ today`
// line (next to the other checks, with their indentation, else the note's last block filling a
// template's empty bullet), an existing check ticked, skipped or opened again — and every other
// byte of the note kept (CRLF, a BOM, a last line without a line ending, other lines and tokens);
// Logseq keywords, the English tag, a link to the habit's page, names that cannot be written.
#include "notebook/habitchecks.h"

#include "markdown/parse.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::notebook;
using habits::Mark;

Q_DECLARE_METATYPE(brulion::habits::Mark)

namespace {
const QDate Day(2026, 11, 3);
const QDate Today(2026, 11, 3);
const VaultPath Path = u"journals/2026-11-03.md"_s;
} // namespace

class TestNotebookHabits : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void edit_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("name");
        QTest::addColumn<Mark>("mark");
        QTest::addColumn<QString>("after");
        QTest::addColumn<bool>("created");

        QTest::newRow("empty note") << QString() << u"Medytacja"_s << Mark::Done
                                    << u"- [x] Medytacja #nawyk ✅ 2026-11-03\n"_s << true;
        QTest::newRow("template's empty bullet filled")
                << u"# Dziennik\n\n- \n"_s << u"Medytacja"_s << Mark::Done
                << u"# Dziennik\n\n- [x] Medytacja #nawyk ✅ 2026-11-03\n"_s << true;
        QTest::newRow("appended after other notes")
                << u"- Rano kawa\n- Spotkanie z Łucją\n"_s << u"Ćwiczenia"_s << Mark::Done
                << u"- Rano kawa\n- Spotkanie z Łucją\n- [x] Ćwiczenia #nawyk ✅ 2026-11-03\n"_s << true;
        QTest::newRow("next to the other checks, CRLF kept")
                << u"- [x] Joga #nawyk\r\n- Notatka\r\n"_s << u"Medytacja"_s << Mark::Done
                << u"- [x] Joga #nawyk\r\n- [x] Medytacja #nawyk ✅ 2026-11-03\r\n- Notatka\r\n"_s << true;
        QTest::newRow("indented like the other checks")
                << u"- Nawyki\n  - [x] Joga #nawyk\n    - z Anią\n- Inne\n"_s << u"Spacer"_s << Mark::Done
                << u"- Nawyki\n  - [x] Joga #nawyk\n    - z Anią\n  - [x] Spacer #nawyk ✅ 2026-11-03\n- Inne\n"_s
                << true;
        QTest::newRow("last line without a line ending")
                << u"- [x] Joga #nawyk"_s << u"Medytacja"_s << Mark::Done
                << u"- [x] Joga #nawyk\n- [x] Medytacja #nawyk ✅ 2026-11-03"_s << true;
        QTest::newRow("BOM kept") << u"\uFEFF- notatka\n"_s << u"Medytacja"_s << Mark::Done
                                  << u"\uFEFF- notatka\n- [x] Medytacja #nawyk ✅ 2026-11-03\n"_s << true;
        QTest::newRow("open check ticked, the rest untouched")
                << u"# Wtorek  \n- [ ] Medytacja #nawyk\n- [ ] Zakupy 📅 2026-11-04\n"_s << u"medytacja"_s << Mark::Done
                << u"# Wtorek  \n- [x] Medytacja #nawyk ✅ 2026-11-03\n- [ ] Zakupy 📅 2026-11-04\n"_s << false;
        QTest::newRow("already done: nothing changes") << u"- [x] Medytacja #nawyk\n"_s << u"Medytacja"_s << Mark::Done
                                                       << u"- [x] Medytacja #nawyk\n"_s << false;
        QTest::newRow("done opened again") << u"- [x] Medytacja #nawyk ✅ 2026-11-03\n- dalej\n"_s << u"Medytacja"_s
                                           << Mark::Open << u"- [ ] Medytacja #nawyk\n- dalej\n"_s << false;
        QTest::newRow("every done check opened again")
                << u"- [x] Medytacja #nawyk\n- [-] medytacja #habit\n"_s << u"Medytacja"_s << Mark::Open
                << u"- [ ] Medytacja #nawyk\n- [ ] medytacja #habit\n"_s << false;
        QTest::newRow("open without a check: nothing written")
                << u"- notatka\n"_s << u"Medytacja"_s << Mark::Open << u"- notatka\n"_s << false;
        QTest::newRow("skipped") << u"- [ ] Medytacja #nawyk\n"_s << u"Medytacja"_s << Mark::Skipped
                                 << u"- [-] Medytacja #nawyk ❌ 2026-11-03\n"_s << false;
        QTest::newRow("skipped, new line")
                << QString() << u"Bieganie"_s << Mark::Skipped << u"- [-] Bieganie #nawyk ❌ 2026-11-03\n"_s << true;
        QTest::newRow("done over skipped") << u"- [-] Medytacja #nawyk ❌ 2026-11-02\n"_s << u"Medytacja"_s
                                           << Mark::Done << u"- [x] Medytacja #nawyk ✅ 2026-11-03\n"_s << false;
        QTest::newRow("a check of another habit is not touched")
                << u"- [ ] Joga #nawyk\n"_s << u"Medytacja"_s << Mark::Done
                << u"- [ ] Joga #nawyk\n- [x] Medytacja #nawyk ✅ 2026-11-03\n"_s << true;
        QTest::newRow("a plain task of that name is no check")
                << u"- [ ] Medytacja\n"_s << u"Medytacja"_s << Mark::Done
                << u"- [ ] Medytacja\n- [x] Medytacja #nawyk ✅ 2026-11-03\n"_s << true;
        QTest::newRow("name with diacritics and spaces") << QString() << u"  Zażółć   gęślą jaźń "_s << Mark::Done
                                                         << u"- [x] Zażółć gęślą jaźń #nawyk ✅ 2026-11-03\n"_s << true;
    }

    void edit()
    {
        QFETCH(QString, before);
        QFETCH(QString, name);
        QFETCH(Mark, mark);
        QFETCH(QString, after);
        QFETCH(bool, created);
        const HabitCheckEdit e = setHabitCheck(before, Path, VaultConfig(), Day, name, mark, Today, u"nawyk"_s);
        QCOMPARE(e.content, after);
        QCOMPARE(e.changed, after != before);
        QCOMPARE(e.created, created);
        QVERIFY(!e.refused);
        // The day reads back with that mark.
        ParsedNote note = markdown::parseNote(Path, e.content, VaultConfig());
        Mark read = Mark::Open;
        for (const habits::Check &c : habits::checksOf(note)) {
            if (c.key == habits::habitKey(name.simplified()))
                read = habits::strongerMark(read, c.mark);
        }
        QCOMPARE(read, mark);
    }

    void lineAndBefore()
    {
        const QString text = u"- notatka\n- [-] Medytacja #nawyk\n"_s;
        const HabitCheckEdit e =
                setHabitCheck(text, Path, VaultConfig(), Day, u"Medytacja"_s, Mark::Done, Today, u"nawyk"_s);
        QCOMPARE(e.line, 1);
        QVERIFY(e.hadCheck);
        QCOMPARE(e.before, Mark::Skipped);
        const HabitCheckEdit fresh =
                setHabitCheck(u"- notatka\n"_s, Path, VaultConfig(), Day, u"Joga"_s, Mark::Done, Today, u"nawyk"_s);
        QCOMPARE(fresh.line, 1);
        QVERIFY(!fresh.hadCheck);
        QCOMPARE(fresh.before, Mark::Open);
    }

    void logseqKeywords()
    {
        const QString text = u"- TODO Medytacja #nawyk\n- DONE Joga #nawyk\n"_s;
        const HabitCheckEdit done =
                setHabitCheck(text, Path, VaultConfig(), Day, u"Medytacja"_s, Mark::Done, Today, u"nawyk"_s);
        QVERIFY(done.content.startsWith(u"- DONE Medytacja #nawyk"_s));
        QVERIFY(done.content.endsWith(u"\n- DONE Joga #nawyk\n"_s));
        const HabitCheckEdit open =
                setHabitCheck(text, Path, VaultConfig(), Day, u"Joga"_s, Mark::Open, Today, u"nawyk"_s);
        QCOMPARE(open.content, u"- TODO Medytacja #nawyk\n- TODO Joga #nawyk\n"_s);
    }

    void tagAndPageLink()
    {
        const HabitCheckEdit english =
                setHabitCheck(QString(), Path, VaultConfig(), Day, u"Reading"_s, Mark::Done, Today, u"habit"_s);
        QCOMPARE(english.content, u"- [x] Reading #habit ✅ 2026-11-03\n"_s);
        const HabitCheckEdit page =
                setHabitCheck(QString(), Path, VaultConfig(), Day, u"Bieganie"_s, Mark::Done, Today, u"nawyk"_s, true);
        QCOMPARE(page.content, u"- [x] [[Bieganie]] #nawyk ✅ 2026-11-03\n"_s);
        // The page's check, written either way, is one habit.
        const HabitCheckEdit again =
                setHabitCheck(page.content, Path, VaultConfig(), Day, u"Bieganie"_s, Mark::Done, Today, u"nawyk"_s);
        QVERIFY(!again.changed);
        QCOMPARE(habitCheckLine(u"Joga"_s, u"nawyk"_s), u"- [ ] Joga #nawyk"_s);
    }

    void refused_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QDate>("date");
        QTest::newRow("empty") << QString() << Day;
        QTest::newRow("only a tag") << u"#nawyk"_s << Day;
        QTest::newRow("invalid date") << u"Medytacja"_s << QDate();
        // Reads back as another name: the link's label.
        QTest::newRow("a link with an alias") << u"[[Bieganie|Bieg]] rano"_s << Day;
    }

    void refused()
    {
        QFETCH(QString, name);
        QFETCH(QDate, date);
        const QString text = u"- notatka\r\n"_s;
        const HabitCheckEdit e = setHabitCheck(text, Path, VaultConfig(), date, name, Mark::Done, Today, u"nawyk"_s);
        if (QTest::currentDataTag() == QByteArrayLiteral("a link with an alias")) {
            // habitName("[[Bieganie|Bieg]] rano") is "Bieg rano": the written line reads back so.
            QVERIFY(!e.refused);
            QVERIFY(e.changed);
            return;
        }
        QVERIFY(e.refused);
        QCOMPARE(e.content, text);
        QVERIFY(!e.changed);
    }

    void definitionBlock()
    {
        QCOMPARE(habitDefinitionBlock(u" Medytacja "_s, u"pn, śr, pt"_s, dates::UiLocale::Polish),
                 u"- Medytacja\n  habit:: pn, śr, pt"_s);
        QCOMPARE(habitDefinitionBlock(u"Joga"_s, QString(), dates::UiLocale::Polish),
                 u"- Joga\n  habit:: codziennie"_s);
        QCOMPARE(habitDefinitionBlock(u"Yoga"_s, QString(), dates::UiLocale::English),
                 u"- Yoga\n  habit:: every day"_s);
        QCOMPARE(habitsPageTitle(dates::UiLocale::Polish), u"Nawyki"_s);
        QCOMPARE(habitsPageTitle(dates::UiLocale::English), u"Habits"_s);
        // The block defines the habit.
        const ParsedNote note = markdown::parseNote(
                u"Nawyki.md"_s, habitDefinitionBlock(u"Joga"_s, u"3 razy w tygodniu"_s, dates::UiLocale::Polish));
        const QList<habits::Habit> list = habits::definitionsOf(u"Nawyki.md"_s, note);
        QCOMPARE(list.size(), 1);
        QVERIFY(list[0].isValid());
    }
};

QTEST_GUILESS_MAIN(TestNotebookHabits)
#include "tst_notebook_habits.moc"
