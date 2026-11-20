// Views of queries (docs/SYNTAX.md §10 "Views"): columns, cells, boards, galleries, calendars,
// summaries, formulas and new rows over a small Polish notebook. "Today" is Wednesday 2026-10-14.
#include "views/edits.h"
#include "views/view.h"

#include <QTest>

using namespace brulion;
using namespace brulion::views;
using namespace Qt::StringLiterals;

namespace {

const QDate Today(2026, 10, 14);

QString lines(const QStringList &l)
{
    return l.join(u'\n');
}

void build(VaultIndex &index)
{
    index.update(u"Projekty/Dom.md"_s,
                 lines({u"---"_s, u"status: w toku"_s, u"koszt: 1200"_s, u"Budżet: 5000"_s, u"termin: 2026-10-20"_s,
                        u"właściciel: \"[[Jan Kowalski]]\""_s, u"etykiety: [remont, łazienka]"_s, u"pilne: true"_s,
                        u"banner: \"![[góry.jpg]]\""_s, u"tags: [projekt]"_s, u"---"_s, u"# Dom"_s, u""_s,
                        u"- [ ] Kupić płytki 📅 2026-10-15"_s, u"  etap:: zakupy"_s, u"- [/] Malowanie salonu ⏫"_s,
                        u"- [x] Wybrać ekipę ✅ 2026-10-01"_s, u"- [-] Anulowane"_s}));
    index.update(u"Projekty/Ogród.md"_s,
                 lines({u"---"_s, u"status: planowany"_s, u"koszt: 300"_s, u"Budżet: 800"_s, u"termin: 2026-11-02"_s,
                        u"pilne: false"_s, u"tags: [projekt]"_s, u"---"_s, u"Zdjęcie: ![[tulipany.png]]"_s, u""_s,
                        u"- [ ] Posadzić tulipany 📅 2026-10-30"_s}));
    index.update(u"Projekty/Garaż.md"_s,
                 lines({u"status:: zrobione"_s, u"koszt:: 99.5"_s, u"tags:: projekt"_s, u""_s, u"Bez zadań."_s}));
    index.update(u"Projekty/Strych.md"_s,
                 lines({u"---"_s, u"status: w toku"_s, u"tags: [projekt]"_s, u"---"_s, u"Notatki."_s}));
    index.update(u"Ludzie/Jan Kowalski.md"_s, u"Hydraulik."_s);
    index.update(u"Zapytania.md"_s, u"Strona z zapytaniami."_s);
    index.update(u"assets/góry.jpg"_s, QString());
    index.update(u"assets/tulipany.png"_s, QString());
    index.update(u"journals/2026-10-12.md"_s, u"- [ ] Zadzwonić 📅 2026-10-12"_s);
}

ViewOptions options()
{
    ViewOptions o;
    o.today = Today;
    o.now = QDateTime(Today, QTime(10, 0));
    o.currentPath = u"Zapytania.md"_s;
    o.newPagesFolder = u"Nowe"_s;
    o.stat = [](const VaultPath &path) -> std::optional<FileStat> {
        return FileStat{path, false, 2048, QDateTime(QDate(2026, 10, 10), QTime(8, 0)).toMSecsSinceEpoch()};
    };
    return o;
}

QStringList labels(const ViewResult &v)
{
    QStringList out;
    for (const Column &c : v.columns)
        out.append(c.label);
    return out;
}

QStringList column(const ViewResult &v, int c)
{
    QStringList out;
    for (const Row &r : v.rows)
        out.append(r.cells.at(c).markdown);
    return out;
}

} // namespace

class TestViews : public QObject {
    Q_OBJECT

private:
    VaultIndex m_index;

