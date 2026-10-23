// Query language (docs/SYNTAX.md §10): parseQuery and runQuery over the fixture vault.
#include "fixture.h"

#include "index/textutil_p.h"
#include "markdown/parse.h"

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>
#include <ctime>

using namespace brulion;
using namespace Qt::StringLiterals;

Q_DECLARE_METATYPE(brulion::QueryType)

namespace {

Query parsed(const QString &source)
{
    const auto r = parseQuery(source);
    if (const auto *error = std::get_if<QString>(&r))
        qWarning("unexpected query error: %s", qPrintable(*error));
    return std::get<Query>(r);
}

/// "path: text" of every item (task text for tasks, block text otherwise, title for pages).
QStringList describe(const VaultIndex &index, const QueryResult &result)
{
    QStringList out;
    for (const QueryItem &item : result.items) {
        if (item.blockIndex < 0)
            out.append(item.path);
        else
            out.append(fixture::blockText(index, item.path, item.blockIndex).section(u'\n', 0, 0));
    }
    return out;
}

} // namespace

class TestIndexQuery : public QObject {
    Q_OBJECT

private:
    VaultIndex m_index;

    QueryResult run(const QString &source, const QueryOptions &options = {}, Qt::DayOfWeek weekStart = Qt::Monday)
    {
        return runQuery(parsed(source), m_index, fixture::Today, weekStart, options);
    }

    QStringList texts(const QString &source, const QueryOptions &options = {})
    {
        return describe(m_index, run(source, options));
    }

private Q_SLOTS:
    void initTestCase() { fixture::build(m_index); }

    // ---- parsing ------------------------------------------------------------------------------

