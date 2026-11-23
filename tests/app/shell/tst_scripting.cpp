// AppScripting (appscripting.h; docs/EXTENSIONS.md "The app (`b.app`)", "Live code") on a real
// AppController and notebook: the registry of settings (every key names a real property), a setting
// set, refused and journalled, an extension page's b.app over the app (a setting, a route, a toast
// with its button, the theme into ThemeOverrides, UI items and their activation), the "route" hook,
// live code that shadows a page and falls back, an evaluation for a client, and revert(); the
// notebook's own settings beside the device's (b.app.settings(), .brulion/config.json); theme pages
// (ThemePages: listed, picked as the notebook's or this device's, applied in both modes, applied
// again after an edit, their ignored lines, a page gone, a page renamed, the look exported).
#include "appcontroller.h"
#include "appscripting.h"
#include "appsettings.h"
#include "commandmodel.h"
#include "themepages.h"
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

const QString kTheme = u"Motywy/Sepia.md"_s;

// A theme page: an accent for both modes, a background for each, a misspelt token (ignored).
const QByteArray kThemePage = QByteArrayLiteral(
        "---\n"
        "tags: [motyw]\n"
        "opis: Papier i morska zieleń\n"
        "---\n"
        "Ciepły motyw.\n"
        "\n"
        "```theme\n"
        "accent: #1f8a80\n"
        "radius: 9\n"
        "bgg: #000000\n"
        "jasny:\n"
        "  bg: #f4ecd8\n"
        "ciemny:\n"
        "  bg: #14110c\n"
        "```\n");