    ViewResult view(const QString &source, const ViewOptions &o = options())
    {
        const auto parsed = parseQuery(source);
        if (const QString *error = std::get_if<QString>(&parsed)) {
            qWarning("query error: %s", qPrintable(*error));
            return {};
        }
        const Query &q = std::get<Query>(parsed);
        QueryOptions qo;
        qo.currentPath = o.currentPath;
        qo.defaultLimit = q.view == QueryView::Calendar ? MaxQueryLimit : o.defaultLimit;
        const QueryResult r = runQuery(q, m_index, o.today, o.weekStart, qo);
        const auto built = buildQueryView(q, r, m_index, o);
        if (const QString *error = std::get_if<QString>(&built)) {
            qWarning("view error: %s", qPrintable(*error));
            return {};
        }
        return std::get<ViewResult>(built);
    }
    QString viewError(const QString &source)
    {
        const Query q = std::get<Query>(parseQuery(source));
        const QueryResult r = runQuery(q, m_index, Today);
        const auto built = buildQueryView(q, r, m_index, options());
        const QString *error = std::get_if<QString>(&built);
        return error ? *error : QString();
    }

private Q_SLOTS:
    void initTestCase() { build(m_index); }

    void defaultColumnsOfPages()
    {
        const ViewResult v = view(u"type:pages tag:projekt view:table sort:page"_s);
        QCOMPARE(v.kind, ViewKind::Table);
        QCOMPARE(v.rows.size(), 4);
        // The title, then the properties most results have (as most notes spell them).
        const QStringList l = labels(v);
        QCOMPARE(l.first(), u"Title"_s);
        QVERIFY2(l.contains(u"status"_s) && l.contains(u"koszt"_s), qPrintable(l.join(u", ")));
        QVERIFY(!l.contains(u"tags"_s) && !l.contains(u"banner"_s));
        QCOMPARE(column(v, 0), QStringList({u"[[Dom]]"_s, u"[[Garaż]]"_s, u"[[Ogród]]"_s, u"[[Strych]]"_s}));
    }

    void columnsAndCells()
    {
        const ViewResult v = view(u"type:pages tag:projekt view:table sort:-koszt "_s
                                  u"columns:title,status,Budżet,koszt,termin,pilne,etykiety,właściciel,"_s
                                  u"\"Brutto = koszt * 1.23\",\"Koszt (zł) = koszt\",path,mtime,size"_s);
        QCOMPARE(labels(v),
                 QStringList({u"Title"_s, u"status"_s, u"Budżet"_s, u"koszt"_s, u"termin"_s, u"pilne"_s, u"etykiety"_s,
                              u"właściciel"_s, u"Brutto"_s, u"Koszt (zł)"_s, u"Path"_s, u"Modified"_s, u"Size"_s}));
        QCOMPARE(column(v, 0), QStringList({u"[[Dom]]"_s, u"[[Ogród]]"_s, u"[[Garaż]]"_s, u"[[Strych]]"_s}));
        const Row &dom = v.rows.first();
        QCOMPARE(dom.cells.at(1).markdown, u"w toku"_s);
        QCOMPARE(dom.cells.at(2).value, QVariant(5000.0));
        QCOMPARE(dom.cells.at(4).value, QVariant(u"2026-10-20"_s));
        QCOMPARE(dom.cells.at(5).value, QVariant(true));
        QCOMPARE(dom.cells.at(6).value, QVariant(QStringList({u"remont"_s, u"łazienka"_s})));
        QCOMPARE(dom.cells.at(6).markdown, u"remont, łazienka"_s);
        QCOMPARE(dom.cells.at(7).markdown, u"[[Jan Kowalski]]"_s);
        QCOMPARE(dom.cells.at(8).markdown, u"1476"_s);
        QCOMPARE(dom.cells.at(9).markdown, u"1200"_s);
        QCOMPARE(dom.cells.at(10).markdown, u"Projekty/Dom.md"_s);
        QCOMPARE(dom.cells.at(11).markdown, u"2026-10-10 08:00"_s);
        QCOMPARE(dom.cells.at(12).markdown, u"2.0 KB"_s);
        // Column types and what may be edited.
        QCOMPARE(v.columns.at(2).type, u"number"_s);
        QCOMPARE(v.columns.at(2).property, u"Budżet"_s);
        QCOMPARE(v.columns.at(5).type, u"checkbox"_s);
        QCOMPARE(v.columns.at(4).type, u"date"_s);
        QCOMPARE(v.columns.at(6).type, u"list"_s);
        QVERIFY(v.columns.at(1).editable);
        QVERIFY(!v.columns.at(0).editable);
        QVERIFY(!v.columns.at(8).editable);
        QCOMPARE(v.columns.at(1).sortKey, u"status::"_s); // a built-in key needs the property mark
        QCOMPARE(v.columns.at(3).sortKey, u"koszt"_s);
        QCOMPARE(v.columns.at(8).sortKey, QString());
        // A Logseq page's values are typed the same.
        const Row &garaz = v.rows.at(2);
        QCOMPARE(garaz.cells.at(3).value, QVariant(99.5));
        QCOMPARE(garaz.cells.at(1).markdown, u"zrobione"_s);
        // An empty cell.
        const Row &strych = v.rows.at(3);
        QVERIFY(strych.cells.at(3).empty);
        QVERIFY(strych.cells.at(8).empty);
    }

