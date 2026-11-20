// Property filters, sorting and grouping by properties, and view options of the query language
// (docs/SYNTAX.md §10 "Properties", "Views"): parseQuery() and runQuery() over a notebook of
// projects written in YAML frontmatter, Logseq `key:: value` lines and block properties.
#include "index/index.h"
#include "index/querycache.h"

#include <QTest>

#include <vector>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

const QDate Today(2026, 10, 14); // a Wednesday

QString lines(const QStringList &l)
{
    return l.join(u'\n');
}

QList<std::pair<QString, QString>> notebook()
{
    return {
            {u"Projekty/Dom.md"_s,
             lines({u"---"_s, u"status: w toku"_s, u"koszt: 1200"_s, u"Budżet: 5000.5"_s, u"termin: 2026-10-20"_s,
                    u"właściciel: \"[[Jan Kowalski]]\""_s, u"etykiety: [remont, łazienka]"_s, u"pilne: true"_s,
                    u"tags: [projekt]"_s, u"Termin oddania: 2026-11-01"_s, u"---"_s, u"# Dom"_s, u""_s,
                    u"- [ ] Kupić płytki 📅 2026-10-15"_s, u"  etap:: zakupy"_s, u"- [x] Wybrać ekipę"_s,
                    u"  etap:: [[Ekipa]]"_s, u"  - [ ] Podpisać umowę"_s, u"- [ ] Bez etapu"_s})},
            {u"Projekty/Ogród.md"_s,
             lines({u"---"_s, u"status: planowany"_s, u"koszt: 300"_s, u"Budżet: 800"_s, u"termin: 2026-10-10"_s,
                    u"właściciel: \"[[Anna]]\""_s, u"etykiety: [ogród]"_s, u"pilne: false"_s, u"tags: [projekt]"_s,
                    u"---"_s, u"- [ ] Posadzić tulipany"_s})},
            {u"Projekty/Garaż.md"_s,
             lines({u"status:: zrobione"_s, u"koszt:: 99.5"_s, u"wlasciciel:: [[Jan Kowalski]]"_s, u"tags:: projekt"_s,
                    u""_s, u"- [ ] Pomalować drzwi"_s})},
            {u"Projekty/Strych.md"_s, lines({u"---"_s, u"status: \"W toku\""_s, u"koszt: \"3,5\""_s,
                                             u"tags: [projekt]"_s, u"---"_s, u"Notatki."_s})},
            {u"Ludzie/Jan Kowalski.md"_s, lines({u"---"_s, u"aliases: [Janek]"_s, u"---"_s, u"Hydraulik."_s})},
            {u"Ludzie/Anna.md"_s, u"Ogrodniczka."_s},
            {u"Ekipa.md"_s, u"Ekipa remontowa."_s},
            {u"Inne.md"_s, u"Bez właściwości."_s},
    };
}

Query parsed(const QString &source)
{
    const auto r = parseQuery(source);
    if (const auto *error = std::get_if<QString>(&r))
        qWarning("unexpected query error: %s", qPrintable(*error));
    return std::get<Query>(r);
}

/// The paths (pages) or the first lines' texts (blocks) of the items, in order.
QStringList describe(const VaultIndex &index, const QueryResult &result)
{
    QStringList out;
    for (const QueryItem &item : result.items) {
        if (item.blockIndex < 0) {
            out.append(item.path);
            continue;
        }
        const ParsedNote *note = index.note(item.path);
        out.append(note ? note->blocks.at(item.blockIndex).text.section(u'\n', 0, 0) : QString());
    }
    return out;
}

} // namespace

class TestIndexPropertyQuery : public QObject {
    Q_OBJECT

private:
    VaultIndex m_index;

    QueryResult run(const QString &source, const QString &currentPath = QString())
    {
        QueryOptions options;
        options.currentPath = currentPath;
        return runQuery(parsed(source), m_index, Today, Qt::Monday, options);
    }

private Q_SLOTS:
    void initTestCase()
    {
        for (const auto &[path, content] : notebook())
            m_index.update(path, content);
    }

