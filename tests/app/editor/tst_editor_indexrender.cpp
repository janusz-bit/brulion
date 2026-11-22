// What an index change renders again in an open page (BlockListModel::followIndex(),
// PageController.active; docs/UI.md "What an index change renders again"): an edit of an
// unrelated note changes no row; rows follow what they read of other notes — a linked page's
// icon, the text of a `((id))` / `[[Page#^id]]` block, embedded pages, table cells —; a change of
// names (a note added, removed, renamed) renders everything again; an inactive controller only
// goes stale and catches up once when active again. Runs with a FakeEditorHost (its index is kept
// and brought up to date, so its change log tells what changed).
#include "blocklistmodel.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Notatki/Strona.md"_s;
const QString Now = u"2026-10-15 10:00"_s;
const QString Inna = u"- Blok z innej strony ^inny1\n- Drugi blok\n"_s;

struct Fixture {
    FakeEditorHost host;
    PageController page;
    std::unique_ptr<QAbstractItemModelTester> tester;

    explicit Fixture(const QString &content)
    {
        host.setFile(u"Inna.md"_s, Inna);
        host.setFile(u"Obca.md"_s, u"- obca strona\n"_s);
        host.setFile(u"Trzecia.md"_s, u"- trzecia strona, zażółć gęślą jaźń\n"_s);
        host.setFile(Page, content);
        page.setNow(Now);
        page.setSaveDelay(0);
        page.setHost(&host);
        page.setPath(Page);
        tester = std::make_unique<QAbstractItemModelTester>(page.blocks(),
                                                            QAbstractItemModelTester::FailureReportingMode::QtTest);
        renderAll();
    }

    BlockListModel *model() const { return page.blocks(); }
    QVariant role(int row, const char *name) const
    {
        return model()->data(model()->index(row), model()->roleNames().key(name));
    }
    QString html(int row) const { return role(row, "html").toString(); }
    int tableRow() const
    {
        for (int r = 0; r < model()->rowCount(); ++r) {
            if (model()->viewAt(r) == u"table")
                return r;
        }
        return -1;
    }
    QString cellHtml(int row, int column) const
    {
        const QVariantList rows = role(tableRow(), "tableData").toMap().value(u"rows"_s).toList();
        return rows.value(row).toList().value(column).toMap().value(u"html"_s).toString();
    }
    QVariantList transclusions(int row) const { return role(row, "transclusions").toList(); }
    /// Every row as a view shows it (the rendering roles computed).
    void renderAll() const
    {
        for (int r = 0; r < model()->rowCount(); ++r) {
            for (const char *name : {"html", "transclusions", "embeds", "properties", "tableData"})
                (void)role(r, name);
        }
    }
    /// `path` saved with `content` (another editor, another device): true once the index told.
    bool change(const QString &path, const QString &content)
    {
        QSignalSpy index(&host, &EditorHost::indexChanged);
        host.write(path, content);
        return index.wait();
    }
};

/// The dataChanged signals a spy saw: "first-last:role,role" (role names).
QStringList changes(const QSignalSpy &spy, const QAbstractItemModel &model)
{
    QStringList out;
    const QHash<int, QByteArray> names = model.roleNames();
    for (const QList<QVariant> &args : spy) {
        QStringList roles;
        for (const int role : args.at(2).value<QList<int>>())
            roles.append(QString::fromLatin1(names.value(role)));
        out.append(u"%1-%2:%3"_s.arg(args.at(0).toModelIndex().row())
                           .arg(args.at(1).toModelIndex().row())
                           .arg(roles.join(u',')));
    }
    return out;
}

/// The text of the blocks a transclusion shows (their html).
QString shownText(const QVariantList &transclusions)
{
    QStringList out;
    for (const QVariant &t : transclusions) {
        for (const QVariant &b : t.toMap().value(u"blocks"_s).toList())
            out.append(b.toMap().value(u"html"_s).toString());
    }
    return out.join(u'\n');
}

} // namespace