    void summaries()
    {
        const ViewResult v = view(u"type:pages tag:projekt view:table columns:title,koszt,termin,pilne,status "_s
                                  u"summary:koszt=suma,termin=najwcześniej,pilne=zaznaczone,status=unikalne"_s);
        QCOMPARE(v.columns.at(1).summaryText, u"1599.5"_s);
        QCOMPARE(v.columns.at(1).summaryLabel, u"Sum"_s);
        QCOMPARE(v.columns.at(2).summaryText, u"2026-10-20"_s);
        QCOMPARE(v.columns.at(3).summaryText, u"1"_s);
        QCOMPARE(v.columns.at(4).summaryText, u"3"_s);
        QCOMPARE(summarize(u"average"_s, {QVariant(1.0), QVariant(2.0), QVariant(u"3,5"_s), QVariant()}), u"2.17"_s);
        QCOMPARE(summarize(u"median"_s, {QVariant(5.0), QVariant(1.0), QVariant(3.0), QVariant(10.0)}), u"4"_s);
        QCOMPARE(summarize(u"range"_s, {QVariant(u"2026-10-01"_s), QVariant(u"2026-10-15"_s)}), u"14 day(s)"_s);
        QCOMPARE(summarize(u"stddev"_s, {QVariant(2.0), QVariant(4.0), QVariant(4.0), QVariant(4.0), QVariant(5.0),
                                         QVariant(5.0), QVariant(7.0), QVariant(9.0)}),
                 u"2"_s);
        QCOMPARE(summarize(u"empty"_s, {QVariant(), QVariant(u""_s), QVariant(QStringList()), QVariant(1.0)}), u"3"_s);
        QCOMPARE(summarize(u"count"_s, {QVariant(), QVariant(1.0)}), u"2"_s);
        QCOMPARE(summarize(u"sum"_s, {QVariant(u"abc"_s)}), QString());
        QVERIFY(viewError(u"type:pages view:table columns:title summary:koszt=sum"_s).contains(u"koszt"_s));
    }

    void formulas()
    {
        const ViewResult v =
                view(u"type:pages tag:projekt sort:page view:table "_s
                     u"columns:title,\"Rok = termin.year\",\"Tekst = upper(status)\",\"Zła = koszt + (\""_s);
        QVERIFY(v.columns.isEmpty()); // a formula that does not parse is the view's error
        QVERIFY(viewError(u"type:pages view:table columns:\"Zła = koszt + (\""_s).startsWith(u"Column “Zła”"_s));
        const ViewResult ok = view(
                u"type:pages tag:projekt sort:page view:table "_s
                u"columns:title,\"Rok = termin.year\",\"Tekst = upper(status)\",\"Pętla = reduce(list(1,2,3), (a, b) => a + b)\""_s);
        QCOMPARE(column(ok, 1), QStringList({u"2026"_s, QString(), u"2026"_s, QString()}));
        QCOMPARE(column(ok, 2).first(), u"W TOKU"_s);
        QCOMPARE(column(ok, 3).first(), u"6"_s);
        // A formula too costly shows its message in the cell, the rest of the view stays.
        ViewOptions tight = options();
        tight.maxSteps = 200000;
        const ViewResult costly =
                view(u"type:pages tag:projekt view:table columns:title,\"X = length(map(list(1,2,3,4,5,6,7,8,9,10), "_s
                     u"(a) => map(list(1,2,3,4,5,6,7,8,9,10), (b) => map(list(1,2,3,4,5,6,7,8,9,10), (c) => "_s
                     u"map(list(1,2,3,4,5,6,7,8,9,10), (d) => map(list(1,2,3,4,5,6,7,8,9,10), (e) => "_s
                     u"map(list(1,2,3,4,5,6,7,8,9,10), (f) => map(list(1,2,3,4,5,6,7,8,9,10), (g) => a))))))))\""_s,
                     tight);
        QVERIFY(!costly.rows.isEmpty());
        QVERIFY(costly.rows.first().cells.at(1).error);
    }

