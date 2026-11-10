// Dataview queries (docs/SYNTAX.md §10 "Dataview queries"): parseQuery and runQuery over a small
// Polish vault. "Today" is Wednesday 2026-10-14, 10:00 (weeks start on Monday).
#include "dataview/dataview.h"

#include <QElapsedTimer>
#include <QTest>
#include <QThread>

#include <functional>
#include <memory>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

const QDateTime Now(QDate(2026, 10, 14), QTime(10, 0));

QString lines(const QStringList &l)
{
    return l.join(u'\n');
}

void build(VaultIndex &index)
{
    index.update(u"Projekty/Dom.md"_s,
                 lines({u"---"_s, u"status: w toku"_s, u"budżet: 12000"_s, u"termin: 2026-10-20"_s,
                        u"tags: [projekt, dom]"_s, u"właściciel: \"[[Jan Kowalski]]\""_s, u"---"_s, u"# Dom"_s, u""_s,
                        u"Remont łazienki. [priorytet:: wysoki]"_s, u""_s,
                        u"- [ ] Kupić płytki 📅 2026-10-10 #zakupy"_s, u"- [x] Zamówić ekipę ✅ 2026-10-01"_s,
                        u"- [ ] Wybrać baterię [due:: 2026-10-25]"_s, u"  - [ ] Porównać ceny"_s}));
    index.update(u"Projekty/Ogród.md"_s,
                 lines({u"---"_s, u"status: planowany"_s, u"budżet: 3000"_s, u"tags: projekt"_s, u"---"_s,
                        u"koszt:: 250"_s, u""_s, u"- [ ] Posadzić tuje 📅 2026-10-15"_s, u"- Link do [[Dom]]."_s}));
    index.update(u"Projekty/Archiwum/Garaż.md"_s,
                 lines({u"---"_s, u"status: zrobione"_s, u"budżet: 8000"_s, u"tags: [projekt]"_s, u"---"_s,
                        u"- [x] Pomalować drzwi"_s}));
    index.update(u"Ludzie/Jan Kowalski.md"_s,
                 lines({u"telefon:: 123"_s, u"rola:: [[Projekty/Dom]], [[Ogród]]"_s, u""_s,
                        u"- Pracuje przy [[Dom]] #praca"_s}));
    index.update(u"journals/2026-10-14.md"_s,
                 lines({u"- Spotkanie z [[Jan Kowalski]] #praca"_s,
                        u"- [ ] Zadzwonić do hydraulika 📅 2026-10-14 ⏫"_s, u"- nastrój:: 7"_s}));
    index.update(u"Zapytania.md"_s, lines({u"```dataview"_s, u"LIST FROM #projekt"_s, u"```"_s}));
}

dataview::Options options()
{
    dataview::Options o;
    o.currentPath = u"Zapytania.md"_s;
    o.now = Now;
    return o;
}

/// The result, or a failed check with the message.
dataview::Result run(const VaultIndex &index, const QString &source, const dataview::Options &o = options())
{
    const auto parsed = dataview::parseQuery(source);
    if (const auto *error = std::get_if<QString>(&parsed)) {
        qWarning("parse error: %s", qPrintable(*error));
        return {};
    }
    const auto result = dataview::runQuery(std::get<dataview::Query>(parsed), index, o);
    if (const auto *error = std::get_if<QString>(&result)) {
        qWarning("run error: %s", qPrintable(*error));
        return {};
    }
    return std::get<dataview::Result>(result);
}

/// The message the query fails with, parsing or running; "" when it runs.
QString failure(const VaultIndex &index, const QString &source, const dataview::Options &o = options())
{
    const auto parsed = dataview::parseQuery(source);
    if (const auto *error = std::get_if<QString>(&parsed))
        return *error;
    const auto result = dataview::runQuery(std::get<dataview::Query>(parsed), index, o);
    const auto *error = std::get_if<QString>(&result);
    return error ? *error : QString();
}

