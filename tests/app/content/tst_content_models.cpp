// The content models (src/app/content) against a real Workspace on a QTemporaryDir vault:
// journals (today first even before its file exists, empty days left out, rows kept across
// updates), backlinks grouped by page (one row per block, breadcrumbs, rendering, collapsing,
// task toggles), unlinked mentions turned into links (ambiguous titles, linkAll), tag blocks and
// the tag list, the page list (Polish order, sorting, filters, backlink counts), search results
// (scopes, folders, highlight positions, diacritics), the history list (newest first, reasons,
// restore, deleted pages), the outline and the page tools (rename impact, title checks, summary,
// wiki links, export).
#include "historymodel.h"
#include "journallistmodel.h"
#include "outlinemodel.h"
#include "pagelistmodel.h"
#include "pagetools.h"
#include "refmodels.h"
#include "searchmodel.h"
#include "taglistmodel.h"

#include "workspace.h"

#include "vault/history.h"
#include "vault/vault.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::content;
using brulion::shell::Workspace;

namespace {

bool writeRaw(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(bytes);
    return true;
}

QStringList column(const QAbstractItemModel &model, const QByteArray &role)
{
    const QHash<int, QByteArray> names = model.roleNames();
    const int r = names.key(role, -1);
    QStringList out;
    for (int i = 0; i < model.rowCount(); ++i)
        out.append(model.data(model.index(i, 0), r).toString());
    return out;
}

QVariant value(const QAbstractItemModel &model, int row, const QByteArray &role)
{
    return model.data(model.index(row, 0), model.roleNames().key(role, -1));
}

/// "type:title" for header rows, "  line:text" for refs (readable comparisons).
QStringList outline(const GroupedRefsModel &model)
{
    QStringList out;
    for (int i = 0; i < model.rowCount(); ++i) {
        const QVariantMap r = model.get(i);
        if (r.value(u"rowType"_s) == u"page"_s)
            out.append(r.value(u"title"_s).toString() + u" ("_s + r.value(u"count"_s).toString() + u')');
        else
            out.append(u"  "_s + r.value(u"line"_s).toString() + u": "_s + r.value(u"text"_s).toString());
    }
    return out;
}

QString markedText(const QString &text, const QVariantList &positions)
{
    QString out;
    int last = -2;
    for (const QVariant &v : positions) {
        const int p = v.toInt();
        if (p != last + 1 && !out.isEmpty())
            out += u'|';
        out += text.at(p);
        last = p;
    }
    return out;
}

} // namespace

