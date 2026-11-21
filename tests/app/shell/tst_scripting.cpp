// AppScripting (appscripting.h; docs/EXTENSIONS.md "The app (`b.app`)", "Live code") on a real
// AppController and notebook: the registry of settings (every key names a real property), a setting
// set, refused and journalled, an extension page's b.app over the app (a setting, a route, a toast
// with its button, the theme into ThemeOverrides, UI items and their activation), the "route" hook,
// live code that shadows a page and falls back, an evaluation for a client, and revert().
#include "appcontroller.h"
#include "appscripting.h"
#include "appsettings.h"
#include "commandmodel.h"
#include "workspace.h"
#include "workspaceextensions.h"

#include "themepalette.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaProperty>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::shell;

namespace {

bool writeFile(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

const QString kPage = u"Rozszerzenia/Wygląd.md"_s;

// An extension page that changes the app (its # in a plain literal: moc reads this file).
const QByteArray kExtension = QByteArrayLiteral(
        "---\n"
        "tags: [rozszerzenie]\n"
        "uprawnienia: [aplikacja, zapis]\n"
        "---\n"
        "```luau\n"
        "b.app.theme({ accent = \"#1f8a80\", light = { bg = \"#f4ecd8\" } })\n"
        "b.view(\"tydzien\", function(params) return b.ui.text(\"Tydzień\") end)\n"
        "b.command(\"zwarty\", function(ctx)\n"
        "    b.app.set(\"density\", \"compact\")\n"
        "    b.app.navigate(\"calendar\")\n"
        "    b.app.toast(\"Zwarty układ\", { tone = \"success\", button = { label = \"Cofnij\", command = \"luzny\" } })\n"
        "    return b.app.get(\"density\")\n"
        "end)\n"
        "b.command(\"luzny\", function(ctx) b.app.set(\"density\", \"comfortable\") end)\n"
        "b.command(\"strona\", function(ctx) b.appendToJournal(b.today(), \"- ze strony \" .. ctx.page) end)\n"
        "b.app.contribute(\"toolbar\", { id = \"tydzien\", label = \"Tydzień\", icon = \"calendar\", view = \"tydzien\" })\n"
        "b.app.contribute(\"pageMenu\", { id = \"strona\", label = \"Zapisz w dzienniku\", command = \"strona\" })\n"
        "b.app.contribute(\"sidebar\", { id = \"kal\", label = \"Kalendarz\", run = \"go.calendar\" })\n"
        "b.on(\"route\", function(e)\n"
        "    b.app.contribute(\"statusbar\", { id = \"widok\", label = \"Widok: \" .. e.view .. \" po \" .. e.previous })\n"
        "end)\n"
        "```\n");

} // namespace

class TestScripting : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_counter = 0;
    std::unique_ptr<AppController> m_app;

    AppScripting *scripting() const { return m_app->scripting(); }
    /// The first item's label in `slot` ("" when it has none: QTRY asks before it is there).
    QString label(const QString &slot) const
    {
        const QVariantList items = extensions()->contributions(slot);
        return items.isEmpty() ? QString() : items.first().toMap().value(u"label"_s).toString();
    }
    WorkspaceExtensions *extensions() const { return m_app->workspace()->extensions(); }

    void openApp(bool withExtension)
    {
        m_app.reset();
        const QString vault = m_tmp.filePath(u"vault-%1"_s.arg(++m_counter));
        QVERIFY(writeFile(vault + u"/Dom.md"_s, "# Dom\n- zadanie\n"));
        if (withExtension)
            QVERIFY(writeFile(vault + u'/' + kPage, kExtension));
        AppController::Options o;
        o.settingsFile = m_tmp.filePath(u"settings-%1.ini"_s.arg(m_counter));
        o.dataDir = m_tmp.filePath(u"data-%1"_s.arg(m_counter));
        m_app = std::make_unique<AppController>(o);
        m_app->setClock([] { return QDateTime(QDate(2026, 11, 21), QTime(10, 0)); });
        m_app->start();
        if (m_app->status() == u"onboarding")
            m_app->openVault(vault);
        QTRY_COMPARE(m_app->status(), u"ready"_s);
        if (withExtension) {
            QTRY_VERIFY(!extensions()->pageState(kPage).isEmpty());
            extensions()->approve(kPage);
            QTRY_COMPARE(extensions()->pageState(kPage), u"running"_s);
        }
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/brulion-test-bus");
    }
    void cleanup()
    {
        m_app.reset();
        brulion::ui::ThemeOverrides::instance()->setTokens({}, {});
    }

