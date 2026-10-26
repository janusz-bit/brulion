// TaskListModel: sections for a fixed "today" (Monday and Sunday week starts, Sunday as today),
// rolled-over scheduled tasks, reminder-only tasks, filters (tag incl. sub-tags and page tags,
// page, folder, priority, status, diacritic-insensitive text), the equivalent query text (and
// that the query engine agrees), sorting, collapsed sections, applyQuery(), escaping of the
// rendered text, choices for the filter menus, and incremental row updates.
#include "index/query.h"
#include "planningtest.h"
#include "planningutil.h"
#include "tasklistmodel.h"

#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::Task;
using brulion::TaskStatus;
using brulion::planning::TaskListModel;
using brulion::planning::test::Files;
using brulion::planning::test::makeIndex;

namespace {

const Files &vault()
{
    static const Files files = {
            {u"journals/2026-10-15.md"_s,
             u"- [ ] Zaległe zadanie 📅 2026-10-14\n"
             u"- [ ] Na dziś 📅 2026-10-15 #praca\n"
             u"- [ ] Zaplanowane przedwczoraj ⏳ 2026-10-13\n"
             u"- [ ] Zadzwonić do Ani ⏰ 2026-10-16 09:00 #praca/telefony\n"
             u"- [ ] Bez daty #dom\n"_s},
            {u"Projekty/Dom/Remont.md"_s,
             u"---\ntags: remont\n---\n"
             u"- [ ] Zamówić farbę ⏫ 📅 2026-10-16\n"
             u"- [/] Zerwać panele 📅 2026-10-18\n"
             u"- [ ] Odebrać listwy 📅 2026-10-19 ➕ 2026-10-01\n"
             u"- [ ] Zapłacić ratę 🔁 every month on the 10th 📅 2026-11-10 ➕ 2026-10-02\n"
             u"- [ ] Kupić żółwia 🔽 🛫 2026-10-20\n"
             u"- [x] Wnieść meble ✅ 2026-10-13\n"
             u"- [-] Wynająć kontener ❌ 2026-10-14\n"
             u"- [x] Stary wpis ✅ 2026-09-01\n"_s},
            {u"Książki.md"_s, u"- [ ] Przeczytać „Lalkę” 🔺 #[[do przeczytania]]\n- [>] Odłożone na potem\n"_s},
    };
    return files;
}

QStringList column(const TaskListModel &m, TaskListModel::Role role, bool tasksOnly = true)
{
    QStringList out;
    for (int r = 0; r < m.rowCount(); ++r) {
        const QModelIndex i = m.index(r, 0);
        if (tasksOnly && m.data(i, TaskListModel::RowTypeRole).toString() != u"task")
            continue;
        out << m.data(i, role).toString();
    }
    return out;
}

QStringList sectionKeys(const TaskListModel &m)
{
    QStringList out;
    for (const QVariant &s : m.sections())
        out << s.toMap().value(u"key"_s).toString();
    return out;
}

int sectionCount(const TaskListModel &m, const QString &key)
{
    for (const QVariant &s : m.sections()) {
        if (s.toMap().value(u"key"_s).toString() == key)
            return s.toMap().value(u"count"_s).toInt();
    }
    return 0;
}

QStringList textsIn(const TaskListModel &m, const QString &section)
{
    QStringList out;
    for (int r = 0; r < m.rowCount(); ++r) {
        const QModelIndex i = m.index(r, 0);
        if (m.data(i, TaskListModel::RowTypeRole).toString() == u"task"
            && m.data(i, TaskListModel::SectionRole).toString() == section)
            out << m.data(i, TaskListModel::TextRole).toString();
    }
    return out;
}

} // namespace

