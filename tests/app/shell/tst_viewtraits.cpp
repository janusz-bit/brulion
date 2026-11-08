// ViewTraits (src/app/shell/viewtraits.h): what ShellRouteView reads of the view it shows,
// through the view's meta-object — typed results, the change signals followed, the optional
// functions called with the arguments their declarations ask for — and folderChoices()
// (folderchoices.h), the rows of "Move to…".
#include "folderchoices.h"
#include "viewtraits.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using brulion::shell::ViewTraits;

namespace {

/// A QML object from `qml` (QtQml only), owned by the caller.
std::unique_ptr<QObject> makeObject(QQmlEngine &engine, const QString &qml)
{
    QQmlComponent component(&engine);
    component.setData(qml.toUtf8(), QUrl(u"qrc:/view.qml"_s));
    std::unique_ptr<QObject> object(component.create());
    if (!object)
        qWarning().noquote() << component.errorString();
    return object;
}

QStringList folders(const QVariantList &rows)
{
    QStringList out;
    for (const QVariant &row : rows) {
        const QVariantMap r = row.toMap();
        // Every row carries every key a delegate requires.
        for (const char *key : {"folder", "title", "subtitle", "icon", "create", "disabled"}) {
            if (!r.contains(QString::fromLatin1(key)))
                out.append(u"missing "_s + QString::fromLatin1(key));
        }
        out.append(r.value(u"create"_s).toBool() ? u"+"_s + r.value(u"folder"_s).toString()
                                                 : r.value(u"folder"_s).toString());
    }
    return out;
}

} // namespace