class TestContentModels : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QDateTime m_now{ QDate(2026, 10, 14), QTime(10, 30) }; // a Wednesday

    std::unique_ptr<Workspace> open(const QHash<QString, QString> &files)
    {
        const QString root = m_tmp.filePath(u"vault-%1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeRaw(root + u'/' + it.key(), it.value().toUtf8());
        auto ws = std::make_unique<Workspace>();
        ws->setClock([this] { return m_now; });
        QString error;
        if (!ws->open(root, root + u"-state"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        QCoreApplication::processEvents();
        return ws;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    // ---- Journals ----------------------------------------------------------------------------

    void journalsTodayFirst()
    {
        auto ws = open({ { u"journals/2026-10-12.md"_s, u"- poniedziałek\n"_s },
                         { u"journals/2026-10-13.md"_s, u"- wtorek: kupić żółty papier\n"_s },
                         { u"journals/2026-10-10.md"_s, u"- \n\n"_s }, // nothing written: left out
                         { u"journals/2026-10-20.md"_s, u"- plan na przyszłość\n"_s }, // future: left out
                         { u"Strona.md"_s, u"- zwykła strona\n"_s } });
        JournalListModel model;
        model.setWorkspace(ws.get());
        model.refreshNow();
        QCOMPARE(column(model, "date"), (QStringList{ u"2026-10-14"_s, u"2026-10-13"_s, u"2026-10-12"_s }));
        QCOMPARE(value(model, 0, "exists").toBool(), false); // today is listed before its file exists
        QCOMPARE(value(model, 0, "isToday").toBool(), true);
        QCOMPARE(value(model, 0, "path").toString(), u"journals/2026-10-14.md"_s);
        QCOMPARE(value(model, 2, "daysAgo").toInt(), 2);
        QCOMPARE(model.todayPath(), u"journals/2026-10-14.md"_s);
        QVERIFY(!model.todayExists());
        QVERIFY(!ws->exists(u"journals/2026-10-14.md"_s)); // listing never creates a file

        QCOMPARE(model.rowOfDate(u"2026-10-13"_s), 1);
        QCOMPARE(model.rowOfDate(u"2026-10-11"_s), 2); // nearest earlier listed day
        QCOMPARE(model.rowOfDate(u"2026-10-30"_s), 0);
        QCOMPARE(model.rowOfDate(u"2020-01-01"_s), 2);
        QCOMPARE(model.exactRowOfDate(u"2026-10-11"_s), -1);

        // Writing today's note (the editor's first content) keeps the rows (no reset) and fills in
        // the file; a day emptied while listed stays listed.
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy pathSpy(&model, &JournalListModel::todayPathChanged);
        QVERIFY(ws->writeFile(u"journals/2026-10-14.md"_s, u"- Środa: zażółć gęślą jaźń\n"_s));
        QVERIFY(ws->writeFile(u"journals/2026-10-12.md"_s, u"- \n"_s));
        model.refreshNow();
        QCOMPARE(resets.size(), 0);
        QCOMPARE(removed.size(), 0);
        QCOMPARE(value(model, 0, "exists").toBool(), true);
        QVERIFY(model.todayExists());
        QCOMPARE(pathSpy.size(), 1);
        QCOMPARE(column(model, "date"), (QStringList{ u"2026-10-14"_s, u"2026-10-13"_s, u"2026-10-12"_s }));

        // A new day is inserted in place; a deleted one is removed.
        QVERIFY(ws->writeFile(u"journals/2026-10-11.md"_s, u"- sobota\n"_s));
        QVERIFY(!ws->deletePage(u"journals/2026-10-13.md"_s).isEmpty());
        model.refreshNow();
        QCOMPARE(column(model, "date"), (QStringList{ u"2026-10-14"_s, u"2026-10-12"_s, u"2026-10-11"_s }));
        QCOMPARE(resets.size(), 0);

        // Empty days on request; a fixed "today".
        model.setIncludeEmpty(true);
        model.refreshNow();
        QVERIFY(column(model, "date").contains(u"2026-10-10"_s));
        model.setToday(u"2026-10-20"_s);
        model.refreshNow();
        QCOMPARE(column(model, "date").first(), u"2026-10-20"_s);
        QCOMPARE(value(model, 0, "exists").toBool(), true);
    }

    void journalsMidnightRollover()
    {
        // The app runs over midnight (JournalsView binds today: DatesHelper.today, which changes
        // at 00:00): yesterday's row must stop saying "today" (the "Dziś" badge, the heading's
        // relative day, daysAgo), the new day comes first without rebuilding the kept rows (a day
        // being edited keeps its delegate and editor).
        auto ws = open({ { u"journals/2026-10-14.md"_s, u"- piszę o północy: zażółć\n"_s },
                         { u"journals/2026-10-13.md"_s, u"- wtorek\n"_s } });
        JournalListModel model;
        model.setWorkspace(ws.get());
        model.setToday(u"2026-10-14"_s);
        model.refreshNow();
        QCOMPARE(column(model, "date"), (QStringList{ u"2026-10-14"_s, u"2026-10-13"_s }));
        QCOMPARE(value(model, 0, "isToday").toBool(), true);

        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy pathSpy(&model, &JournalListModel::todayPathChanged);
        model.setToday(u"2026-10-15"_s);
        model.refreshNow();
        QCOMPARE(column(model, "date"), (QStringList{ u"2026-10-15"_s, u"2026-10-14"_s, u"2026-10-13"_s }));
        QCOMPARE(resets.size(), 0);
        QCOMPARE(removed.size(), 0);
        QCOMPARE(value(model, 0, "isToday").toBool(), true);
        QCOMPARE(value(model, 0, "exists").toBool(), false);
        QCOMPARE(value(model, 1, "isToday").toBool(), false);
        QCOMPARE(value(model, 1, "daysAgo").toInt(), 1);
        QCOMPARE(value(model, 2, "daysAgo").toInt(), 2);
        // The views learn about it: the kept rows' isToday / daysAgo changed.
        bool keptRowsChanged = false;
        for (const QList<QVariant> &args : std::as_const(changed)) {
            const QModelIndex from = args.at(0).value<QModelIndex>();
            const QModelIndex to = args.at(1).value<QModelIndex>();
            if (from.row() <= 1 && to.row() >= 2)
                keptRowsChanged = true;
        }
        QVERIFY2(keptRowsChanged, "no dataChanged for the days kept over midnight");
        QCOMPARE(pathSpy.size(), 1);
        QCOMPARE(model.todayPath(), u"journals/2026-10-15.md"_s);
        QVERIFY(!model.todayExists());
        QVERIFY(!ws->exists(u"journals/2026-10-15.md"_s));

        // Yesterday's note had nothing in it when midnight came: it stays listed (it may be the
        // day being edited) while its file exists.
        QVERIFY(ws->writeFile(u"journals/2026-10-15.md"_s, u"- \n"_s));
        model.setToday(u"2026-10-16"_s);
        model.refreshNow();
        QCOMPARE(column(model, "date"),
                 (QStringList{ u"2026-10-16"_s, u"2026-10-15"_s, u"2026-10-14"_s, u"2026-10-13"_s }));
        QCOMPARE(value(model, 1, "isToday").toBool(), false);
    }

    void journalsAlternativeNamesAndClose()
    {
        auto ws = open({ { u"journals/2026_10_13.md"_s, u"- Logseq\n"_s }, { u"journals/2026-10-13.md"_s, u"- Brulion\n"_s } });
        JournalListModel model;
        model.setWorkspace(ws.get());
        model.refreshNow();
        QCOMPARE(column(model, "date"), (QStringList{ u"2026-10-14"_s, u"2026-10-13"_s }));
        QCOMPARE(value(model, 1, "path").toString(), u"journals/2026-10-13.md"_s); // the configured name
        ws->close();
        model.refreshNow();
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.count(), 0);
    }

    // ---- Backlinks ---------------------------------------------------------------------------

    void backlinksGroupedByPage()
    {
        auto ws = open({
                { u"Projekty/Remont mieszkania.md"_s, u"# Remont\n- kafelki\n"_s },
                { u"journals/2026-10-12.md"_s, u"- Rano: [[Remont mieszkania]]\n"_s },
                { u"journals/2026-10-13.md"_s,
                  u"- Zakupy\n  - [ ] farba do [[Remont mieszkania|remontu]] i [[Remont mieszkania]] <b>tanio</b>\n"_s },
                { u"Budżet.md"_s, u"Akapit o ![[Remont mieszkania]].\n\n- osobny blok z ((abc123))\n"_s },
                { u"Notatki/Ząbki.md"_s, u"- [[Remont mieszkania#Remont]] ^xyz\n"_s },
                { u"Projekty/Remont mieszkania 2.md"_s, u"- blok ^abc123\n"_s },
        });
        BacklinksModel model;
        model.setWorkspace(ws.get());
        model.setPath(u"Projekty/Remont mieszkania.md"_s);
        model.refreshNow();
        QCOMPARE(outline(model), (QStringList{
                                         u"2026-10-13 (1)"_s,
                                         u"  1: farba do remontu i Remont mieszkania <b>tanio</b>"_s,
                                         u"2026-10-12 (1)"_s,
                                         u"  0: Rano: Remont mieszkania"_s,
                                         u"Budżet (1)"_s,
                                         u"  0: Akapit o Remont mieszkania."_s,
                                         u"Ząbki (1)"_s,
                                         u"  0: Remont mieszkania › Remont"_s,
                                 }));
        QCOMPARE(model.refCount(), 4);
        QCOMPARE(model.pageCount(), 4);
        const QVariantMap header = model.get(0);
        QCOMPARE(header.value(u"journalDate"_s).toString(), u"2026-10-13"_s);
        const QVariantMap task = model.get(1);
        QCOMPARE(task.value(u"breadcrumb"_s).toString(), u"Zakupy"_s);
        QCOMPARE(task.value(u"taskStatus"_s).toString(), u"todo"_s);
        const QString html = task.value(u"html"_s).toString();
        QVERIFY2(html.contains(u"page:Remont%20mieszkania"_s), qPrintable(html));
        // The checkbox is the row's own (ContentRefRow's TaskBox), not a glyph in the text.
        QVERIFY2(!html.contains(u"task:toggle"_s) && !html.contains(u"[ ]"_s) && html.contains(u"farba"_s), qPrintable(html));
        QVERIFY2(!html.contains(u"<b>tanio"_s) && html.contains(u"&lt;b&gt;tanio"_s), qPrintable(html)); // escaped
        QCOMPARE(model.get(7).value(u"blockId"_s).toString(), u"xyz"_s);
        QCOMPARE(model.get(7).value(u"folder"_s).toString(), u"Notatki"_s);

        // Collapsing keeps the header and its count.
        model.toggleCollapsed(u"journals/2026-10-13.md"_s);
        QCOMPARE(model.rowCount(), 7);
        QCOMPARE(model.get(0).value(u"collapsed"_s).toBool(), true);
        QCOMPARE(model.get(0).value(u"count"_s).toInt(), 1);
        QCOMPARE(model.refCount(), 4);
        model.toggleCollapsed(u"journals/2026-10-13.md"_s);
        QCOMPARE(model.rowCount(), 8);

        // Checking the task in the list writes the file; the row follows (no reset).
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QVERIFY(model.toggleTask(1));
        QVERIFY(ws->readFile(u"journals/2026-10-13.md"_s).contains(u"- [x] farba"_s));
        model.refreshNow();
        QCOMPARE(model.get(1).value(u"taskStatus"_s).toString(), u"done"_s);
        QCOMPARE(resets.size(), 0);
        QVERIFY(!model.toggleTask(0)); // a header

        // ((abc123)) points at the other page's block.
        BacklinksModel other;
        other.setWorkspace(ws.get());
        other.setPath(u"Projekty/Remont mieszkania 2.md"_s);
        other.refreshNow();
        QCOMPARE(outline(other), (QStringList{ u"Budżet (1)"_s, u"  2: osobny blok z blok"_s }));
    }

    void unlinkedMentionsBecomeLinks()
    {
        auto ws = open({
                { u"Projekty/Dom.md"_s, u"---\naliases: [Domek]\n---\n- strona\n"_s },
                { u"Archiwum/Dom.md"_s, u"- stary\n"_s },
                { u"Plan.md"_s, u"- kupić farbę do domu\n- remont: dom i domek\n- `dom` w kodzie\n- już [[Projekty/Dom]]\n"_s },
                { u"journals/2026-10-13.md"_s, u"- Domek na działce, Dom\r\n- inne\r\n"_s },
        });
        BacklinksModel model;
        model.setWorkspace(ws.get());
        model.setPath(u"Projekty/Dom.md"_s);
        model.setMode(u"unlinked"_s);
        model.refreshNow();
        QCOMPARE(model.names(), (QStringList{ u"Dom"_s, u"Domek"_s }));
        QCOMPARE(outline(model), (QStringList{ u"2026-10-13 (1)"_s, u"  0: Domek na działce, Dom"_s, u"Plan (1)"_s,
                                               u"  1: remont: dom i domek"_s }));
        QVERIFY(model.get(1).value(u"html"_s).toString().contains(u"<span style=\"background-color:"_s));

        // "dom" alone resolves to the other Dom (shorter path wins? no: Archiwum < Projekty) — the
        // link names this page's path and keeps the text as written.
        QVERIFY(model.linkMention(3));
        QCOMPARE(ws->readFile(u"Plan.md"_s),
                 u"- kupić farbę do domu\n- remont: [[Projekty/Dom|dom]] i domek\n- `dom` w kodzie\n- już [[Projekty/Dom]]\n"_s);
        // An alias resolves by itself; CRLF is kept.
        QVERIFY(model.linkMention(1));
        QCOMPARE(ws->readFile(u"journals/2026-10-13.md"_s), u"- [[Domek]] na działce, Dom\r\n- inne\r\n"_s);
        model.refreshNow();

        // linkAll goes through what is left (the journal's "Dom", the plan's "domek").
        const int linked = model.linkAll();
        QVERIFY2(linked >= 1, qPrintable(QString::number(linked)));
        model.refreshNow();
        QCOMPARE(model.refCount(), 0);
        QVERIFY(ws->readFile(u"journals/2026-10-13.md"_s).contains(u"[[Projekty/Dom|Dom]]"_s));

        BacklinksModel linkedModel;
        linkedModel.setWorkspace(ws.get());
        linkedModel.setPath(u"Projekty/Dom.md"_s);
        linkedModel.refreshNow();
        QCOMPARE(linkedModel.pageCount(), 2);
        QVERIFY(!model.linkMention(0)); // nothing listed any more
    }

    // ---- Tags ----------------------------------------------------------------------------------

    void tagBlocksAndList()
    {
        auto ws = open({
                { u"Praca.md"_s, u"---\ntags: [Praca]\n---\n- spotkanie #Praca/Projekt\n- bez tagu\n"_s },
                { u"journals/2026-10-13.md"_s, u"- [ ] raport #praca\n  - szczegóły #inny\n- obiad #dom\n"_s },
                { u"Dom.md"_s, u"- remont #dom #pilne\n"_s },
        });
        TagBlocksModel blocks;
        blocks.setWorkspace(ws.get());
        blocks.setTag(u"#Praca"_s);
        blocks.refreshNow();
        QCOMPARE(blocks.tag(), u"praca"_s);
        QCOMPARE(blocks.display().toLower(), u"praca"_s);
        QCOMPARE(outline(blocks), (QStringList{ u"2026-10-13 (1)"_s, u"  0: raport #praca"_s, u"Praca (1)"_s, u"  3: spotkanie #Praca/Projekt"_s }));
        QCOMPARE(blocks.get(2).value(u"pageLevel"_s).toBool(), true);
        QCOMPARE(blocks.get(0).value(u"pageLevel"_s).toBool(), false);
        QCOMPARE(blocks.subTags().size(), 1);
        QCOMPARE(blocks.subTags().first().toMap().value(u"tag"_s).toString(), u"praca/projekt"_s);
        TagBlocksModel sub;
        sub.setWorkspace(ws.get());
        sub.setTag(u"praca/projekt"_s);
        sub.refreshNow();
        QCOMPARE(sub.parentTag(), u"praca"_s);
        QCOMPARE(sub.refCount(), 1);

        TagListModel list;
        list.setWorkspace(ws.get());
        list.refreshNow();
        QCOMPARE(list.totalCount(), 5);
        QCOMPARE(column(list, "tag").first(), u"dom"_s); // most used first (2), ties by name
        list.setSortKey(u"name"_s);
        list.refreshNow();
        QCOMPARE(column(list, "tag"), (QStringList{ u"dom"_s, u"inny"_s, u"pilne"_s, u"praca"_s, u"praca/projekt"_s }));
        QCOMPARE(value(list, 4, "depth").toInt(), 1);
        QCOMPARE(value(list, 4, "leaf").toString(), u"Projekt"_s);
        QCOMPARE(value(list, 4, "parent").toString(), u"praca"_s);
        list.setFilter(u"PRAC"_s);
        list.refreshNow();
        QCOMPARE(column(list, "tag"), (QStringList{ u"praca"_s, u"praca/projekt"_s }));
        QCOMPARE(value(list, 0, "matches").toList().size(), 4);
    }

    // ---- All pages ---------------------------------------------------------------------------------

    void pageListSortAndFilter()
    {
        auto ws = open({
                { u"Łódź.md"_s, u"- miasto [[Lody]] #podróże\n"_s },
                { u"Lody.md"_s, u"---\naliases: [Sorbet]\n---\n- przepis\n"_s },
                { u"Zebra.md"_s, u"- [[Lody]] i [[Łódź]] #zoo #podróże\n"_s },
                { u"Projekty/Żaba.md"_s, u"- [[Lody]]\n"_s },
                { u"journals/2026-10-13.md"_s, u"- [[Łódź]]\n"_s },
        });
        PageListModel model;
        model.setWorkspace(ws.get());
        model.refreshNow();
        QCOMPARE(column(model, "title"), (QStringList{ u"Lody"_s, u"Łódź"_s, u"Zebra"_s, u"Żaba"_s }));
        QCOMPARE(model.totalCount(), 4);
        QCOMPARE(value(model, 0, "backlinks").toInt(), 3); // Łódź, Zebra, Żaba
        QCOMPARE(value(model, 1, "backlinks").toInt(), 2); // Zebra + the journal
        QCOMPARE(value(model, 2, "tagCount").toInt(), 2);
        QCOMPARE(value(model, 3, "folder").toString(), u"Projekty"_s);

        model.setSortKey(u"backlinks"_s);
        model.setDescending(true);
        model.refreshNow();
        QCOMPARE(column(model, "title").first(), u"Lody"_s);
        model.setSortKey(u"title"_s);
        model.refreshNow();
        QCOMPARE(column(model, "title"), (QStringList{ u"Żaba"_s, u"Zebra"_s, u"Łódź"_s, u"Lody"_s }));
        model.setDescending(false);

        // Diacritic-insensitive filter over titles, aliases and folders, with highlights.
        model.setFilter(u"lodz"_s);
        model.refreshNow();
        QCOMPARE(column(model, "title"), (QStringList{ u"Łódź"_s }));
        QCOMPARE(markedText(u"Łódź"_s, value(model, 0, "titleMatches").toList()), u"Łódź"_s);
        model.setFilter(u"sorbet"_s);
        model.refreshNow();
        QCOMPARE(column(model, "title"), (QStringList{ u"Lody"_s }));
        model.setFilter(QString());
        model.setFolder(u"Projekty"_s);
        model.refreshNow();
        QCOMPARE(column(model, "title"), (QStringList{ u"Żaba"_s }));
        model.setFolder(QString());
        model.setIncludeJournals(true);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 5);

        // Modified order follows writes (newest first) without a reset.
        model.setIncludeJournals(false);
        model.setSortKey(u"modified"_s);
        model.setDescending(true);
        model.refreshNow();
        QTest::qWait(20);
        QVERIFY(ws->writeFile(u"Zebra.md"_s, u"- zmieniona\n"_s));
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        model.refreshNow();
        QCOMPARE(column(model, "title").first(), u"Zebra"_s);
        QCOMPARE(resets.size(), 0);
        QVERIFY(!value(model, 0, "modifiedLabel").toString().isEmpty());
        QCOMPARE(model.rowOf(u"Zebra.md"_s), 0);
    }

    // ---- Search ---------------------------------------------------------------------------------

    void searchResults()
    {
        auto ws = open({
                { u"Projekty/Remont mieszkania.md"_s, u"- kafelki do łazienki\n- remont kuchni w listopadzie\n"_s },
                { u"Żółw.md"_s, u"- karmić żółwia sałatą\n"_s },
                { u"journals/2026-10-13.md"_s, u"- zadzwonić do fachowca od remontu\n"_s },
        });
        SearchResultModel model;
        model.setWorkspace(ws.get());
        model.setQuery(u"remont"_s);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.totalCount(), 2);
        QCOMPARE(model.terms(), QStringList{ u"remont"_s });
        const QVariantMap first = model.get(0);
        QCOMPARE(first.value(u"path"_s).toString(), u"Projekty/Remont mieszkania.md"_s); // title match first
        QCOMPARE(markedText(first.value(u"title"_s).toString(), first.value(u"titleMatches"_s).toList()), u"Remont"_s);
        const int journalRow = column(model, "journalDate").indexOf(u"2026-10-13"_s);
        QVERIFY(journalRow >= 0);
        const QVariantMap j = model.get(journalRow);
        QCOMPARE(j.value(u"line"_s).toInt(), 0);
        QCOMPARE(markedText(j.value(u"excerpt"_s).toString(), j.value(u"excerptMatches"_s).toList()), u"remont"_s);

        model.setScope(u"pages"_s);
        model.refreshNow();
        QCOMPARE(column(model, "path"), QStringList{ u"Projekty/Remont mieszkania.md"_s });
        QCOMPARE(model.totalCount(), 2);
        model.setScope(u"journals"_s);
        model.refreshNow();
        QCOMPARE(column(model, "path"), QStringList{ u"journals/2026-10-13.md"_s });
        model.setScope(u"all"_s);
        model.setFolder(u"Projekty"_s);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.folders().size(), 2); // "Projekty" and "journals"
        model.setFolder(QString());

        // Without diacritics; the results follow the vault.
        model.setQuery(u"zolw"_s);
        model.refreshNow();
        QCOMPARE(column(model, "title"), QStringList{ u"Żółw"_s });
        QCOMPARE(markedText(u"Żółw"_s, value(model, 0, "titleMatches").toList()), u"Żółw"_s);
        QVERIFY(ws->writeFile(u"Notatki/Żółwie.md"_s, u"- żółwie lądowe\n"_s));
        model.refreshNow();
        QCOMPARE(model.rowCount(), 2);
        model.setQuery(u"   "_s);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 0);
    }

    void searchRealMatchesBeforeTypos()
    {
        // The sample notebook: "zolw" is a prefix of "żółwia" in a long day, and one letter away
        // from "Żółć" in a short one, which the index's BM25 score ranks higher.
        auto ws = open({
                { u"journals/2026-10-07.md"_s,
                  u"- Początek remontu! #projekt/dom\n- [x] Wybrać kolor ścian w salonie\n"
                  u"- Skończona „Ferdydurke” — [[Książki 2026]]\n- Żółć, ćma, źdźbło, łąka — Brulion lubi polskie znaki.\n"_s },
                { u"journals/2026-10-14.md"_s,
                  u"- Pierwszy dzień z [[Witaj w Brulionie|Brulionem]] #przewodnik\n"
                  u"- [ ] Przejrzeć [[Remont mieszkania]] i zamówić farbę 📅 2026-10-16\n"
                  u"- Spotkanie z ekipą remontową ⏰ 2026-10-15 17:30\n"
                  u"- Przeczytane dziś: 40 stron „Lalki” — [[Książki 2026]]\n"
                  u"- Pomysł na obiad: [[Przepisy#Pierogi ruskie|pierogi ruskie]] #pomysł\n"
                  u"  - albo żurek, jeśli będzie zakwas\n"
                  u"- Szybkie notatki z telefonu (przycisk „+”) trafiają właśnie tutaj.\n"
                  u"- Plan na wieczór\n  - Nakarmić żółwia Franka\n"_s },
        });
        SearchResultModel model;
        model.setWorkspace(ws.get());
        model.setQuery(u"zolw"_s);
        model.refreshNow();
        QCOMPARE(column(model, "path"), (QStringList{ u"journals/2026-10-14.md"_s, u"journals/2026-10-07.md"_s }));
        QCOMPARE(markedText(value(model, 0, "excerpt").toString(), value(model, 0, "excerptMatches").toList()), u"żółw"_s);
        QVERIFY(value(model, 1, "excerptMatches").toList().isEmpty()); // the typo-tolerant hit, last
    }

    // ---- History ---------------------------------------------------------------------------------

    void historyList()
    {
        auto ws = open({ { u"Notatka.md"_s, u"- wersja 1\n"_s } });
        brulion::HistoryPolicy policy = ws->vault()->history()->policy();
        policy.throttleSeconds = 0;
        ws->vault()->history()->setPolicy(policy);
        HistoryModel model;
        model.setWorkspace(ws.get());
        model.setPath(u"Notatka.md"_s);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.currentText(), u"- wersja 1\n"_s);
        QVERIFY(model.exists());

        QVERIFY(ws->writeFile(u"Notatka.md"_s, u"- wersja 2 zażółć\n"_s));
        m_now = m_now.addSecs(600);
        QVERIFY(ws->writeFile(u"Notatka.md"_s, u"- wersja 3\n"_s));
        model.refreshNow();
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.textAt(0), u"- wersja 2 zażółć\n"_s); // newest first
        QCOMPARE(model.textAt(1), u"- wersja 1\n"_s);
        QCOMPARE(value(model, 0, "reason").toString(), u"user"_s);
        QCOMPARE(value(model, 0, "reasonLabel").toString(), HistoryModel::reasonLabel(u"user"_s));
        QCOMPARE(value(model, 0, "newest").toBool(), true);
        QCOMPARE(value(model, 1, "newest").toBool(), false);
        QCOMPARE(value(model, 0, "day").toString(), u"Today"_s);
        QVERIFY(!value(model, 0, "clock").toString().isEmpty());
        QCOMPARE(value(model, 1, "sizeLabel").toString(), u"11 B"_s);
        QCOMPARE(model.currentText(), u"- wersja 3\n"_s);

        // Restoring keeps the replaced text as a version.
        QVERIFY(model.restore(1));
        QCOMPARE(ws->readFile(u"Notatka.md"_s), u"- wersja 1\n"_s);
        QCOMPARE(model.currentText(), u"- wersja 1\n"_s);
        QCOMPARE(model.textAt(0), u"- wersja 3\n"_s);
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.rowOf(model.get(2).value(u"id"_s).toString()), 2);
        QVERIFY(!model.restore(7));

        // A deleted page keeps its versions.
        QVERIFY(!ws->deletePage(u"Notatka.md"_s).isEmpty());
        model.refreshNow();
        QVERIFY(!model.exists());
        QCOMPARE(model.currentText(), QString());
        QVERIFY(model.rowCount() >= 3);
        QVERIFY(model.restore(0));
        QVERIFY(ws->exists(u"Notatka.md"_s));
    }

    // ---- Outline ----------------------------------------------------------------------------------

    void outlineFollowsTheBlocks()
    {
        QStandardItemModel blocks;
        blocks.setItemRoleNames({ { Qt::UserRole + 1, "headingLevel" }, { Qt::UserRole + 2, "plainText" },
                                  { Qt::UserRole + 3, "view" }, { Qt::UserRole + 4, "line" } });
        const auto add = [&blocks](int level, const QString &text, const QString &view, int line) {
            auto *item = new QStandardItem;
            item->setData(level, Qt::UserRole + 1);
            item->setData(text, Qt::UserRole + 2);
            item->setData(view, Qt::UserRole + 3);
            item->setData(line, Qt::UserRole + 4);
            blocks.appendRow(item);
        };
        add(0, u"wstęp"_s, u"text"_s, 0);
        add(2, u"Plan dnia"_s, u"heading"_s, 2);
        add(0, u"- punkt"_s, u"text"_s, 3);
        add(3, u"Zakupy  na  jutro"_s, u"heading"_s, 5);
        add(2, u"  "_s, u"heading"_s, 7); // empty heading: skipped
        add(1, u"## w kodzie"_s, u"code"_s, 9); // not a heading view
        OutlineModel outline;
        outline.setSource(&blocks);
        QCOMPARE(outline.count(), 2);
        QCOMPARE(column(outline, "text"), (QStringList{ u"Plan dnia"_s, u"Zakupy na jutro"_s }));
        QCOMPARE(column(outline, "depth"), (QStringList{ u"0"_s, u"1"_s }));
        QCOMPARE(column(outline, "row"), (QStringList{ u"1"_s, u"3"_s }));
        QCOMPARE(outline.entryForSourceRow(0), -1);
        QCOMPARE(outline.entryForSourceRow(2), 0);
        QCOMPARE(outline.entryForSourceRow(10), 1);

        // Typing in a heading updates in place; a new heading resets.
        QSignalSpy resets(&outline, &QAbstractItemModel::modelReset);
        blocks.item(1)->setData(u"Plan dnia — środa"_s, Qt::UserRole + 2);
        QTRY_COMPARE(column(outline, "text").first(), u"Plan dnia — środa"_s);
        QCOMPARE(resets.size(), 0);
        add(1, u"Tytuł"_s, u"heading"_s, 11);
        QTRY_COMPARE(outline.count(), 3);
        QCOMPARE(column(outline, "depth"), (QStringList{ u"1"_s, u"2"_s, u"0"_s }));
        outline.setSource(nullptr);
        QCOMPARE(outline.count(), 0);
    }

    // ---- A big notebook -------------------------------------------------------------------------------

    void manyNotes()
    {
        // Thousands of pages and journals: every list builds in one pass (no per-page queries).
        QHash<QString, QString> files;
        for (int i = 0; i < 2500; ++i)
            files.insert(u"Notatki/Strona %1.md"_s.arg(i), u"- [[Strona %1]] #źródło zażółć gęślą jaźń\n"_s.arg((i + 1) % 2500));
        for (int d = 1; d <= 1200; ++d)
            files.insert(u"journals/%1.md"_s.arg(QDate(2026, 10, 14).addDays(-d).toString(Qt::ISODate)), u"- dzień %1 [[Strona 7]]\n"_s.arg(d));
        auto ws = open(files);
        QElapsedTimer timer;
        timer.start();
        PageListModel pages;
        pages.setWorkspace(ws.get());
        pages.setSortKey(u"backlinks"_s);
        pages.setDescending(true);
        pages.refreshNow();
        QCOMPARE(pages.rowCount(), 2500);
        QCOMPARE(pages.get(0).value(u"title"_s).toString(), u"Strona 7"_s); // 1 page + 1200 journals
        QCOMPARE(pages.get(0).value(u"backlinks"_s).toInt(), 1201);
        JournalListModel journals;
        journals.setWorkspace(ws.get());
        journals.refreshNow();
        QCOMPARE(journals.rowCount(), 1201);
        BacklinksModel refs;
        refs.setWorkspace(ws.get());
        refs.setPath(u"Notatki/Strona 7.md"_s);
        refs.setLimit(50);
        refs.refreshNow();
        QCOMPARE(refs.refCount(), 1201);
        QCOMPARE(refs.hiddenCount(), 1201 - 50);
        QVERIFY(refs.rowCount() <= 100); // headers + 50 refs
        TagListModel tags;
        tags.setWorkspace(ws.get());
        tags.refreshNow();
        QCOMPARE(tags.get(0).value(u"tag"_s).toString(), u"źródło"_s);
        SearchResultModel search;
        search.setWorkspace(ws.get());
        search.setQuery(u"zazolc"_s);
        search.refreshNow();
        QCOMPARE(search.rowCount(), 100);
        QCOMPARE(search.totalCount(), 100);
        qInfo() << "lists of a big notebook built in" << timer.elapsed() << "ms";
        QVERIFY2(timer.elapsed() < 20000, "the lists take far too long");
    }

    void backlinksOfABusyPage()
    {
        // Two years of daily notes, each a dozen blocks that mention [[Praca]] here and there: the
        // page's references (50 shown) are refreshed after every save anywhere in the notebook
        // (the editor saves while typing), so a refresh must not render every reference.
        QHash<QString, QString> files;
        files.insert(u"Praca.md"_s, u"# Praca\n- projekty\n"_s);
        files.insert(u"Inna.md"_s, u"- coś innego\n"_s);
        for (int d = 0; d < 730; ++d) {
            QString day;
            for (int b = 0; b < 12; ++b) {
                day += (b % 4 == 0) ? u"- [ ] Spotkanie w [[Praca]] o **budżecie** ⏰ 2026-10-20 09:00 #praca żółć %1\n"_s.arg(b)
                                    : u"- Notatka %1 z dnia: zażółć gęślą jaźń [[Inna]] `kod` ~~skreślone~~\n"_s.arg(b);
                day += u"  - podpunkt z [link](https://example.com) i ((abc))\n"_s;
            }
            files.insert(u"journals/%1.md"_s.arg(QDate(2026, 10, 14).addDays(-d).toString(Qt::ISODate)), day);
        }
        auto ws = open(files);
        BacklinksModel refs;
        refs.setWorkspace(ws.get());
        refs.setPath(u"Praca.md"_s);
        refs.setLimit(50);
        QElapsedTimer timer;
        timer.start();
        refs.refreshNow();
        const qint64 first = timer.elapsed();
        QCOMPARE(refs.refCount(), 730 * 3);
        QCOMPARE(refs.hiddenCount(), 730 * 3 - 50);
        QVERIFY(refs.get(1).value(u"html"_s).toString().contains(u"<b>budżecie</b>"_s));

        // A save in an unrelated note (typing elsewhere): the rows stay, the refresh is cheap.
        QSignalSpy resets(&refs, &QAbstractItemModel::modelReset);
        QSignalSpy changed(&refs, &QAbstractItemModel::dataChanged);
        QVERIFY(ws->writeFile(u"Inna.md"_s, u"- coś innego, dopisane\n"_s));
        timer.restart();
        refs.refreshNow();
        const qint64 again = timer.elapsed();
        QCOMPARE(resets.size(), 0);
        QCOMPARE(changed.size(), 0);
        QCOMPARE(refs.refCount(), 730 * 3);
        qInfo() << "references of a busy page:" << first << "ms, after an unrelated save" << again << "ms";

        // Editing a referencing block updates its row.
        const QString path = refs.get(1).value(u"path"_s).toString();
        QString content = ws->readFile(path);
        content.replace(u"**budżecie**"_s, u"*planie*"_s);
        QVERIFY(ws->writeFile(path, content));
        refs.refreshNow();
        QVERIFY2(refs.get(1).value(u"html"_s).toString().contains(u"<i>planie</i>"_s), qPrintable(refs.get(1).value(u"html"_s).toString()));
        QCOMPARE(resets.size(), 0);

        // Shown rows are rendered; showing more renders the next ones.
        refs.setLimit(100);
        refs.refreshNow();
        int refRows = 0;
        int rendered = 0;
        for (int i = 0; i < refs.rowCount(); ++i) {
            if (refs.get(i).value(u"rowType"_s) != u"ref"_s)
                continue;
            ++refRows;
            if (refs.get(i).value(u"html"_s).toString().contains(u"href=\"page:Praca\""_s))
                ++rendered;
        }
        QCOMPARE(refRows, 100);
        QCOMPARE(rendered, 100);
        // Debug build on the laptop: ~110 ms when every reference was rendered, ~5 ms now.
        QVERIFY2(again < 40 * BRULION_TEST_SLOWDOWN, "an unrelated save re-renders every reference");
    }

    // ---- Page tools ---------------------------------------------------------------------------------

    void pageTools()
    {
        auto ws = open({
                { u"Projekty/Dom.md"_s,
                  u"---\naliases: [Domek, Chata]\ntags: [projekt, \"#Remont\"]\nstatus: w toku\n---\n# Dom\n- plan <script>\n- ![[plan.png]]\n"_s },
                { u"Archiwum/Dom.md"_s, u"- stary\n"_s },
                { u"A.md"_s, u"- [[Projekty/Dom]] i [[Projekty/Dom|dom]]\n"_s },
                { u"B.md"_s, u"- ![[Projekty/Dom]] ((zzz))\n"_s },
                { u"Projekty/Inne.md"_s, u"- tu [[Dom]] #remont\n"_s },
                { u"Projekty/żółw.md"_s, u"- zwierzę\n"_s },
                { u"assets/plan.png"_s, u"\x89PNG"_s },
                { u"journals/2026_10_13.md"_s, u"- z Logseqa\n"_s },
        });
        PageTools tools;
        tools.setWorkspace(ws.get());
        const QVariantMap impact = tools.renameImpact(u"Projekty/Dom.md"_s);
        QCOMPARE(impact.value(u"notes"_s).toInt(), 3);
        QCOMPARE(impact.value(u"links"_s).toInt(), 4);
        QCOMPARE(tools.renameImpact(u"Nie ma.md"_s).value(u"notes"_s).toInt(), 0);

        QCOMPARE(tools.validateTitle(u"Projekty/Dom.md"_s, u"Dom nad jeziorem"_s), QString());
        QVERIFY(!tools.validateTitle(u"Projekty/Dom.md"_s, u"   "_s).isEmpty());
        QVERIFY(!tools.validateTitle(u"Projekty/Dom.md"_s, u"Inne"_s).isEmpty());
        QCOMPARE(tools.validateTitle(u"Projekty/Dom.md"_s, u"dom"_s), QString()); // only the case changes
        QCOMPARE(tools.validateTitle(u"Projekty/Dom.md"_s, u"Dom/Inne"_s), QString()); // "/" becomes a space
        // Another page's name in another case: one file on case-insensitive storage (Android).
        QVERIFY(!tools.validateTitle(u"Projekty/Dom.md"_s, u"inne"_s).isEmpty());
        QVERIFY(!tools.validateTitle(u"Projekty/Dom.md"_s, u"ŻÓŁW"_s).isEmpty());
        QCOMPARE(tools.validateTitle(u"Projekty/Dom.md"_s, u"Żółwie"_s), QString());
        QCOMPARE(tools.validateTitle(u"Projekty/Inne.md"_s, u"INNE"_s), QString()); // its own name

        const QVariantMap s = tools.summary(u"Projekty/Dom.md"_s);
        QCOMPARE(s.value(u"aliases"_s).toStringList(), (QStringList{ u"Domek"_s, u"Chata"_s }));
        const QVariantList tags = s.value(u"tags"_s).toList();
        QCOMPARE(tags.size(), 2);
        QCOMPARE(tags.at(0).toMap().value(u"tag"_s).toString(), u"projekt"_s);
        QCOMPARE(tags.at(1).toMap().value(u"tag"_s).toString(), u"remont"_s);
        const QVariantList props = s.value(u"properties"_s).toList();
        QCOMPARE(props.size(), 1);
        QCOMPARE(props.first().toMap().value(u"value"_s).toString(), u"w toku"_s);
        QCOMPARE(s.value(u"folderParts"_s).toList().size(), 1);
        QCOMPARE(s.value(u"backlinks"_s).toInt(), 3);
        QVERIFY(s.value(u"exists"_s).toBool());
        QVERIFY(s.value(u"words"_s).toInt() >= 3);

        QCOMPARE(tools.wikiLink(u"Projekty/Dom.md"_s), u"[[Projekty/Dom]]"_s); // "Dom" alone is ambiguous
        QCOMPARE(tools.wikiLink(u"Projekty/Inne.md"_s), u"[[Inne]]"_s);
        QCOMPARE(tools.journalPathFor(u"2026-10-13"_s), u"journals/2026_10_13.md"_s); // Logseq's name
        QCOMPARE(tools.journalPathFor(u"2026-10-12"_s), u"journals/2026-10-12.md"_s); // not there yet
        QVERIFY(!ws->exists(u"journals/2026-10-12.md"_s));
        QCOMPARE(tools.journalPathFor(u"2026-13-45"_s), QString());
        QCOMPARE(tools.exportFileName(u"Projekty/Dom.md"_s, u"html"_s), u"Dom.html"_s);
        QCOMPARE(tools.exportText(u"Projekty/Dom.md"_s, u"markdown"_s), ws->readFile(u"Projekty/Dom.md"_s));
        const QString html = tools.exportText(u"Projekty/Dom.md"_s, u"html"_s);
        QVERIFY2(html.contains(u"<html"_s) && html.contains(u"Dom"_s), qPrintable(html.left(400)));
        QVERIFY(!html.contains(u"<script>"_s)); // note text is escaped
        QVERIFY(html.contains(u"data:image/png;base64,"_s)); // the embedded picture travels along
        QCOMPARE(tools.exportText(u"Nie ma.md"_s, u"markdown"_s), QString());

        const QString target = m_tmp.filePath(u"eksport/Dom.html"_s);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QVERIFY(tools.saveExport(u"Projekty/Dom.md"_s, u"html"_s, QUrl::fromLocalFile(target)));
        QFile saved(target);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(saved.readAll()), html);
        QVERIFY(!tools.saveExport(u"Projekty/Dom.md"_s, u"html"_s, QUrl()));
    }
};

QTEST_MAIN(TestContentModels)
#include "tst_content_models.moc"
