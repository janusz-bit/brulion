// Page properties in the block editor (docs/UI.md "Page properties"): PageController's
// pageProperties and its single-property edits (one undo step each, source mode, refusals), the
// suggestions, the model's `properties` / `propertiesInfo` roles, and the leading frontmatter row
// that keeps its row through edits, undo and external changes (the panel's delegate survives).
#include "blocklistmodel.h"
#include "blocktext.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"

#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Notatki/Remont.md"_s;

struct Fixture {
    FakeEditorHost host;
    PageController page;
    std::unique_ptr<QAbstractItemModelTester> tester;

    explicit Fixture(const QString &content)
    {
        host.setFile(Page, content);
        page.setNow(u"2026-10-28 10:00"_s);
        page.setSaveDelay(0);
        page.setHost(&host);
        page.setPath(Page);
        tester = std::make_unique<QAbstractItemModelTester>(page.blocks(),
                                                            QAbstractItemModelTester::FailureReportingMode::QtTest);
    }

    BlockListModel *model() const { return page.blocks(); }
    QVariant role(int row, const char *name) const
    {
        const QHash<int, QByteArray> roles = model()->roleNames();
        return model()->data(model()->index(row), roles.key(name));
    }
    QString text() const { return page.text(); }
    QVariantList items() const { return page.pageProperties().value(u"items"_s).toList(); }
    QVariantMap item(const QString &key) const
    {
        for (const QVariant &v : items()) {
            if (v.toMap().value(u"key"_s).toString() == key)
                return v.toMap();
        }
        return {};
    }
    QStringList names(const char *list) const
    {
        QStringList out;
        for (const QVariant &v : page.pageProperties().value(QString::fromLatin1(list)).toList())
            out.append(v.toMap().value(u"name"_s).toString());
        return out;
    }
};

QString status(const QVariantMap &result)
{
    return result.value(u"status"_s).toString();
}

} // namespace