    void everySettingIsAProperty()
    {
        openApp(false);
        QSet<QString> keys;
        for (const SettingSpec &spec : AppScripting::settingSpecs()) {
            QVERIFY2(!keys.contains(spec.key), qPrintable(spec.key));
            keys.insert(spec.key);
            const QObject *target =
                    spec.onApp ? static_cast<QObject *>(m_app.get()) : static_cast<QObject *>(m_app->settings());
            const int index = target->metaObject()->indexOfProperty(spec.property.toLatin1().constData());
            QVERIFY2(index >= 0, qPrintable(spec.key + u" → "_s + spec.property));
            QVERIFY2(target->metaObject()->property(index).isWritable(), qPrintable(spec.property));
        }
        // Nothing secret or about trust.
        for (const QString &secret : {u"aiProvider"_s, u"aiBaseUrl"_s, u"nextcloudUrl"_s, u"followsOutsideLinks"_s,
                                      u"remoteControl"_s, u"language"_s})
            QVERIFY(!keys.contains(secret));
        const QVariantList rows = scripting()->settings();
        QCOMPARE(rows.size(), AppScripting::settingSpecs().size());
        QCOMPARE(rows.first().toMap().value(u"key"_s).toString(), u"theme"_s);
    }

    void settingsAreChecked_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<QString>("error");
        QTest::newRow("choice") << u"density"_s << QVariant(u"compact"_s) << QString();
        QTest::newRow("choice wrong") << u"density"_s << QVariant(u"tiny"_s) << u"one of comfortable, compact"_s;
        QTest::newRow("toggle") << u"vim"_s << QVariant(true) << QString();
        QTest::newRow("toggle as text") << u"vim"_s << QVariant(u"true"_s) << u"true or false"_s;
        QTest::newRow("number") << u"editorFontSize"_s << QVariant(qint64(18)) << QString();
        QTest::newRow("number too big") << u"editorFontSize"_s << QVariant(qint64(99)) << u"from 12 to 28"_s;
        QTest::newRow("number not whole") << u"editorFontSize"_s << QVariant(17.5) << u"whole number"_s;
        QTest::newRow("width between") << u"editorWidth"_s << QVariant(qint64(300)) << u"0 (the full width)"_s;
        QTest::newRow("factor") << u"fontScale"_s << QVariant(1.2) << QString();
        QTest::newRow("colour") << u"customAccent"_s << QVariant(u"#1F8A80"_s) << QString();
        QTest::newRow("colour wrong") << u"customAccent"_s << QVariant(u"teal"_s) << u"#rrggbb"_s;
        QTest::newRow("list") << u"sidebarHidden"_s << QVariant(QVariantList{u"graph"_s}) << QString();
        QTest::newRow("list unknown") << u"sidebarHidden"_s << QVariant(QVariantList{u"żółw"_s}) << u"holds"_s;
        QTest::newRow("five tabs") << u"bottomNav"_s << QVariant(QVariantList{u"journals"_s}) << u"exactly 5"_s;
        QTest::newRow("text") << u"uiFont"_s << QVariant(u"Inter"_s) << QString();
        QTest::newRow("text of lines") << u"uiFont"_s << QVariant(u"a\nb"_s) << u"one line"_s;
        QTest::newRow("unknown") << u"densty"_s << QVariant(u"compact"_s) << u"did you mean “density”"_s;
    }
    void settingsAreChecked()
    {
        QFETCH(QString, key);
        QFETCH(QVariant, value);
        QFETCH(QString, error);
        openApp(false);
        QString why;
        const bool ok = scripting()->setSetting(u"test"_s, key, value, &why);
        QCOMPARE(ok, error.isEmpty());
        if (!ok)
            QVERIFY2(why.contains(error), qPrintable(why));
    }

    void settingsChangeTheApp()
    {
        openApp(false);
        QString error;
        QVERIFY(scripting()->setSetting(u"test"_s, u"density"_s, u"compact"_s, &error));
        QCOMPARE(m_app->settings()->density(), u"compact"_s);
        QVERIFY(scripting()->setSetting(u"test"_s, u"statusBar"_s, false, &error));
        QCOMPARE(m_app->statusBarShown(), false);
        QCOMPARE(scripting()->setting(u"density"_s, &error).toString(), u"compact"_s);
        // An extension's change is no live change: nothing to undo in the top bar.
        QVERIFY(scripting()->changes().isEmpty());
    }

    void liveChangesAreUndone()
    {
        openApp(false);
        QSignalSpy live(scripting(), &AppScripting::liveChanged);
        QString error;
        QVERIFY(scripting()->setSetting(u"live:Claude Code"_s, u"density"_s, u"compact"_s, &error));
        QVERIFY(scripting()->setSetting(u"live:Claude Code"_s, u"editorFontSize"_s, qint64(20), &error));
        QVERIFY(scripting()->setSetting(u"live:Codex"_s, u"vim"_s, true, &error));
        brulion::extensions::ThemeLayer layer;
        QVERIFY(brulion::extensions::themeLayerFrom({{u"accent"_s, u"#123456"_s}}, &layer, &error));
        scripting()->setLiveTheme(u"live:Claude Code"_s, layer);
        QCOMPARE(brulion::ui::ThemeOverrides::instance()->tokens(false).value(u"accent"_s).toString(), u"#123456"_s);
        QVERIFY(live.count() >= 4);
        QCOMPARE(scripting()->changes().size(), 4);
        const QVariantList clients = scripting()->liveClients();
        QCOMPARE(clients.size(), 2);
        QCOMPARE(clients.first().toMap().value(u"name"_s).toString(), u"Claude Code"_s);
        QCOMPARE(clients.first().toMap().value(u"changes"_s).toInt(), 3);
        // One client's changes back, then everyone's.
        QCOMPARE(scripting()->revert(u"Claude Code"_s), 3);
        QCOMPARE(m_app->settings()->density(), u"comfortable"_s);
        QCOMPARE(m_app->settings()->editorFontSize(), 16);
        QVERIFY(brulion::ui::ThemeOverrides::instance()->tokens(false).isEmpty());
        QCOMPARE(m_app->settings()->vimMode(), true);
        QCOMPARE(scripting()->revert(), 1);
        QCOMPARE(m_app->settings()->vimMode(), false);
        QVERIFY(scripting()->liveClients().isEmpty());
    }

    void anExtensionChangesTheApp()
    {
        openApp(true);
        // Its theme is in force (the light mode's bg too).
        QTRY_COMPARE(brulion::ui::ThemeOverrides::instance()->tokens(false).value(u"accent"_s).toString(),
                     u"#1f8a80"_s);
        QCOMPARE(brulion::ui::ThemeOverrides::instance()->tokens(false).value(u"bg"_s).toString(), u"#f4ecd8"_s);
        QVERIFY(!brulion::ui::ThemeOverrides::instance()->tokens(true).contains(u"bg"_s));
        QCOMPARE(scripting()->theme().value(u"extensions"_s).toList().size(), 1);
        // A command: a setting at once, the route and the toast after the call.
        QSignalSpy toasts(m_app.get(), &AppController::toastWithActionRequested);
        extensions()->runCommand(extensions()->pageExtension(kPage), u"zwarty"_s);
        QCOMPARE(m_app->settings()->density(), u"compact"_s);
        QTRY_COMPARE(m_app->currentView(), u"calendar"_s);
        QTRY_COMPARE(toasts.count(), 1);
        QCOMPARE(toasts.first().value(1).toString(), u"Zwarty układ"_s);
        QCOMPARE(toasts.first().value(3).toString(), u"Cofnij"_s);
        // Its button runs the extension's other command.
        Q_EMIT m_app->toastActionTriggered(toasts.first().value(0).toInt());
        QTRY_COMPARE(m_app->settings()->density(), u"comfortable"_s);
    }

    void itemsShowAndRun()
    {
        openApp(true);
        const QString id = extensions()->pageExtension(kPage);
        QTRY_COMPARE(extensions()->contributions(u"toolbar"_s).size(), 1);
        const QVariantMap button = extensions()->contributions(u"toolbar"_s).first().toMap();
        for (const QString &key :
             {u"extension"_s, u"title"_s, u"slot"_s, u"id"_s, u"label"_s, u"icon"_s, u"tooltip"_s, u"tone"_s,
              u"command"_s, u"view"_s, u"params"_s, u"page"_s, u"run"_s, u"insert"_s, u"key"_s, u"live"_s})
            QVERIFY2(button.contains(key), qPrintable(key));
        QCOMPARE(button.value(u"label"_s).toString(), u"Tydzień"_s);
        QCOMPARE(button.value(u"tooltip"_s).toString(), u"Tydzień"_s); // the label when none
        QCOMPARE(button.value(u"key"_s).toString(), id + u"/tydzien"_s);
        // A view opens.
        QVERIFY(scripting()->activate(u"toolbar"_s, id, u"tydzien"_s));
        QCOMPARE(m_app->currentView(), u"extension"_s);
        QCOMPARE(m_app->currentRoute().value(u"name"_s).toString(), u"tydzien"_s);
        // A palette command runs.
        QVERIFY(scripting()->activate(u"sidebar"_s, id, u"kal"_s));
        QCOMPARE(m_app->currentView(), u"calendar"_s);
        // A page's menu: the command gets the page.
        QVERIFY(scripting()->activate(u"pageMenu"_s, id, u"strona"_s, u"Dom.md"_s));
        QTRY_VERIFY(m_app->workspace()
                            ->readFile(m_app->workspace()->journalFileFor(u"2026-11-21"_s))
                            .contains(u"ze strony Dom"_s));
        // Gone items do nothing.
        QVERIFY(!scripting()->activate(u"toolbar"_s, id, u"nie-ma"_s));
        // The route hook keeps a status bar text: the view and the one before.
        m_app->navigate({{u"view"_s, u"tasks"_s}});
        QTRY_COMPARE(label(u"statusbar"_s), u"Widok: tasks po calendar"_s);
        // Switched off, its items and theme go.
        extensions()->setPageOn(kPage, false);
        QTRY_VERIFY(extensions()->contributions(u"toolbar"_s).isEmpty());
        QTRY_VERIFY(brulion::ui::ThemeOverrides::instance()->tokens(false).isEmpty());
    }

    void liveCodeShadowsAPage()
    {
        openApp(true);
        const QString id = extensions()->pageExtension(kPage);
        QSignalSpy liveChanged(extensions(), &WorkspaceExtensions::liveChanged);
        const QVariantMap loaded = extensions()->loadLive(
                id,
                u"b.command(\"zwarty\", function() return \"na żywo\" end)\n"
                "b.app.contribute(\"toolbar\", { id = \"na-zywo\", label = \"Na żywo\", icon = \"radio\", command = \"zwarty\" })\n"_s,
                {u"app"_s}, u"Wygląd na żywo"_s, u"live:Claude Code"_s);
        QVERIFY2(loaded.value(u"ok"_s).toBool(), qPrintable(loaded.value(u"error"_s).toString()));
        QCOMPARE(loaded.value(u"commands"_s).toList().size(), 1);
        QCOMPARE(loaded.value(u"contributions"_s).toList().size(), 1);
        QCOMPARE(liveChanged.count(), 1);
        QCOMPARE(extensions()->pageState(kPage), u"live"_s);
        QCOMPARE(extensions()->liveOwner(id), u"live:Claude Code"_s);
        // The page's items and theme are gone while the live code runs.
        QTRY_COMPARE(extensions()->contributions(u"toolbar"_s).size(), 1);
        QCOMPARE(extensions()->contributions(u"toolbar"_s).first().toMap().value(u"label"_s).toString(), u"Na żywo"_s);
        QCOMPARE(extensions()->contributions(u"toolbar"_s).first().toMap().value(u"live"_s).toBool(), true);
        QTRY_VERIFY(brulion::ui::ThemeOverrides::instance()->tokens(false).isEmpty());
        // A broken version is refused with its line; the one before keeps running.
        const QVariantMap broken = extensions()->loadLive(id, u"local x = \n\nerror(\"zepsute\")"_s, {},
                                                          u"Wygląd na żywo"_s, u"live:Claude Code"_s);
        QVERIFY(!broken.value(u"ok"_s).toBool());
        QVERIFY2(broken.value(u"error"_s).toString().contains(u"zepsute"_s),
                 qPrintable(broken.value(u"error"_s).toString()));
        QCOMPARE(broken.value(u"line"_s).toInt(), 3);
        QTRY_COMPARE(extensions()->contributions(u"toolbar"_s).size(), 1);
        QCOMPARE(extensions()->liveExtensions().size(), 1);
        // Unknown permissions and bad ids are refused.
        QVERIFY(!extensions()->loadLive(id, u"x = 1"_s, {u"siec"_s}, {}, u"live:x"_s).value(u"ok"_s).toBool());
        QVERIFY(!extensions()->loadLive(u"../etc"_s, u"x = 1"_s, {}, {}, u"live:x"_s).value(u"ok"_s).toBool());
        // Out again: the page runs as before.
        QVERIFY(extensions()->unloadLive(id));
        QVERIFY(!extensions()->unloadLive(id));
        QTRY_COMPARE(extensions()->pageState(kPage), u"running"_s);
        QTRY_COMPARE(label(u"toolbar"_s), u"Tydzień"_s);
        QTRY_COMPARE(brulion::ui::ThemeOverrides::instance()->tokens(false).value(u"accent"_s).toString(),
                     u"#1f8a80"_s);
    }

    void liveCodesChangesAreTheClients()
    {
        openApp(false);
        const QVariantMap loaded = extensions()->loadLive(
                u"pomocnik"_s, u"b.command(\"zwarty\", function() b.app.set(\"density\", \"compact\") end)"_s,
                {u"app"_s}, {}, u"live:Claude Code"_s);
        QVERIFY2(loaded.value(u"ok"_s).toBool(), qPrintable(loaded.value(u"error"_s).toString()));
        extensions()->runCommand(u"pomocnik"_s, u"zwarty"_s);
        QCOMPARE(m_app->settings()->density(), u"compact"_s);
        // Noted as Claude Code's live change, undone with it.
        QCOMPARE(scripting()->changes().size(), 1);
        QCOMPARE(scripting()->changes().first().toMap().value(u"who"_s).toString(), u"Claude Code"_s);
        QCOMPARE(scripting()->revert(), 1);
        QCOMPARE(m_app->settings()->density(), u"comfortable"_s);
    }

    void evaluationsForAClient()
    {
        openApp(false);
        const QVariantMap r = extensions()->evaluateFor(u"print(\"zażółć\")\nreturn b.app.state().view"_s);
        QVERIFY2(r.value(u"ok"_s).toBool(), qPrintable(r.value(u"error"_s).toString()));
        QCOMPARE(r.value(u"value"_s).toString(), m_app->currentView());
        QCOMPARE(r.value(u"printed"_s).toStringList(), QStringList{u"zażółć"_s});
        // Globals stay for the next one, as in the console.
        QVERIFY(extensions()->evaluateFor(u"licznik = 41"_s).value(u"ok"_s).toBool());
        QCOMPARE(extensions()->evaluateFor(u"return licznik + 1"_s).value(u"value"_s).toInt(), 42);
        const QVariantMap error = extensions()->evaluateFor(u"\nerror(\"nie\")"_s);
        QVERIFY(!error.value(u"ok"_s).toBool());
        QCOMPARE(error.value(u"line"_s).toInt(), 2);
        // A question cannot be answered by a client: an error, and nothing waits.
        const QVariantMap asked = extensions()->evaluateFor(u"return b.ask(\"Imię?\")"_s);
        QVERIFY(!asked.value(u"ok"_s).toBool());
        QVERIFY(!extensions()->isWaiting());
        QVERIFY(!extensions()->evaluateFor(u"x"_s, u"nie-ma"_s).value(u"ok"_s).toBool());
    }
};

QTEST_MAIN(TestScripting)
#include "tst_scripting.moc"