    void parseValid_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QueryType>("type");
        QTest::addColumn<int>("filters");
        QTest::newRow("spec example") << u"type:tasks status:todo,doing tag:praca due:<=+7d sort:due limit:20"_s
                                      << QueryType::Tasks << 3;
        QTest::newRow("inferred tasks") << u"status:open"_s << QueryType::Tasks << 1;
        QTest::newRow("inferred by sort") << u"sort:priority"_s << QueryType::Tasks << 0;
        QTest::newRow("inferred by group") << u"group:status"_s << QueryType::Tasks << 0;
        QTest::newRow("inferred reminders") << u"remind:today"_s << QueryType::Reminders << 1;
        QTest::newRow("inferred blocks") << u"tag:praca \"lista zakupów\""_s << QueryType::Blocks << 2;
        QTest::newRow("pages") << u"type:pages tag:projekt sort:page group:tag"_s << QueryType::Pages << 1;
        QTest::newRow("multi-line with comments")
                << u"// moje zadania\ntype:tasks\nstatus:open // otwarte\n"_s << QueryType::Tasks << 1;
        QTest::newRow("polish keys and values")
                << u"typ:zadania stan:otwarte termin:jutro priorytet:wysoki"_s << QueryType::Tasks << 3;
        QTest::newRow("polish diacritics optional") << u"ścieżka:Projekty/ STATUS:Zrobione"_s << QueryType::Tasks << 2;
        QTest::newRow("hash tag and wiki ref") << u"#praca [[Jan Kowalski]]"_s << QueryType::Blocks << 2;
        QTest::newRow("negations") << u"-tag:praca -kupić -\"dwa słowa\""_s << QueryType::Blocks << 3;
        QTest::newRow("polish quotes") << u"„dokładna fraza” “inna fraza”"_s << QueryType::Blocks << 2;
        QTest::newRow("url is text") << u"https://example.com/a"_s << QueryType::Blocks << 1;
        QTest::newRow("time is text") << u"10:30"_s << QueryType::Blocks << 1;
        QTest::newRow("lone minus ignored") << u"- kawa"_s << QueryType::Blocks << 1;
        QTest::newRow("empty") << QString() << QueryType::Blocks << 0;
        QTest::newRow("date range") << u"due:today..+7d"_s << QueryType::Tasks << 1;
        QTest::newRow("date keywords") << u"due:overdue scheduled:none start:any"_s << QueryType::Tasks << 3;
        QTest::newRow("explicit equals") << u"done:=this-week"_s << QueryType::Tasks << 1;
        // Odd but harmless input.
        QTest::newRow("unclosed brackets are text") << u"[[bez końca"_s << QueryType::Blocks << 2; // two words
        QTest::newRow("empty phrase ignored") << u"\"\" „”"_s << QueryType::Blocks << 0;
        QTest::newRow("colons are text") << u"::::"_s << QueryType::Blocks << 1;
        // `#` + space at the start of a line is a comment (Brulion 1, Obsidian Tasks); `#tag` is not.
        QTest::newRow("lone hash is a comment") << u"#"_s << QueryType::Blocks << 0;
        QTest::newRow("hash comment") << u"# Moje zadania: otwarte\nstatus:open"_s << QueryType::Tasks << 1;
        QTest::newRow("indented hash comment") << u"  #\ttytuł: x\r\n#praca"_s << QueryType::Blocks << 1;
        QTest::newRow("hash comment, lone cr") << u"# a: b\rtag:praca"_s << QueryType::Blocks << 1;
        QTest::newRow("hash mid-line is text") << u"kawa # mleko"_s << QueryType::Blocks << 3;
        QTest::newRow("whitespace only") << u" \t\n "_s << QueryType::Blocks << 0;
        QTest::newRow("comment only") << u"// nic"_s << QueryType::Blocks << 0;
        QTest::newRow("comment, lone cr") << u"// nic: x\rstatus:open"_s << QueryType::Tasks << 1;
        QTest::newRow("nfd key") << u"s\u0301ciez\u0307ka:x"_s << QueryType::Blocks << 1;
    }
    void parseValid()
    {
        QFETCH(QString, source);
        QFETCH(QueryType, type);
        QFETCH(int, filters);
        const auto r = parseQuery(source);
        QVERIFY2(std::holds_alternative<Query>(r),
                 qPrintable(std::get_if<QString>(&r) ? *std::get_if<QString>(&r) : QString()));
        const Query &q = std::get<Query>(r);
        QCOMPARE(q.type, type);
        QCOMPARE(q.filters.size(), filters);
        QCOMPARE(q.source, source);
    }

    void parseDetails()
    {
        const Query q = parsed(u"type:tasks status:open,deferred -tag:#Praca,[[Dom Jana]] due:<=+7d,none "
                               "priority:high,brak sort:due,-priority group:page limit:20 \"a b\""_s);
        QVERIFY(q.explicitType);
        QCOMPARE(q.filters.size(), 5);
        QCOMPARE(q.filters[0].kind, QueryFilterKind::Status);
        QCOMPARE(q.filters[0].statuses, QList<TaskStatus>({TaskStatus::Todo, TaskStatus::Doing, TaskStatus::Deferred}));
        QCOMPARE(q.filters[1].kind, QueryFilterKind::Tag);
        QVERIFY(q.filters[1].negate);
        QCOMPARE(q.filters[1].values, QStringList({u"praca"_s, u"dom jana"_s}));
        QCOMPARE(q.filters[2].kind, QueryFilterKind::Date);
        QCOMPARE(q.filters[2].field, QueryDateField::Due);
        QCOMPARE(q.filters[2].dates,
                 QList<QueryDateCondition>({{QueryDateOp::OnOrBefore, u"+7d"_s}, {QueryDateOp::Within, u"none"_s}}));
        QCOMPARE(q.filters[3].priorities, QList<Priority>({Priority::High, Priority::None}));
        QCOMPARE(q.filters[4].kind, QueryFilterKind::Text);
        QCOMPARE(q.filters[4].values, QStringList{u"a b"_s});
        QCOMPARE(q.sort, QList<QuerySort>({{QuerySortKey::Due, false}, {QuerySortKey::Priority, true}}));
        QCOMPARE(q.group, QueryGroupKey::Page);
        QCOMPARE(q.limit, std::optional<int>(20));

        const Query ops = parsed(u"due:<2026-10-15 due:>today due:>=-2w scheduled:<=tomorrow"_s);
        QCOMPARE(ops.filters[0].dates.first().op, QueryDateOp::Before);
        QCOMPARE(ops.filters[0].dates.first().expr, u"2026-10-15"_s);
        QCOMPARE(ops.filters[1].dates.first().op, QueryDateOp::After);
        QCOMPARE(ops.filters[2].dates.first().op, QueryDateOp::OnOrAfter);
        QCOMPARE(ops.filters[3].field, QueryDateField::Scheduled);

        const Query refs = parsed(u"ref:[[Jan Kowalski]],\"Dom Jana\" page:this path:\"Moje notatki/\""_s);
        QCOMPARE(refs.filters[0].values, QStringList({u"Jan Kowalski"_s, u"Dom Jana"_s}));
        QCOMPARE(refs.filters[1].values, QStringList{u"this"_s});
        QCOMPARE(refs.filters[2].values, QStringList{u"Moje notatki/"_s});
    }

    void parseErrors_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("fragment");
        QTest::newRow("unknown key") << u"foo:bar"_s << u"Unknown filter “foo:”"_s;
        QTest::newRow("empty value") << u"status:"_s << u"needs a value"_s;
        QTest::newRow("empty tag") << u"tag:#"_s << u"needs a value"_s;
        QTest::newRow("bad status") << u"status:maybe"_s << u"Invalid value “maybe”"_s;
        QTest::newRow("bad priority") << u"priority:urgent"_s << u"Invalid value “urgent”"_s;
        QTest::newRow("bad type") << u"type:files"_s << u"Invalid value “files”"_s;
        QTest::newRow("two types") << u"type:tasks,pages"_s << u"Invalid value"_s;
        QTest::newRow("bad sort") << u"sort:size"_s << u"Invalid value “size”"_s;
        QTest::newRow("bad group") << u"group:week"_s << u"Invalid value “week”"_s;
        QTest::newRow("bad date") << u"due:someday"_s << u"Cannot understand the date “someday”"_s;
        QTest::newRow("bad date in list") << u"due:today,nigdy"_s << u"“nigdy”"_s;
        QTest::newRow("operator without date") << u"due:<="_s << u"needs a value"_s;
        QTest::newRow("operator with keyword") << u"due:<overdue"_s << u"Operator “<” cannot be combined"_s;
        QTest::newRow("limit zero") << u"limit:0"_s << u"Limit must be a number from 1 to 10000"_s;
        QTest::newRow("limit text") << u"limit:dużo"_s << u"(got “dużo”)"_s;
        QTest::newRow("limit too big") << u"limit:10001"_s << u"Limit must be"_s;
        QTest::newRow("limit negative") << u"limit:-5"_s << u"Limit must be"_s;
        QTest::newRow("unclosed quote") << u"\"bez końca"_s << u"Missing closing quote."_s;
        QTest::newRow("unclosed polish quote") << u"„bez końca"_s << u"Missing closing quote."_s;
        QTest::newRow("negated sort") << u"-sort:due"_s << u"cannot be negated"_s;
        QTest::newRow("negated limit") << u"-limit:5"_s << u"cannot be negated"_s;
        QTest::newRow("pages with task filter")
                << u"type:pages status:todo"_s << u"“status:todo” does not work with type:pages"_s;
        QTest::newRow("pages with remind") << u"type:pages remind:today"_s << u"does not work with type:pages"_s;
        QTest::newRow("pages with task sort") << u"type:pages sort:due"_s << u"“sort:due” does not work"_s;
        QTest::newRow("pages with task group") << u"type:pages group:status"_s << u"“group:status” does not work"_s;
        QTest::newRow("duplicate type") << u"type:tasks typ:bloki"_s << u"may appear only once"_s;
        QTest::newRow("duplicate group") << u"group:page group:tag"_s << u"may appear only once"_s;
        QTest::newRow("empty quoted value") << u"tag:\"\""_s << u"needs a value"_s;
        QTest::newRow("unknown key with underscore") << u"_x:1"_s << u"Unknown filter “_x:”"_s;
        QTest::newRow("comma only") << u"status:,"_s << u"needs a value"_s;
    }
    void parseErrors()
    {
        QFETCH(QString, source);
        QFETCH(QString, fragment);
        const auto r = parseQuery(source);
        QVERIFY(std::holds_alternative<QString>(r));
        const QString error = std::get<QString>(r);
        QVERIFY2(error.contains(fragment), qPrintable(error));
    }

    // ---- running: tasks -------------------------------------------------------------------------

    void taskFilters_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QStringList>("expected");
        // Default order: path (code units), then line.
        QTest::newRow("open") << u"status:open"_s
                              << QStringList({u"Bez daty #praca"_s, u"Kupić płytki #zakupy"_s, u"Zamówić ekipę"_s,
                                              u"Wybrać baterię"_s, u"Mleko"_s, u"Chleb"_s, u"Wysłać ofertę"_s,
                                              u"Kupić farbę #zakupy"_s, u"Pisanie raportu #praca/raporty"_s,
                                              u"Odebrać paczkę"_s, u"Malowanie salonu #dom"_s});
        QTest::newRow("closed") << u"status:closed"_s
                                << QStringList({u"Anulowane zadanie"_s, u"Zadzwonić do hydraulika"_s});
        QTest::newRow("doing") << u"status:doing"_s
                               << QStringList({u"Pisanie raportu #praca/raporty"_s, u"Malowanie salonu #dom"_s});
        QTest::newRow("overdue") << u"due:overdue"_s << QStringList({u"Kupić płytki #zakupy"_s, u"Wysłać ofertę"_s});
        QTest::newRow("today") << u"due:today"_s << QStringList{u"Odebrać paczkę"_s};
        QTest::newRow("polish today") << u"termin:dziś"_s << QStringList{u"Odebrać paczkę"_s};
        QTest::newRow("within a week") << u"due:<=+7d sort:due"_s
                                       << QStringList({u"Anulowane zadanie"_s, u"Kupić płytki #zakupy"_s,
                                                       u"Wysłać ofertę"_s, u"Zadzwonić do hydraulika"_s,
                                                       u"Odebrać paczkę"_s, u"Malowanie salonu #dom"_s,
                                                       u"Zamówić ekipę"_s, u"Pisanie raportu #praca/raporty"_s});
        QTest::newRow("no due date") << u"due:none"_s
                                     << QStringList({u"Bez daty #praca"_s, u"Mleko"_s, u"Chleb"_s,
                                                     u"Kupić farbę #zakupy"_s});
        QTest::newRow("after") << u"due:>2026-10-18"_s
                               << QStringList({u"Wybrać baterię"_s, u"Pisanie raportu #praca/raporty"_s});
        QTest::newRow("on or after") << u"due:>=2026-10-18"_s
                                     << QStringList({u"Zamówić ekipę"_s, u"Wybrać baterię"_s,
                                                     u"Pisanie raportu #praca/raporty"_s});
        QTest::newRow("before") << u"due:<2026-10-13"_s
                                << QStringList({u"Kupić płytki #zakupy"_s, u"Anulowane zadanie"_s});
        QTest::newRow("range") << u"due:2026-10-13..2026-10-16"_s
                               << QStringList({u"Wysłać ofertę"_s, u"Zadzwonić do hydraulika"_s, u"Odebrać paczkę"_s,
                                               u"Malowanie salonu #dom"_s});
        QTest::newRow("either date") << u"due:today,2026-10-25"_s
                                     << QStringList({u"Wybrać baterię"_s, u"Odebrać paczkę"_s});
        QTest::newRow("scheduled tomorrow") << u"scheduled:tomorrow"_s << QStringList{u"Kupić farbę #zakupy"_s};
        QTest::newRow("start today") << u"start:today"_s << QStringList{u"Malowanie salonu #dom"_s};
        QTest::newRow("done this week") << u"done:this-week"_s << QStringList{u"Zadzwonić do hydraulika"_s};
        QTest::newRow("created before") << u"created:<today"_s << QStringList{u"Zamówić ekipę"_s};
        QTest::newRow("high priority") << u"priority:high"_s
                                       << QStringList({u"Kupić płytki #zakupy"_s, u"Pisanie raportu #praca/raporty"_s});
        QTest::newRow("no priority, closed") << u"priority:none status:closed"_s
                                             << QStringList({u"Anulowane zadanie"_s, u"Zadzwonić do hydraulika"_s});
        QTest::newRow("tag incl. ancestors and nested")
                << u"type:tasks tag:praca"_s
                << QStringList({u"Bez daty #praca"_s, u"Wysłać ofertę"_s, u"Pisanie raportu #praca/raporty"_s});
        QTest::newRow("nested tag only") << u"type:tasks tag:praca/raporty"_s
                                         << QStringList{u"Pisanie raportu #praca/raporty"_s};
        QTest::newRow("page tag via tags::")
                << u"type:tasks tag:zakupy"_s
                << QStringList({u"Kupić płytki #zakupy"_s, u"Mleko"_s, u"Chleb"_s, u"Kupić farbę #zakupy"_s});
        QTest::newRow("frontmatter page tag")
                << u"type:tasks #projekt status:open"_s
                << QStringList({u"Kupić płytki #zakupy"_s, u"Zamówić ekipę"_s, u"Wybrać baterię"_s});
        QTest::newRow("tag case-insensitive")
                << u"type:tasks tag:REMONT/łazienka"_s << QStringList{u"Wybrać baterię"_s};
        QTest::newRow("excluded tag") << u"status:open -tag:praca -tag:zakupy"_s
                                      << QStringList({u"Zamówić ekipę"_s, u"Wybrać baterię"_s, u"Odebrać paczkę"_s,
                                                      u"Malowanie salonu #dom"_s});
        QTest::newRow("text, diacritic-insensitive")
                << u"type:tasks kupic"_s << QStringList({u"Kupić płytki #zakupy"_s, u"Kupić farbę #zakupy"_s});
        QTest::newRow("excluded text") << u"status:open -kupić -\"Bez daty\" path:Projekty"_s
                                       << QStringList({u"Zamówić ekipę"_s, u"Wybrać baterię"_s});
        QTest::newRow("path prefix") << u"type:tasks path:journals/2026-10-14"_s
                                     << QStringList({u"Odebrać paczkę"_s, u"Malowanie salonu #dom"_s});
        QTest::newRow("path glob") << u"type:tasks path:*/2026-10-1[3]"_s << QStringList();
        QTest::newRow("path glob star") << u"type:tasks path:journals/*-13"_s
                                        << QStringList({u"Zadzwonić do hydraulika"_s, u"Kupić farbę #zakupy"_s,
                                                        u"Pisanie raportu #praca/raporty"_s});
        QTest::newRow("page by title") << u"type:tasks page:Zakupy"_s << QStringList({u"Mleko"_s, u"Chleb"_s});
        QTest::newRow("page by alias") << u"type:tasks page:remont status:cancelled"_s
                                       << QStringList{u"Anulowane zadanie"_s};
        QTest::newRow("page glob") << u"type:tasks page:*akup*"_s << QStringList({u"Mleko"_s, u"Chleb"_s});
        QTest::newRow("page by path") << u"type:tasks page:journals/2026-10-14"_s
                                      << QStringList({u"Odebrać paczkę"_s, u"Malowanie salonu #dom"_s});
        QTest::newRow("negated page") << u"type:tasks -page:Dom -path:journals status:open"_s
                                      << QStringList({u"Bez daty #praca"_s, u"Mleko"_s, u"Chleb"_s});
        QTest::newRow("sort priority then due")
                << u"status:open sort:priority,due limit:4"_s
                << QStringList({u"Kupić płytki #zakupy"_s, u"Pisanie raportu #praca/raporty"_s,
                                u"Kupić farbę #zakupy"_s, u"Wysłać ofertę"_s});
        QTest::newRow("sort descending, missing last") << u"type:tasks sort:-due path:Zakupy,Notatki"_s
                                                       << QStringList({u"Bez daty #praca"_s, u"Mleko"_s, u"Chleb"_s});
        QTest::newRow("sort -due") << u"status:open sort:-due limit:3"_s
                                   << QStringList({u"Wybrać baterię"_s, u"Pisanie raportu #praca/raporty"_s,
                                                   u"Zamówić ekipę"_s});
        QTest::newRow("sort status") << u"type:tasks path:journals/2026-10-13 sort:status"_s
                                     << QStringList({u"Pisanie raportu #praca/raporty"_s, u"Kupić farbę #zakupy"_s,
                                                     u"Zadzwonić do hydraulika"_s});
        QTest::newRow("sort page") << u"status:open path:Projekty,Zakupy sort:-page,due"_s
                                   << QStringList({u"Mleko"_s, u"Chleb"_s, u"Kupić płytki #zakupy"_s,
                                                   u"Zamówić ekipę"_s, u"Wybrać baterię"_s});
        QTest::newRow("sort created") << u"type:tasks sort:created limit:1"_s << QStringList{u"Zamówić ekipę"_s};
        QTest::newRow("limit") << u"status:open limit:2"_s
                               << QStringList({u"Bez daty #praca"_s, u"Kupić płytki #zakupy"_s});
    }
    void taskFilters()
    {
        QFETCH(QString, source);
        QFETCH(QStringList, expected);
        QStringList got;
        for (const QueryItem &item : run(source).items) {
            QVERIFY(item.task.has_value());
            got.append(item.task->text);
        }
        QCOMPARE(got, expected);
    }

    void totalCountsBeforeLimit()
    {
        const QueryResult r = run(u"status:open limit:3"_s);
        QCOMPARE(r.type, QueryType::Tasks);
        QCOMPARE(r.items.size(), 3);
        QCOMPARE(r.total, 11);
        QueryOptions options;
        options.defaultLimit = 5;
        const QueryResult capped = run(u"status:open"_s, options);
        QCOMPARE(capped.items.size(), 5);
        QCOMPARE(capped.total, 11);
    }

    void weekStartMatters()
    {
        // Wednesday 2026-10-14: Monday weeks end on 10-18 (Zamówić ekipę), Sunday weeks on 10-17.
        QCOMPARE(run(u"due:this-week status:open"_s, {}, Qt::Monday).total, 4);
        QCOMPARE(run(u"due:this-week status:open"_s, {}, Qt::Sunday).total, 3);
    }

    // ---- running: other types -----------------------------------------------------------------

    void blocks()
    {
        // Children of a matching block are shown inside it (collapsed).
        QCOMPARE(texts(u"tag:praca"_s),
                 QStringList({u"Bez daty #praca"_s,
                              u"Spotkanie z [[Jan Kowalski]] w sprawie [[Projekty/Dom|domu]] #praca"_s,
                              u"Pisanie raportu #praca/raporty"_s}));
        QCOMPARE(texts(u"\"żółw\""_s), QStringList({u"Żółw dla dzieci"_s, u"notatka o żółwiu i zółwiach"_s}));
        QCOMPARE(texts(u"ŻÓŁW dzieci"_s), QStringList{u"Żółw dla dzieci"_s});
        QCOMPARE(texts(u"[[Jan Kowalski]]"_s),
                 QStringList({u"Zobacz [[dom]], [[Remont]] i [[Janek]]"_s,
                              u"Spotkanie z [[Jan Kowalski]] w sprawie [[Projekty/Dom|domu]] #praca"_s}));
        QCOMPARE(texts(u"ref:[[Nieistniejąca strona]]"_s),
                 QStringList({u"Kolejny link do [[Nieistniejąca strona]]"_s,
                              u"Lista: [[Budżet]] i [[Nieistniejąca strona]]"_s}));
        QCOMPARE(texts(u"ref:Dom path:journals"_s),
                 QStringList({u"Spotkanie z [[Jan Kowalski]] w sprawie [[Projekty/Dom|domu]] #praca"_s,
                              u"Pomysł na ((plan01))"_s, u"Malowanie salonu #dom"_s,
                              u"Kod: `Dom` oraz https://example.com/Dom i #Dom"_s}));
        // Code blocks are blocks too (calc), but the frontmatter is not.
        QCOMPARE(texts(u"farba path:Projekty"_s), QStringList({u"farba = 120"_s, u"Farba: [lista](../Zakupy.md)"_s}));
        QVERIFY(texts(u"aliases"_s).isEmpty());
    }

    // ref: also matches tags that name the page (SYNTAX §10); tag: stays tags-only.
    void refMatchesTags()
    {
        QCOMPARE(texts(u"ref:praca"_s),
                 QStringList({u"Bez daty #praca"_s,
                              u"Spotkanie z [[Jan Kowalski]] w sprawie [[Projekty/Dom|domu]] #praca"_s}));
        QCOMPARE(texts(u"tag:praca"_s).size(), 3); // with the sub-tag, as before
        QVERIFY(texts(u"ref:zolw"_s).isEmpty());
        // By title; Zakupy.md carries its own tag (as a page linking itself would match).
        QCOMPARE(texts(u"type:pages ref:Zakupy"_s), QStringList({u"journals/2026-10-13.md"_s, u"Projekty/Budżet.md"_s,
                                                                 u"Projekty/Dom.md"_s, u"Zakupy.md"_s}));
        QVERIFY(texts(u"type:pages ref:Dom"_s).contains(u"journals/2026-10-14.md"_s));
        QVERIFY(!texts(u"type:pages -ref:Dom"_s).contains(u"journals/2026-10-14.md"_s));
        // A page tagged as a whole (frontmatter) is a page reference, its blocks are not.
        VaultIndex index;
        index.update(u"Projekt.md"_s, u"# Projekt"_s);
        index.update(u"A.md"_s, u"---\ntags: [projekt]\n---\n- treść"_s);
        index.update(u"B.md"_s, u"- zadanie #Projekt\n  - dziecko"_s);
        const auto inIndex = [&](const QString &source, const QueryOptions &options = {}) {
            return describe(index, runQuery(parsed(source), index, fixture::Today, Qt::Monday, options));
        };
        QCOMPARE(inIndex(u"type:pages ref:Projekt"_s), QStringList({u"A.md"_s, u"B.md"_s}));
        QCOMPARE(inIndex(u"ref:Projekt"_s), QStringList{u"zadanie #Projekt"_s}); // the child shows inside it
        QueryOptions here;
        here.currentPath = u"Projekt.md"_s;
        QCOMPARE(inIndex(u"type:pages ref:this"_s, here), QStringList({u"A.md"_s, u"B.md"_s}));
        QVERIFY(inIndex(u"ref:this"_s).isEmpty()); // no current page
    }

    void queryBlocksDoNotMatchThemselves()
    {
        VaultIndex index;
        index.update(u"Q.md"_s, u"```query\n\"wyjątkowe słowo\"\n```\n\n- wyjątkowe słowo tutaj"_s);
        const QueryResult r = runQuery(parsed(u"\"wyjątkowe słowo\""_s), index, fixture::Today);
        QCOMPARE(describe(index, r), QStringList{u"wyjątkowe słowo tutaj"_s});
    }

    void pageThis()
    {
        QueryOptions options;
        options.currentPath = u"Zakupy.md"_s;
        QCOMPARE(texts(u"type:tasks page:this"_s, options), QStringList({u"Mleko"_s, u"Chleb"_s}));
        QVERIFY(texts(u"type:tasks page:this"_s).isEmpty()); // no current page
        // The link and the blocks tagged #zakupy (SYNTAX §10: ref: includes tags).
        QCOMPARE(texts(u"ref:this"_s, options),
                 QStringList({u"Farba: [lista](../Zakupy.md)"_s, u"Kupić płytki #zakupy"_s, u"Kupić farbę #zakupy"_s}));
    }

    void pages()
    {
        QCOMPARE(texts(u"type:pages path:Projekty/"_s),
                 QStringList({u"Projekty/Budżet.md"_s, u"Projekty/Dom.md"_s})); // by title: Budżet < Dom
        QCOMPARE(texts(u"type:pages tag:projekt"_s), QStringList{u"Projekty/Dom.md"_s});
        QCOMPARE(texts(u"type:pages page:Remont"_s), QStringList{u"Projekty/Dom.md"_s});
        QCOMPARE(texts(u"type:pages page:dom"_s), QStringList({u"Archiwum/Stare/Dom.md"_s, u"Projekty/Dom.md"_s}));
        QCOMPARE(texts(u"type:pages date:today"_s), QStringList{u"journals/2026-10-14.md"_s});
        QCOMPARE(texts(u"type:pages date:any sort:-date"_s),
                 QStringList({u"journals/2026-10-14.md"_s, u"journals/2026-10-13.md"_s, u"journals/2026-10-12.md"_s}));
        QCOMPARE(texts(u"type:pages \"Stary dom\""_s), QStringList{u"Archiwum/Stare/Dom.md"_s});
        // Pages sort by title: "2026-10-12" < "Jan Kowalski" < "Notatki".
        QCOMPARE(texts(u"type:pages kowalski"_s),
                 QStringList({u"journals/2026-10-12.md"_s, u"journals/2026-10-14.md"_s, u"Ludzie/Jan Kowalski.md"_s}));
        QCOMPARE(texts(u"type:pages ref:Janek"_s), QStringList({u"journals/2026-10-12.md"_s, u"Notatki.md"_s}));
        QCOMPARE(run(u"type:pages"_s).total, 9);
    }

    void reminders()
    {
        const QueryResult all = run(u"type:reminders"_s);
        QCOMPARE(all.type, QueryType::Reminders);
        QCOMPARE(all.total, 3);
        QCOMPARE(all.items[0].reminder->at, (DateSpec{QDate(2026, 10, 13), QTime(8, 0)})); // by time
        QCOMPARE(all.items[1].reminder->at, (DateSpec{QDate(2026, 10, 14), QTime(17, 30)}));
        QVERIFY(all.items[1].task.has_value());
        QVERIFY(all.items[2].reminder->alarm);
        QCOMPARE(run(u"remind:today"_s).total, 1);
        QCOMPARE(run(u"remind:>=today"_s).total, 2);
        QCOMPARE(run(u"remind:overdue"_s).total, 1); // before today 00:00
        QueryOptions evening;
        evening.now = QTime(18, 0);
        QCOMPARE(run(u"remind:overdue"_s, evening).total, 2);
        QCOMPARE(run(u"type:reminders status:open"_s).total, 1);
        QCOMPARE(run(u"type:reminders sort:-remind"_s).items.first().reminder->alarm, true);
        // Tasks with a reminder today.
        QCOMPARE(run(u"type:tasks remind:today"_s).total, 1);
        QCOMPARE(run(u"type:tasks remind:none status:open"_s).total, 10);
    }

    void groups()
    {
        const QueryResult byPage = run(u"status:open group:page path:journals"_s);
        QCOMPARE(byPage.groups.size(), 3);
        QCOMPARE(byPage.groups[0].label, u"2026-10-12"_s); // titles, name order
        QCOMPARE(byPage.groups[0].key, u"journals/2026-10-12.md"_s);
        QCOMPARE(byPage.groups[1].items.size(), 2);

        const QueryResult byStatus = run(u"type:tasks group:status path:journals"_s);
        QStringList keys;
        for (const QueryGroup &g : byStatus.groups)
            keys.append(g.key);
        QCOMPARE(keys, QStringList({u"doing"_s, u"todo"_s, u"done"_s}));
        QCOMPARE(byStatus.groups[0].label, u"In progress"_s);

        const QueryResult byDue = run(u"type:tasks path:Zakupy,journals/2026-10-14 group:due"_s);
        QCOMPARE(byDue.groups.size(), 3);
        QCOMPARE(byDue.groups[0].key, u"2026-10-14"_s);
        QCOMPARE(byDue.groups[1].key, u"2026-10-16"_s);
        QCOMPARE(byDue.groups[2].key, QString());
        QCOMPARE(byDue.groups[2].label, u"No date"_s);
        QCOMPARE(byDue.groups[2].items.size(), 2);

        const QueryResult byTag = run(u"type:tasks path:Projekty/Dom group:tag"_s);
        QStringList tagKeys;
        for (const QueryGroup &g : byTag.groups)
            tagKeys.append(g.key + u'=' + QString::number(g.items.size()));
        // Every task carries the page tag; some have more (one group per tag).
        QCOMPARE(tagKeys, QStringList({u"projekt=4"_s, u"remont/łazienka=1"_s, u"zakupy=1"_s}));
        QCOMPARE(byTag.groups[0].label, u"#projekt"_s);

        const QueryResult byPriority = run(u"status:open group:priority path:Projekty,Zakupy"_s);
        QStringList prio;
        for (const QueryGroup &g : byPriority.groups)
            prio.append(g.key);
        QCOMPARE(prio, QStringList({u"high"_s, u"none"_s, u"low"_s}));
        QCOMPARE(byPriority.groups[1].label, u"Normal"_s);

        const QueryResult blocksByStatus = run(u"type:blocks path:Zakupy group:status"_s);
        QCOMPARE(blocksByStatus.groups.last().label, u"No status"_s);

        // Groups are built from the limited items.
        const QueryResult limited = run(u"status:open group:page limit:1"_s);
        QCOMPARE(limited.groups.size(), 1);
        QCOMPARE(limited.total, 11);
    }

    void wildcardsAreLinear()
    {
        // `*` wildcards used to become a backtracking regex (\A.*a.*a….*x\z): a query from a
        // note could freeze the app, and PCRE2's match limit silently turned matches into misses.
        // Every path matches, but only with the early a's: a greedy regex first exhausts the
        // placements in the second run of a's.
        VaultIndex index;
        for (int i = 0; i < 300; ++i) {
            const QString name = QString(20, u'a') + u'x' + QString(40, u'a') + QString::number(i) + u'y';
            index.update(u"Strony/"_s + name + u".md"_s, u"- [ ] zadanie %1"_s.arg(i));
        }
        const QString stars = QString(u"*a"_s).repeated(16);
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(runQuery(parsed(u"type:tasks page:"_s + stars + u"*x*y"_s), index, fixture::Today).total, 300);
        QCOMPARE(runQuery(parsed(u"type:tasks path:strony/"_s + stars + u"*x"_s), index, fixture::Today).total, 300);
        QCOMPARE(runQuery(parsed(u"type:tasks page:"_s + stars + u"*z"_s), index, fixture::Today).total, 0);
        QCOMPARE(runQuery(parsed(u"type:pages path:"_s + stars + u"*z"_s), index, fixture::Today).total, 0);
        QVERIFY2(timer.elapsed() < 5000 * BRULION_TEST_SLOWDOWN, qPrintable(u"wildcard queries took %1 ms"_s.arg(timer.elapsed())));
    }

    void sortsByPageNames()
    {
        // Page order is compareNames() (Polish letters after their base, numbers by value,
        // NFD = NFC, then code units); the runner computes each name's key only once.
        const QStringList titles = {u"Strona 10"_s, u"strona 2"_s, u"Łódź"_s,         u"Lublin"_s,
                                    u"Żywiec"_s,    u"Źródło"_s,   u"Z\u0307ywiec"_s, u"Zakopane"_s,
                                    u"ćma"_s,       u"Cyrk"_s,     u"éclair"_s,       u"Ecz"_s,
                                    u"dom"_s,       u"Dom"_s,      u"Strona 007"_s,   u"Strona 8"_s,
                                    u"İstanbul"_s,  u"istanbul"_s, u"ŁÓDŹ kaliska"_s, u"zażółć"_s};
        VaultIndex index;
        for (const QString &t : titles)
            index.update(t + u".md"_s, u"- [ ] zadanie #lista\n- [ ] drugie #lista"_s);
        QStringList expected = titles;
        std::stable_sort(expected.begin(), expected.end(), [](const QString &a, const QString &b) {
            if (const int c = index_detail::compareNames(a, b); c != 0)
                return c < 0;
            return a < b;
        });
        const auto pages = [](const QueryResult &r) {
            QStringList out;
            for (const QueryItem &item : r.items) {
                const QString page = item.path.chopped(3);
                if (out.isEmpty() || out.last() != page)
                    out.append(page);
            }
            return out;
        };
        QCOMPARE(pages(runQuery(parsed(u"type:pages tag:lista"_s), index, fixture::Today)), expected);
        QCOMPARE(pages(runQuery(parsed(u"type:pages tag:lista sort:page"_s), index, fixture::Today)), expected);
        QCOMPARE(pages(runQuery(parsed(u"type:tasks sort:page"_s), index, fixture::Today)), expected);
        std::reverse(expected.begin(), expected.end());
        QCOMPARE(pages(runQuery(parsed(u"type:tasks sort:-page"_s), index, fixture::Today)), expected);
        std::reverse(expected.begin(), expected.end());
        QStringList groups;
        for (const QueryGroup &g : runQuery(parsed(u"type:tasks group:page"_s), index, fixture::Today).groups)
            groups.append(g.key.chopped(3));
        QCOMPARE(groups, expected);
    }

    void reminderOverdueIsWallClock()
    {
        // Reminders are local wall-clock times. In a DST gap (CET -> CEST on 2026-03-29,
        // 02:00 -> 03:00) "02:30" must still be before "03:15", not moved to 03:30 by QDateTime.
        const QByteArray oldTz = qgetenv("TZ");
        qputenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3");
        tzset();
        VaultIndex index;
        index.update(u"N.md"_s, u"- nocna zmiana ⏰ 2026-03-29 02:30\n- później ⏰ 2026-03-29 03:20"_s);
        QueryOptions options;
        options.now = QTime(3, 15);
        const int overdue = runQuery(parsed(u"remind:overdue"_s), index, QDate(2026, 3, 29), Qt::Monday, options).total;
        if (oldTz.isNull())
            qunsetenv("TZ");
        else
            qputenv("TZ", oldTz);
        tzset();
        QCOMPARE(overdue, 1);
    }

    void emptyIndexAndEmptyQuery()
    {
        VaultIndex empty;
        const QueryResult r = runQuery(parsed(u"status:open"_s), empty, fixture::Today);
        QCOMPARE(r.total, 0);
        QVERIFY(r.items.isEmpty());
        QVERIFY(r.groups.isEmpty());
        // An empty query lists every block (frontmatter excluded, nested blocks collapsed).
        QVERIFY(run(QString()).total > 20);
    }

    // ---- Names in query blocks (renames and moves rewrite them) ------------------------------------

    // findQueryNames(): "kind value" of every name, and the value's range holds it as written.
    void findQueryNames_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QStringList>("names");
        const auto q = [](const QString &body) { return u"Wstęp\n\n```query\n"_s + body + u"\n```\n"_s; };
        QTest::newRow("every key") << q(u"type:tasks tag:praca ref:Dom page:Zakupy path:Projekty/ status:open"_s)
                                   << QStringList{u"tag praca"_s, u"ref Dom"_s, u"page Zakupy"_s, u"path Projekty/"_s};
        QTest::newRow("shorthands and negation")
                << q(u"#dom -#prywatne [[Jan Kowalski]] -[[Stare]] -tag:x"_s)
                << QStringList{u"tag dom"_s, u"tag prywatne"_s, u"ref Jan Kowalski"_s, u"ref Stare"_s, u"tag x"_s};
        QTest::newRow("Polish and folded keys")
                << q(u"tagi:ogród Strona:Łąka ŚCIEŻKA:Działka/ odnośnik:[[Żółw]] folder:a/ linki:b"_s)
                << QStringList{u"tag ogród"_s, u"page Łąka"_s, u"path Działka/"_s,
                               u"ref Żółw"_s,  u"path a/"_s,   u"ref b"_s};
        QTest::newRow("comma lists, quotes, brackets")
                << q(u"tag:praca,#dom,[[Mój tag]] page:\"Lista zakupów\",„Plan”,Inne ref:[[A, B]],C"_s)
                << QStringList{u"tag praca"_s, u"tag dom"_s,   u"tag Mój tag"_s, u"page Lista zakupów"_s,
                               u"page Plan"_s, u"page Inne"_s, u"ref A, B"_s,    u"ref C"_s};
        QTest::newRow("comments, phrases, URLs and text are no names")
                << q(u"# tag:komentarz\nref:Dom // tag:też-komentarz\n\"tag:fraza\" słowo ref://adres tag:"_s)
                << QStringList{u"ref Dom"_s};
        QTest::newRow("only inside query fences")
                << u"tag:tekst ref:Dom\n```js\ntag:kod\n```\n~~~query\ntag:tylda\n~~~\n```Query\nref:Duże\n```\n"_s
                << QStringList{u"tag tylda"_s, u"ref Duże"_s};
        QTest::newRow("in a list item (Logseq, the outline)")
                << u"- notatka\n- ```query\n  tag:lista\n  ```\n\t- dziecko tag:nie\n- tekst\n  ~~~query\n  ref:Dalej\n"
                   "  ~~~\n  ```js\n  tag:kod\n  ```\n1. ```query\n   page:Numer\n   ```\n"_s
                << QStringList{u"tag lista"_s, u"ref Dalej"_s, u"page Numer"_s};
        QTest::newRow("CRLF, an unclosed fence")
                << u"```query\r\ntag:a\r\nref:B\r\n"_s << QStringList{u"tag a"_s, u"ref B"_s};
        QTest::newRow("an unclosed quote: the names before it")
                << q(u"tag:przed \"bez końca ref:po"_s) << QStringList{u"tag przed"_s};
        QTest::newRow("blanks inside brackets")
                << q(u"ref:[[ Dom ]] tag:[[ x ]]"_s) << QStringList{u"ref Dom"_s, u"tag x"_s};
        QTest::newRow("no query") << u"- zwykła notatka #tag [[Link]]\n"_s << QStringList();
        QTest::newRow("two backticks or tildes are no fence")
                << u"``query\ntag:a\n``\n~~query\nref:B\n~~\n"_s << QStringList();
        QTest::newRow("the word query without a fence")
                << u"- query tag:x ref:[[Dom]]\n- `query` i ``query``\n"_s << QStringList();
        QTest::newRow("empty values") << q(u"tag: ref:[[]] page:\"\""_s) << QStringList();
    }
    void findQueryNames()
    {
        QFETCH(QString, content);
        QFETCH(QStringList, names);
        QStringList got;
        for (const QueryNameRef &n : brulion::findQueryNames(content)) {
            const QString kind = n.kind == QueryFilterKind::Tag    ? u"tag"_s
                                 : n.kind == QueryFilterKind::Ref  ? u"ref"_s
                                 : n.kind == QueryFilterKind::Page ? u"page"_s
                                                                   : u"path"_s;
            got.append(kind + u' ' + n.value);
            QCOMPARE(content.sliced(n.from, n.to - n.from), n.value);
        }
        QCOMPARE(got, names);
        // The same with the caller's parse of the note (the index's, with a path).
        const ParsedNote parsed = markdown::parseNote(u"Zapytania/Łąka.md"_s, content);
        QCOMPARE(brulion::findQueryNames(content, &parsed), brulion::findQueryNames(content));
    }

    // formatQueryName(): the new value in the old one's form, or in [[ ]] when bare cannot hold it.
    void formatQueryName_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("written");
        const auto q = [](const QString &body) { return u"```query\n"_s + body + u"\n```"_s; };
        QTest::newRow("bare") << q(u"tag:dom"_s) << u"Mieszkanie"_s << u"Mieszkanie"_s;
        QTest::newRow("bare with a space") << q(u"tag:dom"_s) << u"Mój dom"_s << u"[[Mój dom]]"_s;
        QTest::newRow("bare with a comma") << q(u"ref:dom"_s) << u"A, B"_s << u"[[A, B]]"_s;
        QTest::newRow("bare with a quote") << q(u"page:dom"_s) << u"Dom „Pod lasem”"_s << u"[[Dom „Pod lasem”]]"_s;
        QTest::newRow("bare starting with //") << q(u"path:a/"_s) << u"//x"_s << u"[[//x]]"_s;
        QTest::newRow("bracketed") << q(u"ref:[[Dom]]"_s) << u"Mój dom"_s << u"Mój dom"_s;
        QTest::newRow("quoted") << q(u"page:\"Dom\""_s) << u"Mój dom"_s << u"Mój dom"_s;
        QTest::newRow("quoted, its closing quote inside") << q(u"page:\"Dom\""_s) << u"a\"b"_s << QString();
        QTest::newRow("brackets inside brackets") << q(u"ref:[[Dom]]"_s) << u"a]]b"_s << QString();
        QTest::newRow("a line break") << q(u"ref:Dom"_s) << u"a\nb"_s << QString();
        QTest::newRow("empty") << q(u"ref:Dom"_s) << u" "_s << QString();
        QTest::newRow("the tag shorthand") << q(u"#dom"_s) << u"Projekty/Mój dom"_s << u"[[Projekty/Mój dom]]"_s;
    }
    void formatQueryName()
    {
        QFETCH(QString, content);
        QFETCH(QString, value);
        QFETCH(QString, written);
        const QList<QueryNameRef> names = brulion::findQueryNames(content);
        QCOMPARE(names.size(), 1);
        QCOMPARE(brulion::formatQueryName(names.first(), value), written);
    }

    // retargetQueryNames(): only the named values change, byte for byte everything else; the query
    // still parses and reads the new values.
    void retargetQueryNames()
    {
        const QString content = u"\uFEFF# Zapytania\r\n\r\n```query\r\n# Moje zadania\r\ntype:tasks -tag:dom,praca "
                                "#dom\r\nref:[[Dom]] page:\"Dom\" // tag:dom\r\n```\r\ntag:dom poza zapytaniem\r\n"_s;
        const QList<QueryNameRef> names = brulion::findQueryNames(content);
        QCOMPARE(names.size(), 5);
        QList<QueryNameEdit> edits;
        for (const QueryNameRef &n : names) {
            if (n.value.compare(u"dom"_s, Qt::CaseInsensitive) == 0)
                edits.append({n.from, n.to, n.kind == QueryFilterKind::Tag ? u"Mój dom"_s : u"Projekty/Mój dom"_s});
        }
        QCOMPARE(edits.size(), 4);
        edits.append({0, 3, u"x"_s});                             // no value there: ignored
        edits.append({names.at(0).from, names.at(0).to, u"y"_s}); // overlaps the first: ignored
        int applied = -1;
        const QString out = brulion::retargetQueryNames(content, edits, &applied);
        QCOMPARE(applied, 4);
        QCOMPARE(out, u"\uFEFF# Zapytania\r\n\r\n```query\r\n# Moje zadania\r\ntype:tasks -tag:[[Mój dom]],praca "
                      "#[[Mój dom]]\r\nref:[[Projekty/Mój dom]] page:\"Projekty/Mój dom\" // tag:dom\r\n```\r\n"
                      "tag:dom poza zapytaniem\r\n"_s);
        // The query reads the new names.
        const QList<QueryNameRef> after = brulion::findQueryNames(out);
        QCOMPARE(after.at(0).value, u"Mój dom"_s);
        QCOMPARE(after.at(2).value, u"Mój dom"_s);
        QCOMPARE(after.at(3).value, u"Projekty/Mój dom"_s);
        const QString body = out.section(u"```query\r\n"_s, 1).section(u"\r\n```"_s, 0, 0);
        const auto parsedQuery = parseQuery(body);
        QVERIFY(std::holds_alternative<Query>(parsedQuery));
        const Query &query = std::get<Query>(parsedQuery);
        QCOMPARE(query.filters.at(0).values, (QStringList{u"mój dom"_s, u"praca"_s}));
        QCOMPARE(query.filters.at(1).values, QStringList{u"mój dom"_s});
        QCOMPARE(query.filters.at(2).values, QStringList{u"Projekty/Mój dom"_s});
        QCOMPARE(query.filters.at(3).values, QStringList{u"Projekty/Mój dom"_s});
        // Nothing to do: the same string.
        QCOMPARE(brulion::retargetQueryNames(content, {}), content);
        QCOMPARE(brulion::retargetQueryNames(content, {{names.at(0).from, names.at(0).to, u"dom"_s}}, &applied),
                 content);
        QCOMPARE(applied, 0);
    }
};

QTEST_GUILESS_MAIN(TestIndexQuery)
#include "tst_index_query.moc"