    void taskBoardByStatus()
    {
        const ViewResult v = view(u"type:tasks path:Projekty/Dom view:board"_s);
        QCOMPARE(v.kind, ViewKind::Board);
        QCOMPARE(v.groupKind, u"status"_s);
        QStringList groups;
        for (const Group &g : v.groups)
            groups.append(g.value + u'=' + QString::number(g.rows.size()));
        // To do, in progress and done always; cancelled because it holds a card.
        QCOMPARE(groups, QStringList({u"todo=1"_s, u"doing=1"_s, u"done=1"_s, u"cancelled=1"_s}));
        QVERIFY(std::ranges::all_of(v.groups, &Group::droppable));
        QCOMPARE(labels(v), QStringList({u"Text"_s, u"Status"_s, u"Due"_s, u"Priority"_s, u"Title"_s}));
        QCOMPARE(v.groupColumn, 1);
        // An empty status column of a board without such tasks.
        const ViewResult open = view(u"type:tasks status:todo path:Projekty/ view:board"_s);
        QCOMPARE(open.groups.size(), 3);
        QCOMPARE(open.groups.at(1).rows.size(), 0);
        // Moving a card: its status.
        const Row &card = open.rows.first();
        const std::optional<RowEdit> e = moveEdit(open, card, 0, 2);
        QVERIFY(e);
        QCOMPARE(e->field, EditField::Status);
        QCOMPARE(e->value.toString(), u"done"_s);
        QVERIFY(!moveEdit(open, card, 0, 0));
    }

    void pageBoardByProperty()
    {
        const ViewResult v = view(u"type:pages tag:projekt view:board group:status sort:page"_s);
        QCOMPARE(v.groupKind, u"property"_s);
        QCOMPARE(v.groupProperty, u"status"_s);
        QStringList groups;
        for (const Group &g : v.groups)
            groups.append(g.label + u'=' + QString::number(g.rows.size()) + u'|' + g.value);
        QCOMPARE(groups, QStringList({u"planowany=1|planowany"_s, u"w toku=2|w toku"_s, u"zrobione=1|zrobione"_s,
                                      u"No value=0|"_s}));
        QVERIFY(v.groupColumn >= 0);
        const int garaz = int(std::ranges::find(v.rows, u"Projekty/Garaż.md"_s, &Row::path) - v.rows.begin());
        const std::optional<RowEdit> e = moveEdit(v, v.rows.at(garaz), 2, 0);
        QVERIFY(e);
        QCOMPARE(e->field, EditField::Property);
        QCOMPARE(e->key, u"status"_s);
        QCOMPARE(e->value.toString(), u"planowany"_s);
        QVERIFY(!e->blockRow);
        // To "No value": the property goes.
        const std::optional<RowEdit> none = moveEdit(v, v.rows.at(garaz), 2, 3);
        QVERIFY(none && !none->value.isValid());
        // A board by a list property swaps the item.
        const ViewResult byTag = view(u"type:pages tag:projekt view:board group:etykiety"_s);
        const auto lazienka = std::ranges::find(byTag.groups, u"łazienka"_s, &Group::label);
        const auto remont = std::ranges::find(byTag.groups, u"remont"_s, &Group::label);
        QVERIFY(lazienka != byTag.groups.cend() && remont != byTag.groups.cend());
        const int dom = lazienka->rows.first();
        const std::optional<RowEdit> swap = moveEdit(byTag, byTag.rows.at(dom), int(lazienka - byTag.groups.cbegin()),
                                                     int(byTag.groups.size() - 1));
        QVERIFY(swap);
        QCOMPARE(swap->value.toStringList(), QStringList{u"remont"_s});
    }