class TestViewTraits : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // A view with every optional member: typed, following its changes.
    void fullView()
    {
        QQmlEngine engine;
        auto view = makeObject(engine, uR"(
import QtQml
QtObject {
    property string title: "Zażółć gęślą jaźń"
    property Component sidePanel: Component { QtObject {} }
    property bool editing: false
    property bool toolbarDocked: true
    property var replacesBottomNav: true
    property string noteMode: "outline"
    property bool pictureRoute: false
    property bool pictureAsText: true
    property bool pdfRoute: false
    property bool fileRoute: false
    property bool sourceMode: false
    property bool edgeSwipe: true
    property bool canStepBack: true
    property string statusText: "123 słowa"
    property string statusPosition: "Blok 3 z 12"
    property var restored: null
    property int ended: 0
    property int stepped: 0
    function viewState(): var { return { contentY: 120, row: 3 } }
    function restoreViewState(state: var): void { restored = state }
    function endEditing(): void { ++ended }
    function stepBack(): void { ++stepped; canStepBack = false }
}
)"_s);
        QVERIFY(view);
        ViewTraits traits;
        QSignalSpy changed(&traits, &ViewTraits::traitsChanged);
        traits.setView(view.get());
        QCOMPARE(traits.title(), u"Zażółć gęślą jaźń"_s);
        QVERIFY(traits.sidePanel() != nullptr);
        QCOMPARE(traits.noteMode(), u"outline"_s);
        QVERIFY(!traits.editing());
        QVERIFY(traits.toolbarDocked());
        QVERIFY(traits.replacesBottomNav()); // a `var` holding true counts as `=== true`
        QVERIFY(traits.pictureAsText());
        QVERIFY(traits.edgeSwipe());
        QCOMPARE(traits.statusText(), u"123 słowa"_s);
        QCOMPARE(traits.statusPosition(), u"Blok 3 z 12"_s);
        QCOMPARE(changed.size(), 1);

        view->setProperty("editing", true);
        QVERIFY(traits.editing());
        QCOMPARE(changed.size(), 2);
        view->setProperty("title", u"Łódź"_s);
        QCOMPARE(traits.title(), u"Łódź"_s);
        view->setProperty("noteMode", u"document"_s);
        QCOMPARE(traits.noteMode(), u"document"_s);
        view->setProperty("statusText", u"Zaznaczenie: 2 słowa"_s);
        QCOMPARE(traits.statusText(), u"Zaznaczenie: 2 słowa"_s);
        // The same value again: nothing changed.
        const qsizetype before = changed.size();
        view->setProperty("editing", true);
        QCOMPARE(changed.size(), before);

        const QVariantMap state = traits.viewState();
        QCOMPARE(state.value(u"contentY"_s).toInt(), 120);
        QCOMPARE(state.value(u"row"_s).toInt(), 3);
        traits.restoreViewState({{u"contentY"_s, 40}});
        QCOMPARE(view->property("restored").toMap().value(u"contentY"_s).toInt(), 40);
        // Handed over once: nothing pending for the next view.
        view->setProperty("restored", QVariant());
        traits.applyPendingState();
        QVERIFY(!view->property("restored").isValid() || view->property("restored").isNull());
        // {}: no state, nothing handed over.
        traits.restoreViewState({});
        QVERIFY(!view->property("restored").isValid() || view->property("restored").isNull());
        traits.endEditing();
        QCOMPARE(view->property("ended").toInt(), 1);
        // Back's step in the view (a zoomed block): taken, and the trait follows the view.
        QVERIFY(traits.canStepBack());
        traits.stepBack();
        QCOMPARE(view->property("stepped").toInt(), 1);
        QVERIFY(!traits.canStepBack());
    }

    // Untyped functions and members of other types (the views as they were): `var` parameters,
    // a title that is no string reads "".
    void untypedView()
    {
        QQmlEngine engine;
        auto view = makeObject(engine, uR"(
import QtQml
QtObject {
    property int title: 7
    property string editing: "true"
    property var noteMode: 3
    property var statusText: 5
    property var restored: null
    function viewState() { return { page: "Gęś.md" } }
    function restoreViewState(state) { restored = state }
}
)"_s);
        QVERIFY(view);
        ViewTraits traits;
        traits.setView(view.get());
        QCOMPARE(traits.title(), QString());
        QVERIFY(!traits.editing()); // the text "true" is no boolean true
        QCOMPARE(traits.noteMode(), QString());
        QCOMPARE(traits.statusText(), QString());
        QVERIFY(traits.sidePanel() == nullptr);
        QCOMPARE(traits.viewState().value(u"page"_s).toString(), u"Gęś.md"_s);
        traits.restoreViewState({{u"row"_s, 2}});
        QCOMPARE(view->property("restored").toMap().value(u"row"_s).toInt(), 2);
        traits.endEditing(); // none: nothing
    }

    // No view, a view without any of it, a view loaded after the state was given, a view gone.
    void pendingAndGone()
    {
        QQmlEngine engine;
        ViewTraits traits;
        QCOMPARE(traits.viewState(), QVariantMap());
        traits.restoreViewState({{u"row"_s, 5}}); // kept for the view to come
        traits.endEditing();
        traits.stepBack();
        QVERIFY(!traits.canStepBack());

        auto bare = makeObject(engine, u"import QtQml\nQtObject {}"_s);
        QVERIFY(bare);
        traits.setView(bare.get());
        QCOMPARE(traits.title(), QString());
        QVERIFY(!traits.toolbarDocked());
        QVERIFY(!traits.canStepBack()); // no step of its own: Back walks the history
        traits.stepBack();

        auto view = makeObject(engine, uR"(
import QtQml
QtObject {
    property string title: "Dziennik"
    property var restored: null
    function restoreViewState(state: var): void { restored = state }
}
)"_s);
        QVERIFY(view);
        traits.restoreViewState({{u"row"_s, 6}}); // the bare view takes none: dropped
        traits.setView(view.get());
        traits.applyPendingState();
        QVERIFY(view->property("restored").isNull() || !view->property("restored").isValid());
        traits.restoreViewState({{u"row"_s, 8}});
        QCOMPARE(view->property("restored").toMap().value(u"row"_s).toInt(), 8);

        QSignalSpy viewChanged(&traits, &ViewTraits::viewChanged);
        view.reset();
        QCOMPARE(viewChanged.size(), 1);
        QVERIFY(traits.view() == nullptr);
        QCOMPARE(traits.title(), QString());
    }

    void folderChoices_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("own");
        QTest::addColumn<QString>("current");
        QTest::addColumn<bool>("moving");
        QTest::addColumn<QStringList>("rows");
        QTest::addColumn<QStringList>("disabled");

        QTest::newRow("all") << QString() << QString() << u"Projekty"_s << true
                             << QStringList{QString(), u"Archiwum"_s, u"Projekty"_s, u"Projekty/Łódź"_s,
                                            u"Projekty/Zażółć"_s}
                             << QStringList{u"Projekty"_s};
        QTest::newRow("pick: nothing disabled")
                << QString() << QString() << u"Projekty"_s << false
                << QStringList{QString(), u"Archiwum"_s, u"Projekty"_s, u"Projekty/Łódź"_s, u"Projekty/Zażółć"_s}
                << QStringList{};
        QTest::newRow("a folder moved: not into itself")
                << QString() << u"Projekty"_s << QString() << true << QStringList{QString(), u"Archiwum"_s}
                << QStringList{QString()};
        // Without diacritics; the root's name does not match.
        QTest::newRow("lodz") << u"lodz"_s << QString() << QString() << true
                              << QStringList{u"Projekty/Łódź"_s, u"+lodz"_s} << QStringList{};
        // The root found by its name; a typed name of a folder that exists: no new row.
        QTest::newRow("root") << u"notebook"_s << QString() << QString() << true
                              << QStringList{QString(), u"+notebook"_s} << QStringList{QString()};
        QTest::newRow("existing, other case")
                << u" ARCHIWUM "_s << QString() << QString() << true << QStringList{u"Archiwum"_s} << QStringList{};
        // Slashes at the ends of a new folder dropped.
        QTest::newRow("new nested") << u"/Nowy/Dział/"_s << QString() << QString() << false
                                    << QStringList{u"+Nowy/Dział"_s} << QStringList{};
    }
    void folderChoices()
    {
        QFETCH(QString, query);
        QFETCH(QString, own);
        QFETCH(QString, current);
        QFETCH(bool, moving);
        QFETCH(QStringList, rows);
        QFETCH(QStringList, disabled);
        const QStringList all{u"Archiwum"_s, u"Projekty"_s, u"Projekty/Łódź"_s, u"Projekty/Zażółć"_s};
        const QVariantList out = brulion::shell::folderChoices(
                all, query, own, current, moving, {u"Notebook root"_s, u"New folder “%1”"_s, u"Create it"_s});
        QCOMPARE(folders(out), rows);
        QStringList off;
        for (const QVariant &row : out) {
            if (row.toMap().value(u"disabled"_s).toBool())
                off.append(row.toMap().value(u"folder"_s).toString());
        }
        QCOMPARE(off, disabled);
        for (const QVariant &row : out) {
            const QVariantMap r = row.toMap();
            if (r.value(u"create"_s).toBool()) {
                QCOMPARE(r.value(u"title"_s).toString(), u"New folder “%1”"_s.arg(r.value(u"folder"_s).toString()));
                QCOMPARE(r.value(u"icon"_s).toString(), u"folder-plus"_s);
            } else if (r.value(u"folder"_s).toString() == u"Projekty/Łódź") {
                QCOMPARE(r.value(u"title"_s).toString(), u"Łódź"_s);
                QCOMPARE(r.value(u"subtitle"_s).toString(), u"Projekty"_s);
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestViewTraits)
#include "tst_viewtraits.moc"