class TestPageProps : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void editIsOneUndoStep()
    {
        const QString content = u"---\nstatus: stary # wybrany\ntags: [dom]\n---\n- Zażółć gęślą jaźń\n"_s;
        Fixture f(content);
        const QVariantMap r = f.page.setPageProperty(QString(), u"status"_s, u"w toku"_s, u"text"_s);
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(status(r), u"changed"_s);
        QCOMPARE(r.value(u"reason"_s).toString(), QString());
        const QString edited = u"---\nstatus: w toku # wybrany\ntags: [dom]\n---\n- Zażółć gęślą jaźń\n"_s;
        QCOMPARE(f.text(), edited);
        QVERIFY(f.page.canUndo());
        // The toast's "Cofnij": the edit is the last step.
        const QString token = f.page.undoToken();
        QVERIFY(f.page.undoStep(token));
        QCOMPARE(f.text(), content);
        QVERIFY(!f.page.canUndo());
        f.page.redo();
        QCOMPARE(f.text(), edited);
        QVERIFY(f.page.save());
        QCOMPARE(f.host.file(Page), edited);
        // Already so: nothing changes, no new step.
        f.page.undo();
        f.page.redo();
        const QVariantMap same = f.page.setPageProperty(u"yaml"_s, u"status"_s, u"w toku"_s, u"text"_s);
        QVERIFY(same.value(u"ok"_s).toBool());
        QCOMPARE(status(same), u"unchanged"_s);
        QCOMPARE(f.text(), edited);
    }

    void rowSurvivesEdits()
    {
        Fixture f(u"---\nstatus: a\n---\n- x\n- y\n"_s);
        QCOMPARE(f.role(0, "kind").toString(), u"frontmatter"_s);
        QSignalSpy removed(f.model(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(f.model(), &QAbstractItemModel::rowsInserted);
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        const auto changedRow0 = [&changed] {
            return std::any_of(changed.cbegin(), changed.cend(),
                               [](const QList<QVariant> &args) { return args.at(0).toModelIndex().row() == 0; });
        };
        // A property edit, the note mode, undo, redo and another device's change: the row stays.
        QVERIFY(f.page.setPageProperty(QString(), u"status"_s, u"b"_s, u"text"_s).value(u"ok"_s).toBool());
        QVERIFY(changedRow0());
        QVERIFY(f.page.setPageProperty(QString(), u"termin"_s, u"2026-10-28"_s, u"date"_s).value(u"ok"_s).toBool());
        QVERIFY(f.page.setMode(u"document"_s));
        f.page.undo();
        f.page.undo();
        f.page.redo();
        QVERIFY(f.page.save());
        f.host.externalWrite(Page, u"---\nstatus: z innego urządzenia\n---\n- x\n- y\n"_s);
        QCOMPARE(f.text(), u"---\nstatus: z innego urządzenia\n---\n- x\n- y\n"_s);
        QCOMPARE(removed.size(), 0);
        QCOMPARE(inserted.size(), 0);
        QCOMPARE(f.item(u"status"_s).value(u"value"_s).toString(), u"z innego urządzenia"_s);
        QCOMPARE(f.role(0, "properties").toList().size(), 1);
        // A block whose kind changes is no frontmatter row any more: removed and inserted as before.
        QVERIFY(f.page.removePageProperty(QString(), u"status"_s).value(u"ok"_s).toBool());
        QCOMPARE(f.text(), u"- x\n- y\n"_s);
        QCOMPARE(removed.size(), 1);
        QCOMPARE(f.model()->rowCount(), 2);
    }

    void sourceMode()
    {
        Fixture f(u"---\na: 1\n---\nTreść\n"_s);
        f.page.setSourceMode(true);
        const QVariantMap r = f.page.setPageProperty(QString(), u"b"_s, u"Łódź"_s, u"text"_s);
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(f.page.sourceText(), u"---\na: 1\nb: Łódź\n---\nTreść\n"_s);
        QCOMPARE(f.item(u"b"_s).value(u"value"_s).toString(), u"Łódź"_s);   // the header follows
        QCOMPARE(f.host.file(Page), u"---\na: 1\nb: Łódź\n---\nTreść\n"_s); // saved (no delay here)
        QVERIFY(f.page.addPageListItem(u"tags"_s, u"#remont"_s).value(u"ok"_s).toBool());
        QCOMPARE(f.page.sourceText(), u"---\na: 1\nb: Łódź\ntags:\n  - remont\n---\nTreść\n"_s);
        QCOMPARE(f.names("tags"), QStringList{u"remont"_s});
        f.page.setSourceMode(false);
        QCOMPARE(f.text(), u"---\na: 1\nb: Łódź\ntags:\n  - remont\n---\nTreść\n"_s);
    }

    void refusals()
    {
        const QString indented = u"---\n  a: 1\n---\nx\n"_s;
        Fixture f(indented);
        QVERIFY(!f.page.pageProperties().value(u"canAdd"_s).toBool());
        QCOMPARE(f.page.pageProperties().value(u"problem"_s).toString(), u"indented"_s);
        const QVariantMap r = f.page.setPageProperty(QString(), u"b"_s, u"x"_s, u"text"_s);
        QVERIFY(!r.value(u"ok"_s).toBool());
        QCOMPARE(status(r), u"unsafe"_s);
        QVERIFY(!r.value(u"reason"_s).toString().isEmpty());
        QCOMPARE(f.text(), indented);
        QVERIFY(!f.page.canUndo());
        QCOMPARE(f.role(0, "propertiesInfo").toMap().value(u"editable"_s).toBool(), false);

        Fixture g(u"---\nStatus: a\nliczba: 1\n---\n"_s);
        QCOMPARE(status(g.page.setPageProperty(u"yaml"_s, u"status"_s, u"b"_s, u"text"_s)), u"keyExists"_s);
        QCOMPARE(status(g.page.setPageProperty(QString(), u"brulion"_s, u"x"_s, u"text"_s)), u"reserved"_s);
        const QVariantMap number = g.page.setPageProperty(QString(), u"liczba"_s, u"dużo"_s, u"number"_s);
        QCOMPARE(status(number), u"invalidValue"_s);
        QCOMPARE(number.value(u"reason"_s).toString(), u"Not a number"_s);
        QCOMPARE(status(g.page.renamePageProperty(QString(), u"liczba"_s, u"STATUS"_s)), u"keyExists"_s);
        QCOMPARE(status(g.page.renamePageProperty(QString(), u"liczba"_s, QString())), u"invalidKey"_s);
        QCOMPARE(status(g.page.removePageProperty(QString(), u"brak"_s)), u"notFound"_s);
        QCOMPARE(g.page.removePageProperty(QString(), u"brak"_s).value(u"reason"_s).toString(),
                 u"This property was changed elsewhere"_s);
        QCOMPARE(status(g.page.addPageListItem(u"aliases"_s, u"a|b"_s)), u"invalidValue"_s);
        QCOMPARE(g.text(), u"---\nStatus: a\nliczba: 1\n---\n"_s);
    }

    void firstProperty()
    {
        // No properties at all: a new YAML block (the row appears above the blocks).
        Fixture f(u"- a\n"_s);
        QCOMPARE(f.page.pageProperties().value(u"visibleRow"_s).toInt(), -1);
        QCOMPARE(f.page.pageProperties().value(u"addTarget"_s).toString(), u"yaml"_s);
        QVERIFY(f.page.setPageProperty(QString(), u"status"_s, QVariant(), u"text"_s).value(u"ok"_s).toBool());
        QCOMPARE(f.text(), u"---\nstatus:\n---\n- a\n"_s);
        QCOMPARE(f.role(0, "kind").toString(), u"frontmatter"_s);
        QCOMPARE(f.page.pageProperties().value(u"visibleRow"_s).toInt(), 0);
        QCOMPARE(f.item(u"status"_s).value(u"empty"_s).toBool(), true);
        f.page.undo();
        QCOMPARE(f.text(), u"- a\n"_s);
        QCOMPARE(f.page.pageProperties().value(u"items"_s).toList().size(), 0);

        // A frontmatter holding only the note mode is hidden: the property makes it visible.
        Fixture g(u"---\nbrulion: outline\n---\n- a\n"_s);
        QVERIFY(!g.role(0, "visible").toBool());
        QCOMPARE(g.page.pageProperties().value(u"visibleRow"_s).toInt(), -1);
        QSignalSpy removed(g.model(), &QAbstractItemModel::rowsRemoved);
        QVERIFY(g.page.setPageProperty(QString(), u"status"_s, u"x"_s, u"text"_s).value(u"ok"_s).toBool());
        QCOMPARE(g.text(), u"---\nbrulion: outline\nstatus: x\n---\n- a\n"_s);
        QVERIFY(g.role(0, "visible").toBool());
        QCOMPARE(g.page.pageProperties().value(u"visibleRow"_s).toInt(), 0);
        QCOMPARE(removed.size(), 0);
        QCOMPARE(g.page.mode(), u"outline"_s);

        // Only Logseq lines: a new line there; a key Logseq cannot hold is refused.
        Fixture h(u"tags:: a\n\n- x\n"_s);
        QCOMPARE(h.page.pageProperties().value(u"addTarget"_s).toString(), u"logseq"_s);
        QVERIFY(h.page.setPageProperty(QString(), u"status"_s, u"nowy"_s, u"text"_s).value(u"ok"_s).toBool());
        QCOMPARE(h.text(), u"tags:: a\nstatus:: nowy\n\n- x\n"_s);
        const QVariantMap polish = h.page.setPageProperty(QString(), u"źródło"_s, u"x"_s, u"text"_s);
        QCOMPARE(status(polish), u"invalidKey"_s);
        QVERIFY(polish.value(u"reason"_s).toString().contains(u"Logseq"_s));
    }

    void pagePropertiesFollow()
    {
        Fixture f(u"---\naliases: [Remont łazienki]\ntags: [dom]\n---\ntags:: projekt\n\n- a\n"_s);
        QSignalSpy spy(&f.page, &PageController::pagePropertiesChanged);
        QCOMPARE(f.names("tags"), QStringList({u"dom"_s, u"projekt"_s}));
        QCOMPARE(f.names("aliases"), QStringList({u"Remont łazienki"_s}));
        QVERIFY(f.page.pageProperties().value(u"hasYaml"_s).toBool());
        QVERIFY(f.page.pageProperties().value(u"hasLogseq"_s).toBool());
        QVERIFY(f.page.addPageListItem(u"tags"_s, u"Żółw"_s).value(u"ok"_s).toBool());
        QCOMPARE(f.text(), u"---\naliases: [Remont łazienki]\ntags: [dom, Żółw]\n---\ntags:: projekt\n\n- a\n"_s);
        QCOMPARE(f.names("tags"), QStringList({u"dom"_s, u"Żółw"_s, u"projekt"_s}));
        QVERIFY(spy.size() >= 1);
        QCOMPARE(status(f.page.addPageListItem(u"tags"_s, u"żółw"_s)), u"unchanged"_s);
        QVERIFY(f.page.removePageListItem(u"tags"_s, u"projekt"_s).value(u"ok"_s).toBool());
        QCOMPARE(f.text(), u"---\naliases: [Remont łazienki]\ntags: [dom, Żółw]\n---\n- a\n"_s);
        QCOMPARE(f.names("tags"), QStringList({u"dom"_s, u"Żółw"_s}));
        f.page.undo();
        QCOMPARE(f.names("tags"), QStringList({u"dom"_s, u"Żółw"_s, u"projekt"_s}));
        QVERIFY(f.page.save());
        f.host.externalWrite(Page, u"---\naliases: [Łazienka]\n---\n- a\n"_s);
        QCOMPARE(f.names("aliases"), QStringList({u"Łazienka"_s}));
        QVERIFY(f.names("tags").isEmpty());
        // Zoomed in: the properties row is not shown (the editor zooms out to add one).
        Fixture g(u"---\na: 1\n---\n- a\n\t- b\n"_s);
        QCOMPARE(g.page.pageProperties().value(u"visibleRow"_s).toInt(), 0);
        g.page.zoomInto(1);
        QCOMPARE(g.page.pageProperties().value(u"visibleRow"_s).toInt(), -1);
        g.page.zoomOut();
        QCOMPARE(g.page.pageProperties().value(u"visibleRow"_s).toInt(), 0);
    }

    void typeMenu()
    {
        Fixture f(u"---\nkoszt: dużo\nlista: a, b\n---\n"_s);
        const QVariantMap r = f.page.setPagePropertyType(QString(), u"koszt"_s, u"number"_s);
        QVERIFY(r.value(u"ok"_s).toBool());
        QVERIFY(r.value(u"lost"_s).toBool());
        QCOMPARE(r.value(u"reason"_s).toString(), u"The value “dużo” is not a number — it was cleared."_s);
        QCOMPARE(f.text(), u"---\nkoszt:\nlista: a, b\n---\n"_s);
        f.page.undo();
        QCOMPARE(f.text(), u"---\nkoszt: dużo\nlista: a, b\n---\n"_s);
        const QVariantMap list = f.page.setPagePropertyType(QString(), u"lista"_s, u"list"_s);
        QVERIFY(!list.value(u"lost"_s).toBool());
        QCOMPARE(f.text(), u"---\nkoszt: dużo\nlista:\n  - a\n  - b\n---\n"_s);
        QCOMPARE(f.item(u"lista"_s).value(u"type"_s).toString(), u"list"_s);
        QCOMPARE(status(f.page.setPagePropertyType(QString(), u"brak"_s, u"text"_s)), u"notFound"_s);
    }

    void rename()
    {
        Fixture f(u"---\nstatus: w toku\n---\nstan:: x\n"_s);
        QVERIFY(f.page.renamePageProperty(u"yaml"_s, u"status"_s, u"Stan projektu"_s).value(u"ok"_s).toBool());
        QCOMPARE(f.text(), u"---\nStan projektu: w toku\n---\nstan:: x\n"_s);
        QVERIFY(f.page.renamePageProperty(u"logseq"_s, u"stan"_s, u"faza"_s).value(u"ok"_s).toBool());
        QCOMPARE(f.text(), u"---\nStan projektu: w toku\n---\nfaza:: x\n"_s);
    }

    void suggestions()
    {
        Fixture f(u"---\nstatus: w toku\n---\n- a\n"_s);
        f.host.setFile(u"Dom.md"_s, u"---\nstatus: gotowe\nźródło: Wikipedia\ntermin: 2026-10-01\ntags: [dom, ogród]\n"
                                    u"rel: [\"[[Ogród]]\"]\nbrulion: document\n---\n"_s);
        f.host.setFile(u"Ogród.md"_s, u"---\nźródło: Książka\ntermin: 2026-11-01\nStatus: w toku\n---\n"_s);
        QStringList keys;
        for (const QVariant &v : f.page.suggestPropertyKeys(QString()))
            keys.append(v.toMap().value(u"key"_s).toString());
        // Not the page's own keys (any case), never the note mode.
        QCOMPARE(keys, QStringList({u"termin"_s, u"źródło"_s, u"rel"_s, u"tags"_s}));
        const QVariantList zro = f.page.suggestPropertyKeys(u"zro"_s);
        QCOMPARE(zro.size(), 1);
        QCOMPARE(zro.first().toMap().value(u"key"_s).toString(), u"źródło"_s);
        QCOMPARE(zro.first().toMap().value(u"count"_s).toInt(), 2);
        QCOMPARE(zro.first().toMap().value(u"type"_s).toString(), u"text"_s);
        QCOMPARE(f.page.suggestPropertyKeys(u"r"_s).first().toMap().value(u"key"_s).toString(), u"rel"_s);
        // Values: the notebook's values of the key, without the one the page holds.
        QStringList values;
        for (const QVariant &v : f.page.suggestPropertyValues(u"status"_s, QString()))
            values.append(v.toMap().value(u"value"_s).toString());
        QCOMPARE(values, QStringList({u"gotowe"_s}));
        const QVariantList pages = f.page.suggestPropertyValues(u"rel"_s, u"[[Ogr"_s);
        QVERIFY(!pages.isEmpty());
        QCOMPARE(pages.first().toMap().value(u"value"_s).toString(), u"[[Ogród]]"_s);
        QCOMPARE(pages.first().toMap().value(u"kind"_s).toString(), u"page"_s);
        const QVariantList tags = f.page.suggestPropertyValues(u"tags"_s, u"og"_s);
        QVERIFY(!tags.isEmpty());
        QCOMPARE(tags.first().toMap().value(u"kind"_s).toString(), u"tag"_s);
        QCOMPARE(tags.first().toMap().value(u"value"_s).toString(), u"ogród"_s);
        QVERIFY(f.page.suggestPropertyValues(u"aliases"_s, u"a"_s).isEmpty());
        // The notebook's type of a key (an empty value takes it); follows the index.
        QCOMPARE(f.page.notebookPropertyType(u"Termin"_s), u"date"_s);
        QCOMPARE(f.page.notebookPropertyType(u"brak"_s), QString());
        f.host.setFile(u"Nowa.md"_s, u"---\nocena: 5\n---\n"_s);
        QCOMPARE(f.page.notebookPropertyType(u"ocena"_s), QString()); // cached
        Q_EMIT f.host.indexChanged();
        QCOMPARE(f.page.notebookPropertyType(u"ocena"_s), u"number"_s);
    }

    void propertiesRole()
    {
        Fixture f(u"---\nopis: \"<b>[[Dom]]</b> #remont\"\nn: 3\nl: [\"[[Ogród]]\", x]\ngotowe: false\n---\n- a\n"_s);
        const QVariantList props = f.role(0, "properties").toList();
        QCOMPARE(props.size(), 4);
        const QVariantMap opis = props[0].toMap();
        QCOMPARE(opis.value(u"type"_s).toString(), u"text"_s);
        QCOMPARE(opis.value(u"source"_s).toString(), u"yaml"_s);
        const QString html = opis.value(u"html"_s).toString();
        QVERIFY(html.contains(u"&lt;b&gt;"_s)); // escaped, never raw HTML
        QVERIFY(!html.contains(u"<b>"_s));
        QVERIFY(html.contains(u"page:"_s));
        QVERIFY(html.contains(u"tag:"_s));
        QCOMPARE(props[1].toMap().value(u"type"_s).toString(), u"number"_s);
        QCOMPARE(props[1].toMap().value(u"raw"_s).toLongLong(), 3);
        QCOMPARE(props[2].toMap().value(u"items"_s).toStringList(), QStringList({u"[[Ogród]]"_s, u"x"_s}));
        QCOMPARE(props[2].toMap().value(u"itemsHtml"_s).toStringList().size(), 2);
        QCOMPARE(props[3].toMap().value(u"type"_s).toString(), u"checkbox"_s);
        QCOMPARE(props[3].toMap().value(u"raw"_s).toBool(), false);
        const QVariantMap info = f.role(0, "propertiesInfo").toMap();
        QCOMPARE(info.value(u"source"_s).toString(), u"yaml"_s);
        QVERIFY(info.value(u"editable"_s).toBool());
        QVERIFY(info.value(u"canAdd"_s).toBool());
        // The cache follows the text.
        QVERIFY(f.page.setPageProperty(QString(), u"gotowe"_s, true, u"checkbox"_s).value(u"ok"_s).toBool());
        QCOMPARE(f.role(0, "properties").toList()[3].toMap().value(u"raw"_s).toBool(), true);
        QVERIFY(f.role(1, "properties").toList().isEmpty());
        QVERIFY(f.role(1, "propertiesInfo").toMap().isEmpty());
    }

    void largePage()
    {
        QString content = u"---\nstatus: a\n---\n"_s;
        for (int i = 0; i < 5000; ++i)
            content += u"- blok "_s + QString::number(i) + u" zażółć\n"_s;
        Fixture f(content);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(f.page.setPageProperty(QString(), u"status"_s, u"b"_s, u"text"_s).value(u"ok"_s).toBool());
        const qint64 ms = timer.elapsed();
        qDebug("a property edit on a 5000-block page: %lld ms", ms);
        QVERIFY2(ms < 5000, "an edit costs one applyText, not more");
        QCOMPARE(f.model()->rowCount(), 5001);
    }
};

QTEST_MAIN(TestPageProps)
#include "tst_editor_pageprops.moc"