class TestIndexRender : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // A note nothing on the page reads changes: no row is told anything, every html stays.
    void unrelatedEditKeepsEveryRow()
    {
        Fixture f(u"- [[Inna]] i [[Obca]]\n- ((inny1)) cytat\n- ![[Inna]]\n- [ ] zadanie ⛔ abc\n"
                  "- zwykły tekst, żółć\n\n| a |\n|---|\n| [[Obca]] |\n"_s);
        QVERIFY(f.tableRow() > 0);
        QStringList before;
        for (int r = 0; r < f.model()->rowCount(); ++r)
            before.append(f.html(r));
        const QVariantList embedded = f.transclusions(2);
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        QVERIFY(f.change(u"Trzecia.md"_s, u"- trzecia strona po zmianie, ąęś\n"_s));
        QCOMPARE(changes(changed, *f.model()), QStringList());
        for (int r = 0; r < f.model()->rowCount(); ++r)
            QCOMPARE(f.html(r), before.at(r));
        QCOMPARE(f.transclusions(2), embedded);
        // Another note's edit that changes nothing the rows show: nothing either.
        QVERIFY(f.change(u"Inna.md"_s, Inna + u"- trzeci blok, nowy\n"_s));
        QCOMPARE(changes(changed, *f.model()), QStringList{u"2-2:transclusions"_s});
    }

    // A note added or removed (names): links resolve differently anywhere — everything again.
    void namesRestyleLinks()
    {
        Fixture f(u"- [[Nowa strona]]\n- [[Inna]]\n- tekst bez linków\n"_s);
        const QString unresolved = f.html(0);
        const QString resolved = f.html(1);
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        QVERIFY(f.change(u"Nowa strona.md"_s, u"- nowa, świeża\n"_s));
        QVERIFY(changes(changed, *f.model())
                        .contains(u"0-2:html,renderedText,embeds,onlyEmbeds,transclusions,properties"_s));
        QVERIFY(f.html(0) != unresolved);
        QCOMPARE(f.html(1), resolved);
        // A rename: the old name no longer resolves.
        changed.clear();
        QSignalSpy index(&f.host, &EditorHost::indexChanged);
        f.host.removeFile(u"Inna.md"_s);
        f.host.write(u"Inna2.md"_s, Inna);
        QVERIFY(index.wait());
        QVERIFY(!changed.isEmpty());
        QVERIFY(f.html(1) != resolved);
    }

    // An embedded page changes: that embed is built again, the other one and the text stay.
    void embedFollowsItsNoteOnly()
    {
        Fixture f(u"- tekst przed\n- ![[Inna]]\n- ![[Trzecia]]\n"_s);
        const QVariantList inna = f.transclusions(1);
        QVERIFY(shownText(f.transclusions(2)).contains(u"zażółć"_s));
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        QVERIFY(f.change(u"Trzecia.md"_s, u"- trzecia strona, źdźbło trawy\n"_s));
        QCOMPARE(changes(changed, *f.model()), QStringList{u"2-2:transclusions"_s});
        QVERIFY(shownText(f.transclusions(2)).contains(u"źdźbło trawy"_s));
        QCOMPARE(f.transclusions(1), inna);
    }

    // `((id))` and `[[Page#^id]]` show their block's text as it is now.
    void blockRefsFollowTheirBlock()
    {
        Fixture f(u"- ((inny1)) cytat\n- [[Inna#^inny1]]\n- [[Inna]] bez bloku\n"_s);
        QVERIFY(f.html(0).contains(u"Blok z innej strony"_s));
        const QString plain = f.html(2);
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        QVERIFY(f.change(u"Inna.md"_s, u"- Blok po zmianie, gęś ^inny1\n- Drugi blok\n"_s));
        QCOMPARE(changes(changed, *f.model()), QStringList{u"0-1:html,renderedText"_s});
        QVERIFY(f.html(0).contains(u"Blok po zmianie, gęś"_s));
        QVERIFY(f.html(1).contains(u"Blok po zmianie"_s));
        QCOMPARE(f.html(2), plain);
        // The block moved to another note: the reference finds it there.
        changed.clear();
        QVERIFY(f.change(u"Inna.md"_s, u"- Drugi blok\n"_s));
        QVERIFY(f.change(u"Obca.md"_s, u"- obca strona\n- Blok przeniesiony, łódź ^inny1\n"_s));
        QVERIFY(f.html(0).contains(u"Blok przeniesiony, łódź"_s));
    }

    // A linked page's `icon:` changes: the links to it show the new icon (also in table cells).
    void linkIconFollowsThePage()
    {
        Fixture f(u"- [[Obca]] z ikoną\n- [[Inna]] bez\n\n| strona |\n|---|\n| [[Obca]] |\n"_s);
        const int table = f.tableRow();
        QVERIFY(table > 0);
        QVERIFY(f.cellHtml(0, 0).contains(u"Obca"_s));
        QVERIFY(!f.html(0).contains(u"🚀"_s));
        const QString other = f.html(1);
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        QVERIFY(f.change(u"Obca.md"_s, u"---\nicon: 🚀\n---\n- obca strona\n"_s));
        QVERIFY(f.html(0).contains(u"🚀"_s));
        QCOMPARE(f.html(1), other);
        QVERIFY(f.cellHtml(0, 0).contains(u"🚀"_s));
        QCOMPARE(changes(changed, *f.model()),
                 (QStringList{u"0-0:html,renderedText"_s, u"%1-%1:tableData"_s.arg(table)}));
    }

    // Hidden (a day of the hidden Journal): index changes only make it stale; shown again, it
    // catches up once with all of them.
    void inactiveGoesStaleThenCatchesUp()
    {
        Fixture f(u"- ((inny1))\n- ![[Trzecia]]\n- [[Obca]]\n"_s);
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        QSignalSpy active(&f.page, &PageController::activeChanged);
        f.page.setActive(false);
        QCOMPARE(active.size(), 1);
        QVERIFY(f.change(u"Inna.md"_s, u"- Blok ukryty, ćma ^inny1\n"_s));
        QVERIFY(f.change(u"Trzecia.md"_s, u"- trzecia, ukryta treść\n"_s));
        QVERIFY(f.page.indexStale());
        QCOMPARE(changes(changed, *f.model()), QStringList());
        QVERIFY(f.html(0).contains(u"Blok z innej strony"_s));
        f.page.setActive(true);
        QVERIFY(!f.page.indexStale());
        QCOMPARE(changes(changed, *f.model()), (QStringList{u"0-0:html,renderedText"_s, u"1-1:transclusions"_s}));
        QVERIFY(f.html(0).contains(u"Blok ukryty, ćma"_s));
        QVERIFY(shownText(f.transclusions(1)).contains(u"ukryta treść"_s));
        // Active again without a change in between: nothing to do.
        changed.clear();
        f.page.setActive(false);
        f.page.setActive(true);
        QCOMPARE(changes(changed, *f.model()), QStringList());
    }

    // Source mode shows the page's text, not its rows (the list behind it is hidden): index
    // changes — an autosave of the source among them — render no row; leaving source mode
    // catches up once with all of them, also after an edit of the source.
    void sourceModeGoesStaleThenCatchesUp_data()
    {
        QTest::addColumn<bool>("edited");
        QTest::newRow("source unchanged") << false;
        QTest::newRow("source edited") << true;
    }
    void sourceModeGoesStaleThenCatchesUp()
    {
        QFETCH(bool, edited);
        Fixture f(u"- ((inny1))\n- ![[Trzecia]]\n- [[Obca]]\n"_s);
        f.page.setSourceMode(true);
        QVERIFY(f.page.sourceMode());
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        QVERIFY(f.change(u"Inna.md"_s, u"- Blok zmieniony podczas edycji, ćma ^inny1\n"_s));
        QVERIFY(f.change(u"Trzecia.md"_s, u"- trzecia, zmieniona podczas edycji źródła\n"_s));
        if (edited) {
            // The source edited and autosaved meanwhile (the page's own change in the index).
            QSignalSpy index(&f.host, &EditorHost::indexChanged);
            f.page.setSourceText(f.page.sourceText() + u"- nowy blok, żółw\n"_s);
            QVERIFY(f.page.save());
            QVERIFY(index.size() > 0 || index.wait());
        }
        QVERIFY(f.page.indexStale());
        QCOMPARE(changes(changed, *f.model()), QStringList());
        f.page.setSourceMode(false);
        QVERIFY(!f.page.indexStale());
        QVERIFY(f.html(0).contains(u"Blok zmieniony podczas edycji, ćma"_s));
        QVERIFY(shownText(f.transclusions(1)).contains(u"zmieniona podczas edycji źródła"_s));
        if (edited)
            QVERIFY(f.html(3).contains(u"nowy blok, żółw"_s));
        else
            QCOMPARE(changes(changed, *f.model()), (QStringList{u"0-0:html,renderedText"_s, u"1-1:transclusions"_s}));
        // Source mode again without a change in between: nothing to do on the way out.
        f.renderAll();
        changed.clear();
        f.page.setSourceMode(true);
        f.page.setSourceMode(false);
        QCOMPARE(changes(changed, *f.model()), QStringList());
    }

    // A full invalidation (another palette, language, host) and the index followed after it.
    void fullInvalidationThenFollowing()
    {
        Fixture f(u"- ((inny1))\n- tekst\n"_s);
        f.model()->invalidateRendering();
        f.renderAll();
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        QVERIFY(f.change(u"Trzecia.md"_s, u"- znowu inna treść\n"_s));
        QCOMPARE(changes(changed, *f.model()), QStringList());
        QVERIFY(f.change(u"Inna.md"_s, u"- Blok po raz trzeci, żaba ^inny1\n"_s));
        QCOMPARE(changes(changed, *f.model()), QStringList{u"0-0:html,renderedText"_s});
        QVERIFY(f.html(0).contains(u"żaba"_s));
    }
};

QTEST_MAIN(TestIndexRender)
#include "tst_editor_indexrender.moc"
