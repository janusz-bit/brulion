// Pure content edits (task status incl. recurrence, block ids, list item ranges), link
// renaming, journal paths and templates. Test cases ported from archive/web-v1 (edit / rename /
// journal / template / index tests).
#include "markdown/edit.h"
#include "markdown/journal.h"
#include "markdown/parse.h"
#include "markdown/rename.h"
#include "markdown/templates.h"
#include "markdown/tokens.h"

#include <QRegularExpression>
#include <QSet>
#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

const QDate Today(2026, 10, 14);

QString status(const QString &src, int line, TaskStatus s, QDate today = Today)
{
    return setTaskStatus(src, line, s, today);
}

QString firstLine(const QString &s)
{
    return s.split(u'\n').first();
}

bool isGeneratedId(const QString &id)
{
    static const QRegularExpression re(u"^[a-z0-9]{6}$"_s);
    return re.match(id).hasMatch();
}

/// The block containing `id` resolves to `line`.
bool resolves(const QString &content, const QString &id, int line)
{
    const ParsedNote n = parseNote(u"a.md"_s, content);
    const auto it = n.blockIds.constFind(id);
    return it != n.blockIds.constEnd() && n.blocks[*it].line == line;
}

VaultConfig config(const QString &format, const QString &folder = u"journals"_s)
{
    VaultConfig c;
    c.journalFileFormat = format;
    c.journalsFolder = folder;
    return c;
}

TemplateVars vars(dates::UiLocale locale = dates::UiLocale::Polish, const QString &title = QString())
{
    // Wednesday, 14 October 2026, 09:05 local time.
    return TemplateVars{QDateTime(QDate(2026, 10, 14), QTime(9, 5)), title, locale};
}

} // namespace