    void boardWithoutDrops()
    {
        const ViewResult v = view(u"type:pages tag:projekt view:board group:page"_s);
        QVERIFY(v.groupKind.isEmpty());
        QVERIFY(std::ranges::none_of(v.groups, &Group::droppable));
        QVERIFY(!moveEdit(v, v.rows.first(), 0, 1));
    }

    void gallery()
    {
        const ViewResult v = view(u"type:pages tag:projekt view:gallery sort:page"_s);
        QCOMPARE(v.kind, ViewKind::Gallery);
        QCOMPARE(v.rows.at(0).cover, u"assets/góry.jpg"_s); // the banner
        QCOMPARE(v.rows.at(0).coverKind, u"file"_s);
        QCOMPARE(v.rows.at(2).cover, u"assets/tulipany.png"_s); // the first picture
        QVERIFY(v.rows.at(1).cover.isEmpty());
    }

    void calendar()
    {
        ViewOptions o = options();
        o.month = QDate(2026, 10, 1);
        const ViewResult tasks = view(u"type:tasks view:calendar"_s, o);
        QCOMPARE(tasks.kind, ViewKind::Calendar);
        QCOMPARE(tasks.month, QDate(2026, 10, 1));
        QCOMPARE(tasks.groupKind, u"taskdate"_s);
        QStringList days;
        for (const Row &r : tasks.rows)
            days.append(r.date.toString(Qt::ISODate));
        std::ranges::sort(days);
        // The grid starts on Monday 2026-09-28 and ends on Sunday 2026-11-08.
        QCOMPARE(days, QStringList({u"2026-10-12"_s, u"2026-10-15"_s, u"2026-10-30"_s}));
        const auto [from, to] = calendarRange(QDate(2026, 10, 14), Qt::Monday);
        QCOMPARE(from, QDate(2026, 9, 28));
        QCOMPARE(to, QDate(2026, 11, 8));
        const std::optional<RowEdit> move = dateEdit(tasks, tasks.rows.first(), QDate(2026, 10, 22));
        QVERIFY(move);
        QCOMPARE(move->field, EditField::TaskDate);
        QCOMPARE(move->key, u"due"_s);
        QCOMPARE(move->value.toString(), u"2026-10-22"_s);
        // Pages by a date property (the first date column).
        const ViewResult pages = view(u"type:pages tag:projekt view:calendar columns:title,termin"_s, o);
        QCOMPARE(pages.rows.size(), 2); // Dom (10-20) and Ogród (11-02, inside the grid)
        QCOMPARE(pages.groupKind, u"date"_s);
        QCOMPARE(pages.groupProperty, u"termin"_s);
        QCOMPARE(pages.dateColumn, 1);
        o.month = QDate(2026, 12, 1);
        QCOMPARE(view(u"type:pages tag:projekt view:calendar columns:title,termin"_s, o).rows.size(), 0);
    }

    void newRows()
    {
        const ViewResult pages = view(u"type:pages path:Projekty/ tag:projekt status::planowany view:table"_s);
        QVERIFY(pages.newRow.possible);
        QVERIFY(!pages.newRow.task);
        QCOMPARE(pages.newRow.folder, u"Projekty"_s);
        QCOMPARE(pages.newRow.tags, QStringList{u"projekt"_s});
        QCOMPARE(pages.newRow.properties.size(), 1);
        const QString content = newPageContent(pages);
        QCOMPARE(content, lines({u"---"_s, u"status: planowany"_s, u"tags:"_s, u"  - projekt"_s, u"---"_s, u""_s}));
        // Without a folder filter: the notebook's folder for new pages.
        QCOMPARE(view(u"type:pages tag:projekt view:table"_s).newRow.folder, u"Nowe"_s);
        // A board column's value.
        const ViewResult board = view(u"type:pages tag:projekt view:board group:status sort:page"_s);
        QVERIFY(newPageContent(board, 1).contains(u"status: w toku"_s));
        // A task view: a task on the query's page.
        const ViewResult tasks = view(u"type:tasks tag:zakupy view:board"_s);
        QVERIFY(tasks.newRow.task);
        QCOMPARE(tasks.newRow.taskPage, u"Zapytania.md"_s);
        QCOMPARE(newTaskLines(tasks, u"Kupić farbę"_s, 1), u"- [/] Kupić farbę #zakupy\n"_s);
        const ViewResult cal = view(u"type:tasks view:calendar"_s);
        QCOMPARE(newTaskLines(cal, u"Odebrać"_s, -1, QDate(2026, 10, 21)), u"- [ ] Odebrać 📅 2026-10-21\n"_s);
        // Blocks make no rows.
        QVERIFY(!view(u"type:blocks view:table zakupy"_s).newRow.possible);
    }