class TestTaskListModel : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void sectionOf_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("today");
        QTest::addColumn<int>("weekStart");
        QTest::addColumn<QString>("section");
        // Thursday 15 October 2026, Monday start: the week ends on Sunday the 18th.
        QTest::newRow("overdue") << u"- [ ] x 📅 2026-10-14"_s << u"2026-10-15"_s << 1 << u"overdue"_s;
        QTest::newRow("today") << u"- [ ] x 📅 2026-10-15"_s << u"2026-10-15"_s << 1 << u"today"_s;
        QTest::newRow("scheduled past rolls over") << u"- [ ] x ⏳ 2026-10-01"_s << u"2026-10-15"_s << 1 << u"today"_s;
        QTest::newRow("scheduled before due") << u"- [ ] x ⏳ 2026-10-15 📅 2026-10-30"_s << u"2026-10-15"_s << 1 << u"today"_s;
        QTest::newRow("tomorrow") << u"- [ ] x 📅 2026-10-16"_s << u"2026-10-15"_s << 1 << u"tomorrow"_s;
        QTest::newRow("this week") << u"- [ ] x 📅 2026-10-18"_s << u"2026-10-15"_s << 1 << u"thisWeek"_s;
        QTest::newRow("later") << u"- [ ] x 📅 2026-10-19"_s << u"2026-10-15"_s << 1 << u"later"_s;
        QTest::newRow("no date") << u"- [ ] x"_s << u"2026-10-15"_s << 1 << u"noDate"_s;
        QTest::newRow("start in future") << u"- [ ] x 🛫 2026-10-17"_s << u"2026-10-15"_s << 1 << u"thisWeek"_s;
        QTest::newRow("start in past") << u"- [ ] x 🛫 2026-10-01"_s << u"2026-10-15"_s << 1 << u"noDate"_s;
        QTest::newRow("done") << u"- [x] x 📅 2026-10-01"_s << u"2026-10-15"_s << 1 << u"done"_s;
        QTest::newRow("cancelled") << u"- [-] x"_s << u"2026-10-15"_s << 1 << u"done"_s;
        QTest::newRow("deferred overdue") << u"- [>] x 📅 2026-10-01"_s << u"2026-10-15"_s << 1 << u"overdue"_s;
        // Sunday start: the week of the 15th ends on Saturday the 17th.
        QTest::newRow("sunday start: sunday is later") << u"- [ ] x 📅 2026-10-18"_s << u"2026-10-15"_s << 7 << u"later"_s;
        QTest::newRow("sunday start: saturday") << u"- [ ] x 📅 2026-10-17"_s << u"2026-10-15"_s << 7 << u"thisWeek"_s;
        // Today is Sunday (Monday start): tomorrow is next week, "this week" is empty.
        QTest::newRow("sunday today: tomorrow") << u"- [ ] x 📅 2026-10-19"_s << u"2026-10-18"_s << 1 << u"tomorrow"_s;
        QTest::newRow("sunday today: later") << u"- [ ] x 📅 2026-10-20"_s << u"2026-10-18"_s << 1 << u"later"_s;
        // Year boundary.
        QTest::newRow("new year") << u"- [ ] x 📅 2027-01-01"_s << u"2026-12-31"_s << 1 << u"tomorrow"_s;
    }

    void sectionOf()
    {
        QFETCH(QString, line);
        QFETCH(QString, today);
        QFETCH(int, weekStart);
        QFETCH(QString, section);
        auto index = makeIndex({{u"a.md"_s, line + u'\n'}});
        const auto tasks = index->tasks();
        QCOMPARE(tasks.size(), 1);
        QCOMPARE(TaskListModel::sectionOf(tasks.first().task, QDate::fromString(today, Qt::ISODate),
                                          Qt::DayOfWeek(weekStart)),
                 section);
    }

    void sectionsOfAVault()
    {
        auto index = makeIndex(vault());
        TaskListModel m;
        QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
        m.setToday(u"2026-10-15"_s);
        m.setWeekStart(1);
        m.setVaultIndex(index.get());

        QCOMPARE(sectionKeys(m), (QStringList{u"overdue"_s, u"today"_s, u"tomorrow"_s, u"thisWeek"_s, u"later"_s,
                                              u"noDate"_s, u"done"_s}));
        QCOMPARE(m.totalCount(), 15);
        QCOMPARE(m.vaultTaskCount(), 15);
        QCOMPARE(m.openCount(), 12);
        QCOMPARE(m.overdueCount(), 1);
        QCOMPARE(m.todayCount(), 2);
        QCOMPARE(textsIn(m, u"overdue"_s), QStringList{u"Zaległe zadanie"_s});
        // Today: due today, then the rolled-over scheduled task (earlier date first).
        QCOMPARE(textsIn(m, u"today"_s), (QStringList{u"Zaplanowane przedwczoraj"_s, u"Na dziś #praca"_s}));
        // Tomorrow: the reminder-only task (09:00) and the due one; the all-day date sorts first.
        QCOMPARE(textsIn(m, u"tomorrow"_s), (QStringList{u"Zamówić farbę"_s, u"Zadzwonić do Ani #praca/telefony"_s}));
        QCOMPARE(textsIn(m, u"thisWeek"_s), QStringList{u"Zerwać panele"_s});
        QCOMPARE(textsIn(m, u"later"_s), (QStringList{u"Odebrać listwy"_s, u"Kupić żółwia"_s, u"Zapłacić ratę"_s}));
        QCOMPARE(textsIn(m, u"noDate"_s),
                 (QStringList{u"Przeczytać „Lalkę” #do przeczytania"_s, u"Bez daty #dom"_s, u"Odłożone na potem"_s}));
        // Done is collapsed: its header is there, its tasks are not.
        QCOMPARE(sectionCount(m, u"done"_s), 3);
        QVERIFY(textsIn(m, u"done"_s).isEmpty());
        const int lastRow = m.rowCount() - 1;
        QCOMPARE(m.data(m.index(lastRow, 0), TaskListModel::RowTypeRole).toString(), u"header"_s);
        QCOMPARE(m.data(m.index(lastRow, 0), TaskListModel::SectionRole).toString(), u"done"_s);
        QVERIFY(m.data(m.index(lastRow, 0), TaskListModel::CollapsedRole).toBool());
        QCOMPARE(m.data(m.index(lastRow, 0), TaskListModel::SectionCountRole).toInt(), 3);

        // Expanded: most recently finished first.
        m.toggleSection(u"done"_s);
        QCOMPARE(textsIn(m, u"done"_s), (QStringList{u"Wynająć kontener"_s, u"Wnieść meble"_s, u"Stary wpis"_s}));
        QVERIFY(!m.collapsedSections().contains(u"done"_s));
        m.toggleSection(u"done"_s);
        QVERIFY(textsIn(m, u"done"_s).isEmpty());
    }

    void taskRoles()
    {
        auto index = makeIndex(vault());
        TaskListModel m;
        m.setToday(u"2026-10-15"_s);
        m.setVaultIndex(index.get());
        const int row = m.rowOfKey(u"Projekty/Dom/Remont.md:3"_s);
        QVERIFY(row >= 0);
        const QVariantMap t = m.get(row);
        QCOMPARE(t.value(u"text"_s).toString(), u"Zamówić farbę"_s);
        QCOMPARE(t.value(u"status"_s).toString(), u"todo"_s);
        QCOMPARE(t.value(u"pageTitle"_s).toString(), u"Remont"_s);
        QCOMPARE(t.value(u"folder"_s).toString(), u"Projekty/Dom"_s);
        QCOMPARE(t.value(u"due"_s).toString(), u"2026-10-16"_s);
        QCOMPARE(t.value(u"dueLabel"_s).toString(), u"jutro"_s);
        QCOMPARE(t.value(u"priority"_s).toInt(), 4);
        QCOMPARE(t.value(u"priorityName"_s).toString(), u"high"_s);
        QCOMPARE(t.value(u"isJournal"_s).toBool(), false);

        const QVariantMap rec = m.get(m.rowOfKey(u"Projekty/Dom/Remont.md:6"_s));
        QCOMPARE(rec.value(u"recurrence"_s).toString(), u"every month on the 10th"_s);
        QCOMPARE(rec.value(u"recurrenceLabel"_s).toString(), u"co miesiąc 10."_s);
        QCOMPARE(rec.value(u"created"_s).toString(), u"2026-10-02"_s);

        const QVariantMap call = m.get(m.rowOfKey(u"journals/2026-10-15.md:3"_s));
        QCOMPARE(call.value(u"reminderLabel"_s).toString(), u"jutro 09:00"_s);
        QCOMPARE(call.value(u"isJournal"_s).toBool(), true);
        QVERIFY(call.value(u"tags"_s).toStringList().contains(u"praca/telefony"_s));

        const QVariantMap overdue = m.get(m.rowOfKey(u"journals/2026-10-15.md:0"_s));
        QVERIFY(overdue.value(u"overdue"_s).toBool());
        QCOMPARE(overdue.value(u"dueLabel"_s).toString(), u"wczoraj"_s);

        // English labels.
        m.setLocale(u"en"_s);
        QCOMPARE(m.get(row).value(u"dueLabel"_s).toString(), u"tomorrow"_s);
        QCOMPARE(m.get(m.rowOfKey(u"Projekty/Dom/Remont.md:6"_s)).value(u"recurrenceLabel"_s).toString(),
                 u"every month on the 10th"_s);
    }

    void htmlIsEscaped()
    {
        auto index = makeIndex({{u"a.md"_s, u"- [ ] <b>bold?</b> & [[Strona|link]] #tag\n"_s}});
        TaskListModel m;
        m.setVaultIndex(index.get());
        const QString html = m.get(m.rowOfKey(u"a.md:0"_s)).value(u"html"_s).toString();
        QVERIFY2(html.contains(u"&lt;b&gt;"_s), qPrintable(html));
        QVERIFY(!html.contains(u"<b>bold"_s));
        QVERIFY(html.contains(u"&amp;"_s));
        QVERIFY(html.contains(u"href=\"page:"_s));
        QVERIFY(html.contains(u"href=\"tag:"_s));
        // A palette change re-renders.
        m.setPalette({{u"link"_s, u"#ff0000"_s}, {u"unresolved"_s, u"#ff0000"_s}});
        QVERIFY(m.get(m.rowOfKey(u"a.md:0"_s)).value(u"html"_s).toString().contains(u"#ff0000"_s));
    }

    void filters_data()
    {
        QTest::addColumn<QString>("filter");
        QTest::addColumn<QString>("value");
        QTest::addColumn<int>("count");
        QTest::addColumn<QString>("query");
        QTest::newRow("tag") << u"tag"_s << u"praca"_s << 2 << u"type:tasks tag:praca sort:due"_s;
        QTest::newRow("tag with #, upper case") << u"tag"_s << u"#Praca"_s << 2 << u"type:tasks tag:praca sort:due"_s;
        QTest::newRow("sub-tag") << u"tag"_s << u"praca/telefony"_s << 1 << u"type:tasks tag:praca/telefony sort:due"_s;
        QTest::newRow("page tag") << u"tag"_s << u"remont"_s << 8 << u"type:tasks tag:remont sort:due"_s;
        QTest::newRow("tag with space") << u"tag"_s << u"do przeczytania"_s << 1
                                        << u"type:tasks tag:\"do przeczytania\" sort:due"_s;
        QTest::newRow("folder") << u"folder"_s << u"Projekty/"_s << 8 << u"type:tasks path:Projekty/ sort:due"_s;
        QTest::newRow("page") << u"page"_s << u"Książki.md"_s << 2 << u"type:tasks page:Książki sort:due"_s;
        QTest::newRow("priority") << u"priority"_s << u"highest"_s << 1 << u"type:tasks priority:highest sort:due"_s;
        QTest::newRow("no priority") << u"priority"_s << u"none"_s << 12 << u"type:tasks priority:none sort:due"_s;
        QTest::newRow("open") << u"status"_s << u"open"_s << 11 << u"type:tasks status:open sort:due"_s;
        QTest::newRow("done") << u"status"_s << u"done"_s << 2 << u"type:tasks status:done sort:due"_s;
        QTest::newRow("deferred") << u"status"_s << u"deferred"_s << 1 << u"type:tasks status:deferred sort:due"_s;
        QTest::newRow("text without diacritics") << u"text"_s << u"zolwia"_s << 1 << u"type:tasks zolwia sort:due"_s;
        QTest::newRow("two words") << u"text"_s << u"zamówić FARBĘ"_s << 1 << u"type:tasks zamówić FARBĘ sort:due"_s;
        QTest::newRow("syntax-looking word") << u"text"_s << u"tag:x"_s << 0 << u"type:tasks \"tag:x\" sort:due"_s;
    }

    void filters()
    {
        QFETCH(QString, filter);
        QFETCH(QString, value);
        QFETCH(int, count);
        QFETCH(QString, query);
        auto index = makeIndex(vault());
        TaskListModel m;
        m.setToday(u"2026-10-15"_s);
        m.setVaultIndex(index.get());
        QSignalSpy filtersChanged(&m, &TaskListModel::filtersChanged);
        if (filter == u"tag")
            m.setTagFilter(value);
        else if (filter == u"folder")
            m.setFolderFilter(value);
        else if (filter == u"page")
            m.setPageFilter(value);
        else if (filter == u"priority")
            m.setPriorityFilter(value);
        else if (filter == u"status")
            m.setStatusFilter(value);
        else
            m.setTextFilter(value);
        QCOMPARE(filtersChanged.count(), 1);
        QVERIFY(m.filtered());
        QCOMPARE(m.totalCount(), count);
        QCOMPARE(m.vaultTaskCount(), 15);
        QCOMPARE(m.queryText(), query);
        // The query engine agrees with the list ("Pokaż jako zapytanie" shows the truth).
        const auto parsed = brulion::parseQuery(m.queryText());
        QVERIFY(std::holds_alternative<brulion::Query>(parsed));
        const auto result = brulion::runQuery(std::get<brulion::Query>(parsed), *index, QDate(2026, 10, 15));
        QCOMPARE(result.total, count);

        m.clearFilters();
        QVERIFY(!m.filtered());
        QCOMPARE(m.totalCount(), 15);
        QCOMPARE(m.queryText(), u"type:tasks sort:due"_s);
    }

    void sorting()
    {
        auto index = makeIndex({
                {u"b.md"_s, u"- [ ] Niski 🔽 📅 2026-10-20 ➕ 2026-10-03\n- [ ] Wysoki ⏫ 📅 2026-10-21 ➕ 2026-10-01\n"_s},
                {u"a.md"_s, u"- [ ] Średni 🔼 📅 2026-10-22 ➕ 2026-10-02\n- [ ] Brak 📅 2026-10-20\n"_s},
        });
        TaskListModel m;
        m.setToday(u"2026-10-15"_s);
        m.setVaultIndex(index.get());
        // Default (due): by date, then priority, then page.
        QCOMPARE(column(m, TaskListModel::TextRole), (QStringList{u"Brak"_s, u"Niski"_s, u"Wysoki"_s, u"Średni"_s}));
        m.setSortKey(u"priority"_s);
        QCOMPARE(column(m, TaskListModel::TextRole), (QStringList{u"Wysoki"_s, u"Średni"_s, u"Brak"_s, u"Niski"_s}));
        QCOMPARE(m.queryText(), u"type:tasks sort:priority"_s);
        m.setSortKey(u"page"_s);
        QCOMPARE(column(m, TaskListModel::TextRole), (QStringList{u"Średni"_s, u"Brak"_s, u"Niski"_s, u"Wysoki"_s}));
        m.setSortKey(u"created"_s);
        QCOMPARE(column(m, TaskListModel::TextRole), (QStringList{u"Niski"_s, u"Średni"_s, u"Wysoki"_s, u"Brak"_s}));
        QCOMPARE(m.queryText(), u"type:tasks sort:-created"_s);
        m.setSortKey(u"bogus"_s);
        QCOMPARE(m.sortKey(), u"due"_s);
    }

    void pageTitlesSortLikeCompareNames()
    {
        // Tasks that tie on date and priority are ordered by page title (Polish collation:
        // case- and diacritic-aware, numbers by value), then path and line — in the due and the
        // page sort alike.
        const QStringList titles{u"Żaba"_s,      u"zebra"_s,  u"Ćma"_s,    u"cma"_s,    u"Strona 2"_s, u"Strona 10"_s,
                                 u"strona 2b"_s, u"Ąb"_s,     u"ab"_s,     u"Ab"_s,     u"Łódź"_s,     u"lody"_s,
                                 u"Ośrodek"_s,   u"osa"_s,    u"Ń"_s,      u"ń2"_s,     u"20 lat"_s,   u"3 dni"_s,
                                 u"Źródło"_s,    u"zrodlo"_s, u"Ślimak"_s, u"slimak"_s, u"ęa"_s,       u"Ea"_s};
        brulion::planning::test::Files files;
        for (int k = 0; k < titles.size(); ++k) {
            files.append(
                    {u"Folder %1/%2.md"_s.arg(k % 3).arg(titles.at(k)),
                     u"- [ ] Pierwsze 📅 2026-10-20\n- [ ] Drugie 📅 2026-10-20\n- [x] Zrobione ✅ 2026-10-10\n"_s});
        }
        auto index = makeIndex(files);
        for (const QString &sort : {u"due"_s, u"page"_s, u"priority"_s}) {
            TaskListModel m;
            m.setToday(u"2026-10-15"_s);
            m.setSortKey(sort);
            m.setCollapsedSections({});
            m.setVaultIndex(index.get());
            QCOMPARE(m.totalCount(), 3 * int(titles.size()));
            const auto text = [&](int row, TaskListModel::Role role) {
                return m.data(m.index(row, 0), role).toString();
            };
            const auto ordered = [&](int a, int b) { // title, then path, then line
                if (const int c = brulion::planning::compareNames(text(a, TaskListModel::PageTitleRole),
                                                                  text(b, TaskListModel::PageTitleRole));
                    c != 0)
                    return c < 0;
                if (text(a, TaskListModel::PathRole) != text(b, TaskListModel::PathRole))
                    return text(a, TaskListModel::PathRole) < text(b, TaskListModel::PathRole);
                return text(a, TaskListModel::LineRole).toInt() < text(b, TaskListModel::LineRole).toInt();
            };
            for (int r = 1; r < m.rowCount(); ++r) {
                if (text(r - 1, TaskListModel::RowTypeRole) != u"task"_s
                    || text(r, TaskListModel::RowTypeRole) != u"task"_s
                    || text(r - 1, TaskListModel::SectionRole) != text(r, TaskListModel::SectionRole))
                    continue;
                QVERIFY2(ordered(r - 1, r),
                         qPrintable(u"%1: %2 (%3) before %4 (%5)"_s.arg(
                                 sort, text(r - 1, TaskListModel::PageTitleRole), text(r - 1, TaskListModel::PathRole),
                                 text(r, TaskListModel::PageTitleRole), text(r, TaskListModel::PathRole))));
            }
        }
    }

    void nameSortKeysOrderLikeCompareNames()
    {
        const QStringList names{u"Żaba"_s,      u"zebra"_s, u"Ćma"_s,  u"cma"_s,  u"Strona 2"_s, u"Strona 10"_s,
                                u"strona 02"_s, u"Ąb"_s,    u"ab"_s,   u"Ab"_s,   u"AB"_s,       u"Łódź"_s,
                                u"lodz"_s,      u"Ń"_s,     u"n"_s,    QString(), u" "_s,        u"20 lat"_s,
                                u"3 dni"_s,     u"a1b"_s,   u"a01b"_s, u"é"_s,    u"e\u0301"_s,  u"Ölüdeniz"_s,
                                u"ß"_s,         u"ss"_s,    u"日本"_s, u"🙂"_s};
        const auto sign = [](int v) { return v < 0 ? -1 : v > 0 ? 1 : 0; };
        for (const QString &a : names) {
            for (const QString &b : names) {
                int byKey = brulion::planning::nameSortKey(a).compare(brulion::planning::nameSortKey(b));
                if (byKey == 0)
                    byKey = QString::compare(a, b);
                QVERIFY2(sign(byKey) == sign(brulion::planning::compareNames(a, b)), qPrintable(a + u" / "_s + b));
            }
        }
    }

    void taskKeys()
    {
        // "path:line" as u"%1:%2"_s.arg(path).arg(line) builds it — also for paths with '%'.
        auto index = makeIndex({{u"Zwykła strona.md"_s, u"- [ ] A\n"_s},
                                {u"100% gotowe.md"_s, u"Wstęp\n\n- [ ] B\n"_s},
                                {u"a%1b%2.md"_s, u"- [ ] C\n- [ ] D\n"_s}});
        TaskListModel m;
        m.setToday(u"2026-10-15"_s);
        m.setVaultIndex(index.get());
        QCOMPARE(m.totalCount(), 4);
        for (int r = 0; r < m.rowCount(); ++r) {
            const QModelIndex i = m.index(r, 0);
            if (m.data(i, TaskListModel::RowTypeRole) != u"task"_s)
                continue;
            const QString key = m.data(i, TaskListModel::KeyRole).toString();
            QCOMPARE(key, u"%1:%2"_s.arg(m.data(i, TaskListModel::PathRole).toString())
                                  .arg(m.data(i, TaskListModel::LineRole).toInt()));
            QCOMPARE(m.rowOfKey(key), r);
        }
        QCOMPARE(m.rowOfKey(u"Zwykła strona.md:0"_s) >= 0, true);
    }

    void createdFromQmlRebuildsOnce()
    {
        // QML sets vaultIndex, today, weekStart, locale and palette one after another while it
        // creates the model: one rebuild when the component is complete, the same rows as a
        // model given the same properties directly.
        auto index = makeIndex(vault());
        TaskListModel direct;
        direct.setToday(u"2026-10-15"_s);
        direct.setWeekStart(7);
        direct.setVaultIndex(index.get());

        TaskListModel m;
        QSignalSpy counts(&m, &TaskListModel::countsChanged);
        m.classBegin();
        m.setVaultIndex(index.get());
        m.setToday(u"2026-10-15"_s);
        m.setWeekStart(7);
        m.setLocale(u"pl"_s);
        m.setPalette({{u"link"_s, u"#123456"_s}});
        QCOMPARE(counts.count(), 0);
        QCOMPARE(m.rowCount(), 0);
        m.componentComplete();
        QCOMPARE(counts.count(), 1);
        QCOMPARE(column(m, TaskListModel::KeyRole, false), column(direct, TaskListModel::KeyRole, false));
        QCOMPARE(sectionKeys(m), sectionKeys(direct));
        // Afterwards every change rebuilds at once.
        m.setTagFilter(u"praca"_s);
        QCOMPARE(counts.count(), 2);
        direct.setTagFilter(u"praca"_s);
        QCOMPARE(column(m, TaskListModel::KeyRole, false), column(direct, TaskListModel::KeyRole, false));

        // Nothing set while it was created: nothing to build.
        TaskListModel empty;
        QSignalSpy emptyCounts(&empty, &TaskListModel::countsChanged);
        empty.classBegin();
        empty.componentComplete();
        QCOMPARE(emptyCounts.count(), 0);
        QCOMPARE(empty.rowCount(), 0);
    }

    void applyQuery()
    {
        auto index = makeIndex(vault());
        TaskListModel m;
        m.setToday(u"2026-10-15"_s);
        m.setVaultIndex(index.get());
        QVERIFY(m.applyQuery(u"type:tasks tag:praca status:open sort:priority"_s));
        QCOMPARE(m.tagFilter(), u"praca"_s);
        QCOMPARE(m.statusFilter(), u"open"_s);
        QCOMPARE(m.sortKey(), u"priority"_s);
        QCOMPARE(m.totalCount(), 2);
        // Polish spellings and a page by its title.
        QVERIFY(m.applyQuery(u"strona:Książki status:odłożone"_s));
        QCOMPARE(m.pageFilter(), u"Książki.md"_s);
        QCOMPARE(m.tagFilter(), QString());
        QCOMPARE(m.totalCount(), 1);
        QVERIFY(m.applyQuery(u"path:Projekty/Dom/ priority:high farbę"_s));
        QCOMPARE(m.folderFilter(), u"Projekty/Dom"_s);
        QCOMPARE(m.priorityFilter(), u"high"_s);
        QCOMPARE(m.textFilter(), u"farbę"_s);
        QCOMPARE(m.totalCount(), 1);
        // Broken queries change nothing.
        QVERIFY(!m.applyQuery(u"due:<=jutrzejszy"_s));
        QCOMPARE(m.totalCount(), 1);
        // The same filters again (a view applies its route twice): nothing to rebuild.
        QSignalSpy counts(&m, &TaskListModel::countsChanged);
        QSignalSpy filters(&m, &TaskListModel::filtersChanged);
        QVERIFY(m.applyQuery(u"path:Projekty/Dom/ priority:high farbę"_s));
        QCOMPARE(counts.count(), 0);
        QCOMPARE(filters.count(), 0);
        QCOMPARE(m.totalCount(), 1);
        QVERIFY(m.applyQuery(u"path:Projekty/Dom/ priority:high farbę sort:page"_s)); // the sort differs
        QCOMPARE(counts.count(), 1);
        QCOMPARE(m.sortKey(), u"page"_s);
    }

    void choices()
    {
        auto index = makeIndex(vault());
        TaskListModel m;
        m.setVaultIndex(index.get());
        const QVariantList tags = m.tagChoices();
        QVERIFY(!tags.isEmpty());
        QStringList tagValues;
        for (const QVariant &t : tags)
            tagValues << t.toMap().value(u"value"_s).toString();
        QVERIFY(tagValues.contains(u"praca"_s));
        QVERIFY(tagValues.contains(u"do przeczytania"_s));
        QCOMPARE(tags.first().toMap().value(u"label"_s).toString().front(), QChar(u'#'));

        const QVariantList folders = m.folderChoices();
        QCOMPARE(folders.size(), 3); // journals, Projekty, Projekty/Dom
        QCOMPARE(folders.at(0).toMap().value(u"value"_s).toString(), u"journals"_s);
        QCOMPARE(folders.at(1).toMap().value(u"value"_s).toString(), u"Projekty"_s);
        QCOMPARE(folders.at(1).toMap().value(u"count"_s).toInt(), 8);

        const QVariantList pages = m.pageChoices();
        QCOMPARE(pages.first().toMap().value(u"label"_s).toString(), u"Remont"_s);
        QCOMPARE(pages.first().toMap().value(u"count"_s).toInt(), 8);
    }

    void incrementalUpdates()
    {
        auto index = makeIndex(vault());
        TaskListModel m;
        QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
        m.setRefreshDelay(0);
        m.setToday(u"2026-10-15"_s);
        m.setVaultIndex(index.get());
        m.toggleSection(u"done"_s);
        QSignalSpy resets(&m, &QAbstractItemModel::modelReset);
        QSignalSpy moves(&m, &QAbstractItemModel::rowsMoved);
        // Tick off "Zamówić farbę": it moves from "tomorrow" to "done".
        QString content = index->content(u"Projekty/Dom/Remont.md"_s);
        content.replace(u"- [ ] Zamówić farbę ⏫ 📅 2026-10-16"_s, u"- [x] Zamówić farbę ⏫ 📅 2026-10-16 ✅ 2026-10-15"_s);
        index->update(u"Projekty/Dom/Remont.md"_s, content);
        QTRY_COMPARE(textsIn(m, u"done"_s).first(), u"Zamówić farbę"_s);
        QCOMPARE(resets.count(), 0);
        QVERIFY(moves.count() >= 1);
        QCOMPARE(textsIn(m, u"tomorrow"_s), QStringList{u"Zadzwonić do Ani #praca/telefony"_s});

        // A task disappears and a new section appears.
        index->update(u"Nowy.md"_s, u"- [ ] Nowe zadanie 📅 2026-10-14\n"_s);
        QTRY_COMPARE(m.overdueCount(), 2);
        QCOMPARE(resets.count(), 0);
        index->remove(u"journals/2026-10-15.md"_s);
        QTRY_COMPARE(m.overdueCount(), 1);
        QCOMPARE(textsIn(m, u"overdue"_s), QStringList{u"Nowe zadanie"_s});
        QCOMPARE(resets.count(), 0);
    }

    void emptyAndNoIndex()
    {
        TaskListModel none;
        QCOMPARE(none.rowCount(), 0);
        QCOMPARE(none.totalCount(), 0);
        QCOMPARE(none.queryText(), u"type:tasks sort:due"_s);
        auto index = makeIndex({{u"a.md"_s, u"Brak zadań tutaj.\n"_s}});
        TaskListModel m;
        m.setVaultIndex(index.get());
        QCOMPARE(m.rowCount(), 0);
        QVERIFY(m.sections().isEmpty());
        QCOMPARE(m.vaultTaskCount(), 0);
    }

    void manyTasks()
    {
        // A big vault: thousands of tasks build quickly and sections stay consistent.
        brulion::planning::test::Files files;
        for (int p = 0; p < 300; ++p) {
            QString content;
            for (int t = 0; t < 15; ++t) {
                const QDate due = QDate(2026, 10, 1).addDays((p * 15 + t) % 60);
                content += u"- [%1] Zadanie %2 na stronie %3 📅 %4\n"_s.arg(t % 5 == 0 ? u'x' : u' ')
                                   .arg(t)
                                   .arg(p)
                                   .arg(due.toString(Qt::ISODate));
            }
            files.append({u"Strony/Strona %1.md"_s.arg(p), content});
        }
        auto index = makeIndex(files);
        TaskListModel m;
        m.setToday(u"2026-10-15"_s);
        QElapsedTimer t;
        t.start();
        m.setVaultIndex(index.get());
        qInfo("4500 tasks: %lld ms", static_cast<long long>(t.elapsed()));
        QCOMPARE(m.totalCount(), 4500);
        int sum = 0;
        for (const QVariant &s : m.sections())
            sum += s.toMap().value(u"count"_s).toInt();
        QCOMPARE(sum, 4500);
        QCOMPARE(sectionCount(m, u"done"_s), 900);
    }
};

QTEST_MAIN(TestTaskListModel)
#include "tst_planning_tasks.moc"