    void parseProperties_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("key");
        QTest::addColumn<int>("op");
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("to");
        QTest::addColumn<bool>("quoted");
        QTest::addColumn<bool>("negate");
        using O = QueryPropertyOp;
        QTest::newRow("plain") << u"status::done"_s << u"status"_s << int(O::Equals) << u"done"_s << QString() << false
                               << false;
        QTest::newRow("quoted value") << u"status::\"w toku\""_s << u"status"_s << int(O::Equals) << u"w toku"_s
                                      << QString() << true << false;
        QTest::newRow("Polish quotes") << u"status::„w toku”"_s << u"status"_s << int(O::Equals) << u"w toku"_s
                                       << QString() << true << false;
        QTest::newRow("greater") << u"koszt::>100"_s << u"koszt"_s << int(O::Greater) << u"100"_s << QString() << false
                                 << false;
        QTest::newRow("at most") << u"koszt::<=1200"_s << u"koszt"_s << int(O::LessOrEqual) << u"1200"_s << QString()
                                 << false << false;
        QTest::newRow("spaced operator") << u"koszt::\">= 5\""_s << u"koszt"_s << int(O::Equals) << u">= 5"_s
                                         << QString() << true << false; // quoted: a text
        QTest::newRow("range") << u"koszt::100..200"_s << u"koszt"_s << int(O::Range) << u"100"_s << u"200"_s << false
                               << false;
        QTest::newRow("date range") << u"termin::today..+7d"_s << u"termin"_s << int(O::Range) << u"today"_s << u"+7d"_s
                                    << false << false;
        QTest::newRow("none") << u"termin::none"_s << u"termin"_s << int(O::None) << QString() << QString() << false
                              << false;
        QTest::newRow("Polish none") << u"termin::brak"_s << u"termin"_s << int(O::None) << QString() << QString()
                                     << false << false;
        QTest::newRow("any") << u"ocena::dowolna"_s << u"ocena"_s << int(O::Any) << QString() << QString() << false
                             << false;
        QTest::newRow("negated") << u"-status::zrobione"_s << u"status"_s << int(O::Equals) << u"zrobione"_s
                                 << QString() << false << true;
        QTest::newRow("glob") << u"nazwa::*dom*"_s << u"nazwa"_s << int(O::Glob) << u"*dom*"_s << QString() << false
                              << false;
        QTest::newRow("link") << u"właściciel::[[Jan Kowalski]]"_s << u"wlasciciel"_s << int(O::Equals)
                              << u"[[Jan Kowalski]]"_s << QString() << false << false;
        QTest::newRow("Polish key folded") << u"Budżet::>=100"_s << u"budzet"_s << int(O::GreaterOrEqual) << u"100"_s
                                           << QString() << false << false;
        QTest::newRow("quoted key") << u"\"Termin oddania\"::2026-11-01"_s << u"termin-oddania"_s << int(O::Equals)
                                    << u"2026-11-01"_s << QString() << false << false;
        QTest::newRow("key with a dash") << u"termin-oddania::any"_s << u"termin-oddania"_s << int(O::Any) << QString()
                                         << QString() << false << false;
        QTest::newRow("quoted none is text")
                << u"status::\"none\""_s << u"status"_s << int(O::Equals) << u"none"_s << QString() << true << false;
        QTest::newRow("equals sign") << u"koszt::=99.5"_s << u"koszt"_s << int(O::Equals) << u"99.5"_s << QString()
                                     << false << false;
    }
    void parseProperties()
    {
        QFETCH(QString, source);
        const Query q = parsed(source);
        QCOMPARE(q.filters.size(), 1);
        const QueryFilter &f = q.filters.first();
        QCOMPARE(f.kind, QueryFilterKind::Property);
        QTEST(f.property, "key");
        QTEST(f.negate, "negate");
        QCOMPARE(f.conditions.size(), 1);
        QTEST(int(f.conditions.first().op), "op");
        QTEST(f.conditions.first().value, "value");
        QTEST(f.conditions.first().to, "to");
        QTEST(f.conditions.first().quoted, "quoted");
        // A property filter makes no task query.
        QCOMPARE(q.type, QueryType::Blocks);
    }

    void parseOrValues()
    {
        const Query q = parsed(u"status::\"w toku\",planowany koszt::<100,>1000"_s);
        QCOMPARE(q.filters.size(), 2);
        QCOMPARE(q.filters[0].conditions.size(), 2);
        QCOMPARE(q.filters[0].conditions[1].value, u"planowany"_s);
        QCOMPARE(q.filters[1].conditions.size(), 2);
        QCOMPARE(q.filters[1].conditions[0].op, QueryPropertyOp::Less);
        QCOMPARE(q.filters[1].conditions[1].op, QueryPropertyOp::Greater);
    }

    void parseErrors_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("fragment");
        QTest::newRow("empty value") << u"status::"_s << u"needs a value"_s;
        QTest::newRow("operator only") << u"koszt::>"_s << u"needs a value"_s;
        QTest::newRow("operator and keyword") << u"termin::<none"_s << u"cannot be combined"_s;
        QTest::newRow("open range") << u"koszt::100.."_s << u"needs both ends"_s;
        QTest::newRow("range without start") << u"koszt::..5"_s << u"needs both ends"_s;
        QTest::newRow("bad view") << u"view:mapa"_s << u"Invalid value “mapa”"_s;
        QTest::newRow("two views") << u"view:table widok:tablica"_s << u"only once"_s;
        QTest::newRow("view negated") << u"-view:table"_s << u"cannot be negated"_s;
        QTest::newRow("empty column label") << u"columns:\" = koszt\""_s << u"Invalid column"_s;
        QTest::newRow("empty formula") << u"columns:\"Brutto =\""_s << u"Invalid column"_s;
        QTest::newRow("bad summary") << u"summary:koszt=suma2"_s << u"Invalid value"_s;
        QTest::newRow("summary without function") << u"summary:koszt"_s << u"Invalid value"_s;
        QTest::newRow("two groups") << u"group:status group:etap"_s << u"only once"_s;
        QTest::newRow("unclosed quoted key") << u"\"termin::5"_s << u"Missing closing quote"_s;
    }
    void parseErrors()
    {
        QFETCH(QString, source);
        QFETCH(QString, fragment);
        const auto r = parseQuery(source);
        QVERIFY2(std::holds_alternative<QString>(r), qPrintable(source));
        const QString error = std::get<QString>(r);
        QVERIFY2(error.contains(fragment), qPrintable(error));
    }

    void parseViews()
    {
        const Query q =
                parsed(u"type:pages view:tabela kolumny:tytuł,status,Budżet,\"Brutto = koszt * 1.23\","
                       u"\"Koszt (zł) = koszt\",due summary:koszt=suma,Brutto=średnia sort:-koszt group:status"_s);
        QCOMPARE(q.view, QueryView::Table);
        QCOMPARE(q.columns.size(), 6);
        QCOMPARE(q.columns[0].kind, QueryColumnKind::Title);
        // On pages a task key names a property.
        QCOMPARE(q.columns[1].kind, QueryColumnKind::Property);
        QCOMPARE(q.columns[1].property, u"status"_s);
        QCOMPARE(q.columns[2].kind, QueryColumnKind::Property);
        QCOMPARE(q.columns[2].property, u"budzet"_s);
        QCOMPARE(q.columns[2].name, u"Budżet"_s);
        QCOMPARE(q.columns[3].kind, QueryColumnKind::Formula);
        QCOMPARE(q.columns[3].label, u"Brutto"_s);
        QCOMPARE(q.columns[3].formula, u"koszt * 1.23"_s);
        QCOMPARE(q.columns[4].kind, QueryColumnKind::Property);
        QCOMPARE(q.columns[4].label, u"Koszt (zł)"_s);
        QCOMPARE(q.columns[4].property, u"koszt"_s);
        QCOMPARE(q.columns[5].kind, QueryColumnKind::Property);
        QCOMPARE(q.columns[5].property, u"due"_s);
        QCOMPARE(q.summaries.size(), 2);
        QCOMPARE(q.summaries[0].column, u"koszt"_s);
        QCOMPARE(q.summaries[0].function, u"sum"_s);
        QCOMPARE(q.summaries[1].column, u"brutto"_s);
        QCOMPARE(q.summaries[1].function, u"average"_s);
        QCOMPARE(q.sort.size(), 1);
        QCOMPARE(q.sort[0].key, QuerySortKey::Property);
        QCOMPARE(q.sort[0].property, u"koszt"_s);
        QVERIFY(q.sort[0].descending);
        QCOMPARE(q.group, QueryGroupKey::Property);
        QCOMPARE(q.groupProperty, u"status"_s);

        // Tasks keep the built-ins; `::` forces the property.
        const Query t = parsed(u"view:board group:status sort:due,priorytet::"_s);
        QCOMPARE(t.type, QueryType::Tasks);
        QCOMPARE(t.view, QueryView::Board);
        QCOMPARE(t.group, QueryGroupKey::Status);
        QCOMPARE(t.sort[0].key, QuerySortKey::Due);
        QCOMPARE(t.sort[1].key, QuerySortKey::Property);
        QCOMPARE(t.sort[1].property, u"priorytet"_s);
        const Query g = parsed(u"type:tasks group:status::"_s);
        QCOMPARE(g.group, QueryGroupKey::Property);
        QCOMPARE(g.groupProperty, u"status"_s);
        // An unknown sort name is a property (it used to be an error).
        const Query u = parsed(u"sort:size,-ocena"_s);
        QCOMPARE(u.sort[0].key, QuerySortKey::Property);
        QCOMPARE(u.sort[1].property, u"ocena"_s);
        QVERIFY(u.sort[1].descending);
        QCOMPARE(u.type, QueryType::Blocks);
        for (const QString &view :
             {u"widok:kalendarz"_s, u"view:galeria"_s, u"view:cards"_s, u"view:lista"_s, u"view:kanban"_s}) {
            QVERIFY2(parsed(view).view != QueryView::None, qPrintable(view));
        }
    }

    void pageFilters_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QStringList>("expected");
        const QString pages = u"type:pages path:Projekty/ "_s;
        QTest::newRow("text equality, case and diacritics")
                << pages + u"status::\"W TOKU\""_s << QStringList{u"Projekty/Dom.md"_s, u"Projekty/Strych.md"_s};
        QTest::newRow("bare words need quotes")
                << pages + u"status::planowany"_s << QStringList{u"Projekty/Ogród.md"_s};
        QTest::newRow("folded value") << pages + u"status::\"w toku\",planowany"_s
                                      << QStringList{u"Projekty/Dom.md"_s, u"Projekty/Ogród.md"_s,
                                                     u"Projekty/Strych.md"_s};
        QTest::newRow("negated") << pages + u"-status::zrobione"_s
                                 << QStringList{u"Projekty/Dom.md"_s, u"Projekty/Ogród.md"_s, u"Projekty/Strych.md"_s};
        QTest::newRow("number greater") << pages + u"koszt::>100"_s
                                        << QStringList{u"Projekty/Dom.md"_s, u"Projekty/Ogród.md"_s};
        QTest::newRow("number less (decimal comma read)")
                << pages + u"koszt::<100"_s << QStringList{u"Projekty/Garaż.md"_s, u"Projekty/Strych.md"_s};
        QTest::newRow("number equal") << pages + u"koszt::99.5"_s << QStringList{u"Projekty/Garaż.md"_s};
        QTest::newRow("number range") << pages + u"koszt::99..1000"_s
                                      << QStringList{u"Projekty/Garaż.md"_s, u"Projekty/Ogród.md"_s};
        QTest::newRow("Polish key, diacritics folded")
                << pages + u"budzet::>1000"_s << QStringList{u"Projekty/Dom.md"_s};
        QTest::newRow("date before") << pages + u"termin::<today"_s << QStringList{u"Projekty/Ogród.md"_s};
        QTest::newRow("date within a week") << pages + u"termin::today..+7d"_s << QStringList{u"Projekty/Dom.md"_s};
        QTest::newRow("date this month") << pages + u"termin::this-month"_s
                                         << QStringList{u"Projekty/Dom.md"_s, u"Projekty/Ogród.md"_s};
        QTest::newRow("date exact") << pages + u"termin::2026-10-20"_s << QStringList{u"Projekty/Dom.md"_s};
        QTest::newRow("date none") << pages + u"termin::none"_s
                                   << QStringList{u"Projekty/Garaż.md"_s, u"Projekty/Strych.md"_s};
        QTest::newRow("any") << pages + u"termin::any"_s << QStringList{u"Projekty/Dom.md"_s, u"Projekty/Ogród.md"_s};
        QTest::newRow("not none") << pages + u"-termin::brak"_s
                                  << QStringList{u"Projekty/Dom.md"_s, u"Projekty/Ogród.md"_s};
        QTest::newRow("link by page") << pages + u"właściciel::[[Jan Kowalski]]"_s
                                      << QStringList{u"Projekty/Dom.md"_s, u"Projekty/Garaż.md"_s};
        QTest::newRow("link by alias") << pages + u"wlasciciel::[[Janek]]"_s
                                       << QStringList{u"Projekty/Dom.md"_s, u"Projekty/Garaż.md"_s};
        QTest::newRow("link by bare name") << pages + u"właściciel::anna"_s << QStringList{u"Projekty/Ogród.md"_s};
        QTest::newRow("list item") << pages + u"etykiety::łazienka"_s << QStringList{u"Projekty/Dom.md"_s};
        QTest::newRow("list item folded")
                << pages + u"etykiety::lazienka,ogrod"_s << QStringList{u"Projekty/Dom.md"_s, u"Projekty/Ogród.md"_s};
        QTest::newRow("checkbox") << pages + u"pilne::tak"_s << QStringList{u"Projekty/Dom.md"_s};
        QTest::newRow("checkbox false") << pages + u"pilne::false"_s << QStringList{u"Projekty/Ogród.md"_s};
        QTest::newRow("glob") << pages + u"status::*tok*"_s
                              << QStringList{u"Projekty/Dom.md"_s, u"Projekty/Strych.md"_s};
        QTest::newRow("quoted key with blank")
                << pages + u"\"termin oddania\"::2026-11-01"_s << QStringList{u"Projekty/Dom.md"_s};
        QTest::newRow("text compare") << pages + u"status::>r"_s
                                      << QStringList{u"Projekty/Dom.md"_s, u"Projekty/Strych.md"_s,
                                                     u"Projekty/Garaż.md"_s};
        QTest::newRow("number vs text never") << pages + u"status::>5"_s << QStringList{};
        QTest::newRow("tags key") << u"type:pages tags::projekt"_s
                                  << QStringList{u"Projekty/Dom.md"_s, u"Projekty/Garaż.md"_s, u"Projekty/Ogród.md"_s,
                                                 u"Projekty/Strych.md"_s};
        QTest::newRow("two filters AND") << pages + u"koszt::>100 pilne::true"_s << QStringList{u"Projekty/Dom.md"_s};
        QTest::newRow("unknown key none") << pages + u"nieznany::none"_s
                                          << QStringList{u"Projekty/Dom.md"_s, u"Projekty/Garaż.md"_s,
                                                         u"Projekty/Ogród.md"_s, u"Projekty/Strych.md"_s};
    }
    void pageFilters()
    {
        QFETCH(QString, source);
        QFETCH(QStringList, expected);
        QStringList got = describe(m_index, run(source + u" sort:path"_s));
        std::ranges::sort(got);
        std::ranges::sort(expected);
        QCOMPARE(got, expected);
    }

    void blockFilters()
    {
        // A block's own property, else its parent's, else its page's.
        QCOMPARE(describe(m_index, run(u"type:tasks etap::zakupy"_s)), QStringList{u"Kupić płytki"_s});
        QCOMPARE(describe(m_index, run(u"type:tasks etap::[[Ekipa]]"_s)),
                 QStringList({u"Wybrać ekipę"_s, u"Podpisać umowę"_s}));
        QCOMPARE(describe(m_index, run(u"type:tasks etap::none path:Projekty/Dom"_s)), QStringList{u"Bez etapu"_s});
        // Page properties reach the page's tasks.
        QCOMPARE(describe(m_index, run(u"type:tasks status::zrobione"_s)), QStringList{u"Pomalować drzwi"_s});
        QCOMPARE(describe(m_index, run(u"type:tasks koszt::<500 sort:path"_s)),
                 QStringList({u"Pomalować drzwi"_s, u"Posadzić tulipany"_s}));
        QCOMPARE(describe(m_index, run(u"status:open koszt::300"_s)), QStringList{u"Posadzić tulipany"_s});
    }

    void sortByProperty()
    {
        QCOMPARE(describe(m_index, run(u"type:pages path:Projekty/ sort:koszt"_s)),
                 QStringList({u"Projekty/Strych.md"_s, u"Projekty/Garaż.md"_s, u"Projekty/Ogród.md"_s,
                              u"Projekty/Dom.md"_s}));
        // Descending; a missing value stays last.
        QCOMPARE(describe(m_index, run(u"type:pages path:Projekty/ sort:-termin"_s)),
                 QStringList({u"Projekty/Dom.md"_s, u"Projekty/Ogród.md"_s, u"Projekty/Garaż.md"_s,
                              u"Projekty/Strych.md"_s}));
        QCOMPARE(describe(m_index, run(u"type:pages path:Projekty/ sort:status,page"_s)),
                 QStringList({u"Projekty/Ogród.md"_s, u"Projekty/Dom.md"_s, u"Projekty/Strych.md"_s,
                              u"Projekty/Garaż.md"_s}));
        QCOMPARE(describe(m_index, run(u"type:tasks path:Projekty/Dom sort:etap::"_s)),
                 QStringList({u"Wybrać ekipę"_s, u"Podpisać umowę"_s, u"Kupić płytki"_s, u"Bez etapu"_s}));
    }

    void groupByProperty()
    {
        const QueryResult r = run(u"type:pages path:Projekty/ group:status"_s);
        QStringList groups;
        for (const QueryGroup &g : r.groups)
            groups.append(g.label + u'=' + QString::number(g.items.size()) + u'|' + g.value);
        // Texts by name (case-insensitive keys: "w toku" and "W toku" are one group).
        QCOMPARE(groups, QStringList({u"planowany=1|planowany"_s, u"w toku=2|w toku"_s, u"zrobione=1|zrobione"_s}));

        const QueryResult numbers = run(u"type:pages path:Projekty/ group:koszt"_s);
        QStringList keys;
        for (const QueryGroup &g : numbers.groups)
            keys.append(g.label);
        QCOMPARE(keys, QStringList({u"3,5"_s, u"99.5"_s, u"300"_s, u"1200"_s}));

        // A link groups by the page it names; a list by each item; none last.
        const QueryResult owners = run(u"type:pages group:właściciel"_s);
        QStringList owned;
        for (const QueryGroup &g : owners.groups)
            owned.append(g.label + u'=' + QString::number(g.items.size()));
        QCOMPARE(owned.first(), u"Anna=1"_s);
        QCOMPARE(owned.at(1), u"Jan Kowalski=2"_s);
        QCOMPARE(owners.groups.last().key, QString());
        QCOMPARE(owners.groups.at(1).key, u"Ludzie/Jan Kowalski.md"_s);

        const QueryResult labels = run(u"type:pages path:Projekty/ group:etykiety"_s);
        QStringList tags;
        for (const QueryGroup &g : labels.groups)
            tags.append(g.label);
        QCOMPARE(tags, QStringList({u"łazienka"_s, u"ogród"_s, u"remont"_s, u"No value"_s}));

        const QueryResult stages = run(u"type:tasks path:Projekty/Dom group:etap"_s);
        QStringList stageGroups;
        for (const QueryGroup &g : stages.groups)
            stageGroups.append(g.label + u'=' + QString::number(g.items.size()) + u'|' + g.value);
        QCOMPARE(stageGroups, QStringList({u"Ekipa=2|Ekipa"_s, u"zakupy=1|zakupy"_s, u"No value=1|"_s}));
    }

    void cacheEqualsRunAfterEdits()
    {
        VaultIndex index;
        for (const auto &[path, content] : notebook())
            index.update(path, content);
        const QStringList queries{u"type:pages koszt::>100 sort:-koszt"_s, u"type:pages group:status"_s,
                                  u"type:tasks etap::[[Ekipa]]"_s, u"type:pages właściciel::[[Jan Kowalski]]"_s};
        std::vector<QueryCache> caches(queries.size());
        const auto check = [&](const char *step) {
            for (qsizetype i = 0; i < queries.size(); ++i) {
                const Query q = parsed(queries.at(i));
                const QueryResult cached = caches[size_t(i)].run(q, index, Today);
                const QueryResult fresh = runQuery(q, index, Today);
                QCOMPARE(describe(index, cached), describe(index, fresh));
                QCOMPARE(cached.groups.size(), fresh.groups.size());
                for (qsizetype g = 0; g < fresh.groups.size(); ++g)
                    QCOMPARE(cached.groups[g].key, fresh.groups[g].key);
                Q_UNUSED(step);
            }
        };
        check("start");
        index.update(u"Projekty/Strych.md"_s, lines({u"---"_s, u"status: zrobione"_s, u"koszt: 5000"_s,
                                                     u"właściciel: \"[[Janek]]\""_s, u"---"_s, u"Notatki."_s}));
        check("edit");
        // A rename of the page links name: link filters follow it.
        index.remove(u"Ludzie/Jan Kowalski.md"_s);
        index.update(u"Ludzie/Jan Nowak.md"_s, lines({u"---"_s, u"aliases: [Janek]"_s, u"---"_s, u"Hydraulik."_s}));
        check("rename");
    }

    void propertyLinksAreRenameNames()
    {
        const QString note = lines({u"```query"_s, u"type:pages projekt::[[Projekty/Dom]],[[Ogród]] -szef::=[[Jan]]"_s,
                                    u"status::done koszt::[[a]]..[[b]]"_s, u"```"_s});
        const QList<QueryNameRef> names = findQueryNames(note);
        QStringList values;
        for (const QueryNameRef &n : names) {
            QCOMPARE(n.kind, QueryFilterKind::Ref);
            QVERIFY(n.bracketed);
            values.append(n.value);
            QCOMPARE(note.sliced(n.from, n.to - n.from), n.value);
        }
        QCOMPARE(values, QStringList({u"Projekty/Dom"_s, u"Ogród"_s, u"Jan"_s}));
        const QString renamed = retargetQueryNames(note, {{names[0].from, names[0].to, u"Archiwum/Dom"_s}});
        QVERIFY(renamed.contains(u"projekt::[[Archiwum/Dom]],[[Ogród]]"_s));
    }

    void namesResolve()
    {
        QVERIFY(queryResolvesNames(parsed(u"status::done"_s)));
        QVERIFY(queryResolvesNames(parsed(u"type:pages group:etap"_s)));
        QVERIFY(!queryResolvesNames(parsed(u"type:pages sort:koszt"_s)));
    }

    void keyAndSummaryHelpers()
    {
        QCOMPARE(queryPropertyKey(u"Termin Oddania"), u"termin-oddania"_s);
        QCOMPARE(queryPropertyKey(u"budżet_roczny::"), u"budzet-roczny"_s);
        QCOMPARE(querySummaryFunction(u"Średnia"), u"average"_s);
        QCOMPARE(querySummaryFunction(u"najpóźniej"), u"latest"_s);
        QCOMPARE(querySummaryFunction(u"bogus"), QString());
    }

    void malformedValuesNeverCrash()
    {
        const QStringList sources{u"a::[["_s,
                                  u"a::]]"_s,
                                  u"a::\""_s,
                                  u"a::„x"_s,
                                  u"a::<<<5"_s,
                                  u"a::.."_s,
                                  u"a::*"_s,
                                  u"a::[[x]].."_s,
                                  u"::x"_s,
                                  u"a:: "_s,
                                  u"a::,,,"_s,
                                  u"a::>=[[x]]"_s,
                                  u"a::9999999999999999999999999"_s,
                                  u"a::-1e400"_s,
                                  u"a::2026-13-45"_s,
                                  u"\"\"::x"_s,
                                  u"a::b::c"_s};
        for (const QString &s : sources) {
            const auto r = parseQuery(s);
            if (const Query *q = std::get_if<Query>(&r))
                runQuery(*q, m_index, Today);
        }
    }
};

QTEST_GUILESS_MAIN(TestIndexPropertyQuery)
#include "tst_index_propquery.moc"
