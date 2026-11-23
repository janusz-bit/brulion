// b.app (docs/EXTENSIONS.md "The app (`b.app`)"): the theme's tokens and their checks, the items an
// extension puts into the UI (slots, fields, limits, replacing, removing, dropped with the
// extension), the app's operations as the host sees them (arguments, the permission "app", refused
// while loading and in macros), the "route" hook, a dry run's notes, hostile input.
#include "extensions/appapi.h"
#include "extensions/engine.h"
#include "extensions/host.h"
#include "extensions/notebookhost.h"
#include "extensions/sandbox.h"
#include "index/vaultindex.h"
#include "vault/vault.h"

#include "testtiming.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace brulion;
using namespace brulion::extensions;
using namespace Qt::StringLiterals;

namespace {

const Limits kRoomy{brulion::test::kRoomyMs, 32ll << 20};

/// A host whose app answers and remembers: every operation asked, the changes noted.
class AppHost : public Host {
public:
    QList<QPair<QString, QVariantMap>> asked; // (operation, args)
    QStringList askedBy;
    QVariantMap settings{{u"density"_s, u"comfortable"_s}, {u"vim"_s, false}};
    QStringList changed; // appChanged(extension)
    QStringList logs;

    QStringList pages(const QString &) override { return {}; }
    std::optional<QString> read(const QString &) override { return std::nullopt; }
    QVariantMap page(const QString &) override { return {}; }
    QVariantList blocks(const BlockQuery &) override { return {}; }
    QVariantList search(const QString &, int) override { return {}; }
    QVariantMap query(const QString &, int, QString *) override
    {
        return {{u"total"_s, 0}, {u"items"_s, QVariantList{}}};
    }
    QString write(const QString &, const QString &, const QString &) override { return {}; }
    QString appendToJournal(const QString &, const QString &, const QString &) override { return {}; }
    QString replaceBlock(const QString &, const QString &, const QString &, const std::optional<QString> &,
                         QString *) override
    {
        return {};
    }
    QString today() override { return u"2026-11-21"_s; }
    QString language() override { return u"pl"_s; }
    QString uiLanguage() override { return u"en"_s; }
    void timer(const QString &, int, const QString &) override { }
    void log(const QString &, const QString &message) override { logs << message; }

    QVariant app(const QString &extension, const QString &operation, const QVariantMap &args, QString *error) override
    {
        asked.append({operation, args});
        askedBy << extension;
        if (operation == u"state")
            return QVariantMap{{u"view"_s, u"journals"_s}, {u"dark"_s, true}, {u"platform"_s, u"linux"_s}};
        if (operation == u"commands")
            return QVariantList{QVariantMap{{u"id"_s, u"go.calendar"_s}, {u"title"_s, u"Calendar"_s}}};
        if (operation == u"settings")
            return QVariantList{QVariantMap{{u"key"_s, u"density"_s}, {u"kind"_s, u"choice"_s}}};
        if (operation == u"get") {
            const QString key = args.value(u"key"_s).toString();
            if (!settings.contains(key)) {
                *error = u"no setting “%1”"_s.arg(key);
                return {};
            }
            return settings.value(key);
        }
        if (operation == u"set")
            settings.insert(args.value(u"key"_s).toString(), args.value(u"value"_s));
        if (operation == u"run" && args.value(u"id"_s).toString() == u"nope") {
            *error = u"no command “nope”"_s;
            return {};
        }
        return {};
    }
    void appChanged(const QString &extension) override { changed << extension; }
};

Extension ext(const QString &id, const QString &source, QStringList permissions = {u"app"_s})
{
    Extension e;
    e.id = id;
    e.source = source;
    e.permissions = std::move(permissions);
    return e;
}

} // namespace

class TestExtensionsApp : public QObject {
    Q_OBJECT

    AppHost m_host;
    Engine m_engine;

    void restart()
    {
        m_host = AppHost();
        QVERIFY(m_engine.start(&m_host, kRoomy));
    }
    Result eval(const QString &code, const QString &extension = u"t"_s)
    {
        return m_engine.evaluate(extension, code, u"t"_s);
    }

private Q_SLOTS:
    void init() { restart(); }

    // ---- the theme --------------------------------------------------------------------------------

    void tokensAreKnown()
    {
        QVERIFY(themeToken(u"bg"));
        QVERIFY(themeToken(u"accent"));
        QVERIFY(themeToken(u"chipReminderFg"));
        QCOMPARE(themeToken(u"radius")->kind, ThemeTokenKind::Number);
        QCOMPARE(themeToken(u"fontMono")->kind, ThemeTokenKind::Font);
        QVERIFY(!themeToken(u"Bg"));
        QVERIFY(!themeToken(u""));
        QSet<QString> names;
        for (const ThemeToken &t : themeTokens())
            QVERIFY2(!names.contains(t.name) && (names.insert(t.name), true), qPrintable(t.name));
        QVERIFY(themeTokens().size() > 80);
    }