    void blockRows()
    {
        const ViewResult v = view(u"type:tasks path:Projekty/Dom view:table columns:text,etap,status,due,priority"_s);
        QCOMPARE(v.rows.size(), 4);
        QCOMPARE(v.rows.first().cells.at(0).markdown, u"Kupić płytki"_s);
        QCOMPARE(v.rows.first().cells.at(1).markdown, u"zakupy"_s);
        QCOMPARE(v.rows.first().cells.at(1).target, u"block"_s);
        QCOMPARE(v.rows.first().cells.at(2).value, QVariant(u"todo"_s));
        QCOMPARE(v.rows.first().cells.at(3).markdown, u"2026-10-15"_s);
        QCOMPARE(v.rows.at(1).cells.at(4).value, QVariant(u"high"_s));
        QVERIFY(v.columns.at(1).editable); // an ASCII key: a `key::` line
        QCOMPARE(v.columns.at(3).type, u"taskdate"_s);
        // A Polish key cannot be a block's line.
        const ViewResult polish = view(u"type:tasks path:Projekty/Dom view:table columns:text,właściciel"_s);
        QVERIFY(!polish.columns.at(1).editable);
        QCOMPARE(polish.rows.first().cells.at(1).markdown, u"[[Jan Kowalski]]"_s); // the page's value shows
    }

    void limitsRows()
    {
        const ViewResult v = view(u"type:pages tag:projekt view:table limit:2"_s);
        QCOMPARE(v.rows.size(), 2);
        QCOMPARE(v.total, 4);
        QVERIFY(v.limited);
    }

    void queryTextEdits()
    {
        const QString source = u"type:pages tag:projekt // projekty\nview:table columns:title,status\n"_s;
        QCOMPARE(setQueryOption(source, u"columns"_s, queryList({u"title"_s, u"status"_s, u"termin oddania"_s})),
                 u"type:pages tag:projekt // projekty\nview:table columns:title,status,\"termin oddania\"\n"_s);
        QCOMPARE(setQueryOption(source, u"sort"_s, u"-koszt"_s),
                 u"type:pages tag:projekt // projekty\nview:table columns:title,status\nsort:-koszt\n"_s);
        QCOMPARE(setQueryOption(u"a sortuj:x b sort:y"_s, u"sort"_s, u"z"_s), u"a sortuj:z b"_s);
        QCOMPARE(setQueryOption(u"view:table kolumny:a"_s, u"columns"_s, QString()), u"view:table"_s);
        QCOMPARE(setQueryOption(u"tag:x\r\n"_s, u"view"_s, u"board"_s), u"tag:x\r\nview:board\r\n"_s);
        QCOMPARE(setQueryOption(QString(), u"view"_s, u"table"_s), u"view:table"_s);
        // A filter `-sort:` (not an option) and a phrase are left alone.
        QCOMPARE(setQueryOption(u"\"sort:x\" view:table"_s, u"sort"_s, u"page"_s),
                 u"\"sort:x\" view:table\nsort:page"_s);
        const Query q = std::get<Query>(
                parseQuery(u"type:pages view:table columns:title,\"Brutto = koszt * 1.23\",\"Koszt (zł) = koszt\""_s));
        QCOMPARE(columnNames(q, {}), QStringList({u"title"_s, u"Brutto = koszt * 1.23"_s, u"Koszt (zł) = koszt"_s}));
        QCOMPARE(queryList({u"a"_s, u"b c"_s, u"d,e"_s, u"Brutto = x"_s}), u"a,\"b c\",\"d,e\",\"Brutto = x\""_s);
    }
};

QTEST_GUILESS_MAIN(TestViews)
#include "tst_views.moc"