QString overridden(bool dark, const QString &token)
{
    return brulion::ui::ThemeOverrides::instance()->tokens(dark).value(token).toString();
}

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

    QString m_vault;
    ThemePages *themes() const { return m_app->themePages(); }

    void openApp(bool withExtension, bool withTheme = false)
    {
        m_app.reset();
        const QString vault = m_tmp.filePath(u"vault-%1"_s.arg(++m_counter));
        m_vault = vault;
        QVERIFY(writeFile(vault + u"/Dom.md"_s, "# Dom\n- zadanie\n"));
        if (withExtension)
            QVERIFY(writeFile(vault + u'/' + kPage, kExtension));
        if (withTheme)
            QVERIFY(writeFile(vault + u'/' + kTheme, kThemePage));
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
        QCOMPARE(rows.size(), AppScripting::settingSpecs().size() + AppScripting::notebookSettingSpecs().size());
        QCOMPARE(rows.first().toMap().value(u"key"_s).toString(), u"theme"_s);
        QCOMPARE(rows.first().toMap().value(u"scope"_s).toString(), u"device"_s);
        // The notebook's: every key one of its settings (Workspace.config), none a device's key.
        const QVariantMap config = m_app->workspace()->vaultConfig();
        for (const SettingSpec &spec : AppScripting::notebookSettingSpecs()) {
            QVERIFY2(!keys.contains(spec.key), qPrintable(spec.key));
            keys.insert(spec.key);
            QVERIFY(spec.notebook);
            const qsizetype dot = spec.property.indexOf(u'.');
            QVERIFY2(dot < 0 ? config.contains(spec.property)
                             : config.value(spec.property.left(dot)).toMap().contains(spec.property.mid(dot + 1)),
                     qPrintable(spec.property));
        }
        QCOMPARE(rows.last().toMap().value(u"scope"_s).toString(), u"notebook"_s);
    }

    void notebookSettingsAreChecked_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<QString>("error");
        QTest::newRow("folder") << u"journalsFolder"_s << QVariant(u"Dziennik/Codzienny"_s) << QString();
        QTest::newRow("the root") << u"newPagesFolder"_s << QVariant(QString()) << QString();
        QTest::newRow("out of the notebook") << u"journalsFolder"_s << QVariant(u"../poza"_s) << u"path in the notebook"_s;
        QTest::newRow("hidden folder") << u"templatesFolder"_s << QVariant(u".git"_s) << u"path in the notebook"_s;
        QTest::newRow("folder as a number") << u"journalsFolder"_s << QVariant(qint64(3)) << u"path in the notebook"_s;
        QTest::newRow("format") << u"journalFileFormat"_s << QVariant(u"dd.MM.yyyy"_s) << QString();
        QTest::newRow("format of one file") << u"journalFileFormat"_s << QVariant(u"yyyy"_s) << u"cannot be"_s;
        QTest::newRow("time") << u"defaultReminderTime"_s << QVariant(u"8:30"_s) << QString();
        QTest::newRow("no time") << u"defaultReminderTime"_s << QVariant(u"25:00"_s) << u"cannot be"_s;
        QTest::newRow("week") << u"weekStartsOn"_s << QVariant(u"sunday"_s) << QString();
        QTest::newRow("week in Polish") << u"weekStartsOn"_s << QVariant(u"niedziela"_s) << u"one of monday"_s;
        QTest::newRow("mode") << u"journalMode"_s << QVariant(u"document"_s) << QString();
        QTest::newRow("cloze") << u"flashcards.clozeBold"_s << QVariant(true) << QString();
        QTest::newRow("cloze as text") << u"flashcards.clozeBold"_s << QVariant(u"tak"_s) << u"true or false"_s;
        QTest::newRow("separator") << u"flashcards.singleLineSeparator"_s << QVariant(u" :: "_s) << QString();
        QTest::newRow("tags") << u"flashcards.tags"_s << QVariant(u"fiszki, słówka"_s) << QString();
        QTest::newRow("no theme page") << u"themePage"_s << QVariant(u"Dom.md"_s) << u"theme page"_s;
        QTest::newRow("no theme") << u"themePage"_s << QVariant(QString()) << QString();
        QTest::newRow("misspelt") << u"journalFolder"_s << QVariant(u"x"_s) << u"did you mean “journalsFolder”"_s;
    }
    void notebookSettingsAreChecked()
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
        else if (key != u"defaultReminderTime") // (read back as "08:30")
            QCOMPARE(scripting()->setting(key, &why), value);
    }

    void notebookSettingsChangeTheNotebook()
    {
        openApp(false);
        QString error;
        QCOMPARE(scripting()->setting(u"journalsFolder"_s, &error).toString(), u"journals"_s);
        QCOMPARE(scripting()->setting(u"weekStartsOn"_s, &error).toString(), u"monday"_s);
        QCOMPARE(scripting()->setting(u"flashcards.clozeHighlights"_s, &error).toBool(), true);
        // From Luau, as an extension with the permission "app" (the console's: the user's own).
        const QVariantMap r = extensions()->evaluateFor(
                u"b.app.set(\"journalsFolder\", \"Dziennik\")\n"
                "b.app.set(\"weekStartsOn\", \"sunday\")\n"
                "b.app.set(\"flashcards.tags\", \"fiszki, słówka\")\n"
                "local n = 0\n"
                "for _, s in b.app.settings() do if s.scope == \"notebook\" then n += 1 end end\n"
                "return `{b.app.get(\"journalsFolder\")} {n}`"_s);
        QVERIFY2(r.value(u"ok"_s).toBool(), qPrintable(r.value(u"error"_s).toString()));
        QCOMPARE(r.value(u"value"_s).toString(),
                 u"Dziennik %1"_s.arg(AppScripting::notebookSettingSpecs().size()));
        const QVariantMap config = m_app->workspace()->vaultConfig();
        QCOMPARE(config.value(u"journalsFolder"_s).toString(), u"Dziennik"_s);
        QCOMPARE(config.value(u"weekStartsOn"_s).toInt(), 7);
        QCOMPARE(config.value(u"flashcards"_s).toMap().value(u"tags"_s).toString(), u"fiszki, słówka"_s);
        QFile file(m_vault + u"/.brulion/config.json"_s);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray json = file.readAll();
        QVERIFY(json.contains("\"journalsFolder\": \"Dziennik\""));
        QVERIFY(json.contains("\"weekStartsOn\": 0")); // (the file's convention: 0 is Sunday)
        // A live client's change is undone with the rest of its changes.
        QVERIFY(scripting()->setSetting(u"live:Claude Code"_s, u"pageMode"_s, u"outline"_s, &error));
        QCOMPARE(m_app->workspace()->vaultConfig().value(u"pageMode"_s).toString(), u"outline"_s);
        QCOMPARE(scripting()->revert(u"Claude Code"_s), 1);
        QCOMPARE(m_app->workspace()->vaultConfig().value(u"pageMode"_s).toString(), u"document"_s);
        // What the notebook refuses (a date format that names one file for every day) changes nothing.
        QVERIFY(!scripting()->setSetting(u"test"_s, u"journalFileFormat"_s, u"MM"_s, &error));
        QCOMPARE(m_app->workspace()->vaultConfig().value(u"journalFileFormat"_s).toString(), u"yyyy-MM-dd"_s);
    }

    void themePagesApply()
    {
        openApp(false, true);
        QTRY_COMPARE(themes()->count(), 1);
        const QVariantMap row = themes()->pages().first().toMap();
        QCOMPARE(row.value(u"title"_s).toString(), u"Sepia"_s);
        QCOMPARE(row.value(u"description"_s).toString(), u"Papier i morska zieleń"_s);
        QCOMPARE(row.value(u"issues"_s).toInt(), 1);
        QCOMPARE(row.value(u"tokens"_s).toInt(), 4);
        QCOMPARE(row.value(u"lightBg"_s).toString(), u"#f4ecd8"_s);
        QCOMPARE(row.value(u"darkAccent"_s).toString(), u"#1f8a80"_s);
        QVERIFY(!row.value(u"current"_s).toBool());
        // The page's chip: not in use, the misspelt line ignored and why.
        QVERIFY(themes()->isThemePage(kTheme));
        QVERIFY(!themes()->pageApplied(kTheme));
        QCOMPARE(themes()->pageIssueLine(kTheme), 9);
        QVERIFY2(themes()->pageIssueText(kTheme).contains(u"bgg"_s), qPrintable(themes()->pageIssueText(kTheme)));
        QCOMPARE(themes()->pageState(kTheme).value(u"issues"_s).toList().size(), 1);
        QVERIFY(themes()->pageState(u"Dom.md"_s).isEmpty());
        QVERIFY(overridden(false, u"accent"_s).isEmpty());

        // Picked: the notebook's choice, in force in both modes.
        QVERIFY(!themes()->pick(u"Dom.md"_s));
        QSignalSpy changed(themes(), &ThemePages::changed);
        QVERIFY(themes()->pick(kTheme));
        QVERIFY(changed.count() >= 1);
        QCOMPARE(m_app->workspace()->vaultConfig().value(u"themePage"_s).toString(), kTheme);
        QCOMPARE(themes()->current(), kTheme);
        QVERIFY(themes()->pageApplied(kTheme));
        QCOMPARE(overridden(false, u"accent"_s), u"#1f8a80"_s);
        QCOMPARE(overridden(true, u"accent"_s), u"#1f8a80"_s);
        QCOMPARE(overridden(false, u"bg"_s), u"#f4ecd8"_s);
        QCOMPARE(overridden(true, u"bg"_s), u"#14110c"_s);
        QCOMPARE(brulion::ui::ThemeOverrides::instance()->tokens(false).value(u"radius"_s).toDouble(), 9.0);
        QVERIFY(!brulion::ui::ThemeOverrides::instance()->tokens(false).contains(u"bgg"_s));
        QCOMPARE(scripting()->theme().value(u"page"_s).toMap().value(u"path"_s).toString(), kTheme);
        QString error;
        QCOMPARE(scripting()->setting(u"themePage"_s, &error).toString(), kTheme);

        // Edited while in force: applied again.
        QByteArray edited = kThemePage;
        edited.replace("accent: #1f8a80", "accent: #c4422f");
        QVERIFY(m_app->workspace()->write(kTheme, QString::fromUtf8(edited)));
        QTRY_COMPARE(overridden(false, u"accent"_s), u"#c4422f"_s);
        QCOMPARE(overridden(true, u"bg"_s), u"#14110c"_s);

        // This device's own choice: none here, the notebook's stays.
        themes()->setDeviceOnly(true);
        QVERIFY(themes()->deviceOnly());
        QCOMPARE(themes()->current(), kTheme); // switched on, it keeps what shows
        QVERIFY(themes()->pick(QString()));
        QVERIFY(overridden(false, u"accent"_s).isEmpty());
        QCOMPARE(themes()->notebookTheme(), kTheme);
        QCOMPARE(m_app->workspace()->vaultConfig().value(u"themePage"_s).toString(), kTheme);
        themes()->setDeviceOnly(false);
        QCOMPARE(overridden(false, u"accent"_s), u"#c4422f"_s);

        // The notebook's choice from Luau's b.app.set: none.
        QVERIFY(scripting()->setSetting(u"test"_s, u"themePage"_s, QString(), &error));
        QVERIFY(themes()->current().isEmpty());
        QVERIFY(overridden(false, u"accent"_s).isEmpty());
        QVERIFY(scripting()->setSetting(u"test"_s, u"themePage"_s, kTheme, &error));
        QCOMPARE(overridden(false, u"accent"_s), u"#c4422f"_s);

        // A page gone (deleted elsewhere, not synced yet): nothing applies, and Settings says so.
        QVERIFY(m_app->workspace()->setConfig({{u"themePage"_s, u"Motywy/Nie ma.md"_s}}));
        QVERIFY(themes()->missing());
        QVERIFY(overridden(false, u"accent"_s).isEmpty());
        QVERIFY(m_app->workspace()->setConfig({{u"themePage"_s, kTheme}}));
        QVERIFY(!themes()->missing());

        // Renamed: still the notebook's theme.
        const QString renamed = m_app->workspace()->renamePage(kTheme, u"Sepia ciepła"_s);
        QCOMPARE(renamed, u"Motywy/Sepia ciepła.md"_s);
        QTRY_COMPARE(themes()->current(), renamed);
        QTRY_COMPARE(overridden(false, u"accent"_s), u"#c4422f"_s);
        QVERIFY(!themes()->missing());
    }

    void theLookIsExported()
    {
        openApp(false);
        m_app->settings()->setAccentColor(u"teal"_s);
        m_app->settings()->setDarkSurface(u"black"_s);
        QTRY_VERIFY(m_app->workspace()->ready());
        const QString path = themes()->exportCurrent();
        QVERIFY(!path.isEmpty());
        QVERIFY2(path.startsWith(u"Motywy/"_s) || path.startsWith(u"Themes/"_s), qPrintable(path));
        QTRY_COMPARE(themes()->count(), 1);
        QVERIFY(themes()->isThemePage(path));
        QCOMPARE(themes()->pageIssueCount(path), 0);
        // The look it shows: the black surface in the dark, the teal accent; picked, nothing changes.
        const QString content = m_app->workspace()->read(path);
        QVERIFY(content.contains(u"```theme\n"_s));
        QVERIFY(content.contains(u"tags: [motyw]"_s) || content.contains(u"tags: [theme]"_s));
        QVERIFY(themes()->pick(path));
        QCOMPARE(overridden(true, u"bg"_s), u"#000000"_s);
        QVERIFY(!overridden(false, u"accent"_s).isEmpty());
        brulion::ui::ThemeColors light, dark;
        dark.setDark(true);
        for (brulion::ui::ThemeColors *c : {&light, &dark}) {
            c->setLightSurface(u"paper"_s);
            c->setDarkSurface(u"graphite"_s);
            c->setAccentName(u"coral"_s);
        }
        // Over the default settings the page shows the exported look (teal on black).
        QCOMPARE(dark.bg(), u"#000000"_s);
        QCOMPARE(dark.accent(), brulion::ui::paletteMap(true, u"paper", u"black", u"teal", u"").value(u"accent"_s).toString());
        QCOMPARE(light.accentText(),
                 brulion::ui::paletteMap(false, u"paper", u"black", u"teal", u"").value(u"accentText"_s).toString());
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
