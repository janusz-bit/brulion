// Tests for UiModels and PaletteFilter (src/app/ui/uimodels.h): the rows of the components'
// JavaScript models and the fields read from JavaScript objects, with the JavaScript engine's
// semantics (=== and String() compared with what QJSEngine gives) — Polish text included.
#include "uimodels.h"

#include <QJSEngine>
#include <QJSValue>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::ui::PaletteFilter;
using brulion::ui::UiModels;

namespace {

QVariantMap row(std::initializer_list<std::pair<QString, QVariant>> fields)
{
    QVariantMap m;
    for (const auto &[k, v] : fields)
        m.insert(k, v);
    return m;
}

QStringList field(const QVariantList &rows, const QString &key)
{
    QStringList out;
    for (const QVariant &r : rows)
        out.append(r.toMap().value(key).toString());
    return out;
}

} // namespace

class TestUiModels : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void strictEquals_data()
    {
        QTest::addColumn<QVariant>("a");
        QTest::addColumn<QVariant>("b");
        QTest::addColumn<bool>("equal");
        QTest::newRow("undefined") << QVariant() << QVariant() << true;
        QTest::newRow("null, undefined") << QVariant::fromValue(nullptr) << QVariant() << false;
        QTest::newRow("null") << QVariant::fromValue(nullptr) << QVariant::fromValue(nullptr) << true;
        QTest::newRow("int, double") << QVariant(1) << QVariant(1.0) << true;
        QTest::newRow("numbers differ") << QVariant(1) << QVariant(1.5) << false;
        QTest::newRow("string, number") << QVariant(u"1"_s) << QVariant(1) << false;
        QTest::newRow("bool, number") << QVariant(true) << QVariant(1) << false;
        QTest::newRow("strings") << QVariant(u"źdźbło"_s) << QVariant(u"źdźbło"_s) << true;
        QTest::newRow("case") << QVariant(u"Łódź"_s) << QVariant(u"łódź"_s) << false;
        QTest::newRow("bools") << QVariant(false) << QVariant(false) << true;
    }

    void strictEquals()
    {
        QFETCH(QVariant, a);
        QFETCH(QVariant, b);
        QFETCH(bool, equal);
        QCOMPARE(brulion::ui::jsStrictEquals(a, b), equal);
        QCOMPARE(brulion::ui::jsStrictEquals(b, a), equal);
        QCOMPARE(UiModels().strictEquals(a, b), equal);
    }

    void jsStringMatchesTheEngine_data()
    {
        QTest::addColumn<QString>("expression");
        QTest::newRow("int") << u"5"_s;
        QTest::newRow("fraction") << u"1.5"_s;
        QTest::newRow("negative") << u"-0.25"_s;
        QTest::newRow("true") << u"true"_s;
        QTest::newRow("text") << u"'zażółć'"_s;
        QTest::newRow("null") << u"null"_s;
        QTest::newRow("undefined") << u"undefined"_s;
    }

    void jsStringMatchesTheEngine()
    {
        QFETCH(QString, expression);
        QJSEngine engine;
        const QJSValue value = engine.evaluate(expression);
        QCOMPARE(brulion::ui::jsString(value.toVariant()), engine.evaluate(u"String("_s + expression + u")"_s).toString());
    }

    void fieldsOfMapsAndJsObjects()
    {
        QJSEngine engine;
        const QJSValue object = engine.evaluate(u"({ title: 'Żółw', count: 3, done: true, empty: '', none: null,"
                                                 u" tags: ['dom', 'ogród'] })"_s);
        UiModels m;
        for (const QVariant &v : {QVariant::fromValue(object), object.toVariant()}) {
            QCOMPARE(m.stringField(v, u"title"_s), u"Żółw"_s);
            QCOMPARE(m.stringField(v, u"count"_s), u"3"_s);
            QCOMPARE(m.stringField(v, u"none"_s), QString());
            QCOMPARE(m.stringField(v, u"missing"_s), QString());
            QCOMPARE(m.boolField(v, u"done"_s), true);
            QCOMPARE(m.boolField(v, u"empty"_s), false);
            QCOMPARE(m.boolField(v, u"count"_s), true);
            QCOMPARE(m.boolField(v, u"missing"_s), false);
            QCOMPARE(m.numberField(v, u"count"_s, -1), 3.0);
            QCOMPARE(m.numberField(v, u"title"_s, -1), -1.0);
            QCOMPARE(m.stringListField(v, u"tags"_s), (QStringList{u"dom"_s, u"ogród"_s}));
            QCOMPARE(m.stringListField(v, u"title"_s), QStringList());
            QVERIFY(!m.field(v, u"missing"_s).isValid());
        }
        // Not an object: nothing.
        QCOMPARE(m.stringField(QVariant(u"tekst"_s), u"length"_s), QString());
        QCOMPARE(m.stringField(QVariant(), u"title"_s), QString());
        QCOMPARE(m.fieldAt({row({{u"a"_s, 1}})}, 0, u"a"_s), QVariant(1));
        QVERIFY(!m.fieldAt({row({{u"a"_s, 1}})}, 1, u"a"_s).isValid());
        QVERIFY(!m.fieldAt({}, -1, u"a"_s).isValid());
    }

    void segmentRows()
    {
        UiModels m;
        const QVariantList rows = m.segmentRows(
                {u"dzień"_s, row({{u"value"_s, u"week"_s}, {u"text"_s, u"Tydzień"_s}, {u"icon"_s, u"calendar"_s}}), 5,
                 row({{u"value"_s, 2}})});
        QCOMPARE(rows.size(), 4);
        QCOMPARE(field(rows, u"text"_s), (QStringList{u"dzień"_s, u"Tydzień"_s, u"5"_s, QString()}));
        QCOMPARE(field(rows, u"iconName"_s), (QStringList{QString(), u"calendar"_s, QString(), QString()}));
        QCOMPARE(rows.at(0).toMap().value(u"value"_s), QVariant(u"dzień"_s));
        QCOMPARE(rows.at(2).toMap().value(u"value"_s), QVariant(5));
        // Every row has every key a delegate requires.
        for (const QVariant &r : rows)
            QCOMPARE(r.toMap().keys(), (QStringList{u"iconName"_s, u"text"_s, u"value"_s}));

        QCOMPARE(m.indexOfValue(rows, u"week"_s), 1);
        QCOMPARE(m.indexOfValue(rows, 5.0), 2);
        QCOMPARE(m.indexOfValue(rows, u"5"_s), -1);
        QCOMPARE(m.indexOfValue(rows, 2), 3);
        QCOMPARE(m.indexOfValue(rows, QVariant()), -1);
        QCOMPARE(m.indexOfValue({}, u"week"_s), -1);
    }

    void crumbs()
    {
        UiModels m;
        const QVariantList rows = m.crumbRows({u"Dziennik"_s, row({{u"title"_s, u"2026"_s}, {u"icon"_s, u"folder"_s}}),
                                               u"Październik"_s, u"Środa"_s, u"Notatka"_s});
        QCOMPARE(field(rows, u"title"_s),
                 (QStringList{u"Dziennik"_s, u"2026"_s, u"Październik"_s, u"Środa"_s, u"Notatka"_s}));
        QCOMPARE(m.stringFields(rows, u"icon"_s), (QStringList{{}, u"folder"_s, {}, {}, {}}));

        const QVariantList all = m.shownCrumbs(rows, 5, false);
        QCOMPARE(field(all, u"partIndex"_s), (QStringList{u"0"_s, u"1"_s, u"2"_s, u"3"_s, u"4"_s}));
        const QVariantList collapsed = m.shownCrumbs(rows, 2, true);
        QCOMPARE(field(collapsed, u"title"_s), (QStringList{u"Dziennik"_s, u"…"_s, u"Środa"_s, u"Notatka"_s}));
        QCOMPARE(field(collapsed, u"partIndex"_s), (QStringList{u"0"_s, u"-1"_s, u"3"_s, u"4"_s}));
        QCOMPARE(collapsed.at(1).toMap().value(u"icon"_s).toString(), QString());
        QVERIFY(m.shownCrumbs({}, 0, true).isEmpty());
    }

    void enabledRowsAndRowsByKey()
    {
        UiModels m;
        const QVariantList rows{row({{u"id"_s, u"a"_s}}), row({{u"id"_s, u"b"_s}, {u"enabled"_s, false}}),
                                row({{u"id"_s, u"c"_s}, {u"enabled"_s, true}}), row({{u"id"_s, u"d"_s}, {u"enabled"_s, 0}})};
        // Only `enabled: false` hides a row (JavaScript's c.enabled !== false).
        QCOMPARE(field(m.enabledRows(rows), u"id"_s), (QStringList{u"a"_s, u"c"_s, u"d"_s}));
        QCOMPARE(field(m.rowsByKey(rows, u"id"_s, {u"d"_s, u"x"_s, u"a"_s}), u"id"_s), (QStringList{u"d"_s, u"a"_s}));
        QVERIFY(m.rowsByKey(rows, u"id"_s, {}).isEmpty());
    }

    void withDefaults()
    {
        UiModels m;
        const QVariantList rows = m.withDefaults(
                {row({{u"kind"_s, u"embed"_s}, {u"title"_s, u"Ogród"_s}}), row({{u"kind"_s, u"block"_s}, {u"title"_s, QVariant::fromValue(nullptr)}, {u"depth"_s, 2}})},
                row({{u"kind"_s, QString()}, {u"title"_s, QString()}, {u"depth"_s, 0}}));
        QCOMPARE(field(rows, u"kind"_s), (QStringList{u"embed"_s, u"block"_s}));
        QCOMPARE(field(rows, u"title"_s), (QStringList{u"Ogród"_s, QString()}));
        QCOMPARE(field(rows, u"depth"_s), (QStringList{u"0"_s, u"2"_s}));
        QVERIFY(m.withDefaults({}, row({{u"a"_s, 1}})).isEmpty());
    }

    void paletteFilterRanksAndMarks()
    {
        PaletteFilter f;
        f.setMarkColor(u"#2c6aa3"_s);
        f.setDefaultIcon(u"file-text"_s);
        f.setItems({row({{u"title"_s, u"Dom"_s}, {u"path"_s, u"Dom.md"_s}}),
                    row({{u"title"_s, u"Żółw"_s}, {u"subtitle"_s, u"Zwierzęta"_s}, {u"icon"_s, u"turtle"_s}, {u"path"_s, u"Zolw.md"_s}}),
                    row({{u"title"_s, u"Zebra"_s}, {u"emoji"_s, u"🦓"_s}, {u"shortcut"_s, u"Ctrl+Z"_s}})});
        // Empty query: all, in order, nothing marked.
        QCOMPARE(f.count(), 3);
        QCOMPARE(field(f.rows(), u"text"_s), (QStringList{u"Dom"_s, u"Żółw"_s, u"Zebra"_s}));
        QCOMPARE(field(f.rows(), u"titleMarkup"_s), (QStringList{{}, {}, {}}));
        QCOMPARE(field(f.rows(), u"iconName"_s), (QStringList{u"file-text"_s, u"turtle"_s, u"file-text"_s}));
        QCOMPARE(field(f.rows(), u"iconEmoji"_s), (QStringList{{}, {}, u"🦓"_s}));
        QCOMPARE(field(f.rows(), u"shortcut"_s), (QStringList{{}, {}, u"Ctrl+Z"_s}));
        for (const QVariant &r : f.rows())
            QCOMPARE(r.toMap().keys(), (QStringList{u"iconEmoji"_s, u"iconName"_s, u"shortcut"_s, u"subtitle"_s,
                                                    u"text"_s, u"titleMarkup"_s}));

        // Diacritics folded; the matched characters marked.
        f.setQuery(u"zolw"_s);
        QCOMPARE(f.count(), 1);
        QCOMPARE(f.rows().at(0).toMap().value(u"text"_s).toString(), u"Żółw"_s);
        QCOMPARE(f.rows().at(0).toMap().value(u"titleMarkup"_s).toString(),
                 u"<b><font color=\"#2c6aa3\">Żółw</font></b>"_s);
        QCOMPARE(f.sourceIndex(0), 1);
        QCOMPARE(f.itemString(0, u"path"_s), u"Zolw.md"_s);
        QCOMPARE(f.itemString(0, u"missing"_s), QString());
        QCOMPARE(f.sourceIndex(1), INT_MIN);
        QCOMPARE(f.itemString(1, u"path"_s), QString());

        // A subtitle matches at half weight.
        f.setQuery(u"zwierz"_s);
        QCOMPARE(field(f.rows(), u"text"_s), QStringList{u"Żółw"_s});
        QCOMPARE(f.rows().at(0).toMap().value(u"titleMarkup"_s).toString(), QString());

        // Extras after the matches, whatever the query.
        f.setExtras({row({{u"title"_s, u"Utwórz stronę „xyz”"_s}, {u"kind"_s, u"create"_s}, {u"name"_s, u"xyz"_s}})});
        f.setQuery(u"xyz"_s);
        QCOMPARE(f.count(), 1);
        QVERIFY(f.isExtra(0));
        QCOMPARE(f.sourceIndex(0), -1);
        QCOMPARE(f.itemString(0, u"kind"_s), u"create"_s);
        QCOMPARE(f.itemBool(0, u"name"_s), true);
        QCOMPARE(f.rows().at(0).toMap().value(u"iconName"_s).toString(), u"file-text"_s);

        // The limit counts the matches only.
        f.setQuery(QString());
        f.setMaxResults(2);
        QCOMPARE(f.count(), 3);
        QVERIFY(!f.isExtra(1));
        QVERIFY(f.isExtra(2));

        QVERIFY(f.hasTitle(u"ŻÓŁW"_s));
        QVERIFY(f.hasTitle(u"zolw"_s));
        QVERIFY(!f.hasTitle(u"Żó"_s));
    }

    void paletteFilterTakesJsObjects()
    {
        QJSEngine engine;
        const QVariant items = engine.evaluate(u"[{ title: 'Łąka', action: function () {} }, { title: 'Las' }]"_s).toVariant();
        PaletteFilter f;
        f.setItems(items.toList());
        f.setQuery(u"laka"_s);
        QCOMPARE(field(f.rows(), u"text"_s), QStringList{u"Łąka"_s});
        QCOMPARE(f.sourceIndex(0), 0);
    }

    void paletteFilterSignalsEveryChange()
    {
        PaletteFilter f;
        int changes = 0;
        connect(&f, &PaletteFilter::rowsChanged, this, [&changes] { ++changes; });
        f.setItems({row({{u"title"_s, u"A"_s}})});
        f.setQuery(u"a"_s);
        f.setQuery(u"a"_s); // unchanged: no new rows
        f.setExtras({});
        QCOMPARE(changes, 3);
    }
};

QTEST_GUILESS_MAIN(TestUiModels)
#include "tst_uimodels.moc"