/// `work` on a thread with half the stack of an Android thread (about 1 MB; more in sanitizer
/// builds, whose frames are larger); false when it did not finish in time.
bool runOnASmallStack(const std::function<void()> &work)
{
    std::unique_ptr<QThread> thread(QThread::create(work));
    thread->setStackSize(512 * 1024 * BRULION_TEST_SLOWDOWN);
    thread->start();
    if (!thread->wait(QDeadlineTimer(300'000))) {
        (void)thread.release(); // still running: left alone rather than aborting the test run
        return false;
    }
    return true;
}

/// 10 000 one-letter texts (padleft() makes at most 10 000 characters).
const QString Letters = u"split(padleft(\"\", 10000, \"ż\"), \"\")"_s;

/// The first cell of every row ("path" for task rows).
QStringList firstCells(const dataview::Result &r)
{
    QStringList out;
    for (const auto &row : r.rows)
        out.append(row.cells.isEmpty() ? row.path : row.cells.first());
    return out;
}

/// "group: item, item" per group (task texts for TASK).
QStringList groups(const dataview::Result &r)
{
    QStringList out;
    for (const auto &g : r.groups) {
        QStringList items;
        for (const auto &row : g.rows)
            items.append(row.task ? row.task->text : row.cells.value(0));
        out.append(g.label + u": "_s + items.join(u", "_s));
    }
    return out;
}

} // namespace

class TestDataview : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { build(m_index); }

    void parseErrors_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("message");
        QTest::newRow("empty") << u"  \n // tylko komentarz"_s << u"Write a query"_s;
        QTest::newRow("type") << u"SELECT x"_s << u"starts with LIST, TABLE or TASK, not “SELECT”"_s;
        QTest::newRow("calendar") << u"CALENDAR file.day"_s << u"CALENDAR views are not supported"_s;
        QTest::newRow("missing expression") << u"LIST\nWHERE"_s << u"Line 2: An expression is missing"_s;
        QTest::newRow("bad source") << u"LIST FROM 12"_s << u"Cannot read the source “12”"_s;
        QTest::newRow("csv") << u"TABLE x FROM csv(\"dane.csv\")"_s << u"csv() sources are not supported"_s;
        QTest::newRow("regex") << u"LIST WHERE regextest(\"a+\", file.name)"_s << u"“regextest” is not supported"_s;
        QTest::newRow("unknown function") << u"LIST WHERE żółw(1)"_s << u"Unknown function “żółw”"_s;
        QTest::newRow("arguments") << u"LIST WHERE length(1, 2)"_s << u"Wrong number of arguments for “length”"_s;
        QTest::newRow("limit zero") << u"LIST LIMIT 0"_s << u"LIMIT needs a whole number"_s;
        QTest::newRow("limit text") << u"LIST LIMIT dużo"_s << u"not “dużo”"_s;
        QTest::newRow("unclosed string") << u"LIST WHERE status = \"w toku"_s << u"A string is not closed"_s;
        QTest::newRow("unclosed link") << u"LIST FROM [[Dom"_s << u"A link is not closed"_s;
        QTest::newRow("from late") << u"LIST WHERE x\nFROM #projekt"_s << u"Line 2: FROM comes once"_s;
        QTest::newRow("unknown command") << u"LIST FROM #a\nGRUPUJ x"_s << u"Unknown command “GRUPUJ”"_s;
        QTest::newRow("unexpected") << u"LIST WHERE a ; b"_s << u"Unexpected “;”"_s;
        QTest::newRow("closing") << u"LIST WHERE (a = 1"_s << u"“)” is missing at the end"_s;
        QTest::newRow("as") << u"TABLE x AS"_s << u"A name is missing after AS"_s;
        QTest::newRow("bad date") << u"LIST WHERE file.day = date(2026-13-45)"_s << u"Cannot read the date “2026-13-45”"_s;
        QTest::newRow("bad duration") << u"LIST WHERE dur(3 lata świetlne) > 0"_s << u"Cannot read the duration"_s;
        QTest::newRow("deep") << u"LIST WHERE "_s + QString(200, u'(') + u'1' + QString(200, u')')
                              << u"nested too deeply"_s;
        // Past the limits (dataview.h) a query from a synced note is an error at its line, never a
        // stack overflow: each level is a recursion when it is evaluated, selected or freed.
        using dataview::MaxExpressionDepth, dataview::MaxExpressionNesting;
        QTest::newRow("long chain") << u"LIST\nWHERE 1"_s + u" + 1"_s.repeated(10000)
                                    << u"Line 2: The expression is too long: at most 256 operators"_s;
        QTest::newRow("one operator too many") << u"LIST WHERE 1"_s + u" + 1"_s.repeated(MaxExpressionDepth)
                                               << u"Line 1: The expression is too long"_s;
        QTest::newRow("long or chain") << u"TASK WHERE completed"_s + u" OR !completed"_s.repeated(10000)
                                       << u"too long"_s;
        QTest::newRow("long field chain") << u"LIST WHERE file"_s + u".name"_s.repeated(10000) << u"too long"_s;
        QTest::newRow("long index chain") << u"LIST WHERE file.tasks"_s + u"[0]"_s.repeated(10000) << u"too long"_s;
        QTest::newRow("long FROM chain") << u"LIST FROM #projekt"_s + u" OR #dom"_s.repeated(10000) << u"too long"_s;
        QTest::newRow("long FROM and") << u"LIST\nFROM \"Projekty\""_s + u" AND -#zakupy"_s.repeated(10000)
                                       << u"Line 2: The expression is too long"_s;
        QTest::newRow("one parenthesis too many")
                << u"LIST WHERE "_s + QString(MaxExpressionNesting, u'(') + u'1' + QString(MaxExpressionNesting, u')')
                << u"nested too deeply"_s;
        QTest::newRow("negations") << u"LIST WHERE "_s + QString(10000, u'!') + u"completed"_s
                                   << u"nested too deeply"_s;
        QTest::newRow("minus signs") << u"LIST WHERE "_s + QString(10000, u'-') + u"1"_s << u"nested too deeply"_s;
        QTest::newRow("lists in lists") << u"LIST WHERE "_s + u"[ "_s.repeated(10000) << u"nested too deeply"_s;
        QTest::newRow("calls in calls") << u"LIST WHERE "_s + u"lower("_s.repeated(10000) << u"nested too deeply"_s;
        QTest::newRow("indexes in indexes") << u"LIST WHERE "_s + u"a["_s.repeated(10000) << u"nested too deeply"_s;
        QTest::newRow("lambdas in lambdas")
                << u"LIST WHERE map(list(1), "_s + u"(x) => "_s.repeated(10000) << u"nested too deeply"_s;
        QTest::newRow("FROM parentheses")
                << u"LIST FROM "_s + QString(10000, u'(') + u"#projekt"_s << u"nested too deeply"_s;
        QTest::newRow("FROM negations") << u"LIST FROM "_s + QString(10000, u'-') + u"#projekt"_s
                                        << u"nested too deeply"_s;
    }
    void parseErrors()
    {
        QFETCH(QString, source);
        QFETCH(QString, message);
        const auto r = dataview::parseQuery(source);
        const auto *error = std::get_if<QString>(&r);
        QVERIFY2(error, qPrintable(source));
        QVERIFY2(error->contains(message), qPrintable(*error));
    }

    void parses()
    {
        const auto r = dataview::parseQuery(u"table without id file.link AS \"Strona\", status\n"
                                            u"// komentarz\nfrom #Projekt or \"Ludzie\"\nwhere budżet > 1000\n"
                                            u"sort budżet desc, file.name\nlimit 5"_s);
        QVERIFY(std::holds_alternative<dataview::Query>(r));
        const auto &q = std::get<dataview::Query>(r);
        QCOMPARE(q.type, dataview::QueryType::Table);
        QVERIFY(q.withoutId);
        QCOMPARE(q.columns.size(), 2);
        QCOMPARE(q.columns.at(0).second, u"Strona"_s);
        QCOMPARE(q.columns.at(1).second, u"status"_s);
        QVERIFY(q.from);
        QCOMPARE(q.commands.size(), 3);
    }

    void listFromTag()
    {
        const auto r = run(m_index, u"LIST FROM #projekt"_s);
        QCOMPARE(r.type, dataview::QueryType::List);
        QCOMPARE(firstCells(r), (QStringList{u"[[Garaż]]"_s, u"[[Dom]]"_s, u"[[Ogród]]"_s}));
        QCOMPARE(r.total, 3);
        // Keywords and tags in any case; a tag also finds its sub-tags.
        QCOMPARE(firstCells(run(m_index, u"list from #PROJEKT"_s)).size(), 3);
        QCOMPARE(firstCells(run(m_index, u"LIST FROM #praca"_s)),
                 (QStringList{u"[[Jan Kowalski]]"_s, u"[[2026-10-14]]"_s}));
    }

    void sources_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QStringList>("pages");
        QTest::newRow("folder") << u"LIST FROM \"Projekty\""_s
                                << QStringList{u"[[Garaż]]"_s, u"[[Dom]]"_s, u"[[Ogród]]"_s};
        QTest::newRow("folder minus folder")
                << u"LIST FROM \"Projekty\" AND -\"Projekty/Archiwum\""_s << QStringList{u"[[Dom]]"_s, u"[[Ogród]]"_s};
        QTest::newRow("file") << u"LIST FROM \"Projekty/Dom\""_s << QStringList{u"[[Dom]]"_s};
        QTest::newRow("file with .md") << u"LIST FROM \"Projekty/Dom.md\""_s << QStringList{u"[[Dom]]"_s};
        QTest::newRow("folder in other case") << u"LIST FROM \"ludzie/\""_s << QStringList{u"[[Jan Kowalski]]"_s};
        QTest::newRow("links to") << u"LIST FROM [[Dom]]"_s
                                  << QStringList{u"[[Jan Kowalski]]"_s, u"[[Ogród]]"_s};
        QTest::newRow("this page") << u"LIST FROM [[]]"_s << QStringList{};
        QTest::newRow("outgoing") << u"LIST FROM outgoing([[Jan Kowalski]])"_s
                                  << QStringList{u"[[Dom]]"_s, u"[[Ogród]]"_s};
        QTest::newRow("or") << u"LIST FROM #dom OR \"Ludzie\""_s << QStringList{u"[[Jan Kowalski]]"_s, u"[[Dom]]"_s};
        QTest::newRow("not tag") << u"LIST FROM \"Projekty\" AND !#dom"_s << QStringList{u"[[Garaż]]"_s, u"[[Ogród]]"_s};
        QTest::newRow("parentheses") << u"LIST FROM (#praca OR #dom) AND -\"journals\""_s
                                     << QStringList{u"[[Jan Kowalski]]"_s, u"[[Dom]]"_s};
        QTest::newRow("missing page") << u"LIST FROM [[Nie ma takiej]]"_s << QStringList{};
        // A long list of alternatives, as deep as a FROM may go (dataview.h).
        QString tags;
        for (int i = 0; i < dataview::MaxExpressionDepth - 1; ++i)
            tags += u"#żadna-%1 OR "_s.arg(i);
        QTest::newRow("long OR") << u"LIST FROM "_s + tags + u"(#projekt AND -#dom)"_s
                                 << QStringList{u"[[Garaż]]"_s, u"[[Ogród]]"_s};
    }
    void sources()
    {
        QFETCH(QString, source);
        QFETCH(QStringList, pages);
        QCOMPARE(firstCells(run(m_index, source)), pages);
    }

    void table()
    {
        const auto r = run(m_index, u"TABLE status, budżet AS \"Budżet\" FROM #projekt WHERE budżet > 5000 SORT budżet DESC"_s);
        QCOMPARE(r.headers, (QStringList{u"File"_s, u"status"_s, u"Budżet"_s}));
        QCOMPARE(r.rows.size(), 2);
        QCOMPARE(r.rows.at(0).cells, (QStringList{u"[[Dom]]"_s, u"w toku"_s, u"12000"_s}));
        QCOMPARE(r.rows.at(1).cells, (QStringList{u"[[Garaż]]"_s, u"zrobione"_s, u"8000"_s}));
        QCOMPARE(r.rows.at(0).path, u"Projekty/Dom.md"_s);
        const auto without = run(m_index, u"TABLE WITHOUT ID file.name, termin FROM \"Projekty/Dom\""_s);
        QCOMPARE(without.headers, (QStringList{u"file.name"_s, u"termin"_s}));
        QCOMPARE(without.rows.at(0).cells, (QStringList{u"Dom"_s, u"2026-10-20"_s}));
        // A missing value.
        QCOMPARE(run(m_index, u"TABLE koszt FROM \"Projekty/Dom\""_s).rows.at(0).cells.at(1), u"–"_s);
    }

    void inlineFields()
    {
        QCOMPARE(firstCells(run(m_index, u"LIST WHERE koszt"_s)), QStringList{u"[[Ogród]]"_s});
        QCOMPARE(firstCells(run(m_index, u"LIST WHERE priorytet = \"wysoki\""_s)), QStringList{u"[[Dom]]"_s});
        QCOMPARE(firstCells(run(m_index, u"LIST WITHOUT ID nastrój + 1 FROM \"journals\""_s)), QStringList{u"8"_s});
        QCOMPARE(firstCells(run(m_index, u"LIST WITHOUT ID telefon FROM \"Ludzie\""_s)), QStringList{u"123"_s});
    }

    void links()
    {
        QCOMPARE(firstCells(run(m_index, u"LIST WHERE właściciel = [[Jan Kowalski]]"_s)), QStringList{u"[[Dom]]"_s});
        QCOMPARE(firstCells(run(m_index, u"LIST WHERE contains(rola, [[Ogród]])"_s)),
                 QStringList{u"[[Jan Kowalski]]"_s});
        QCOMPARE(firstCells(run(m_index, u"LIST rola FROM \"Ludzie\""_s)),
                 QStringList{u"[[Jan Kowalski]]: [[Dom]], [[Ogród]]"_s});
        QCOMPARE(firstCells(run(m_index, u"LIST WHERE contains(file.outlinks, [[Dom]])"_s)),
                 (QStringList{u"[[Jan Kowalski]]"_s, u"[[Ogród]]"_s}));
        QCOMPARE(firstCells(run(m_index, u"LIST WITHOUT ID length(file.inlinks) FROM \"Projekty/Dom\""_s)),
                 QStringList{u"2"_s});
    }

    void tasks()
    {
        const auto r = run(m_index, u"TASK FROM #projekt WHERE !completed"_s);
        QCOMPARE(r.type, dataview::QueryType::Task);
        QCOMPARE(groups(r), (QStringList{u"Dom: Kupić płytki #zakupy, Wybrać baterię [due:: 2026-10-25], Porównać ceny"_s,
                                         u"Ogród: Posadzić tuje"_s}));
        QCOMPARE(r.groups.first().key, u"Projekty/Dom.md"_s);
        QCOMPARE(r.total, 4);
        const auto &first = r.groups.first().rows.first();
        QVERIFY(first.task);
        QCOMPARE(first.task->line, 11);
        QVERIFY(first.blockIndex >= 0);
        // Dates of the task and its own [due:: …] field; the page's fields still readable.
        QCOMPARE(groups(run(m_index, u"TASK WHERE due AND due <= date(today) AND !completed"_s)),
                 (QStringList{u"Dom: Kupić płytki #zakupy"_s, u"2026-10-14: Zadzwonić do hydraulika"_s}));
        QCOMPARE(groups(run(m_index, u"TASK WHERE due = date(2026-10-25)"_s)), QStringList{u"Dom: Wybrać baterię [due:: 2026-10-25]"_s});
        QCOMPARE(groups(run(m_index, u"TASK WHERE status = \"x\""_s)),
                 (QStringList{u"Garaż: Pomalować drzwi"_s, u"Dom: Zamówić ekipę"_s}));
        QCOMPARE(groups(run(m_index, u"TASK WHERE contains(tags, \"#zakupy\")"_s)), QStringList{u"Dom: Kupić płytki #zakupy"_s});
        QCOMPARE(groups(run(m_index, u"TASK WHERE file.name = \"Ogród\""_s)), QStringList{u"Ogród: Posadzić tuje"_s});
        QCOMPARE(groups(run(m_index, u"TASK WHERE completion = date(2026-10-01)"_s)), QStringList{u"Dom: Zamówić ekipę"_s});
        QCOMPARE(groups(run(m_index, u"TASK WHERE length(children) > 0"_s)), QStringList{u"Dom: Wybrać baterię [due:: 2026-10-25]"_s});
        // Grouped by an expression.
        QCOMPARE(groups(run(m_index, u"TASK FROM \"Projekty\" WHERE !completed GROUP BY file.folder"_s)),
                 QStringList{u"Projekty: Kupić płytki #zakupy, Wybrać baterię [due:: 2026-10-25], Porównać ceny, Posadzić tuje"_s});
    }

    void groupBy()
    {
        const auto t = run(m_index, u"TABLE length(rows) AS Liczba, sum(rows.budżet) AS Razem FROM #projekt GROUP BY status"_s);
        QCOMPARE(t.headers, (QStringList{u"status"_s, u"Liczba"_s, u"Razem"_s}));
        QCOMPARE(t.rows.size(), 3);
        QCOMPARE(t.rows.at(0).cells, (QStringList{u"planowany"_s, u"1"_s, u"3000"_s}));
        QCOMPARE(t.rows.at(2).cells, (QStringList{u"zrobione"_s, u"1"_s, u"8000"_s}));
        QCOMPARE(t.rows.at(0).path, QString());
        const auto l = run(m_index, u"LIST FROM \"Projekty\" GROUP BY file.folder AS Folder SORT Folder DESC"_s);
        QCOMPARE(groups(l), (QStringList{u"Projekty/Archiwum: [[Garaż]]"_s, u"Projekty: [[Dom]], [[Ogród]]"_s}));
        const auto withValue = run(m_index, u"LIST length(rows) FROM #projekt GROUP BY budżet > 5000"_s);
        QCOMPARE(firstCells(withValue), (QStringList{u"false: 1"_s, u"true: 2"_s}));
        const auto named = run(m_index, u"TABLE rows.file.link AS Strony FROM #projekt GROUP BY budżet > 5000 AS Duże"_s);
        QCOMPARE(named.headers.first(), u"Duże"_s);
        QCOMPARE(named.rows.at(1).cells.at(1), u"[[Garaż]], [[Dom]]"_s);
    }

    void flatten()
    {
        const auto r = run(m_index, u"TABLE T.text AS Zadanie FROM \"Projekty/Dom\" FLATTEN file.tasks AS T WHERE !T.completed"_s);
        QCOMPARE(r.rows.size(), 3);
        QCOMPARE(r.rows.at(0).cells, (QStringList{u"[[Dom]]"_s, u"Kupić płytki #zakupy"_s}));
        const auto tags = run(m_index, u"LIST WITHOUT ID file.etags FROM \"Projekty/Dom\" FLATTEN file.etags SORT file.etags"_s);
        QCOMPARE(firstCells(tags), (QStringList{u"#dom"_s, u"#projekt"_s, u"#zakupy"_s}));
        // A value that is no list: the row stays.
        QCOMPARE(run(m_index, u"LIST FROM #projekt FLATTEN status"_s).rows.size(), 3);
    }

    void expressions_data()
    {
        QTest::addColumn<QString>("expr");
        QTest::addColumn<QString>("value");
        QTest::newRow("tasks") << u"length(file.tasks)"_s << u"4"_s;
        QTest::newRow("lists") << u"length(file.lists)"_s << u"4"_s;
        QTest::newRow("lower") << u"lower(\"ĄĆĘŁŃÓŚŹŻ\")"_s << u"ąćęłńóśźż"_s;
        QTest::newRow("upper list") << u"upper(list(\"ż\", \"ó\"))"_s << u"Ż, Ó"_s;
        QTest::newRow("etags") << u"join(sort(file.etags), \" \")"_s << u"#dom #projekt #zakupy"_s;
        QTest::newRow("round") << u"round(budżet / 7, 2)"_s << u"1714.29"_s;
        QTest::newRow("choice") << u"choice(budżet > 10000, \"duży\", \"mały\")"_s << u"duży"_s;
        QTest::newRow("default") << u"default(brak, \"—\")"_s << u"—"_s;
        QTest::newRow("typeof date") << u"typeof(termin)"_s << u"date"_s;
        QTest::newRow("typeof number") << u"typeof(budżet)"_s << u"number"_s;
        QTest::newRow("typeof link") << u"typeof(właściciel)"_s << u"link"_s;
        QTest::newRow("typeof list") << u"typeof(file.tags)"_s << u"array"_s;
        QTest::newRow("map") << u"map(list(1, 2, 3), (x) => x * 2)"_s << u"2, 4, 6"_s;
        QTest::newRow("filter") << u"filter(file.tasks.text, (t) => contains(t, \"Kupić\"))"_s << u"Kupić płytki #zakupy"_s;
        QTest::newRow("filter tasks") << u"length(filter(file.tasks, (t) => !t.completed))"_s << u"3"_s;
        QTest::newRow("reduce") << u"reduce(list(1, 2, 3), (a, b) => a + b)"_s << u"6"_s;
        QTest::newRow("reduce op") << u"reduce(list(2, 3, 4), \"*\")"_s << u"24"_s;
        QTest::newRow("dateformat") << u"dateformat(termin, \"dd.MM.yyyy\")"_s << u"20.10.2026"_s;
        QTest::newRow("dateformat names") << u"dateformat(termin, \"EEEE, d MMMM 'r.'\")"_s << u"Tuesday, 20 October r."_s;
        // (No translator here: the app's English catalogue gives "6 days".)
        QTest::newRow("date minus date") << u"termin - date(2026-10-14)"_s << u"6 day(s)"_s;
        QTest::newRow("date minus duration") << u"termin - dur(3 days)"_s << u"2026-10-17"_s;
        QTest::newRow("duration words") << u"dur(1h 30m)"_s << u"1 hour(s), 30 minute(s)"_s;
        QTest::newRow("duration string") << u"dur(\"2 tygodnie\")"_s << u"14 day(s)"_s;
        QTest::newRow("duration days") << u"(termin - date(today)).days"_s << u"6"_s;
        QTest::newRow("today") << u"date(today) + dur(1 week)"_s << u"2026-10-21"_s;
        QTest::newRow("now") << u"date(now)"_s << u"2026-10-14 10:00"_s;
        QTest::newRow("striptime") << u"striptime(date(now))"_s << u"2026-10-14"_s;
        QTest::newRow("sow") << u"date(sow)"_s << u"2026-10-12"_s;
        QTest::newRow("eom") << u"date(eom)"_s << u"2026-10-31"_s;
        QTest::newRow("date string") << u"date(\"2026-10-15 09:30\").hour"_s << u"9"_s;
        QTest::newRow("date format") << u"date(\"15.10.2026\", \"dd.MM.yyyy\")"_s << u"2026-10-15"_s;
        QTest::newRow("date of link") << u"date([[2026-10-14]])"_s << u"2026-10-14"_s;
        QTest::newRow("weekday") << u"termin.weekday"_s << u"2"_s;
        QTest::newRow("concat") << u"\"a\" + 1"_s << u"a1"_s;
        QTest::newRow("contains") << u"contains(file.name, \"Do\")"_s << u"true"_s;
        QTest::newRow("contains case") << u"contains(file.name, \"do\")"_s << u"false"_s;
        QTest::newRow("icontains") << u"icontains(file.name, \"do\")"_s << u"true"_s;
        QTest::newRow("contains list") << u"contains(list(\"abc\"), \"b\")"_s << u"true"_s;
        QTest::newRow("econtains") << u"econtains(list(\"abc\"), \"b\")"_s << u"false"_s;
        QTest::newRow("containsword") << u"containsword(\"Remont łazienki\", \"łazienki\")"_s << u"true"_s;
        QTest::newRow("sum") << u"sum(list(1, 2, 3.5))"_s << u"6.5"_s;
        QTest::newRow("average") << u"average(list(1, 2, 3))"_s << u"2"_s;
        QTest::newRow("min") << u"min(3, 1, 2)"_s << u"1"_s;
        QTest::newRow("max list") << u"max(list(3, 7, 2))"_s << u"7"_s;
        QTest::newRow("string link") << u"string(file.link)"_s << u"[[Dom]]"_s;
        QTest::newRow("padleft") << u"padleft(\"7\", 3, \"0\")"_s << u"007"_s;
        QTest::newRow("truncate") << u"truncate(\"Remont łazienki\", 8)"_s << u"Remon..."_s;
        QTest::newRow("split index") << u"split(\"a,b,c\", \",\")[1]"_s << u"b"_s;
        QTest::newRow("replace") << u"replace(\"żółw\", \"ó\", \"o\")"_s << u"żołw"_s;
        QTest::newRow("substring") << u"substring(\"Kraków\", 0, 3)"_s << u"Kra"_s;
        QTest::newRow("startswith") << u"startswith(file.path, \"Projekty/\")"_s << u"true"_s;
        QTest::newRow("meta") << u"meta([[Dom#Dom]]).type"_s << u"header"_s;
        QTest::newRow("this") << u"this.file.name"_s << u"Zapytania"_s;
        QTest::newRow("frontmatter") << u"file.frontmatter.status"_s << u"w toku"_s;
        QTest::newRow("row index") << u"row[\"budżet\"]"_s << u"12000"_s;
        QTest::newRow("link field") << u"[[Jan Kowalski]].telefon"_s << u"123"_s;
        QTest::newRow("link of field") << u"właściciel.rola"_s << u"[[Dom]], [[Ogród]]"_s;
        QTest::newRow("null") << u"null"_s << u"–"_s;
        QTest::newRow("list literal") << u"[1, 2][0]"_s << u"1"_s;
        QTest::newRow("negative") << u"-budżet"_s << u"-12000"_s;
        QTest::newRow("modulo") << u"5 % 3"_s << u"2"_s;
        QTest::newRow("not") << u"!true"_s << u"false"_s;
        QTest::newRow("any") << u"any(list(0, 1))"_s << u"true"_s;
        QTest::newRow("all lambda") << u"all(list(2, 4), (x) => x % 2 = 0)"_s << u"true"_s;
        QTest::newRow("none") << u"none(list(0, false))"_s << u"true"_s;
        QTest::newRow("nonnull") << u"nonnull(1, null, 2)"_s << u"1, 2"_s;
        QTest::newRow("flat") << u"flat(list(1, list(2, list(3))))"_s << u"1, 2, 3"_s;
        QTest::newRow("slice") << u"slice(list(1, 2, 3, 4), -2)"_s << u"3, 4"_s;
        QTest::newRow("unique") << u"unique(list(1, 1, 2))"_s << u"1, 2"_s;
        QTest::newRow("reverse") << u"reverse(\"żółw\")"_s << u"włóż"_s;
        QTest::newRow("minby") << u"minby(file.tasks, (t) => t.line).text"_s << u"Kupić płytki #zakupy"_s;
        QTest::newRow("sort by key") << u"sort(list(\"bb\", \"a\", \"ccc\"), (s) => length(s))"_s << u"a, bb, ccc"_s;
        QTest::newRow("number") << u"number(\"ok. 12,5 zł\")"_s << u"12"_s;
        QTest::newRow("extract") << u"extract(file, \"name\").name"_s << u"Dom"_s;
        QTest::newRow("object") << u"object(\"a\", 1).a"_s << u"1"_s;
        QTest::newRow("display") << u"display(właściciel)"_s << u"Jan Kowalski"_s;
        QTest::newRow("elink") << u"elink(\"https://example.com\", \"strona\")"_s << u"[strona](https://example.com)"_s;
        QTest::newRow("compare dates") << u"termin > date(today)"_s << u"true"_s;
        QTest::newRow("null sorts first") << u"brak < 1"_s << u"true"_s;
        QTest::newRow("file.day") << u"file.day"_s << u"–"_s;
        QTest::newRow("folder") << u"file.folder"_s << u"Projekty"_s;
        QTest::newRow("ext") << u"file.ext"_s << u"md"_s;
        QTest::newRow("size") << u"file.size > 100"_s << u"true"_s;
        QTest::newRow("section") << u"file.tasks[0].section"_s << u"[[Dom#Dom|Dom > Dom]]"_s;
        QTest::newRow("task due") << u"file.tasks[0].due"_s << u"2026-10-10"_s;
        // As deep and as nested as allowed (dataview.h): evaluated as ever.
        using dataview::MaxExpressionDepth, dataview::MaxExpressionNesting;
        QTest::newRow("longest sum") << u"1"_s + u" + 1"_s.repeated(MaxExpressionDepth - 1)
                                     << QString::number(MaxExpressionDepth);
        QTest::newRow("deepest parentheses")
                << QString(MaxExpressionNesting - 1, u'(') + u"2 * 3"_s + QString(MaxExpressionNesting - 1, u')')
                << u"6"_s;
        QTest::newRow("deepest calls") << u"lower("_s.repeated(MaxExpressionNesting - 1) + u"\"ŻÓŁW\""_s
                                                  + QString(MaxExpressionNesting - 1, u')')
                                       << u"żółw"_s;
        QString names;
        for (int i = 0; i < 100; ++i)
            names += u"file.name = \"Strona %1\" OR "_s.arg(i);
        QTest::newRow("long or") << names + u"file.name = \"Dom\""_s << u"true"_s;
        // Values nested as deep as allowed (value.h): shown, compared, freed.
        const auto nested = [](int levels) {
            return u"reduce(split(padleft(\"\", %1, \"ż\"), \"\"), (a, b) => list(a))"_s.arg(levels + 1);
        };
        QTest::newRow("deepest list") << nested(dataview::MaxValueDepth) << u"ż"_s;
        QTest::newRow("deepest lists compared")
                << u"length(unique(list(%1, %1)))"_s.arg(nested(dataview::MaxValueDepth - 1)) << u"1"_s;
        QTest::newRow("deepest list as text")
                << u"string(%1) + \"!\""_s.arg(nested(dataview::MaxValueDepth)) << u"ż!"_s;
        QTest::newRow("lambdas in lambdas")
                << u"typeof(reduce(split(padleft(\"\", 60, \"ż\"), \"\"), (a, b) => (x) => a))"_s << u"function"_s;
        QTest::newRow("list doubled 10 times")
                << u"length(reduce(map(slice(%1, 0, 11), (x) => list(x)), (a, b) => a + a))"_s.arg(Letters)
                << u"1024"_s;
    }
    void expressions()
    {
        QFETCH(QString, expr);
        QFETCH(QString, value);
        const auto r = run(m_index, u"LIST WITHOUT ID "_s + expr + u"\nFROM \"Projekty/Dom\""_s);
        QCOMPARE(firstCells(r), QStringList{value});
    }

    void journalDay()
    {
        QCOMPARE(firstCells(run(m_index, u"LIST WHERE file.day = date(today)"_s)), QStringList{u"[[2026-10-14]]"_s});
        QCOMPARE(firstCells(run(m_index, u"LIST WHERE file.day >= date(sow) AND file.day <= date(eow)"_s)),
                 QStringList{u"[[2026-10-14]]"_s});
    }

    void sortAndLimit()
    {
        QCOMPARE(firstCells(run(m_index, u"LIST FROM #projekt SORT file.name DESC LIMIT 2"_s)),
                 (QStringList{u"[[Ogród]]"_s, u"[[Garaż]]"_s}));
        // Commands one after another: LIMIT before SORT keeps the first rows (by path) only.
        QCOMPARE(firstCells(run(m_index, u"LIST FROM #projekt LIMIT 2 SORT file.name DESC"_s)),
                 (QStringList{u"[[Garaż]]"_s, u"[[Dom]]"_s}));
        // Missing values sort first ascending, last descending.
        QCOMPARE(firstCells(run(m_index, u"LIST FROM \"Projekty\" SORT koszt DESC"_s)).first(), u"[[Ogród]]"_s);
    }

    void defaultLimit()
    {
        auto o = options();
        o.defaultLimit = 2;
        const auto r = run(m_index, u"LIST FROM #projekt"_s, o);
        QCOMPARE(r.rows.size(), 2);
        QCOMPARE(r.total, 3);
        const auto t = run(m_index, u"TASK"_s, o);
        int shown = 0;
        for (const auto &g : t.groups)
            shown += int(g.rows.size());
        QCOMPARE(shown, 2);
        QCOMPARE(t.total, 7);
    }

    void tooCostly()
    {
        auto o = options();
        o.maxSteps = 50;
        const auto parsed = dataview::parseQuery(u"LIST WHERE length(filter(file.tasks, (t) => !t.completed)) >= 0"_s);
        const auto r = dataview::runQuery(std::get<dataview::Query>(parsed), m_index, o);
        const auto *error = std::get_if<QString>(&r);
        QVERIFY(error);
        QVERIFY2(error->contains(u"takes too long"_s), qPrintable(*error));
    }

    void tooManyRows()
    {
        const auto parsed = dataview::parseQuery(u"LIST FLATTEN map(slice(list(0,0,0,0,0,0,0,0,0,0), 0), (x) => 1) AS a "
                                                 u"FLATTEN list(1,2,3,4,5,6,7,8,9,10) AS b FLATTEN list(1,2,3,4,5,6,7,8,9,10) AS c "
                                                 u"FLATTEN list(1,2,3,4,5,6,7,8,9,10) AS d FLATTEN list(1,2,3,4,5,6,7,8,9,10) AS e"_s);
        QVERIFY(std::holds_alternative<dataview::Query>(parsed));
        const auto r = dataview::runQuery(std::get<dataview::Query>(parsed), m_index, options());
        const auto *error = std::get_if<QString>(&r);
        QVERIFY(error);
        QVERIFY2(error->contains(u"more than"_s), qPrintable(*error));
    }

    void valueLimits_data()
    {
        QTest::addColumn<QString>("expr");
        QTest::addColumn<QString>("message");
        // reduce() feeding what it makes back into itself (a query from a synced note): past the limits
        // of value.h an error, never a stack overflow, an endless rendering or all the memory.
        QTest::newRow("list doubled") << u"length(reduce(map(%1, (x) => list(x)), (a, b) => a + a))"_s.arg(Letters)
                                      << u"A value is too large: more than 1000000 items or 10000000 characters."_s;
        QTest::newRow("text doubled") << u"length(reduce(%1, (a, b) => a + a))"_s.arg(Letters) << u"too large"_s;
        QTest::newRow("list in a list") << u"length(reduce(%1, (a, b) => list(a)))"_s.arg(Letters)
                                        << u"A list, object or lambda is nested more than 64 levels deep."_s;
        QTest::newRow("list of itself twice")
                << u"length(reduce(%1, (a, b) => list(a, a)))"_s.arg(Letters) << u"too large"_s;
        QTest::newRow("object in an object") << u"reduce(%1, (a, b) => object(\"klucz\", a)).klucz"_s.arg(Letters)
                                             << u"nested more than 64 levels"_s;
        QTest::newRow("lambda in a lambda")
                << u"typeof(reduce(%1, (a, b) => (x) => a))"_s.arg(Letters) << u"nested more than 64 levels"_s;
        QTest::newRow("long separator") << u"join(%1, padleft(\"\", 10000, \"ą\"))"_s.arg(Letters) << u"too large"_s;
        QTest::newRow("long text split") << u"length(split(reduce(slice(%1, 0, 24), (a, b) => a + a), \"\"))"_s.arg(
                Letters) << u"too large"_s;
        QTest::newRow("page shown often") << u"length(string(map(%1, (x) => row)))"_s.arg(Letters) << u"too large"_s;
        QTest::newRow("lambda calling itself")
                << u"reduce(list((f) => map(list(f), f), 0), (g, x) => map(list(g), g))"_s << u"nested too deeply"_s;
    }
    void valueLimits()
    {
        QFETCH(QString, expr);
        QFETCH(QString, message);
        const QString error = failure(m_index, u"LIST WITHOUT ID "_s + expr + u"\nFROM \"Projekty/Dom\""_s);
        QVERIFY2(error.contains(message), qPrintable(error));
    }

    void copyingIsWork_data()
    {
        QTest::addColumn<QString>("expr");
        QTest::addColumn<qint64>("maxSteps");
        // 100 000 one-letter texts, and reduce() copying what it made again and again (a list or a
        // text one longer each time: the square of the items, 5·10^9 copies). Copies cost steps, so
        // each stops with the error of a query too costly — soon, not after minutes. (The others
        // with a tenth of the steps, for a quicker test.)
        const QString many = u"flat(map(slice(%1, 0, 10), (x) => %1))"_s.arg(Letters);
        const QString lists = u"map(%1, (x) => list(x))"_s.arg(many);
        const qint64 all = dataview::Options().maxSteps, tenth = all / 10;
        QTest::newRow("list grown by one") << u"length(reduce(%1, (a, b) => a + b))"_s.arg(lists) << all;
        QTest::newRow("list sorted again") << u"length(reduce(%1, (a, b) => sort(a + b)))"_s.arg(lists) << tenth;
        QTest::newRow("list sliced again") << u"length(reduce(%1, (a, b) => slice(a + b, 0)))"_s.arg(lists) << tenth;
        QTest::newRow("list reversed again") << u"length(reduce(%1, (a, b) => reverse(a + b)))"_s.arg(lists) << tenth;
        QTest::newRow("text grown by one") << u"length(reduce(%1, (a, b) => a + b))"_s.arg(many) << tenth;
        QTest::newRow("text split again")
                << u"length(reduce(%1, (a, b) => join(split(a + b, \"\"), \"\")))"_s.arg(many) << tenth;
        QTest::newRow("text upper again") << u"length(reduce(%1, (a, b) => upper(a + b)))"_s.arg(many) << tenth;
    }
    void copyingIsWork()
    {
        QFETCH(QString, expr);
        QFETCH(qint64, maxSteps);
        auto o = options();
        o.maxSteps = maxSteps;
        QElapsedTimer timer;
        timer.start();
        const QString error = failure(m_index, u"LIST WITHOUT ID "_s + expr + u"\nFROM \"Projekty/Dom\""_s, o);
        QVERIFY2(error.contains(u"takes too long"_s), qPrintable(error));
        QVERIFY2(timer.elapsed() < 5000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(timer.elapsed())));
    }

    void keptValuesStayBounded()
    {
        // A list of 10 000 new values for each of 10 000 items or 150 rows, all kept until the end
        // (map()'s results, the keys sorted or grouped by): too large, not 10^8 values in memory.
        VaultIndex index;
        for (int i = 0; i < 150; ++i)
            index.update(u"Strona %1.md"_s.arg(i), u"# Strona %1\n\nTreść ąę."_s.arg(i));
        const QString big = u"split(padleft(\"\", 10000, \"ą\"), \"\")"_s;
        const QStringList sources{u"LIST WITHOUT ID length(map(%1, (x) => %2))"_s.arg(Letters, big),
                                  u"LIST WITHOUT ID length(sort(%1, (x) => %2))"_s.arg(Letters, big),
                                  u"LIST SORT list(file.name, %1)"_s.arg(big),
                                  u"LIST GROUP BY list(file.name, %1)"_s.arg(big)};
        for (const QString &source : sources) {
            const QString error = failure(index, source);
            QVERIFY2(error.contains(u"A value is too large"_s), qPrintable(source.left(40) + u": "_s + error));
        }
    }

    void resultSizeIsBounded()
    {
        // What a result shows holds at most MaxValueItems cells, labels and tasks with MaxTextLength
        // characters in all: a page shown whole in many cells of many rows is an error, not gigabytes.
        VaultIndex index;
        QString tasks;
        for (int i = 0; i < 200; ++i)
            tasks += u"- [ ] zadanie %1 ąćęłńóśźż\n"_s.arg(i);
        index.update(u"Duża.md"_s, tasks);
        dataview::Options o = options();
        o.currentPath = u"Duża.md"_s;
        const QString rows = u" FLATTEN split(padleft(\"\", 50, \"x\"), \"\") AS x"_s;
        const QString error = failure(index, u"TABLE this, this, this"_s + rows, o);
        QCOMPARE(error, u"The results are too large: more than 1000000 items or 10000000 characters."_s);
        QVERIFY(failure(index, u"TABLE this, this, this"_s + rows + u" LIMIT 10"_s, o).isEmpty());
        // Only the rows shown are made, all of them are counted (3000 cells of 10 000 characters would
        // be too many).
        const auto r = run(index,
                           u"TABLE WITHOUT ID padleft(x, 10000, \"ż\") "
                           u"FLATTEN split(padleft(\"\", 3000, \"x\"), \"\") AS x"_s,
                           o);
        QCOMPARE(r.rows.size(), dataview::DefaultRowLimit);
        QCOMPARE(r.total, 3000);
    }

    void manyFieldsStayFast()
    {
        // A synced note repeating one key 50 000 times next to 10 000 other keys: its fields are read
        // in one pass (copying the key's list on every repeat took the square of that).
        QString note = u"# Pola\n\n"_s;
        for (int i = 0; i < 50000; ++i)
            note += u"nastrój:: %1\n"_s.arg(i % 10);
        note += u"\n"_s;
        for (int i = 0; i < 10000; ++i)
            note += u"klucz-%1:: ż%1\n"_s.arg(i);
        VaultIndex index;
        index.update(u"Pola.md"_s, note);
        QElapsedTimer timer;
        timer.start();
        const auto r = run(index, u"TABLE WITHOUT ID length(nastrój), nastrój[49999], klucz-9999"_s);
        QCOMPARE(r.rows.size(), 1);
        QCOMPARE(r.rows.at(0).cells, (QStringList{u"50000"_s, u"9"_s, u"ż9999"_s}));
        QVERIFY2(timer.elapsed() < 3000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(timer.elapsed())));
    }

    void fieldOfAListStaysBounded()
    {
        // A page counts as one value: the list of one page 10 000 times is small, its field (each
        // time the page's 10 000 numbers) would be 10^8 values — an error before it is gathered.
        VaultIndex index;
        QStringList numbers;
        for (int i = 0; i < 10000; ++i)
            numbers.append(QString::number(i));
        index.update(u"Liczby.md"_s, u"---\nliczby: ["_s + numbers.join(u", "_s) + u"]\n---\n# Liczby"_s);
        QCOMPARE(firstCells(run(index, u"LIST WITHOUT ID length(liczby)"_s)), QStringList{u"10000"_s});
        const QString error = failure(index, u"LIST WITHOUT ID length(map(%1, (x) => row).liczby)"_s.arg(Letters));
        QVERIFY2(error.contains(u"too large"_s), qPrintable(error));
    }

    void deepOutlineStaysBounded()
    {
        // A synced note with tasks nested 1000 levels deep: a task object holds its children as deep
        // as the outline. Shown, it stops at MaxValueDepth levels ("…" for the rest); compared with
        // itself and checked for fullyCompleted it does not recurse level by level either.
        QString note;
        for (int i = 0; i < 1000; ++i)
            note += QString(i, u'\t') + u"- [ ] zadanie %1 ąę\n"_s.arg(i);
        VaultIndex index;
        index.update(u"Głęboka.md"_s, note);
        dataview::Result result;
        QVERIFY(runOnASmallStack([&] {
            result = run(index, u"TABLE WITHOUT ID file.tasks[0], string(file.tasks[0]), display(file.tasks[0]), "
                                u"file.tasks[0].fullyCompleted, length(unique(list(file.tasks[0], file.tasks[0])))"_s);
        }));
        QCOMPARE(result.rows.size(), 1);
        const QStringList cells = result.rows.at(0).cells;
        for (const QString &shown : cells.first(3)) {
            // Task k is 2k levels inside the first (each through its parent's list of children).
            QVERIFY2(shown.contains(u"zadanie 31 ąę"_s) && shown.contains(u"…"_s), qPrintable(shown.left(200)));
            QVERIFY(!shown.contains(u"zadanie 32 ąę"_s));
        }
        QCOMPARE(cells.sliced(3), (QStringList{u"false"_s, u"1"_s}));
    }

    void limitsFitASmallStack()
    {
        // The deepest a query may go — an expression as deep as allowed whose deepest part shows,
        // compares and frees a value nested about as deep as allowed, a FROM source and calls as
        // deep as allowed, a lambda calling itself up to the limit — fits half an Android thread's
        // stack, far from overflowing it.
        using dataview::MaxExpressionDepth, dataview::MaxExpressionNesting;
        const auto query = [](const QString &expr) {
            return u"LIST WITHOUT ID "_s + expr + u"\nFROM \"Projekty/Dom\""_s;
        };
        const QString value =
                u"reduce(split(padleft(\"\", %1, \"ż\"), \"\"), (a, b) => list(a))"_s.arg(dataview::MaxValueDepth);
        // (Its first part is 8 levels deep.)
        const QString deepest = u"length(string(%1)) + length(unique(list(%1, %1)))"_s.arg(value)
                                + u" + 1"_s.repeated(MaxExpressionDepth - 8);
        QString tags;
        for (int i = 0; i < MaxExpressionDepth - 1; ++i)
            tags += u"#żadna-%1 OR "_s.arg(i);
        const QString calls =
                u"lower("_s.repeated(MaxExpressionNesting - 1) + u"\"Ż\""_s + QString(MaxExpressionNesting - 1, u')');
        QStringList cells, errors;
        QVERIFY(runOnASmallStack([&] {
            cells = firstCells(run(m_index, query(deepest)));
            cells += firstCells(run(m_index, u"LIST FROM "_s + tags + u"#projekt"_s));
            cells += firstCells(run(m_index, query(calls)));
            errors.append(failure(m_index, query(deepest + u" + 1"_s)));
            errors.append(
                    failure(m_index, query(u"reduce(list((f) => map(list(f), f), 0), (g, x) => map(list(g), g))"_s)));
        }));
        QCOMPARE(cells, (QStringList{u"250"_s, u"[[Garaż]]"_s, u"[[Dom]]"_s, u"[[Ogród]]"_s, u"ż"_s}));
        QVERIFY2(errors.at(0).contains(u"too long"_s), qPrintable(errors.at(0)));
        QVERIFY2(errors.at(1).contains(u"nested too deeply"_s), qPrintable(errors.at(1)));
    }

    void fileTimes()
    {
        auto o = options();
        o.stat = [](const VaultPath &path) -> std::optional<FileStat> {
            if (path != u"Projekty/Dom.md")
                return std::nullopt;
            return FileStat{path, false, 4321, QDateTime(QDate(2026, 10, 13), QTime(18, 45)).toMSecsSinceEpoch()};
        };
        const auto r = run(m_index, u"TABLE WITHOUT ID file.mtime, file.mday, file.size, file.ctime FROM \"Projekty/Dom\""_s, o);
        QCOMPARE(r.rows.at(0).cells, (QStringList{u"2026-10-13 18:45"_s, u"2026-10-13"_s, u"4321"_s, u"2026-10-13 18:45"_s}));
        QCOMPARE(firstCells(run(m_index, u"LIST WHERE file.mtime > date(today) - dur(2 days)"_s, o)), QStringList{u"[[Dom]]"_s});
    }

    void malformedInput_data()
    {
        QTest::addColumn<QString>("source");
        QTest::newRow("only type") << u"TABLE"_s;
        QTest::newRow("field of null") << u"LIST WHERE brak.cokolwiek.dalej = 1"_s;
        QTest::newRow("index of number") << u"LIST WHERE budżet[3]"_s;
        QTest::newRow("divide by zero") << u"TABLE budżet / 0"_s;
        QTest::newRow("lambda called") << u"LIST WHERE map(file.tasks, (t) => t.nic.nic)"_s;
        QTest::newRow("emoji field") << u"TABLE 📅"_s;
        QTest::newRow("dataviewjs-like") << u"LIST WHERE dv"_s;
        QTest::newRow("dates of text") << u"TABLE date(status), dur(status), number(status)"_s;
    }
    void malformedInput()
    {
        QFETCH(QString, source);
        const auto parsed = dataview::parseQuery(source);
        if (const auto *error = std::get_if<QString>(&parsed))
            QFAIL(qPrintable(*error));
        const auto r = dataview::runQuery(std::get<dataview::Query>(parsed), m_index, options());
        QVERIFY(std::holds_alternative<dataview::Result>(r));
    }

    void longLinesStayFast()
    {
        // A note synced from anywhere: one line of 40 000 unclosed "[a:: " fields and a key line.
        VaultIndex index;
        QString line;
        for (int i = 0; i < 40000; ++i)
            line += u"[a:: "_s;
        index.update(u"Długa.md"_s, line + u"\nklucz:: "_s + QString(100000, u' ') + u"x"_s);
        QElapsedTimer timer;
        timer.start();
        const auto r = run(index, u"TABLE WITHOUT ID klucz, a"_s);
        QCOMPARE(r.rows.size(), 1);
        QCOMPARE(r.rows.at(0).cells.at(0), u"x"_s);
        QVERIFY2(timer.elapsed() < 3000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(timer.elapsed())));
    }

    void values()
    {
        using dataview::Value;
        QVERIFY(!Value().truthy());
        QVERIFY(!Value::string(QString()).truthy());
        QVERIFY(Value::number(-1).truthy());
        QVERIFY(dataview::compare(Value(), Value::number(0)) < 0);
        QVERIFY(dataview::compare(Value::string(u"ala"_s), Value::string(u"Bartek"_s)) < 0);
        QVERIFY(dataview::equals(Value::number(2), Value::number(2)));
        QVERIFY(!dataview::equals(Value::number(2), Value::string(u"2"_s)));
        QCOMPARE(dataview::canonicalKey(u" **Termin Oddania** "), u"termin-oddania"_s);
        QVERIFY(dataview::parseDuration(u"3 dni 2 godziny"));
        QCOMPARE(dataview::parseDuration(u"3 dni 2 godziny")->ms, qint64((3 * 24 + 2) * 3600000LL));
        QCOMPARE(dataview::parseDuration(u"1 rok")->months, 12);
        QVERIFY(!dataview::parseDuration(u"trzy dni"));
        QVERIFY(!dataview::parseDuration(u"3 dni i"));
        QVERIFY(dataview::parseDate(u"2026-10-15T09:30:15Z"));
        QVERIFY(dataview::parseDate(u"2026-10-15T09:30:15Z")->hasTime);
        QVERIFY(!dataview::parseDate(u"2026-02-30"));
        QVERIFY(!dataview::parseDate(u"15.10.2026"));
    }

private:
    VaultIndex m_index;
};

QTEST_GUILESS_MAIN(TestDataview)
#include "tst_dataview.moc"