    void colorsAreNormalized_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("normal");
        QTest::newRow("short") << u"#ABC"_s << u"#aabbcc"_s;
        QTest::newRow("long") << u"#0B0D10"_s << u"#0b0d10"_s;
        QTest::newRow("alpha") << u"#80ff7a66"_s << u"#80ff7a66"_s;
        QTest::newRow("spaces") << u"  #fff "_s << u"#ffffff"_s;
        QTest::newRow("no hash") << u"fff"_s << QString();
        QTest::newRow("name") << u"red"_s << QString();
        QTest::newRow("five") << u"#12345"_s << QString();
        QTest::newRow("not hex") << u"#ggg"_s << QString();
        QTest::newRow("rgb()") << u"rgb(1,2,3)"_s << QString();
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("polish") << u"#żółw"_s << QString();
    }
    void colorsAreNormalized()
    {
        QFETCH(QString, typed);
        QFETCH(QString, normal);
        QCOMPARE(normalizedThemeColor(typed), normal);
    }

    void layersAreChecked_data()
    {
        QTest::addColumn<QVariantMap>("table");
        QTest::addColumn<QString>("error"); // "" = accepted
        QTest::newRow("empty") << QVariantMap() << QString();
        QTest::newRow("colour") << QVariantMap{{u"accent"_s, u"#3a7"_s}} << QString();
        QTest::newRow("modes") << QVariantMap{{u"dark"_s, QVariantMap{{u"bg"_s, u"#000"_s}}},
                                              {u"light"_s, QVariantMap{{u"bg"_s, u"#fff"_s}}}}
                               << QString();
        QTest::newRow("radius") << QVariantMap{{u"radius"_s, 12}} << QString();
        QTest::newRow("font") << QVariantMap{{u"fontMono"_s, u"Iosevka"_s}} << QString();
        QTest::newRow("misspelt") << QVariantMap{{u"bgg"_s, u"#000"_s}} << u"did you mean “bg”"_s;
        QTest::newRow("unknown far") << QVariantMap{{u"wallpaper"_s, u"#000"_s}} << u"unknown token “wallpaper”"_s;
        QTest::newRow("not a colour") << QVariantMap{{u"fg"_s, u"red"_s}} << u"is a colour"_s;
        QTest::newRow("number as colour") << QVariantMap{{u"fg"_s, 3}} << u"is a colour"_s;
        QTest::newRow("radius too big") << QVariantMap{{u"radius"_s, 1000}} << u"from 0 to 32"_s;
        QTest::newRow("radius negative") << QVariantMap{{u"radius"_s, -1}} << u"from 0 to 32"_s;
        QTest::newRow("radius as text") << QVariantMap{{u"radius"_s, u"12"_s}} << u"is a number"_s;
        QTest::newRow("font of two lines") << QVariantMap{{u"fontUi"_s, u"A\nB"_s}} << u"font family"_s;
        QTest::newRow("mode not a table") << QVariantMap{{u"dark"_s, u"#000"_s}} << u"table of tokens"_s;
        QTest::newRow("mode in a mode") << QVariantMap{{u"dark"_s, QVariantMap{{u"light"_s, QVariantMap()}}}}
                                        << u"inside a mode"_s;
    }
    void layersAreChecked()
    {
        QFETCH(QVariantMap, table);
        QFETCH(QString, error);
        ThemeLayer layer;
        layer.both.insert(u"untouched"_s, 1);
        QString why;
        const bool ok = themeLayerFrom(table, &layer, &why);
        QCOMPARE(ok, error.isEmpty());
        if (!ok) {
            QVERIFY2(why.contains(error), qPrintable(why));
            QCOMPARE(layer.both.value(u"untouched"_s).toInt(), 1); // nothing kept
        }
    }

    void tooManyTokens()
    {
        QVariantMap table;
        // 82+ colours, both modes: more than MaxThemeTokens in all.
        for (const ThemeToken &t : themeTokens())
            if (t.kind == ThemeTokenKind::Color)
                table.insert(t.name, u"#123456"_s);
        QVariantMap dark = table, light = table;
        table.insert(u"dark"_s, dark);
        table.insert(u"light"_s, light);
        ThemeLayer layer;
        QString error;
        QVERIFY(!themeLayerFrom(table, &layer, &error));
        QVERIFY2(error.contains(u"at most"_s), qPrintable(error));
    }

    void modesMergeInOrder()
    {
        ThemeLayer a, b;
        QString error;
        QVERIFY(themeLayerFrom({{u"accent"_s, u"#111111"_s},
                                {u"bg"_s, u"#222222"_s},
                                {u"dark"_s, QVariantMap{{u"bg"_s, u"#000000"_s}}}},
                               &a, &error));
        QVERIFY(themeLayerFrom({{u"accent"_s, u"#333333"_s}}, &b, &error));
        QCOMPARE(a.forMode(true).value(u"bg"_s).toString(), u"#000000"_s);
        QCOMPARE(a.forMode(false).value(u"bg"_s).toString(), u"#222222"_s);
        const QVariantMap merged = mergedTheme({a, b}, false);
        QCOMPARE(merged.value(u"accent"_s).toString(), u"#333333"_s); // the later one wins
        QCOMPARE(merged.value(u"bg"_s).toString(), u"#222222"_s);
        const QVariantMap table = themeLayerTable(a);
        QCOMPARE(table.value(u"dark"_s).toMap().value(u"bg"_s).toString(), u"#000000"_s);
        QVERIFY(!table.contains(u"light"_s));
    }

    void themeFromLuau()
    {
        QVERIFY(m_engine.load(ext(u"motyw"_s, uR"lua(
            b.app.theme({ accent = string.char(35) .. "3A7", radius = 10, dark = { bg = string.char(35) .. "0b0d10" } })
        )lua"_s))
                        .ok());
        QCOMPARE(m_engine.themes().size(), 1);
        const ThemeInfo t = m_engine.themes().first();
        QCOMPARE(t.extension, u"motyw"_s);
        QCOMPARE(t.layer.both.value(u"accent"_s).toString(), u"#33aa77"_s);
        QCOMPARE(t.layer.both.value(u"radius"_s).toDouble(), 10.0);
        QCOMPARE(t.layer.dark.value(u"bg"_s).toString(), u"#0b0d10"_s);
        QVERIFY(m_host.changed.contains(u"motyw"_s));
        const int revision = m_engine.appRevision();
        // The same layer again changes nothing; nil takes it away.
        QVERIFY(eval(u"b.app.theme({ accent = \"#33aa77\", radius = 10, dark = { bg = \"#0b0d10\" } })"_s, u"motyw"_s)
                        .ok());
        QCOMPARE(m_engine.appRevision(), revision);
        QVERIFY(eval(u"b.app.theme(nil)"_s, u"motyw"_s).ok());
        QVERIFY(m_engine.themes().isEmpty());
        QVERIFY(m_engine.appRevision() > revision);
    }

    void themeErrorsNameTheToken()
    {
        const Result r = m_engine.load(ext(u"zly"_s, u"b.app.theme({ bgg = \"#000\" })"_s));
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains(u"b.app.theme: unknown token “bgg” (did you mean “bg”?)"_s), qPrintable(r.error));
        QVERIFY2(r.error.contains(u"zly:1:"_s), qPrintable(r.error));
        QVERIFY(m_engine.themes().isEmpty());
        const Result notTable = m_engine.load(ext(u"zly"_s, u"b.app.theme(\"dark\")"_s));
        QVERIFY(!notTable.ok());
        QVERIFY2(notTable.error.contains(u"table of tokens"_s), qPrintable(notTable.error));
    }

    void laterThemeWins()
    {
        QVERIFY(m_engine.load(ext(u"a"_s, u"b.app.theme({ accent = \"#111111\" })"_s)).ok());
        QVERIFY(m_engine.load(ext(u"b"_s, u"b.app.theme({ accent = \"#222222\" })"_s)).ok());
        QCOMPARE(m_engine.themes().size(), 2);
        QCOMPARE(m_engine.themes().last().extension, u"b"_s);
        // Setting again moves it last: whoever set it last wins.
        QVERIFY(eval(u"b.app.theme({ accent = \"#333333\" })"_s, u"a"_s).ok());
        QCOMPARE(m_engine.themes().last().extension, u"a"_s);
        QList<ThemeLayer> layers;
        for (const ThemeInfo &t : m_engine.themes())
            layers << t.layer;
        QCOMPARE(mergedTheme(layers, false).value(u"accent"_s).toString(), u"#333333"_s);
        // Unloaded, its layer goes with it.
        m_engine.unload(u"a"_s);
        QCOMPARE(m_engine.themes().size(), 1);
        QCOMPARE(m_engine.themes().first().extension, u"b"_s);
    }

    // ---- items of the UI -----------------------------------------------------------------------------

    void itemsAreChecked_data()
    {
        QTest::addColumn<QString>("slot");
        QTest::addColumn<QVariantMap>("item");
        QTest::addColumn<QString>("error");
        const QVariantMap button{
                {u"id"_s, u"gym"_s}, {u"label"_s, u"Gym"_s}, {u"icon"_s, u"dumbbell"_s}, {u"view"_s, u"today"_s}};
        QTest::newRow("button") << u"toolbar"_s << button << QString();
        QTest::newRow("status text") << u"statusbar"_s
                                     << QVariantMap{{u"id"_s, u"words"_s}, {u"label"_s, u"321 słów"_s}} << QString();
        QTest::newRow("slash insert") << u"slash"_s
                                      << QVariantMap{{u"id"_s, u"sig"_s},
                                                     {u"label"_s, u"Podpis"_s},
                                                     {u"insert"_s, u"— Ala, ąćęłńóśźż"_s}}
                                      << QString();
        QTest::newRow("page menu run") << u"pageMenu"_s
                                       << QVariantMap{{u"id"_s, u"cal"_s},
                                                      {u"label"_s, u"Calendar"_s},
                                                      {u"run"_s, u"go.calendar"_s}}
                                       << QString();
        QTest::newRow("sidebar page") << u"sidebar"_s
                                      << QVariantMap{{u"id"_s, u"home"_s},
                                                     {u"label"_s, u"Dom"_s},
                                                     {u"page"_s, u"Projekty/Dom"_s}}
                                      << QString();
        QTest::newRow("view params") << u"toolbar"_s
                                     << QVariantMap{{u"id"_s, u"m"_s},
                                                    {u"label"_s, u"M"_s},
                                                    {u"view"_s, u"month"_s},
                                                    {u"params"_s, QVariantMap{{u"month"_s, u"2026-11"_s}}}}
                                     << QString();
        QTest::newRow("unknown slot") << u"titlebar"_s << button << u"unknown slot “titlebar”"_s;
        QTest::newRow("no id") << u"toolbar"_s << QVariantMap{{u"label"_s, u"x"_s}, {u"run"_s, u"go.calendar"_s}}
                               << u"needs an “id”"_s;
        QTest::newRow("bad id") << u"toolbar"_s
                                << QVariantMap{{u"id"_s, u"a b"_s}, {u"label"_s, u"x"_s}, {u"run"_s, u"go.calendar"_s}}
                                << u"needs an “id”"_s;
        QTest::newRow("misspelt field") << u"toolbar"_s
                                        << QVariantMap{{u"id"_s, u"a"_s}, {u"label"_s, u"x"_s}, {u"comand"_s, u"go"_s}}
                                        << u"no field “comand”"_s;
        QTest::newRow("two actions") << u"toolbar"_s
                                     << QVariantMap{{u"id"_s, u"a"_s},
                                                    {u"label"_s, u"x"_s},
                                                    {u"command"_s, u"go"_s},
                                                    {u"page"_s, u"Dom"_s}}
                                     << u"does one thing"_s;
        QTest::newRow("no action") << u"sidebar"_s << QVariantMap{{u"id"_s, u"a"_s}, {u"label"_s, u"x"_s}}
                                   << u"needs what it does"_s;
        QTest::newRow("insert outside slash")
                << u"pageMenu"_s << QVariantMap{{u"id"_s, u"a"_s}, {u"label"_s, u"x"_s}, {u"insert"_s, u"x"_s}}
                << u"slash menu only"_s;
        QTest::newRow("slash view") << u"slash"_s
                                    << QVariantMap{{u"id"_s, u"a"_s}, {u"label"_s, u"x"_s}, {u"view"_s, u"v"_s}}
                                    << u"not a view"_s;
        QTest::newRow("no label") << u"toolbar"_s << QVariantMap{{u"id"_s, u"a"_s}, {u"run"_s, u"go.calendar"_s}}
                                  << u"needs a “label”"_s;
        QTest::newRow("long label") << u"toolbar"_s
                                    << QVariantMap{{u"id"_s, u"a"_s},
                                                   {u"label"_s, QString(101, u'x')},
                                                   {u"run"_s, u"go.calendar"_s}}
                                    << u"at most 100"_s;
        QTest::newRow("bad icon") << u"toolbar"_s
                                  << QVariantMap{{u"id"_s, u"a"_s},
                                                 {u"label"_s, u"x"_s},
                                                 {u"icon"_s, u"../../etc"_s},
                                                 {u"run"_s, u"go.calendar"_s}}
                                  << u"app's icons"_s;
        QTest::newRow("bad tone") << u"statusbar"_s
                                  << QVariantMap{{u"id"_s, u"a"_s}, {u"label"_s, u"x"_s}, {u"tone"_s, u"pink"_s}}
                                  << u"“tone”"_s;
        QTest::newRow("params without view") << u"toolbar"_s
                                             << QVariantMap{{u"id"_s, u"a"_s},
                                                            {u"label"_s, u"x"_s},
                                                            {u"run"_s, u"go.calendar"_s},
                                                            {u"params"_s, QVariantMap()}}
                                             << u"go with a “view”"_s;
        QTest::newRow("order as text") << u"toolbar"_s
                                       << QVariantMap{{u"id"_s, u"a"_s},
                                                      {u"label"_s, u"x"_s},
                                                      {u"run"_s, u"go.calendar"_s},
                                                      {u"order"_s, u"1"_s}}
                                       << u"“order” is a number"_s;
        QTest::newRow("label not text") << u"toolbar"_s
                                        << QVariantMap{{u"id"_s, u"a"_s}, {u"label"_s, 5}, {u"run"_s, u"go.calendar"_s}}
                                        << u"“label” is a text"_s;
    }
    void itemsAreChecked()
    {
        QFETCH(QString, slot);
        QFETCH(QVariantMap, item);
        QFETCH(QString, error);
        Contribution c;
        QString why;
        const bool ok = contributionFrom(slot, item, &c, &why);
        QCOMPARE(ok, error.isEmpty());
        if (!ok)
            QVERIFY2(why.contains(error), qPrintable(why));
        else
            QCOMPARE(c.slot, slot);
    }

    void labelsAreOneLine()
    {
        Contribution c;
        QString error;
        QVERIFY(contributionFrom(u"statusbar"_s, {{u"id"_s, u"a"_s}, {u"label"_s, u"  dwa\n  wiersze  "_s}}, &c,
                                 &error));
        QCOMPARE(c.label, u"dwa wiersze"_s);
    }

    void contributeFromLuau()
    {
        QVERIFY(m_engine.load(ext(u"gym"_s, uR"lua(
            b.command("start", function(ctx) return "go" end, { label = "Start" })
            b.view("today", function(params) return b.ui.text("Dziś") end)
            b.app.contribute("toolbar", { id = "gym", icon = "dumbbell", label = "Siłownia", view = "today" })
            b.app.contribute("statusbar", { id = "sets", label = "3 serie", tone = "success", command = "start" })
            b.app.contribute("slash", { id = "set", label = "Seria", insert = "- Przysiad 3×5 100 kg" })
        )lua"_s))
                        .ok());
        QCOMPARE(m_engine.contributions().size(), 3);
        QCOMPARE(m_engine.contributions(u"toolbar"_s).size(), 1);
        const Contribution c = m_engine.contributions(u"toolbar"_s).first();
        QCOMPARE(c.extension, u"gym"_s);
        QCOMPARE(c.label, u"Siłownia"_s);
        QCOMPARE(c.view, u"today"_s);
        // The same id replaces the item in its place (a status bar's counter).
        QVERIFY(eval(u"b.app.contribute(\"statusbar\", { id = \"sets\", label = \"4 serie\", command = \"start\" })"_s,
                     u"gym"_s)
                        .ok());
        QCOMPARE(m_engine.contributions(u"statusbar"_s).size(), 1);
        QCOMPARE(m_engine.contributions(u"statusbar"_s).first().label, u"4 serie"_s);
        // Read back as tables.
        const Result list = eval(u"return b.app.contributions(\"statusbar\")"_s, u"gym"_s);
        QVERIFY2(list.ok(), qPrintable(list.error));
        QCOMPARE(list.value.toList().first().toMap().value(u"label"_s).toString(), u"4 serie"_s);
        QCOMPARE(list.value.toList().first().toMap().value(u"extension"_s).toString(), u"gym"_s);
        // Removed one by one, or a whole slot.
        const Result removed = eval(u"return b.app.remove(\"toolbar\", \"gym\")"_s, u"gym"_s);
        QVERIFY(removed.ok() && removed.value.toBool());
        const Result again = eval(u"return b.app.remove(\"toolbar\", \"gym\")"_s, u"gym"_s);
        QVERIFY(again.ok() && !again.value.toBool());
        QVERIFY(eval(u"b.app.remove(\"slash\")"_s, u"gym"_s).ok());
        QCOMPARE(m_engine.contributions().size(), 1);
        // The extension goes: so do its items.
        m_engine.unload(u"gym"_s);
        QVERIFY(m_engine.contributions().isEmpty());
    }

    void itemsNameTheirOwnCommands()
    {
        // At the top level a command may come later; afterwards it must exist.
        QVERIFY(m_engine.load(ext(u"x"_s, uR"lua(
            b.app.contribute("toolbar", { id = "a", label = "A", command = "later" })
            b.command("later", function() end)
        )lua"_s))
                        .ok());
        const Result r =
                eval(u"b.app.contribute(\"toolbar\", { id = \"b\", label = \"B\", command = \"missing\" })"_s, u"x"_s);
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains(u"has no command “missing”"_s), qPrintable(r.error));
        const Result v =
                eval(u"b.app.contribute(\"toolbar\", { id = \"b\", label = \"B\", view = \"missing\" })"_s, u"x"_s);
        QVERIFY(!v.ok());
        QVERIFY2(v.error.contains(u"has no view “missing”"_s), qPrintable(v.error));
    }

    void itemsAreOrdered()
    {
        QVERIFY(m_engine.load(ext(u"x"_s, uR"lua(
            b.app.contribute("sidebar", { id = "c", label = "C", page = "C" })
            b.app.contribute("sidebar", { id = "a", label = "A", page = "A", order = -1 })
            b.app.contribute("sidebar", { id = "b", label = "B", page = "B" })
        )lua"_s))
                        .ok());
        QStringList ids;
        for (const Contribution &c : m_engine.contributions(u"sidebar"_s))
            ids << c.id;
        QCOMPARE(ids, (QStringList{u"a"_s, u"c"_s, u"b"_s}));
    }

    void slotsHaveLimits()
    {
        const Result r = m_engine.load(ext(u"x"_s, uR"lua(
            for i = 1, 17 do
                b.app.contribute("toolbar", { id = "b" .. i, label = "B" .. i, run = "go.calendar" })
            end
        )lua"_s));
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains(u"at most 16 items of one extension"_s), qPrintable(r.error));
        QVERIFY(m_engine.contributions().isEmpty()); // a failed load leaves nothing
        // Four extensions of 16 fill a slot.
        for (int e = 0; e < 4; ++e)
            QVERIFY(m_engine.load(ext(u"e%1"_s.arg(e), uR"lua(
                for i = 1, 16 do b.app.contribute("statusbar", { id = "s" .. i, label = "S" }) end
            )lua"_s))
                            .ok());
        const Result full =
                m_engine.load(ext(u"e4"_s, u"b.app.contribute(\"statusbar\", { id = \"s\", label = \"S\" })"_s));
        QVERIFY(!full.ok());
        QVERIFY2(full.error.contains(u"is full"_s), qPrintable(full.error));
    }

    void contributionsOfOthersAreSeen()
    {
        QVERIFY(m_engine.load(ext(u"a"_s, u"b.app.contribute(\"statusbar\", { id = \"x\", label = \"X\" })"_s)).ok());
        QVERIFY(m_engine.load(ext(u"b"_s, u"seen = #b.app.contributions()"_s, {})).ok());
        const Result r = eval(u"return seen"_s, u"b"_s);
        QVERIFY(r.ok());
        QCOMPARE(r.value.toInt(), 1);
        // … but one cannot remove another's.
        QVERIFY(m_engine.load(ext(u"c"_s, u"gone = b.app.remove(\"statusbar\", \"x\")"_s)).ok());
        QCOMPARE(m_engine.contributions().size(), 1);
    }

    // ---- input rules and styles of blocks by tag -------------------------------------------------------

    void inputRulesAreChecked_data()
    {
        QTest::addColumn<QVariantMap>("table");
        QTest::addColumn<QString>("error");
        QTest::newRow("arrow") << QVariantMap{{u"match"_s, u"->"_s}, {u"replace"_s, u"→"_s}} << QString();
        QTest::newRow("word") << QVariantMap{{u"match"_s, u"zolw"_s}, {u"replace"_s, u"żółw"_s}, {u"word"_s, true}}
                              << QString();
        QTest::newRow("spaces in the match")
                << QVariantMap{{u"match"_s, u" - "_s}, {u"replace"_s, u" – "_s}} << QString();
        QTest::newRow("removing the match") << QVariantMap{{u"match"_s, u"!!"_s}, {u"replace"_s, u""_s}} << QString();
        QTest::newRow("no match") << QVariantMap{{u"replace"_s, u"→"_s}} << u"needs “match”"_s;
        QTest::newRow("empty match") << QVariantMap{{u"match"_s, u""_s}, {u"replace"_s, u"→"_s}} << u"needs “match”"_s;
        QTest::newRow("match not text") << QVariantMap{{u"match"_s, 5}, {u"replace"_s, u"→"_s}} << u"needs “match”"_s;
        QTest::newRow("no replace") << QVariantMap{{u"match"_s, u"->"_s}} << u"needs “replace”"_s;
        QTest::newRow("misspelt field") << QVariantMap{{u"match"_s, u"->"_s}, {u"replce"_s, u"→"_s}}
                                        << u"no field “replce”"_s;
        QTest::newRow("long match") << QVariantMap{{u"match"_s, QString(33, u'ą')}, {u"replace"_s, u"x"_s}}
                                    << u"at most 32"_s;
        QTest::newRow("long replace") << QVariantMap{{u"match"_s, u"x"_s}, {u"replace"_s, QString(201, u'ż')}}
                                      << u"at most 200"_s;
        QTest::newRow("line break") << QVariantMap{{u"match"_s, u"--"_s}, {u"replace"_s, u"a\nb"_s}} << u"one line"_s;
        QTest::newRow("the same") << QVariantMap{{u"match"_s, u"--"_s}, {u"replace"_s, u"--"_s}} << u"itself"_s;
        QTest::newRow("word with spaces")
                << QVariantMap{{u"match"_s, u"teh "_s}, {u"replace"_s, u"the"_s}, {u"word"_s, true}}
                << u"no spaces around"_s;
        QTest::newRow("word not a boolean")
                << QVariantMap{{u"match"_s, u"teh"_s}, {u"replace"_s, u"the"_s}, {u"word"_s, u"yes"_s}}
                << u"“word” is true or false"_s;
    }
    void inputRulesAreChecked()
    {
        QFETCH(QVariantMap, table);
        QFETCH(QString, error);
        InputRule rule;
        QString why;
        const bool ok = inputRuleFrom(table, &rule, &why);
        QCOMPARE(ok, error.isEmpty());
        if (!ok)
            QVERIFY2(why.contains(error), qPrintable(why));
        else
            QCOMPARE(rule.match, table.value(u"match"_s).toString());
    }

    void inputRulesFromLuau()
    {
        QVERIFY(m_engine.load(ext(u"strzalki"_s, uR"lua(
            b.app.inputRule({ match = "->", replace = "→" })
            b.app.inputRule({ match = "<-", replace = "←" })
            b.app.inputRule({ match = "zolw", replace = "żółw", word = true })
        )lua"_s))
                        .ok());
        QCOMPARE(m_engine.inputRules().size(), 3);
        QCOMPARE(m_engine.inputRules().first().extension, u"strzalki"_s);
        QCOMPARE(m_engine.inputRules().first().replace, u"→"_s);
        QVERIFY(m_engine.inputRules().last().word);
        QVERIFY(m_host.changed.contains(u"strzalki"_s));
        const int revision = m_engine.appRevision();
        // The same rule again changes nothing; the same match replaces the rule in its place.
        QVERIFY(eval(u"b.app.inputRule({ match = \"->\", replace = \"→\" })"_s, u"strzalki"_s).ok());
        QCOMPARE(m_engine.appRevision(), revision);
        QVERIFY(eval(u"b.app.inputRule({ match = \"->\", replace = \"⇒\" })"_s, u"strzalki"_s).ok());
        QCOMPARE(m_engine.inputRules().size(), 3);
        QCOMPARE(m_engine.inputRules().first().replace, u"⇒"_s);
        QVERIFY(m_engine.appRevision() > revision);
        // Listed as tables, with their extension.
        const Result list = eval(
                u"local r = b.app.inputRules() return r[3].match .. r[3].replace .. tostring(r[3].word) .. r[1].extension"_s,
                u"strzalki"_s);
        QVERIFY2(list.ok(), qPrintable(list.error));
        QCOMPARE(list.value.toString(), u"zolwżółwtruestrzalki"_s);
        // Taken away one by one, or all of them.
        const Result removed = eval(u"return b.app.remove(\"inputRule\", \"<-\")"_s, u"strzalki"_s);
        QVERIFY(removed.ok() && removed.value.toBool());
        QCOMPARE(m_engine.inputRules().size(), 2);
        const Result again = eval(u"return b.app.remove(\"inputRule\", \"<-\")"_s, u"strzalki"_s);
        QVERIFY(again.ok() && !again.value.toBool());
        // An error names the function and keeps nothing.
        const Result bad = eval(u"b.app.inputRule({ match = \"\", replace = \"x\" })"_s, u"strzalki"_s);
        QVERIFY(!bad.ok());
        QVERIFY2(bad.error.contains(u"b.app.inputRule: an input rule needs “match”"_s), qPrintable(bad.error));
        const Result notTable = eval(u"b.app.inputRule(\"->\")"_s, u"strzalki"_s);
        QVERIFY(!notTable.ok());
        QVERIFY2(notTable.error.contains(u"argument 1 is the rule"_s), qPrintable(notTable.error));
        QCOMPARE(m_engine.inputRules().size(), 2);
        // Loaded again, the old version's rules go first; unloaded, all of them.
        QVERIFY(m_engine.load(ext(u"strzalki"_s, u"b.app.inputRule({ match = \"(c)\", replace = \"©\" })"_s)).ok());
        QCOMPARE(m_engine.inputRules().size(), 1);
        QCOMPARE(m_engine.inputRules().first().match, u"(c)"_s);
        QVERIFY(eval(u"b.app.remove(\"inputRule\")"_s, u"strzalki"_s).ok());
        QVERIFY(m_engine.inputRules().isEmpty());
        QVERIFY(eval(u"b.app.inputRule({ match = \"(c)\", replace = \"©\" })"_s, u"strzalki"_s).ok());
        m_engine.unload(u"strzalki"_s);
        QVERIFY(m_engine.inputRules().isEmpty());
    }

    void inputRulesHaveLimits()
    {
        const Result r = m_engine.load(ext(u"x"_s, uR"lua(
            for i = 1, 65 do b.app.inputRule({ match = "r" .. i, replace = "R" }) end
        )lua"_s));
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains(u"at most 64 rules of one extension"_s), qPrintable(r.error));
        QVERIFY(m_engine.inputRules().isEmpty());
        for (int e = 0; e < 4; ++e)
            QVERIFY(m_engine.load(ext(u"e%1"_s.arg(e), uR"lua(
                for i = 1, 64 do b.app.inputRule({ match = "r" .. i, replace = "R" }) end
            )lua"_s))
                            .ok());
        const Result full = m_engine.load(ext(u"e4"_s, u"b.app.inputRule({ match = \"q\", replace = \"Q\" })"_s));
        QVERIFY(!full.ok());
        QVERIFY2(full.error.contains(u"of all extensions"_s), qPrintable(full.error));
    }

    void hostileRulesAndStyles()
    {
        // Thousands of calls with the same match or tag: one rule, one style, the engine fine.
        const Result loop = m_engine.load(ext(u"h"_s, uR"lua(
            for i = 1, 100000 do
                b.app.inputRule({ match = "->", replace = "→" .. (i % 3) })
                b.app.tagStyle({ tag = "t", tint = if i % 2 == 0 then "info" else "danger" })
            end
        )lua"_s));
        QVERIFY2(loop.ok() || loop.status == Status::Timeout, qPrintable(loop.error));
        QVERIFY(m_engine.inputRules().size() <= 1);
        QVERIFY(m_engine.tagStyles().size() <= 1);
        const Result big =
                m_engine.load(ext(u"h"_s, u"b.app.inputRule({ match = \"x\", replace = string.rep(\"ą\", 1e6) })"_s));
        QVERIFY(!big.ok());
        QVERIFY2(big.error.contains(u"at most 200"_s), qPrintable(big.error));
    }

    void tagStylesAreChecked_data()
    {
        QTest::addColumn<QVariantMap>("table");
        QTest::addColumn<QString>("tag");
        QTest::addColumn<QString>("tint");
        QTest::addColumn<QString>("error");
        QTest::newRow("theme tint") << QVariantMap{{u"tag"_s, u"pilne"_s}, {u"tint"_s, u"danger"_s}} << u"pilne"_s
                                    << u"danger"_s << QString();
        QTest::newRow("with its hash") << QVariantMap{{u"tag"_s, u"#ważne"_s}, {u"tint"_s, u"warning"_s}} << u"ważne"_s
                                       << u"warning"_s << QString();
        QTest::newRow("a tag with spaces") << QVariantMap{{u"tag"_s, u"#[[Jan Kowalski]]"_s}, {u"tint"_s, u"info"_s}}
                                           << u"Jan Kowalski"_s << u"info"_s << QString();
        QTest::newRow("nested tag") << QVariantMap{{u"tag"_s, u"Projekt/Dom/"_s}, {u"tint"_s, u"neutral"_s}}
                                    << u"Projekt/Dom"_s << u"neutral"_s << QString();
        QTest::newRow("colour") << QVariantMap{{u"tag"_s, u"x"_s}, {u"tint"_s, u"#12AB34"_s}} << u"x"_s << u"#12ab34"_s
                                << QString();
        QTest::newRow("short colour") << QVariantMap{{u"tag"_s, u"x"_s}, {u"tint"_s, u"#abc"_s}} << u"x"_s
                                      << u"#aabbcc"_s << QString();
        QTest::newRow("bar only") << QVariantMap{{u"tag"_s, u"x"_s}, {u"tint"_s, u"accent"_s}, {u"fill"_s, false}}
                                  << u"x"_s << u"accent"_s << QString();
        QTest::newRow("no tag") << QVariantMap{{u"tint"_s, u"danger"_s}} << QString() << QString() << u"needs “tag”"_s;
        QTest::newRow("no tag's name") << QVariantMap{{u"tag"_s, u"a]b"_s}, {u"tint"_s, u"danger"_s}} << QString()
                                       << QString() << u"no tag's name"_s;
        QTest::newRow("alpha colour") << QVariantMap{{u"tag"_s, u"x"_s}, {u"tint"_s, u"#80ff0000"_s}} << QString()
                                      << QString() << u"“tint” is one of"_s;
        QTest::newRow("CSS") << QVariantMap{{u"tag"_s, u"x"_s}, {u"tint"_s, u"red; background: url(x)"_s}} << QString()
                             << QString() << u"“tint” is one of"_s;
        QTest::newRow("unknown tint") << QVariantMap{{u"tag"_s, u"x"_s}, {u"tint"_s, u"pink"_s}} << QString()
                                      << QString() << u"“tint” is one of"_s;
        QTest::newRow("nothing shown") << QVariantMap{{u"tag"_s, u"x"_s},
                                                      {u"tint"_s, u"info"_s},
                                                      {u"fill"_s, false},
                                                      {u"bar"_s, false}}
                                       << QString() << QString() << u"a “fill”, a “bar” or both"_s;
        QTest::newRow("misspelt field") << QVariantMap{{u"tag"_s, u"x"_s}, {u"tint"_s, u"info"_s}, {u"colour"_s, 1}}
                                        << QString() << QString() << u"no field “colour”"_s;
    }
    void tagStylesAreChecked()
    {
        QFETCH(QVariantMap, table);
        QFETCH(QString, tag);
        QFETCH(QString, tint);
        QFETCH(QString, error);
        TagStyle style;
        QString why;
        const bool ok = tagStyleFrom(table, &style, &why);
        QCOMPARE(ok, error.isEmpty());
        if (!ok) {
            QVERIFY2(why.contains(error), qPrintable(why));
            return;
        }
        QCOMPARE(style.tag, tag);
        QCOMPARE(style.tint, tint);
        QCOMPARE(style.fill, table.value(u"fill"_s, true).toBool());
    }

    void tagStyleKeys()
    {
        QCOMPARE(tagStyleKey(u"#Pilne"_s), u"pilne"_s);
        QCOMPARE(tagStyleKey(u"ŻÓŁW"_s), u"żółw"_s);
        QCOMPARE(tagStyleKey(u"#[[Jan Kowalski]]"_s), u"jan kowalski"_s);
        QCOMPARE(tagStyleKey(u"Projekt/Dom/"_s), u"projekt/dom"_s);
        QCOMPARE(tagStyleKey(u"#"_s), QString());
        QCOMPARE(tagStyleKey(u"a|b"_s), QString());
    }

    void tagStylesFromLuau()
    {
        QVERIFY(m_engine.load(ext(u"kolory"_s, uR"lua(
            b.app.tagStyle({ tag = "pilne", tint = "danger" })
            b.app.tagStyle({ tag = "projekt", tint = string.char(35) .. "1f8a80", fill = false })
        )lua"_s))
                        .ok());
        QCOMPARE(m_engine.tagStyles().size(), 2);
        QCOMPARE(m_engine.tagStyles().first().tint, u"danger"_s);
        QCOMPARE(m_engine.tagStyles().last().tint, u"#1f8a80"_s);
        QVERIFY(!m_engine.tagStyles().last().fill);
        // The same tag (any case, with its '#'): replaced, and last now.
        QVERIFY(eval(u"b.app.tagStyle({ tag = \"#PILNE\", tint = \"warning\" })"_s, u"kolory"_s).ok());
        QCOMPARE(m_engine.tagStyles().size(), 2);
        QCOMPARE(m_engine.tagStyles().last().tint, u"warning"_s);
        QCOMPARE(m_engine.tagStyles().last().tag, u"PILNE"_s);
        // Another extension's style of the tag is declared after it: it wins.
        QVERIFY(m_engine.load(ext(u"inne"_s, u"b.app.tagStyle({ tag = \"pilne\", tint = \"info\" })"_s)).ok());
        QCOMPARE(m_engine.tagStyles().last().extension, u"inne"_s);
        const Result list = eval(
                u"local s = b.app.tagStyles() return #s .. s[1].tag .. s[1].tint .. tostring(s[1].fill)"_s, u"inne"_s);
        QVERIFY2(list.ok(), qPrintable(list.error));
        QCOMPARE(list.value.toString(), u"3projekt#1f8a80false"_s);
        // Removed by its tag (one cannot remove another's), then by the extension going.
        const Result removed = eval(u"return b.app.remove(\"tagStyle\", \"#Pilne\")"_s, u"kolory"_s);
        QVERIFY(removed.ok() && removed.value.toBool());
        QCOMPARE(m_engine.tagStyles().size(), 2);
        m_engine.unload(u"kolory"_s);
        QCOMPARE(m_engine.tagStyles().size(), 1);
        QCOMPARE(m_engine.tagStyles().first().extension, u"inne"_s);
        const Result bad = eval(u"b.app.tagStyle({ tag = \"pilne\", tint = \"red\" })"_s, u"inne"_s);
        QVERIFY(!bad.ok());
        QVERIFY2(bad.error.contains(u"b.app.tagStyle: “tint” is one of"_s), qPrintable(bad.error));
        QCOMPARE(m_engine.tagStyles().first().tint, u"info"_s);
    }

    // ---- the app's operations ------------------------------------------------------------------------

    void readingNeedsNoPermission()
    {
        QVERIFY(m_engine.load(ext(u"r"_s, uR"lua(
            state = b.app.state()
            commands = b.app.commands()
            settings = b.app.settings()
            density = b.app.get("density")
        )lua"_s,
                                  {}))
                        .ok());
        const Result r = eval(u"return state.view .. \",\" .. commands[1].id .. \",\" .. density"_s, u"r"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"journals,go.calendar,comfortable"_s);
        QCOMPARE(m_host.askedBy.first(), u"r"_s);
    }

    void changesNeedThePermission_data()
    {
        QTest::addColumn<QString>("code");
        QTest::newRow("set") << u"b.app.set(\"density\", \"compact\")"_s;
        QTest::newRow("navigate") << u"b.app.navigate(\"calendar\")"_s;
        QTest::newRow("open") << u"b.app.open(\"Dom\")"_s;
        QTest::newRow("back") << u"b.app.back()"_s;
        QTest::newRow("run") << u"b.app.run(\"go.calendar\")"_s;
        QTest::newRow("toast") << u"b.app.toast(\"Cześć\")"_s;
        QTest::newRow("theme") << u"b.app.theme({ accent = \"#000\" })"_s;
        QTest::newRow("contribute") << u"b.app.contribute(\"statusbar\", { id = \"a\", label = \"A\" })"_s;
        QTest::newRow("remove") << u"b.app.remove(\"statusbar\")"_s;
        QTest::newRow("inputRule") << u"b.app.inputRule({ match = \"->\", replace = \"→\" })"_s;
        QTest::newRow("tagStyle") << u"b.app.tagStyle({ tag = \"pilne\", tint = \"danger\" })"_s;
        QTest::newRow("remove inputRule") << u"b.app.remove(\"inputRule\")"_s;
    }
    void changesNeedThePermission()
    {
        QFETCH(QString, code);
        QVERIFY(m_engine.load(ext(u"p"_s, u"b.command(\"go\", function() "_s + code + u" end)"_s, {u"write"_s})).ok());
        const Result r = m_engine.runCommand(u"p"_s, u"go"_s, {});
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains(u"permission denied: app"_s), qPrintable(r.error));
        for (const auto &[operation, args] : m_host.asked)
            QVERIFY2(!isAppChange(operation), qPrintable(operation));
    }

    void changesReachTheHost()
    {
        QVERIFY(m_engine.load(ext(u"c"_s, uR"lua(
            b.command("go", function(ctx)
                b.app.set("density", "compact")
                b.app.navigate({ view = "page", path = "Dom.md" })
                b.app.navigate("calendar")
                b.app.open("Projekty/Dom", { line = 3, side = true })
                b.app.back()
                b.app.run("go.calendar")
                b.app.toast("Gotowe — zażółć gęślą jaźń", { tone = "success", button = { label = "Cofnij", command = "go" } })
                return b.app.get("density")
            end)
        )lua"_s))
                        .ok());
        const Result r = m_engine.runCommand(u"c"_s, u"go"_s, {});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"compact"_s);
        QStringList operations;
        for (const auto &[operation, args] : m_host.asked)
            operations << operation;
        QCOMPARE(operations, (QStringList{u"set"_s, u"navigate"_s, u"navigate"_s, u"open"_s, u"back"_s, u"run"_s,
                                          u"toast"_s, u"get"_s}));
        QCOMPARE(m_host.asked[1].second.value(u"route"_s).toMap().value(u"path"_s).toString(), u"Dom.md"_s);
        QCOMPARE(m_host.asked[2].second.value(u"route"_s).toString(), u"calendar"_s);
        QCOMPARE(m_host.asked[3].second.value(u"line"_s).toInt(), 3);
        QCOMPARE(m_host.asked[3].second.value(u"side"_s).toBool(), true);
        const QVariantMap toast = m_host.asked[6].second;
        QCOMPARE(toast.value(u"text"_s).toString(), u"Gotowe — zażółć gęślą jaźń"_s);
        QCOMPARE(toast.value(u"tone"_s).toString(), u"success"_s);
        QCOMPARE(toast.value(u"button"_s).toMap().value(u"command"_s).toString(), u"go"_s);
    }

    void hostErrorsAreScriptErrors()
    {
        QVERIFY(m_engine.load(ext(u"e"_s, u"b.command(\"go\", function() b.app.run(\"nope\") end)"_s)).ok());
        const Result r = m_engine.runCommand(u"e"_s, u"go"_s, {});
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains(u"b.app.run: no command “nope”"_s), qPrintable(r.error));
        const Result g = eval(u"return b.app.get(\"nothing\")"_s, u"e"_s);
        QVERIFY(!g.ok());
        QVERIFY2(g.error.contains(u"b.app.get: no setting “nothing”"_s), qPrintable(g.error));
    }

    void argumentsAreChecked_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("error");
        QTest::newRow("get no key") << u"b.app.get()"_s << u"argument 1 must be a string"_s;
        QTest::newRow("set no value") << u"b.app.set(\"vim\")"_s << u"argument 2 is the setting's new value"_s;
        QTest::newRow("set a function") << u"b.app.set(\"vim\", print)"_s << u"cannot pass a value of type function"_s;
        QTest::newRow("navigate number") << u"b.app.navigate(3)"_s << u"a view's name"_s;
        QTest::newRow("open empty") << u"b.app.open(\"  \")"_s << u"the page's name is empty"_s;
        QTest::newRow("open bad option") << u"b.app.open(\"Dom\", { lien = 3 })"_s << u"no option “lien”"_s;
        QTest::newRow("open line zero") << u"b.app.open(\"Dom\", { line = 0 })"_s << u"counted from 1"_s;
        QTest::newRow("open options text") << u"b.app.open(\"Dom\", \"side\")"_s << u"argument 2 is a table"_s;
        QTest::newRow("toast empty") << u"b.app.toast(\"\")"_s << u"the text is empty"_s;
        QTest::newRow("toast tone") << u"b.app.toast(\"x\", { tone = \"pink\" })"_s << u"“tone” is success"_s;
        QTest::newRow("toast button") << u"b.app.toast(\"x\", { button = { label = \"x\" } })"_s << u"a button is"_s;
        QTest::newRow("toast option") << u"b.app.toast(\"x\", { colour = \"red\" })"_s << u"no option “colour”"_s;
        QTest::newRow("run number") << u"b.app.run(1)"_s << u"argument 1 must be a string"_s;
        QTest::newRow("remove slot") << u"b.app.remove(\"nowhere\")"_s << u"unknown slot “nowhere”"_s;
        QTest::newRow("contribute no table")
                << u"b.app.contribute(\"toolbar\", \"x\")"_s << u"argument 2 is the item"_s;
        QTest::newRow("contributions slot") << u"b.app.contributions(\"x\")"_s << u"unknown slot “x”"_s;
    }
    void argumentsAreChecked()
    {
        QFETCH(QString, code);
        QFETCH(QString, error);
        QVERIFY(m_engine.load(ext(u"a"_s, u"b.command(\"go\", function() "_s + code + u" end)"_s)).ok());
        const Result r = m_engine.runCommand(u"a"_s, u"go"_s, {});
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains(error), qPrintable(r.error));
    }

    void notWhileLoading_data()
    {
        QTest::addColumn<QString>("code");
        QTest::newRow("set") << u"b.app.set(\"vim\", true)"_s;
        QTest::newRow("navigate") << u"b.app.navigate(\"calendar\")"_s;
        QTest::newRow("run") << u"b.app.run(\"go.calendar\")"_s;
        QTest::newRow("toast") << u"b.app.toast(\"x\")"_s;
    }
    void notWhileLoading()
    {
        // What moves the UI never runs at the top level (it would on every save of the page).
        QFETCH(QString, code);
        const Result r = m_engine.load(ext(u"l"_s, code));
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains(u"cannot run while the extension loads"_s), qPrintable(r.error));
        QVERIFY(m_host.asked.isEmpty());
    }

    void macrosCannotChangeTheApp()
    {
        QVERIFY(m_engine.load(ext(u"m"_s, uR"lua(
            b.macro("theme", function(ctx) b.app.theme({ accent = string.char(35) .. "000" }) return "x" end)
            b.macro("set", function(ctx) b.app.set("vim", true) return "x" end)
            b.macro("item", function(ctx) b.app.contribute("statusbar", { id = "a", label = "A" }) return "x" end)
            b.macro("state", function(ctx) return b.app.state().view end)
        )lua"_s))
                        .ok());
        for (const QString &name : {u"theme"_s, u"set"_s, u"item"_s}) {
            const Result r = m_engine.runMacro(u"m"_s, name, {{u"args"_s, QString()}}, 250);
            QVERIFY2(!r.ok() && r.error.contains(u"cannot change the app"_s), qPrintable(name + u": "_s + r.error));
        }
        const Result state = m_engine.runMacro(u"m"_s, u"state"_s, {{u"args"_s, QString()}}, 250);
        QVERIFY2(state.ok(), qPrintable(state.error));
        QCOMPARE(state.value.toString(), u"journals"_s);
        QVERIFY(m_engine.themes().isEmpty());
        QVERIFY(m_engine.contributions().isEmpty());
    }

    void appIsReadOnly()
    {
        QVERIFY(m_engine.load(ext(u"h"_s, u"b.command(\"go\", function() b.app.state = nil end)"_s)).ok());
        const Result r = m_engine.runCommand(u"h"_s, u"go"_s, {});
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains(u"readonly"_s) || r.error.contains(u"read-only"_s), qPrintable(r.error));
    }

    void routeIsAHookEvent()
    {
        QVERIFY(Engine::hookEvents().contains(u"route"_s));
        QVERIFY(m_engine.load(ext(u"r"_s, uR"lua(
            seen = {}
            b.on("route", function(e) table.insert(seen, e.view .. ":" .. (e.page or "")) end)
        )lua"_s,
                                  {}))
                        .ok());
        const QList<HookInfo> hooks = m_engine.hooks(u"route"_s);
        QCOMPARE(hooks.size(), 1);
        QVERIFY(m_engine.runHook(hooks.first().id, {{u"view"_s, u"page"_s}, {u"page"_s, u"Dom"_s}}).ok());
        const Result r = eval(u"return seen[1]"_s, u"r"_s);
        QCOMPARE(r.value.toString(), u"page:Dom"_s);
    }

    void hostileItems()
    {
        // Megabytes of label, thousands of calls, nesting: refused or bounded, the engine fine.
        const Result big = m_engine.load(
                ext(u"h"_s, u"b.app.contribute(\"statusbar\", { id = \"a\", label = string.rep(\"ą\", 1e6) })"_s));
        QVERIFY(!big.ok());
        QVERIFY2(big.error.contains(u"at most 100"_s), qPrintable(big.error));
        const Result loop = m_engine.load(ext(u"h"_s, uR"lua(
            for i = 1, 100000 do
                b.app.contribute("statusbar", { id = "a", label = "n" .. (i % 7) })
            end
        )lua"_s));
        QVERIFY2(loop.ok() || loop.status == Status::Timeout, qPrintable(loop.error));
        QVERIFY(m_engine.contributions(u"statusbar"_s).size() <= 1);
        QVERIFY(eval(u"return 1 + 1"_s, u"h"_s).ok() || !m_engine.isLoaded(u"h"_s));
    }

    // ---- a dry run (brulion-cli extension run / check) ------------------------------------------------

    void dryRunNotesTheChanges()
    {
        QTemporaryDir dir;
        Vault vault(dir.filePath(u"v"_s), dir.filePath(u"s"_s));
        QDir().mkpath(dir.filePath(u"v"_s));
        QVERIFY(vault.open());
        VaultIndex index(vault.config());
        NotebookHost::Options options;
        options.dryRun = true;
        NotebookHost host(vault, index, options);
        Engine engine;
        QVERIFY(engine.start(&host, kRoomy));
        QVERIFY(engine.load(ext(u"d"_s, uR"lua(
            b.command("go", function()
                b.app.set("density", "compact")
                b.app.navigate("calendar")
                return b.app.state().platform
            end)
        )lua"_s))
                        .ok());
        const Result r = engine.runCommand(u"d"_s, u"go"_s, {});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"cli"_s);
        const QVariantList app = host.dryApp();
        QCOMPARE(app.size(), 2);
        QCOMPARE(app[0].toMap().value(u"operation"_s).toString(), u"set"_s);
        QCOMPARE(app[0].toMap().value(u"key"_s).toString(), u"density"_s);
        QCOMPARE(app[1].toMap().value(u"route"_s).toString(), u"calendar"_s);
        // A check reports the items and the theme a page's top level sets.
        Extension e = ext(u"k"_s, uR"lua(
            b.command("go", function() end)
            b.app.contribute("toolbar", { id = "go", label = "Go", icon = "play", command = "go" })
            b.app.theme({ accent = string.char(35) .. "123456" })
            b.app.inputRule({ match = "->", replace = "→" })
            b.app.tagStyle({ tag = "pilne", tint = "danger", bar = false })
        )lua"_s);
        const QVariantMap report = checkExtension(vault, index, e, options);
        QVERIFY2(report.value(u"ok"_s).toBool(), qPrintable(report.value(u"error"_s).toString()));
        QCOMPARE(report.value(u"contributions"_s).toList().size(), 1);
        QCOMPARE(report.value(u"theme"_s).toMap().value(u"accent"_s).toString(), u"#123456"_s);
        QCOMPARE(report.value(u"inputRules"_s).toList().size(), 1);
        QCOMPARE(report.value(u"inputRules"_s).toList().first().toMap().value(u"replace"_s).toString(), u"→"_s);
        QCOMPARE(report.value(u"tagStyles"_s).toList().first().toMap().value(u"bar"_s).toBool(), false);
    }

    void appIsNotThereWithoutTheApp()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath(u"v"_s));
        Vault vault(dir.filePath(u"v"_s), dir.filePath(u"s"_s));
        QVERIFY(vault.open());
        VaultIndex index(vault.config());
        NotebookHost host(vault, index, {});
        Engine engine;
        QVERIFY(engine.start(&host, kRoomy));
        QVERIFY(engine.load(ext(u"n"_s, u"b.command(\"go\", function() return b.app.state() end)"_s)).ok());
        const Result r = engine.runCommand(u"n"_s, u"go"_s, {});
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains(u"the app is not running here"_s), qPrintable(r.error));
    }
};

QTEST_GUILESS_MAIN(TestExtensionsApp)
#include "tst_extensions_app.moc"