class TestEdit : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // --- setTaskStatus: checkboxes ---------------------------------------------------------------

    void statusCharacters()
    {
        QCOMPARE(status(u"- [ ] a"_s, 0, TaskStatus::Doing), u"- [/] a"_s);
        QCOMPARE(status(u"- [ ] a"_s, 0, TaskStatus::Deferred), u"- [>] a"_s);
        QCOMPARE(status(u"- [/] a"_s, 0, TaskStatus::Todo), u"- [ ] a"_s);
    }

    void completionDate()
    {
        QCOMPARE(status(u"- [ ] a 📅 2026-10-15"_s, 0, TaskStatus::Done), u"- [x] a 📅 2026-10-15 ✅ 2026-10-14"_s);
        QCOMPARE(status(u"- [ ] a ^id1"_s, 0, TaskStatus::Done), u"- [x] a ✅ 2026-10-14 ^id1"_s);
        QCOMPARE(status(u"- [ ] a   "_s, 0, TaskStatus::Done), u"- [x] a ✅ 2026-10-14"_s);
    }

    void leavingDoneAndCancelled()
    {
        QCOMPARE(status(u"- [x] a ✅ 2026-10-13 ^id1"_s, 0, TaskStatus::Todo), u"- [ ] a ^id1"_s);
        QCOMPARE(status(u"- [x] a ✅ 2026-10-13"_s, 0, TaskStatus::Cancelled), u"- [-] a ❌ 2026-10-14"_s);
        QCOMPARE(status(u"- [-] a ❌ 2026-10-01 b"_s, 0, TaskStatus::Done), u"- [x] a b ✅ 2026-10-14"_s);
    }

    void sameStatusUnchanged()
    {
        const QString src = u"- [X] a ✅ 2026-10-01"_s;
        QCOMPARE(status(src, 0, TaskStatus::Done), src);
    }

    void orderedNestedCallouts()
    {
        QCOMPARE(status(u"1. [ ] a"_s, 0, TaskStatus::Done), u"1. [x] a ✅ 2026-10-14"_s);
        QCOMPARE(status(u"- p\n\t- [ ] a"_s, 1, TaskStatus::Done), u"- p\n\t- [x] a ✅ 2026-10-14"_s);
        QCOMPARE(status(u"> [!todo]\n> - [ ] a"_s, 1, TaskStatus::Done), u"> [!todo]\n> - [x] a ✅ 2026-10-14"_s);
    }

    void anyLineTargetsFirstLine()
    {
        QCOMPARE(status(u"- [ ] a\n  details\n  - child"_s, 1, TaskStatus::Done),
                 u"- [x] a ✅ 2026-10-14\n  details\n  - child"_s);
    }

    void inlineCodeUntouched()
    {
        QCOMPARE(status(u"- [x] use `✅ 2026-01-01`"_s, 0, TaskStatus::Todo), u"- [ ] use `✅ 2026-01-01`"_s);
    }

    void logseqKeywords()
    {
        QCOMPARE(status(u"- TODO a"_s, 0, TaskStatus::Doing), u"- DOING a"_s);
        QCOMPARE(status(u"- LATER a"_s, 0, TaskStatus::Doing), u"- NOW a"_s);
        QCOMPARE(status(u"- NOW a"_s, 0, TaskStatus::Todo), u"- LATER a"_s);
        QCOMPARE(status(u"- TODO a"_s, 0, TaskStatus::Cancelled), u"- CANCELED a ❌ 2026-10-14"_s);
        QCOMPARE(status(u"- TODO a"_s, 0, TaskStatus::Deferred), u"- WAITING a"_s);
        QCOMPARE(status(u"- DOING [#A] a"_s, 0, TaskStatus::Done), u"- DONE [#A] a ✅ 2026-10-14"_s);
        QCOMPARE(status(u"- DONE a ✅ 2026-10-01"_s, 0, TaskStatus::Todo), u"- TODO a"_s);
    }

    void nonTaskLines_data()
    {
        QTest::addColumn<QString>("src");
        QTest::addColumn<int>("st");
        QTest::addColumn<QString>("expected");
        QTest::newRow("star item") << u"  * Kup chleb"_s << int(TaskStatus::Todo) << u"  * [ ] Kup chleb"_s;
        QTest::newRow("plain") << u"Kup chleb"_s << int(TaskStatus::Todo) << u"- [ ] Kup chleb"_s;
        QTest::newRow("indented") << u"  wcięty"_s << int(TaskStatus::Doing) << u"  - [/] wcięty"_s;
        QTest::newRow("bare marker") << u"-"_s << int(TaskStatus::Todo) << u"- [ ] "_s;
        QTest::newRow("empty") << QString() << int(TaskStatus::Todo) << u"- [ ] "_s;
        QTest::newRow("done") << u"Zrobione"_s << int(TaskStatus::Done) << u"- [x] Zrobione ✅ 2026-10-14"_s;
        QTest::newRow("quote") << u"> cytat"_s << int(TaskStatus::Todo) << u"> - [ ] cytat"_s;
        // Review regression: a byte order mark stays the first character of the file.
        QTest::newRow("bom plain") << u"\uFEFFKup chleb"_s << int(TaskStatus::Todo) << u"\uFEFF- [ ] Kup chleb"_s;
        QTest::newRow("bom indented") << u"\uFEFF  wcięty"_s << int(TaskStatus::Done)
                                      << u"\uFEFF  - [x] wcięty ✅ 2026-10-14"_s;
        QTest::newRow("bom quote") << u"\uFEFF> cytat"_s << int(TaskStatus::Todo) << u"\uFEFF> - [ ] cytat"_s;
        QTest::newRow("bom item") << u"\uFEFF- punkt"_s << int(TaskStatus::Doing) << u"\uFEFF- [/] punkt"_s;
    }

    void nonTaskLines()
    {
        QFETCH(QString, src);
        QFETCH(int, st);
        QFETCH(QString, expected);
        QCOMPARE(status(src, 0, TaskStatus(st)), expected);
    }

    void untouchableLines()
    {
        const QString code = u"```\n- [ ] x\n```"_s;
        QCOMPARE(status(code, 1, TaskStatus::Done), code);
        const QString fm = u"---\na: 1\n---\n"_s;
        QCOMPARE(status(fm, 1, TaskStatus::Done), fm);
        QCOMPARE(status(u"- [ ] a"_s, 5, TaskStatus::Done), u"- [ ] a"_s);
        QCOMPARE(status(u"- [ ] a"_s, -1, TaskStatus::Done), u"- [ ] a"_s);
        // A line that is only `$$` opens a display formula: "- [ ] $$" would open none and the
        // formula's lines ("+ b", "- c") would become blocks (review regression).
        for (const QString &math :
             {u"$$\nf(x)\n+ b\n$$"_s, u"- $$\n  a\n  - c\n  $$"_s, u"> $$\n> a\n> $$"_s, u"  *   $$  "_s}) {
            QCOMPARE(status(math, 0, TaskStatus::Todo), math);
            QCOMPARE(toggleTask(math, 0, Today), math);
        }
        QCOMPARE(status(u"- $$\n  a\n  $$"_s, 1, TaskStatus::Done), u"- $$\n  a\n  $$"_s); // the item's line
        // Its task marker still changes (written elsewhere, it opens no formula).
        QCOMPARE(status(u"- [ ] $$"_s, 0, TaskStatus::Done), u"- [x] $$ ✅ 2026-10-14"_s);
        QCOMPARE(status(u"Wzór $$x$$"_s, 0, TaskStatus::Todo), u"- [ ] Wzór $$x$$"_s);
    }

    void crlfPreserved()
    {
        QCOMPARE(status(u"head\r\n- [ ] zadanie\r\ntail\r\n"_s, 1, TaskStatus::Done),
                 u"head\r\n- [x] zadanie ✅ 2026-10-14\r\ntail\r\n"_s);
    }

    // --- recurring tasks -------------------------------------------------------------------------

    void nextOccurrenceAbove()
    {
        const QString src =
                u"  - [ ] Zapłać 📅 2026-10-15 ⏳ 2026-10-13 🛫 2026-10-12 🔁 every week ⏰ 2026-10-15 09:00 ➕ 2026-10-01 ^pay\n  - inne"_s;
        QCOMPARE(status(src, 0, TaskStatus::Done), u"  - [ ] Zapłać 📅 2026-10-22 ⏳ 2026-10-20 🛫 2026-10-19 🔁 every "
                                                   u"week ⏰ 2026-10-22 09:00 ➕ 2026-10-14\n"
                                                   "  - [x] Zapłać 📅 2026-10-15 ⏳ 2026-10-13 🛫 2026-10-12 🔁 every "
                                                   "week ⏰ 2026-10-15 09:00 ➕ 2026-10-01 ✅ 2026-10-14 ^pay\n"
                                                   "  - inne"_s);
    }

    void whenDoneFromToday()
    {
        QCOMPARE(
                status(u"- [ ] Podlać 🔁 every 3 days when done 📅 2026-10-01"_s, 0, TaskStatus::Done),
                u"- [ ] Podlać 🔁 every 3 days when done 📅 2026-10-17\n- [x] Podlać 🔁 every 3 days when done 📅 2026-10-01 ✅ 2026-10-14"_s);
    }

    void polishRulesAndTimes()
    {
        QCOMPARE(
                status(u"- [ ] Czynsz 📅 2026-01-31 10:00 🔁 co miesiąc"_s, 0, TaskStatus::Done, QDate(2026, 1, 30)),
                u"- [ ] Czynsz 📅 2026-02-28 10:00 🔁 every month\n- [x] Czynsz 📅 2026-01-31 10:00 🔁 every month ✅ 2026-01-30"_s);
        QCOMPARE(status(u"- [ ] a 🔁 co tydzień w poniedziałek, piątek"_s, 0, TaskStatus::Doing),
                 u"- [/] a 🔁 every week on Monday, Friday"_s);
        QCOMPARE(status(u"- [ ] a 🔁 kiedyś tam"_s, 0, TaskStatus::Doing), u"- [/] a 🔁 kiedyś tam"_s);
    }

    void otherReferenceDates()
    {
        QCOMPARE(firstLine(status(u"- [ ] a ⏳ 2026-10-14 🔁 every day"_s, 0, TaskStatus::Done)),
                 u"- [ ] a ⏳ 2026-10-15 🔁 every day"_s);
        QCOMPARE(firstLine(status(u"- [ ] a ⏰! 2026-10-14 06:30 🔁 every weekday"_s, 0, TaskStatus::Done)),
                 u"- [ ] a ⏰! 2026-10-15 06:30 🔁 every weekday"_s);
        QCOMPARE(firstLine(status(u"- [ ] a (@2026-10-16 07:00) 🔁 every week"_s, 0, TaskStatus::Done)),
                 u"- [ ] a (@2026-10-23 07:00) 🔁 every week"_s);
    }

    void copyWithoutDates()
    {
        QCOMPARE(status(u"- [ ] Rozciąganie 🔁 every day"_s, 0, TaskStatus::Done),
                 u"- [ ] Rozciąganie 🔁 every day\n- [x] Rozciąganie 🔁 every day ✅ 2026-10-14"_s);
    }

    void noNewInstance()
    {
        QCOMPARE(status(u"- [ ] a 🔁 every blue moon 📅 2026-10-15"_s, 0, TaskStatus::Done),
                 u"- [x] a 🔁 every blue moon 📅 2026-10-15 ✅ 2026-10-14"_s);
        QCOMPARE(status(u"- [ ] a 🔁 every day 📅 2026-10-15"_s, 0, TaskStatus::Cancelled),
                 u"- [-] a 🔁 every day 📅 2026-10-15 ❌ 2026-10-14"_s);
        QCOMPARE(status(u"- [x] a 🔁 every day 📅 2026-10-15 ✅ 2026-10-14"_s, 0, TaskStatus::Done),
                 u"- [x] a 🔁 every day 📅 2026-10-15 ✅ 2026-10-14"_s);
    }

    void recurringNonTaskLine()
    {
        QCOMPARE(status(u"- Stretch 🔁 every day 📅 2026-10-14"_s, 0, TaskStatus::Done),
                 u"- [ ] Stretch 🔁 every day 📅 2026-10-15\n- [x] Stretch 🔁 every day 📅 2026-10-14 ✅ 2026-10-14"_s);
    }

    void logseqRepeaters()
    {
        const QString src = u"- TODO Zadzwoń\n  SCHEDULED: <2026-10-15 Thu 09:00 +1w>\n  - dziecko"_s;
        QCOMPARE(
                status(src, 0, TaskStatus::Done),
                u"- TODO Zadzwoń\n  SCHEDULED: <2026-10-22 Thu 09:00 +1w>\n- DONE Zadzwoń ✅ 2026-10-14\n  SCHEDULED: <2026-10-15 Thu 09:00 +1w>\n  - dziecko"_s);
        const QString whenDone = status(u"- LATER a\n  DEADLINE: <2026-10-01 Thu .+1w>"_s, 0, TaskStatus::Done);
        QCOMPARE(whenDone.split(u'\n').mid(0, 2),
                 QStringList({u"- LATER a"_s, u"  DEADLINE: <2026-10-21 Wed .+1w>"_s}));
    }

    void catchUpRepeaters()
    {
        QCOMPARE(
                toggleTask(u"- TODO podlać\n  SCHEDULED: <2026-10-01 Thu ++1w>"_s, 0, Today),
                u"- TODO podlać\n  SCHEDULED: <2026-10-15 Thu ++1w>\n- DONE podlać ✅ 2026-10-14\n  SCHEDULED: <2026-10-01 Thu ++1w>"_s);
        QCOMPARE(toggleTask(u"- TODO a\n  DEADLINE: <2026-10-20 Tue ++1d>"_s, 0, Today).split(u'\n')[1],
                 u"  DEADLINE: <2026-10-21 Wed ++1d>"_s);
        QCOMPARE(toggleTask(u"- TODO a\n  DEADLINE: <2026-10-01 Thu +1w>"_s, 0, Today).split(u'\n')[1],
                 u"  DEADLINE: <2026-10-08 Thu +1w>"_s);
    }

    void recurringInCalloutCrlf()
    {
        QCOMPARE(
                status(u"> [!todo]\r\n> - [ ] x 🔁 every day 📅 2026-10-14\r\n"_s, 1, TaskStatus::Done),
                u"> [!todo]\r\n> - [ ] x 🔁 every day 📅 2026-10-15\r\n> - [x] x 🔁 every day 📅 2026-10-14 ✅ 2026-10-14\r\n"_s);
    }

    void recurringParsesBack()
    {
        const QString out = status(u"- [ ] Faktura 📅 2026-10-20 🔁 every month ^f"_s, 0, TaskStatus::Done);
        const ParsedNote n = parseNote(u"a.md"_s, out);
        QCOMPARE(n.tasks.size(), 2);
        QCOMPARE(n.tasks[0].status, TaskStatus::Todo);
        QCOMPARE(n.tasks[0].due->toString(), u"2026-11-20"_s);
        QVERIFY(!n.tasks[0].doneDate);
        QCOMPARE(n.tasks[1].status, TaskStatus::Done);
        QCOMPARE(n.tasks[1].due->toString(), u"2026-10-20"_s);
        QCOMPARE(n.tasks[1].doneDate, Today);
        QCOMPARE(n.blockIds.keys(), QStringList({u"f"_s}));
    }

    // --- toggleTask ------------------------------------------------------------------------------

    void toggle()
    {
        QCOMPARE(toggleTask(u"- [ ] a"_s, 0, Today), u"- [x] a ✅ 2026-10-14"_s);
        QCOMPARE(toggleTask(u"- [/] a"_s, 0, Today), u"- [x] a ✅ 2026-10-14"_s);
        QCOMPARE(toggleTask(u"- [>] a"_s, 0, Today), u"- [x] a ✅ 2026-10-14"_s);
        QCOMPARE(toggleTask(u"- [x] a ✅ 2026-10-01"_s, 0, Today), u"- [ ] a"_s);
        QCOMPARE(toggleTask(u"- [-] a ❌ 2026-10-01"_s, 0, Today), u"- [ ] a"_s);
        QCOMPARE(toggleTask(u"- WAITING a"_s, 0, Today), u"- DONE a ✅ 2026-10-14"_s);
        QCOMPARE(toggleTask(u"- a"_s, 0, Today), u"- [ ] a"_s);
        QCOMPARE(toggleTask(u"```\nx\n```"_s, 1, Today), u"```\nx\n```"_s);
    }

    // --- removeTaskMarker ("no status") ------------------------------------------------------------

    void removeMarker_data()
    {
        QTest::addColumn<QString>("src");
        QTest::addColumn<int>("line");
        QTest::addColumn<QString>("expected");
        QTest::newRow("todo") << u"- [ ] Kupić mleko"_s << 0 << u"- Kupić mleko"_s;
        QTest::newRow("doing") << u"- [/] Pisać raport"_s << 0 << u"- Pisać raport"_s;
        QTest::newRow("deferred") << u"- [>] Odpisać Żanecie"_s << 0 << u"- Odpisać Żanecie"_s;
        QTest::newRow("done date") << u"- [x] Zapłacić czynsz 📅 2026-10-10 ✅ 2026-10-09"_s << 0
                                   << u"- Zapłacić czynsz 📅 2026-10-10"_s;
        QTest::newRow("capital X, block id") << u"- [X] a ✅ 2026-10-01 ^id1"_s << 0 << u"- a ^id1"_s;
        QTest::newRow("cancelled date") << u"- [-] Wizyta ❌ 2026-10-02 u dentysty"_s << 0 << u"- Wizyta u dentysty"_s;
        QTest::newRow("custom character") << u"- [?] pytanie"_s << 0 << u"- pytanie"_s;
        QTest::newRow("only a date") << u"- [x] ✅ 2026-10-14"_s << 0 << u"- "_s;
        // A date right after the marker: one space stays between the list marker and the text.
        QTest::newRow("date first") << u"- [x] ✅ 2026-10-14 Pranie"_s << 0 << u"- Pranie"_s;
        QTest::newRow("date first, logseq") << u"- DONE ✅ 2026-10-14 Pranie"_s << 0 << u"- Pranie"_s;
        QTest::newRow("tab after the checkbox") << u"- [ ]\tżółw"_s << 0 << u"- żółw"_s;
        QTest::newRow("byte order mark") << u"﻿- [/] ą"_s << 0 << u"﻿- ą"_s;
        QTest::newRow("empty task") << u"- [ ] "_s << 0 << u"- "_s;
        QTest::newRow("ordered") << u"3. [ ] trzeci"_s << 0 << u"3. trzeci"_s;
        QTest::newRow("star, nested") << u"- p\n  * [/] zagnieżdżone"_s << 1 << u"- p\n  * zagnieżdżone"_s;
        QTest::newRow("continuation line targets the item") << u"- [ ] a\n  szczegóły"_s << 1 << u"- a\n  szczegóły"_s;
        QTest::newRow("quote") << u"> - [ ] w cytacie"_s << 0 << u"> - w cytacie"_s;
        QTest::newRow("callout") << u"> [!todo] Lista\n> - [x] ząb ✅ 2026-10-01"_s << 1
                                 << u"> [!todo] Lista\n> - ząb"_s;
        QTest::newRow("logseq todo") << u"- TODO zadzwonić"_s << 0 << u"- zadzwonić"_s;
        QTest::newRow("logseq done") << u"- DONE zadzwonić ✅ 2026-10-01"_s << 0 << u"- zadzwonić"_s;
        QTest::newRow("logseq waiting, priority") << u"- WAITING [#A] paczka"_s << 0 << u"- [#A] paczka"_s;
        QTest::newRow("logseq planning kept")
                << u"- LATER a\n  SCHEDULED: <2026-10-15 Thu>"_s << 0 << u"- a\n  SCHEDULED: <2026-10-15 Thu>"_s;
        QTest::newRow("dates in code kept")
                << u"- [x] użyj `✅ 2026-01-01` ✅ 2026-10-01"_s << 0 << u"- użyj `✅ 2026-01-01`"_s;
        QTest::newRow("recurrence kept as written")
                << u"- [ ] podlać 🔁 co tydzień"_s << 0 << u"- podlać 🔁 co tydzień"_s;
        QTest::newRow("comment tail kept")
                << u"- [x] zadanie ✅ 2026-10-14 %%notatka\nciąg%%"_s << 0 << u"- zadanie %%notatka\nciąg%%"_s;
        QTest::newRow("crlf") << u"- [ ] a\r\n- [x] b ✅ 2026-10-01\r\nc"_s << 1 << u"- [ ] a\r\n- b\r\nc"_s;
        QTest::newRow("not a task") << u"- zwykły punkt"_s << 0 << u"- zwykły punkt"_s;
        QTest::newRow("paragraph") << u"Akapit"_s << 0 << u"Akapit"_s;
        QTest::newRow("in code") << u"```\n- [ ] kod\n```"_s << 1 << u"```\n- [ ] kod\n```"_s;
        QTest::newRow("heading") << u"# [ ] Nagłówek"_s << 0 << u"# [ ] Nagłówek"_s;
        QTest::newRow("out of range") << u"- [ ] a"_s << 3 << u"- [ ] a"_s;
        QTest::newRow("negative line") << u"- [ ] a"_s << -1 << u"- [ ] a"_s;
    }

    void removeMarker()
    {
        QFETCH(QString, src);
        QFETCH(int, line);
        QFETCH(QString, expected);
        const QString out = removeTaskMarker(src, line);
        QCOMPARE(out, expected);
        // The task is gone (the others stay); an unchanged text had none there.
        const qsizetype before = parseNote(u"a.md"_s, src).tasks.size();
        QCOMPARE(parseNote(u"a.md"_s, out).tasks.size(), out == src ? before : before - 1);
    }

    void removeMarkerThenStatus()
    {
        // Back from "no status": setTaskStatus makes the plain item a task again.
        const QString plain = removeTaskMarker(u"- [x] Pranie ✅ 2026-10-01"_s, 0);
        QCOMPARE(plain, u"- Pranie"_s);
        QCOMPARE(status(plain, 0, TaskStatus::Deferred), u"- [>] Pranie"_s);
        QCOMPARE(removeTaskMarker(plain, 0), plain);
    }

    void linesThatCannotBecomeTasks_data()
    {
        QTest::addColumn<QString>("src");
        QTest::addColumn<int>("line");
        QTest::newRow("table header") << u"| a |\n|---|\n| 1 |"_s << 0;
        QTest::newRow("table delimiter") << u"| a |\n|---|\n| 1 |"_s << 1;
        QTest::newRow("table row") << u"| a |\n|---|\n| 1 |"_s << 2;
        QTest::newRow("heading") << u"# Nagłówek"_s << 0;
        QTest::newRow("setext text") << u"Tytuł\n==="_s << 0;
        QTest::newRow("setext underline") << u"Tytuł\n==="_s << 1;
        QTest::newRow("rule") << u"---"_s << 0;
        QTest::newRow("rule stars") << u"***"_s << 0;
        QTest::newRow("callout header") << u"> [!note] Uwaga\n> treść"_s << 0;
        QTest::newRow("heading in callout") << u"> [!note] Uwaga\n> ## Śródtytuł"_s << 1;
        QTest::newRow("id line") << u"```\nx\n```\n^kod"_s << 3;
        QTest::newRow("html") << u"<div>\nx\n</div>"_s << 1;
    }

    void linesThatCannotBecomeTasks()
    {
        QFETCH(QString, src);
        QFETCH(int, line);
        QCOMPARE(status(src, line, TaskStatus::Todo), src);
        QCOMPARE(toggleTask(src, line, Today), src);
    }

    void blankLinesInCallouts()
    {
        QCOMPARE(toggleTask(u"> [!todo] Lista\n> "_s, 1, Today), u"> [!todo] Lista\n> - [ ] "_s);
        QCOMPARE(toggleTask(u"> [!todo] Lista\n>"_s, 1, Today), u"> [!todo] Lista\n> - [ ] "_s);
        QCOMPARE(toggleTask(u"> > zagnieżdżony\n> >"_s, 1, Today), u"> > zagnieżdżony\n> > - [ ] "_s);
        QCOMPARE(toggleTask(u"> > zagnieżdżony"_s, 0, Today), u"> > - [ ] zagnieżdżony"_s);
        QCOMPARE(toggleTask(u"> [!note]\n> > cytat w callout"_s, 1, Today), u"> [!note]\n> > - [ ] cytat w callout"_s);
    }

    void tasksInPlainQuotes()
    {
        QCOMPARE(
                toggleTask(u"> Cytat\n> - [ ] zadanie 🔁 every day 📅 2026-10-14"_s, 1, Today),
                u"> Cytat\n> - [ ] zadanie 🔁 every day 📅 2026-10-15\n> - [x] zadanie 🔁 every day 📅 2026-10-14 ✅ 2026-10-14"_s);
    }

    void hiddenCommentLines()
    {
        const QString src = u"Tekst %%ukryty\nkomentarz\n\nkoniec%% widoczny"_s;
        for (int line : {1, 2, 3}) {
            QCOMPARE(status(src, line, TaskStatus::Todo), src);
            QCOMPARE(toggleTask(src, line, Today), src);
        }
    }

    void tasksAroundComments()
    {
        QCOMPARE(toggleTask(u"%%\nx\n%% - [ ] zadanie"_s, 2, Today), u"%%\nx\n%% - [x] zadanie ✅ 2026-10-14"_s);
        const QString done = toggleTask(u"- [ ] zadanie %%notatka\nciąg%%"_s, 0, Today);
        QCOMPARE(done, u"- [x] zadanie ✅ 2026-10-14 %%notatka\nciąg%%"_s);
        const ParsedNote n = parseNote(u"a.md"_s, done);
        QCOMPARE(n.tasks.first().status, TaskStatus::Done);
        QCOMPARE(n.tasks.first().doneDate, Today);
        QCOMPARE(n.tasks.first().text, u"zadanie"_s);
        QCOMPARE(toggleTask(done, 0, Today), u"- [ ] zadanie %%notatka\nciąg%%"_s);
        QCOMPARE(toggleTask(u"- [ ] a 📅 2026-10-14 🔁 every day %%x\ny%%"_s, 0, Today),
                 u"- [ ] a 📅 2026-10-15 🔁 every day\n- [x] a 📅 2026-10-14 🔁 every day ✅ 2026-10-14 %%x\ny%%"_s);
    }

    void unclosedFenceBlankLines()
    {
        const QString src = u"Tekst\n```\nkod\n\n"_s;
        QCOMPARE(toggleTask(src, 3, Today), src);
        QCOMPARE(toggleTask(src, 4, Today), src);
        const QString callout = u"> [!note]\n> ```\n> kod\n>\n\n"_s;
        QCOMPARE(toggleTask(callout, 3, Today), callout);
        QCOMPARE(toggleTask(callout, 4, Today), u"> [!note]\n> ```\n> kod\n>\n- [ ] \n"_s);
        QCOMPARE(toggleTask(u"```\nkod\n```\n"_s, 3, Today), u"```\nkod\n```\n- [ ] "_s);
    }

    // --- block ids -------------------------------------------------------------------------------

    void generatedIds()
    {
        QSet<QString> ids;
        for (int i = 0; i < 200; ++i) {
            const QString id = generateBlockId();
            QVERIFY2(isGeneratedId(id), qPrintable(id));
            ids.insert(id);
        }
        QVERIFY(ids.size() > 190);
    }

    void existingIds()
    {
        auto r = ensureBlockId(u"- a ^abc\n  more"_s, 1);
        QCOMPARE(r.content, u"- a ^abc\n  more"_s);
        QCOMPARE(r.id, u"abc"_s);
        r = ensureBlockId(u"- a\n  id:: 6f1c"_s, 0);
        QCOMPARE(r.content, u"- a\n  id:: 6f1c"_s);
        QCOMPARE(r.id, u"6f1c"_s);
        QCOMPARE(ensureBlockId(u"| a | b |\n|---|---|\n^tbl"_s, 0).id, u"tbl"_s);
    }

    void firstLinePlacements()
    {
        const auto li = ensureBlockId(u"- a\n  more\n  - child"_s, 1);
        QVERIFY(isGeneratedId(li.id));
        QCOMPARE(li.content, u"- a ^"_s + li.id + u"\n  more\n  - child"_s);
        const auto h = ensureBlockId(u"## Tytuł ##  "_s, 0);
        QCOMPARE(h.content, u"## Tytuł ## ^"_s + h.id);
        const auto c = ensureBlockId(u"> [!note] T\n> body"_s, 0);
        QCOMPARE(c.content, u"> [!note] T ^"_s + c.id + u"\n> body"_s);
        QVERIFY(resolves(c.content, c.id, 0));
    }

    void lastLinePlacements()
    {
        const auto p = ensureBlockId(u"p1\np2\n\nx"_s, 0);
        QCOMPARE(p.content, u"p1\np2 ^"_s + p.id + u"\n\nx"_s);
        QVERIFY(resolves(p.content, p.id, 0));
        const auto q = ensureBlockId(u"> a\n> b"_s, 0);
        QCOMPARE(q.content, u"> a\n> b ^"_s + q.id);
    }

    void separateIdLines()
    {
        const auto code = ensureBlockId(u"```\nx\n```\nafter"_s, 1);
        QCOMPARE(code.content, u"```\nx\n```\n^"_s + code.id + u"\n\nafter"_s);
        const ParsedNote n = parseNote(u"a.md"_s, code.content);
        QCOMPARE(n.blocks[n.blockIds.value(code.id)].kind, BlockKind::Code);
        const auto table = ensureBlockId(u"| a | b |\r\n|---|---|\r\n| 1 | 2 |"_s, 2);
        QCOMPARE(table.content, u"| a | b |\r\n|---|---|\r\n| 1 | 2 |\r\n^"_s + table.id);
        const ParsedNote t = parseNote(u"a.md"_s, table.content);
        QCOMPARE(t.blocks[t.blockIds.value(table.id)].kind, BlockKind::Table);
    }

    void idempotentAndTakenIds()
    {
        const auto first = ensureBlockId(u"- zadanie"_s, 0);
        const auto again = ensureBlockId(first.content, 0);
        QCOMPARE(again.content, first.content);
        QCOMPARE(again.id, first.id);
        int calls = 0;
        const auto avoided = ensureBlockId(u"- x"_s, 0, [&](const QString &) { return ++calls < 3; });
        QCOMPARE(calls, 3);
        QVERIFY(isGeneratedId(avoided.id));
    }

    void itemOpeningFence()
    {
        const auto r = ensureBlockId(u"- ```\n  code\n  ```"_s, 0);
        QCOMPARE(r.content, u"- ```\n  code\n  ```\n  ^"_s + r.id);
        QVERIFY(resolves(r.content, r.id, 0));
    }

    void noIdPossible()
    {
        for (const auto &[src, line] : {std::pair{u"a\n\nb"_s, 1}, std::pair{u"---\nx: 1\n---\n"_s, 1},
                                        std::pair{QString(), 0}, std::pair{u"Tekst\n\n```js\nconst a = 1;\n"_s, 3},
                                        std::pair{u"> [!note]\n> ```\n> kod"_s, 2}, std::pair{u"a %%b\n\nc%%"_s, 0}}) {
            const auto r = ensureBlockId(src, line);
            QCOMPARE(r.content, src);
            QVERIFY(r.id.isEmpty());
        }
    }

    void quoteWithNestedBlocks()
    {
        const QString src = u"> - a\n> - b\n>\n> tekst\n\nPo"_s;
        const auto r = ensureBlockId(src, 2);
        QCOMPARE(r.content, u"> - a\n> - b\n>\n> tekst\n\n^"_s + r.id + u"\n\nPo"_s);
        const ParsedNote n = parseNote(u"a.md"_s, r.content);
        QCOMPARE(n.blocks[n.blockIds.value(r.id)].kind, BlockKind::Quote);
        QCOMPARE(n.blocks[n.blockIds.value(r.id)].line, 0);
        const auto r3 = ensureBlockId(u"> - a\n>\n> - b\n## Dalej"_s, 1);
        QCOMPARE(r3.content, u"> - a\n>\n> - b\n\n^"_s + r3.id + u"\n\n## Dalej"_s);
        const ParsedNote n3 = parseNote(u"a.md"_s, r3.content);
        QCOMPARE(n3.blocks[n3.blockIds.value(r3.id)].endLine, 4);
        const auto r2 = ensureBlockId(src, 1);
        QCOMPARE(r2.content, u"> - a\n> - b ^"_s + r2.id + u"\n>\n> tekst\n\nPo"_s);
        const ParsedNote n2 = parseNote(u"a.md"_s, r2.content);
        QCOMPARE(n2.blocks[n2.blockIds.value(r2.id)].kind, BlockKind::ListItem);
    }

    void htmlIdLines()
    {
        const auto r = ensureBlockId(u"<div>\nśrodek\n</div>\n\nDalej"_s, 1);
        QCOMPARE(r.content, u"<div>\nśrodek\n</div>\n\n^"_s + r.id + u"\n\nDalej"_s);
        QVERIFY(resolves(r.content, r.id, 0));
        const auto r2 = ensureBlockId(u"<script>\nx()\n</script>\nDalej"_s, 0);
        QCOMPARE(r2.content, u"<script>\nx()\n</script>\n\n^"_s + r2.id + u"\n\nDalej"_s);
        QVERIFY(resolves(r2.content, r2.id, 0));
    }

    void commentFallbacks()
    {
        const auto r = ensureBlockId(u"- zadanie %%początek\n  komentarza%% koniec"_s, 0);
        QCOMPARE(r.content, u"- zadanie %%początek\n  komentarza%% koniec ^"_s + r.id);
        QVERIFY(resolves(r.content, r.id, 0));
        const auto p = ensureBlockId(u"pierwsza\ndruga %%komentarz\n\nciąg dalszy%%"_s, 1);
        QCOMPARE(p.content, u"pierwsza ^"_s + p.id + u"\ndruga %%komentarz\n\nciąg dalszy%%"_s);
        QVERIFY(resolves(p.content, p.id, 0));
    }

    // --- listItemRange ---------------------------------------------------------------------------

    void listItemRanges()
    {
        const QString src = u"- a\n  - b\n    more b\n  - c\n- d\n\ntext"_s;
        QCOMPARE(listItemRange(src, 0), TextRange({0, src.indexOf(u"\n- d")}));
        const qsizetype b = src.indexOf(u"  - b");
        QCOMPARE(listItemRange(src, 1), TextRange({b, src.indexOf(u"\n  - c")}));
        QCOMPARE(listItemRange(src, 2), TextRange({b, src.indexOf(u"\n  - c")}));
        QCOMPARE(listItemRange(src, 4), TextRange({src.indexOf(u"- d"), src.indexOf(u"- d") + 3}));
        QVERIFY(!listItemRange(src, 5));
        QVERIFY(!listItemRange(src, 6));
        QVERIFY(!listItemRange(src, 99));
        QCOMPARE(listItemRange(u"- a\r\n  - b\r\n- c"_s, 0), TextRange({0, 10}));
        const QString callout = u"> [!todo]\n> - a\n>   - b\n> - c"_s;
        QCOMPARE(listItemRange(callout, 1), TextRange({10, callout.indexOf(u"\n> - c")}));
    }

    // --- renameLinks -----------------------------------------------------------------------------

    void renameWiki()
    {
        QCOMPARE(
                renameLinks(u"[[Old]] [[Old|alias]] [[Old#Nagłówek]] [[Old#^abc]] ![[Old]] ![[Old#^abc|x]]"_s, u"Old"_s,
                            u"Nowa strona"_s),
                u"[[Nowa strona]] [[Nowa strona|alias]] [[Nowa strona#Nagłówek]] [[Nowa strona#^abc]] ![[Nowa strona]] ![[Nowa strona#^abc|x]]"_s);
        QCOMPARE(renameLinks(u"[[żółw]] [[ŻÓŁW|Ż]]"_s, u"Żółw"_s, u"Kot"_s), u"[[Kot]] [[Kot|Ż]]"_s);
        QCOMPARE(renameLinks(u"[[żółw]]"_s, u"żółw"_s, u"Kot"_s), u"[[Kot]]"_s);
        QCOMPARE(renameLinks(u"[[ Old ]] [[Old.md]] | [[Old\\|a]] |"_s, u"Old"_s, u"New"_s),
                 u"[[ New ]] [[New.md]] | [[New\\|a]] |"_s);
    }

    void renamePathQualified()
    {
        QCOMPARE(renameLinks(u"[[Projekty/Old]] [[projekty/old#h|x]] [[Inne/Old]] [[Old]]"_s, u"Old"_s, u"New"_s,
                             u"Projekty/Old.md"_s, u"Archiwum/New.md"_s),
                 u"[[Archiwum/New]] [[Archiwum/New#h|x]] [[Inne/Old]] [[New]]"_s);
        QCOMPARE(renameLinks(u"[[Dom/Old]]"_s, u"Old"_s, u"New"_s, u"Projekty/Dom/Old.md"_s), u"[[Dom/New]]"_s);
        QCOMPARE(renameLinks(u"[[a/Note]] [[Note]]"_s, u"Note"_s, u"Note"_s, u"a/Note.md"_s, u"b/Note.md"_s),
                 u"[[b/Note]] [[Note]]"_s);
    }

    void renameSkipsCodeAndComments()
    {
        const QString src = u"[[Older]] [[#Old]] `[[Old]]` %% [[Old]] %%\n```\n[[Old]]\n```\n    [[Old]]"_s;
        QString expected = src;
        expected.replace(u"    [[Old]]"_s, u"    [[New]]"_s);
        QCOMPARE(renameLinks(src, u"Old"_s, u"New"_s), expected);
    }

    void renameMarkdownLinks()
    {
        QCOMPARE(renameLinks(u"[x](Old.md) ![i](Old.md#^b) [y](./Old.md#Sekcja%201)"_s, u"Old"_s, u"Nowa nazwa"_s),
                 u"[x](Nowa%20nazwa.md) ![i](Nowa%20nazwa.md#^b) [y](./Nowa%20nazwa.md#Sekcja%201)"_s);
        QCOMPARE(renameLinks(u"[a](folder/Old%20Name.md) [b](<folder/Old Name.md>) [c](other/Old%20Name.md)"_s,
                             u"Old Name"_s, u"New"_s, u"folder/Old Name.md"_s, u"dest/New.md"_s),
                 u"[a](dest/New.md) [b](<dest/New.md>) [c](other/Old%20Name.md)"_s);
        const QString external = u"[a](https://x.pl/Old.md) [b](Old.png) [c](mailto:Old)"_s;
        QCOMPARE(renameLinks(external, u"Old"_s, u"New"_s), external);
        // Review regression: '#' would start a fragment and '%XX' would be decoded, so both are
        // percent-encoded (pages synced from other apps may use them); the links parse back.
        const QString renamed = renameLinks(u"[x](Old.md#H) [y](<Old.md>)"_s, u"Old"_s, u"C# 100%41"_s);
        QCOMPARE(renamed, u"[x](C%23%20100%2541.md#H) [y](<C%23 100%2541.md>)"_s);
        const ParsedNote note = parseNote(u"a.md"_s, renamed);
        QCOMPARE(note.links.size(), 2);
        QCOMPARE(note.links[0].target, u"C# 100%41.md"_s);
        QCOMPARE(note.links[0].heading, u"H"_s);
        QCOMPARE(note.links[1].target, u"C# 100%41.md"_s);
    }

    void renameMisc()
    {
        const QString src = u"Brak linków.\r\n[[Inna]]"_s;
        QCOMPARE(renameLinks(src, u"Old"_s, u"New"_s), src);
        QCOMPARE(renameLinks(u"[[Old]]"_s, QString(), u"New"_s), u"[[Old]]"_s);
        QCOMPARE(renameLinks(u"[[Old]]"_s, u"Old"_s, u"Old"_s), u"[[Old]]"_s);
        QCOMPARE(renameLinks(u"---\r\nup: \"[[Old]]\"\r\n---\r\n- [[Old]]\r\n"_s, u"Old"_s, u"New"_s),
                 u"---\r\nup: \"[[New]]\"\r\n---\r\n- [[New]]\r\n"_s);
    }

    void renameKeepsWrittenForm()
    {
        QCOMPARE(renameLinks(u"[d](../folder/Old%20Name.md#H) [e](./Old%20Name.md)"_s, u"Old Name"_s, u"Nowa nazwa"_s,
                             u"folder/Old Name.md"_s, u"folder/Nowa nazwa.md"_s),
                 u"[d](../folder/Nowa%20nazwa.md#H) [e](./Nowa%20nazwa.md)"_s);
        QCOMPARE(renameLinks(u"[[b/Old]] [[a/b/Old|x]] ![[b/Old#^id]]"_s, u"Old"_s, u"Nowy"_s, u"a/b/Old.md"_s,
                             u"a/b/Nowy.md"_s),
                 u"[[b/Nowy]] [[a/b/Nowy|x]] ![[b/Nowy#^id]]"_s);
        QCOMPARE(renameLinks(u"[[b/Old]] [x](b/Old.md)"_s, u"Old"_s, u"Nowy"_s, u"a/b/Old.md"_s, u"c/Nowy.md"_s),
                 u"[[c/Nowy]] [x](c/Nowy.md)"_s);
        QCOMPARE(renameLinks(u"[[Old]] [x](Old.md)"_s, u"Old"_s, QString()), u"[[Old]] [x](Old.md)"_s);
        QCOMPARE(renameLinks(u"[[Old]]"_s, u"Old"_s, u"   "_s), u"[[Old]]"_s);
    }

    // --- journals --------------------------------------------------------------------------------

    void journalPaths()
    {
        const VaultConfig def;
        QCOMPARE(journalPathForDate(QDate(2026, 10, 14), def), u"journals/2026-10-14.md"_s);
        QCOMPARE(journalPathForDate(QDate(2026, 10, 14), config(u"yyyy_MM_dd"_s)), u"journals/2026_10_14.md"_s);
        QCOMPARE(journalPathForDate(QDate(2026, 1, 5), config(u"yyyy/MM/yyyy-MM-dd"_s, u"Dziennik/"_s)),
                 u"Dziennik/2026/01/2026-01-05.md"_s);
        QCOMPARE(journalPathForDate(QDate(2026, 10, 14), config(u"yyyy-MM-dd"_s, QString())), u"2026-10-14.md"_s);
        QCOMPARE(journalPathForDate(QDate(2026, 10, 4), config(u"d MMM yyyy"_s)), u"journals/4 Oct 2026.md"_s);
        QCOMPARE(journalPathForDate(QDate(2026, 10, 14), config(u"yyyy-MM-dd dddd"_s), QLocale(QLocale::Polish)),
                 u"journals/2026-10-14 środa.md"_s);
        // Formats that do not round-trip fall back to yyyy-MM-dd.
        QCOMPARE(journalPathForDate(QDate(2026, 10, 14), config(u"MM-dd"_s)), u"journals/2026-10-14.md"_s);
        QCOMPARE(journalPathForDate(QDate(2026, 10, 14), config(QString())), u"journals/2026-10-14.md"_s);
    }

    void journalDates()
    {
        const VaultConfig def;
        QCOMPARE(journalDateFromPath(u"journals/2026-10-14.md"_s, def), QDate(2026, 10, 14));
        QCOMPARE(journalDateFromPath(u"Journals/2026-10-14.md"_s, def), QDate(2026, 10, 14));
        QCOMPARE(journalDateFromPath(u"journals/2026_10_14.md"_s, def), QDate(2026, 10, 14));
        for (const QString &p : {u"pages/2026-10-14.md"_s, u"journals/2026-10-14.png"_s, u"journals/2026-02-30.md"_s,
                                 u"journals/2026-1-5.md"_s, u"journals/Notatki.md"_s, u"journals/sub/2026-10-14.md"_s,
                                 u"journals2026-10-14.md"_s})
            QVERIFY2(!journalDateFromPath(p, def), qPrintable(p));
        const VaultConfig root = config(u"yyyy-MM-dd"_s, QString());
        QCOMPARE(journalDateFromPath(u"2026-10-14.md"_s, root), QDate(2026, 10, 14));
        QVERIFY(!journalDateFromPath(u"Projekt.md"_s, root));
    }

    void journalDatesOfLinkTargets()
    {
        const VaultConfig def;
        QCOMPARE(journalDateFromLinkTarget(u"2026-10-14"_s, def), QDate(2026, 10, 14));
        QCOMPARE(journalDateFromLinkTarget(u" 2026-10-14.md "_s, def), QDate(2026, 10, 14));
        QCOMPARE(journalDateFromLinkTarget(u"journals/2026-10-14"_s, def), QDate(2026, 10, 14));
        QCOMPARE(journalDateFromLinkTarget(u"2026_10_14"_s, def), QDate(2026, 10, 14)); // Logseq's names
        for (const QString &t : {u"Żółw"_s, u"2026-02-30"_s, u"2026-1-5"_s, u"Projekty/2026-10-14"_s, QString(),
                                 u"2026"_s, u"14.10.2026"_s})
            QVERIFY2(!journalDateFromLinkTarget(t, def), qPrintable(t));
        // The vault's own format: links as its daily notes are named.
        const VaultConfig dots = config(u"dd.MM.yyyy"_s, u"Dziennik"_s);
        QCOMPARE(journalDateFromLinkTarget(u"14.10.2026"_s, dots), QDate(2026, 10, 14));
        QCOMPARE(journalDateFromLinkTarget(u"Dziennik/14.10.2026"_s, dots), QDate(2026, 10, 14));
        QCOMPARE(journalDateFromLinkTarget(u"2026-10-14"_s, dots), QDate(2026, 10, 14)); // ISO always
        const VaultConfig polish = config(u"d MMMM yyyy"_s);
        QCOMPARE(journalDateFromLinkTarget(u"4 października 2026"_s, polish), QDate(2026, 10, 4));
    }

    void journalRoundTrip_data()
    {
        QTest::addColumn<QString>("format");
        for (const QString &f : {u"yyyy-MM-dd"_s, u"yyyy_MM_dd"_s, u"dd.MM.yyyy"_s, u"yyyy/MM/dd"_s, u"MMM d, yyyy"_s,
                                 u"yyyy/MM/yyyy-MM-dd"_s, u"yyyy/MM-MMMM/yyyy-MM-dd-dddd"_s, u"yyyy/yyyy-MM-dd dddd"_s})
            QTest::newRow(qPrintable(f)) << f;
    }

    void journalRoundTrip()
    {
        QFETCH(QString, format);
        const VaultConfig c = config(format);
        for (const QDate d : {QDate(2026, 1, 1), QDate(2026, 1, 5), QDate(2026, 2, 28), QDate(2028, 2, 29),
                              QDate(2026, 10, 14), QDate(2026, 12, 31)}) {
            const VaultPath p = journalPathForDate(d, c);
            QCOMPARE(journalDateFromPath(p, c), d);
        }
    }

    void journalNestedDisagreement()
    {
        const VaultConfig c = config(u"yyyy/MM/yyyy-MM-dd"_s);
        QVERIFY(!journalDateFromPath(u"journals/2025/10/2026-10-14.md"_s, c));
        QVERIFY(!journalDateFromPath(u"journals/2026/11/2026-10-14.md"_s, c));
    }

    void journalPolishNames()
    {
        const VaultConfig c = config(u"yyyy-MM-dd dddd"_s);
        QCOMPARE(journalDateFromPath(u"journals/2026-10-14 środa.md"_s, c), QDate(2026, 10, 14));
        QCOMPARE(journalDateFromPath(u"journals/2026-10-14 Wednesday.md"_s, c), QDate(2026, 10, 14));
        QVERIFY(!journalDateFromPath(u"journals/2026-10-14 czwartek.md"_s, c));
        const VaultConfig months = config(u"d MMMM yyyy"_s);
        QCOMPARE(journalDateFromPath(u"journals/14 października 2026.md"_s, months), QDate(2026, 10, 14));
    }

    // --- templates -------------------------------------------------------------------------------

    void templateVariables()
    {
        const auto r = applyTemplate(u"# {{title}}\n{{date}} {{time}} ({{weekday}})",
                                     vars(dates::UiLocale::Polish, u"Dziennik"_s));
        QCOMPARE(r.text, u"# Dziennik\n2026-10-14 09:05 (środa)"_s);
        QCOMPARE(r.cursor, -1);
        QCOMPARE(applyTemplate(u"{{weekday}}", vars(dates::UiLocale::English)).text, u"Wednesday"_s);
    }

    void templateFormats()
    {
        QCOMPARE(applyTemplate(u"{{date:dddd, d MMMM yyyy}}", vars()).text, u"środa, 14 października 2026"_s);
        QCOMPARE(applyTemplate(u"{{date:dddd, d MMMM yyyy}}", vars(dates::UiLocale::English)).text,
                 u"Wednesday, 14 October 2026"_s);
        QCOMPARE(applyTemplate(u"{{time:HH:mm:ss}}", vars()).text, u"09:05:00"_s);
        QCOMPARE(applyTemplate(u"{{date:ddd}}", vars(dates::UiLocale::English)).text, u"Wed"_s);
        QCOMPARE(applyTemplate(u"{{time:h:mm AP}}", vars(dates::UiLocale::English)).text, u"9:05 AM"_s);
        QCOMPARE(applyTemplate(u"{{date:yyyy-MM-dd'T'HH:mm}}", vars()).text, u"2026-10-14T09:05"_s);
    }

    void templateCursor()
    {
        const auto r =
                applyTemplate(u"# {{title}}\n\n- {{cursor}}\n{{cursor}}end", vars(dates::UiLocale::Polish, u"Żółw"_s));
        QCOMPARE(r.text, u"# Żółw\n\n- \nend"_s);
        QCOMPARE(r.cursor, QString(u"# Żółw\n\n- "_s).size());
    }

    void templateTolerance()
    {
        QCOMPARE(applyTemplate(u"{{ Title }} {{unknown}} {{date:}} {{", vars(dates::UiLocale::Polish, u"T"_s)).text,
                 u"T {{unknown}} 2026-10-14 {{"_s);
        QCOMPARE(applyTemplate(u"{{title}}", vars(dates::UiLocale::Polish, u"{{date}}"_s)).text, u"{{date}}"_s);
        QCOMPARE(applyTemplate(u"{{TIME}}", vars()).text, u"09:05"_s);
    }

    // --- end to end (archive index test) ---------------------------------------------------------

    void endToEnd()
    {
        const QDate date(2026, 10, 14);
        const VaultConfig def;
        const VaultPath path = journalPathForDate(date, def);
        const auto tpl = applyTemplate(u"# {{date:dddd}}\n- [ ] {{cursor}}Przegląd [[Projekt]] ⏰ {{date}} 09:30",
                                       TemplateVars{QDateTime(date, QTime(8, 0)), u"x"_s, dates::UiLocale::Polish});
        QCOMPARE(tpl.text, u"# środa\n- [ ] Przegląd [[Projekt]] ⏰ 2026-10-14 09:30"_s);
        const ParsedNote note = parseNote(path, tpl.text);
        QCOMPARE(note.journalDate, date);
        QCOMPARE(note.reminders.first().at.toString(), u"2026-10-14 09:30"_s);
        QCOMPARE(note.reminders.first().text, u"Przegląd [[Projekt]]"_s);
        QCOMPARE(note.reminders.first().line, 1);
        const QString done = toggleTask(tpl.text, 1, date);
        const ParsedNote after = parseNote(path, done);
        QCOMPARE(after.tasks.first().status, TaskStatus::Done);
        QCOMPARE(after.tasks.first().doneDate, date);
        QVERIFY(renameLinks(done, u"Projekt"_s, u"Remont"_s).contains(u"[[Remont]]"));
        QCOMPARE(stripTokens(u"Przegląd ⏰ 2026-10-14 09:30"), u"Przegląd"_s);
    }
};

QTEST_GUILESS_MAIN(TestEdit)
#include "tst_markdown_edit.moc"
