// Obsidian vault settings as Brulion's defaults: the moment.js → Qt date format conversion,
// adoptObsidianSettings() and the Vault taking them while .brulion/config.json is missing.
#include "vault/obsidiansettings.h"
#include "vaulttest.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QtTest>

Q_DECLARE_METATYPE(std::optional<QByteArray>)

using namespace brulion;
using namespace vaulttest;
using namespace Qt::StringLiterals;

namespace {

/// "journalsFolder=Dziennik|journalFileFormat=dd.MM.yyyy|…" — the adoptable fields of `c`.
QString fields(const VaultConfig &c)
{
    return u"journals=%1|format=%2|template=%3|templates=%4|attachments=%5"_s.arg(
            c.journalsFolder, c.journalFileFormat, c.journalTemplate, c.templatesFolder, c.attachmentsFolder);
}

std::optional<QByteArray> bytes(const char *json)
{
    return json ? std::optional<QByteArray>(QByteArray(json)) : std::nullopt;
}

} // namespace

class TestVaultObsidian : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- qtDateFormatFromMoment ----------------------------------------------------------
    void momentToQt_data()
    {
        QTest::addColumn<QString>("moment");
        QTest::addColumn<QString>("qt");      // empty = not convertible
        QTest::addColumn<QString>("example"); // QDate(2026, 10, 7).toString(qt)
        QTest::newRow("iso") << u"YYYY-MM-DD"_s << u"yyyy-MM-dd"_s << u"2026-10-07"_s;
        QTest::newRow("polish") << u"DD.MM.YYYY"_s << u"dd.MM.yyyy"_s << u"07.10.2026"_s;
        QTest::newRow("nested") << u"YYYY/MM/YYYY-MM-DD"_s << u"yyyy/MM/yyyy-MM-dd"_s << u"2026/10/2026-10-07"_s;
        QTest::newRow("short") << u"D.M.YY"_s << u"d.M.yy"_s << u"7.10.26"_s;
        QTest::newRow("compact") << u"YYYYMMDD"_s << u"yyyyMMdd"_s << u"20261007"_s;
        QTest::newRow("month name") << u"D MMMM YYYY"_s << u"d MMMM yyyy"_s << u"7 October 2026"_s;
        QTest::newRow("short names") << u"ddd, D MMM YYYY"_s << u"ddd, d MMM yyyy"_s << u"Wed, 7 Oct 2026"_s;
        QTest::newRow("weekday") << u"dddd, DD.MM.YYYY"_s << u"dddd, dd.MM.yyyy"_s << u"Wednesday, 07.10.2026"_s;
        QTest::newRow("literal") << u"[Dzień] YYYY-MM-DD"_s << u"'Dzień' yyyy-MM-dd"_s << u"Dzień 2026-10-07"_s;
        QTest::newRow("literal letters") << u"[Notatka z] DD.MM.YY"_s << u"'Notatka z' dd.MM.yy"_s
                                         << u"Notatka z 07.10.26"_s;
        QTest::newRow("backslash escape") << u"YYYY-MM-DD \\Ż"_s << u"yyyy-MM-dd 'Ż'"_s << u"2026-10-07 Ż"_s;
        QTest::newRow("empty literal") << u"YYYY[]-MM-DD"_s << u"yyyy-MM-dd"_s << u"2026-10-07"_s;
        // Adjacent literals form one quoted run ("'a''b'" would write a "'").
        QTest::newRow("adjacent literals") << u"[Dzień][ z] DD.MM.YYYY"_s << u"'Dzień z' dd.MM.yyyy"_s
                                           << u"Dzień z 07.10.2026"_s;
        QTest::newRow("adjacent escapes") << u"\\D\\z DD.MM.YYYY"_s << u"'Dz' dd.MM.yyyy"_s << u"Dz 07.10.2026"_s;
        QTest::newRow("literal then escape") << u"[Ł]\\ó YYYY-MM-DD"_s << u"'Łó' yyyy-MM-dd"_s << u"Łó 2026-10-07"_s;
        QTest::newRow("literals apart") << u"[a]-[b] YYYY-MM-DD"_s << u"'a'-'b' yyyy-MM-dd"_s << u"a-b 2026-10-07"_s;
        QTest::newRow("ordinal") << u"Do MMMM YYYY"_s << QString() << QString();
        QTest::newRow("iso week") << u"gggg-[W]ww"_s << QString() << QString();
        QTest::newRow("week number") << u"YYYY-[W]WW"_s << QString() << QString();
        QTest::newRow("time") << u"YYYY-MM-DD HH:mm"_s << QString() << QString();
        QTest::newRow("unix") << u"X"_s << QString() << QString();
        QTest::newRow("day of year") << u"YYYY-DDDD"_s << QString() << QString();
        QTest::newRow("weekday number") << u"YYYY-MM-DD d"_s << QString() << QString();
        QTest::newRow("quarter") << u"YYYY-Q"_s << QString() << QString();
        QTest::newRow("one Y") << u"Y-MM-DD"_s << QString() << QString();
        QTest::newRow("unknown letter") << u"YYYY-MM-DD-ź"_s << QString() << QString();
        QTest::newRow("quote in literal") << u"[l'été] YYYY-MM-DD"_s << QString() << QString();
        QTest::newRow("bare quote") << u"YYYY'MM'DD"_s << QString() << QString();
        QTest::newRow("unclosed literal") << u"[niezamknięty YYYY"_s << QString() << QString();
        QTest::newRow("empty") << QString() << QString() << QString();
    }

    void momentToQt()
    {
        QFETCH(QString, moment);
        QFETCH(QString, qt);
        QFETCH(QString, example);
        const std::optional<QString> converted = qtDateFormatFromMoment(moment);
        if (qt.isEmpty()) {
            QVERIFY2(!converted, qPrintable(converted.value_or(QString())));
            return;
        }
        QVERIFY(converted);
        QCOMPARE(*converted, qt);
        QCOMPARE(QDate(2026, 10, 7).toString(*converted), example);
        // Read back as the journal reads file names (markdown/journal.cpp).
        QCOMPARE(QLocale::c().toDate(example, *converted, 2000), QDate(2026, 10, 7));
    }

    // ---- adoptObsidianSettings -----------------------------------------------------------
    void adopt_data()
    {
        QTest::addColumn<std::optional<QByteArray>>("dailyNotes");
        QTest::addColumn<std::optional<QByteArray>>("app");
        QTest::addColumn<std::optional<QByteArray>>("templates");
        QTest::addColumn<std::optional<QByteArray>>("corePlugins");
        QTest::addColumn<QStringList>("tracked");
        QTest::addColumn<QString>("expected");     // fields() of the result
        QTest::addColumn<QStringList>("adopted");
        QTest::addColumn<int>("warnings");

        const QString defaults = fields(VaultConfig{});
        const auto none = std::optional<QByteArray>();
        const QStringList noFiles;
        const QString root = u"journals=|format=yyyy-MM-dd|template=templates/journal.md|templates=templates|attachments=assets"_s;

        QTest::newRow("nothing") << none << none << none << none << noFiles << defaults << QStringList() << 0;
        QTest::newRow("full daily notes")
                << bytes(R"({"folder":"Dziennik/","format":"DD.MM.YYYY","template":"Szablony/Dzień"})") << none
                << none << none << noFiles
                << u"journals=Dziennik|format=dd.MM.yyyy|template=Szablony/Dzień.md|templates=templates|attachments=assets"_s
                << QStringList{u"journalsFolder"_s, u"journalFileFormat"_s, u"journalTemplate"_s} << 0;
        QTest::newRow("folder with leading slash")
                << bytes(R"({"folder":"/Życie/Dziennik","format":"YYYY-MM-DD"})") << none << none << none << noFiles
                << u"journals=Życie/Dziennik|format=yyyy-MM-dd|template=|templates=templates|attachments=assets"_s
                << QStringList{u"journalsFolder"_s, u"journalTemplate"_s} << 0;
        QTest::newRow("root as slash") << bytes(R"({"folder":"/","format":"","template":""})") << none << none
                                       << none << noFiles
                                       << u"journals=|format=yyyy-MM-dd|template=|templates=templates|attachments=assets"_s
                                       << QStringList{u"journalsFolder"_s, u"journalTemplate"_s} << 0;
        QTest::newRow("empty object") << bytes("{}") << none << none << none << noFiles
                                      << u"journals=|format=yyyy-MM-dd|template=|templates=templates|attachments=assets"_s
                                      << QStringList{u"journalsFolder"_s, u"journalTemplate"_s} << 0;
        QTest::newRow("template with extension")
                << bytes(R"({"folder":"Dziennik","template":"Szablony/Dzień.md"})") << none << none << none
                << noFiles << u"journals=Dziennik|format=yyyy-MM-dd|template=Szablony/Dzień.md|templates=templates|attachments=assets"_s
                << QStringList{u"journalsFolder"_s, u"journalTemplate"_s} << 0;
        QTest::newRow("no template keeps Brulion's own file")
                << bytes(R"({"folder":"Dziennik"})") << none << none << none
                << QStringList{u"templates/journal.md"_s}
                << u"journals=Dziennik|format=yyyy-MM-dd|template=templates/journal.md|templates=templates|attachments=assets"_s
                << QStringList{u"journalsFolder"_s} << 0;
        QTest::newRow("month names warn")
                << bytes(R"({"folder":"Dziennik","format":"D MMMM YYYY"})") << none << none << none << noFiles
                << u"journals=Dziennik|format=d MMMM yyyy|template=|templates=templates|attachments=assets"_s
                << QStringList{u"journalsFolder"_s, u"journalFileFormat"_s, u"journalTemplate"_s} << 1;
        QTest::newRow("unconvertible format")
                << bytes(R"({"folder":"Dziennik","format":"Do MMMM YYYY"})") << none << none << none << noFiles
                << u"journals=Dziennik|format=yyyy-MM-dd|template=|templates=templates|attachments=assets"_s
                << QStringList{u"journalsFolder"_s, u"journalTemplate"_s} << 1;
        QTest::newRow("format without a year")
                << bytes(R"({"folder":"Dziennik","format":"DD.MM"})") << none << none << none << noFiles
                << u"journals=Dziennik|format=yyyy-MM-dd|template=|templates=templates|attachments=assets"_s
                << QStringList{u"journalsFolder"_s, u"journalTemplate"_s} << 1;
        QTest::newRow("invalid JSON") << bytes("{ zepsuty") << none << none << none << noFiles << defaults
                                      << QStringList() << 1;
        QTest::newRow("JSON array") << bytes(R"(["Dziennik"])") << none << none << none << noFiles << defaults
                                    << QStringList() << 1;
        // A value of the wrong type is skipped: Brulion's value stays (not Obsidian's default).
        QTest::newRow("wrong types")
                << bytes(R"({"folder":5,"format":true,"template":["a"]})") << none << none << none << noFiles
                << defaults << QStringList() << 3;
        QTest::newRow("wrong type of the folder only")
                << bytes(R"({"folder":{"a":1},"format":"DD.MM.YYYY","template":""})") << none << none << none
                << noFiles << u"journals=journals|format=dd.MM.yyyy|template=|templates=templates|attachments=assets"_s
                << QStringList{u"journalFileFormat"_s, u"journalTemplate"_s} << 1;
        QTest::newRow("BOM") << bytes("\xEF\xBB\xBF{\"folder\":\"Dziennik\",\"format\":\"YYYY-MM-DD\"}") << none
                             << none << none << noFiles
                             << u"journals=Dziennik|format=yyyy-MM-dd|template=|templates=templates|attachments=assets"_s
                             << QStringList{u"journalsFolder"_s, u"journalTemplate"_s} << 0;
        QTest::newRow("guard: Brulion's journals")
                << bytes(R"({"folder":"Dziennik","format":"DD.MM.YYYY","template":"Szablony/Dzień"})") << none
                << none << none << QStringList{u"journals/2026-10-01.md"_s} << defaults << QStringList() << 0;
        QTest::newRow("guard: case of the folder")
                << bytes(R"({"folder":"Dziennik"})") << none << none << none
                << QStringList{u"Journals/2026-10-01.md"_s} << defaults << QStringList() << 0;
        // Obsidian's own days in Brulion's folder (any letter case) are no sign of Brulion's use.
        QTest::newRow("Obsidian's days in journals/")
                << bytes(R"({"folder":"journals","format":"DD.MM.YYYY","template":"Szablony/Dzień"})") << none
                << none << none << QStringList{u"journals/26.10.2026.md"_s}
                << u"journals=journals|format=dd.MM.yyyy|template=Szablony/Dzień.md|templates=templates|attachments=assets"_s
                << QStringList{u"journalFileFormat"_s, u"journalTemplate"_s} << 0;
        QTest::newRow("Obsidian's days in Journals/")
                << bytes(R"({"folder":"Journals"})") << none << none << none
                << QStringList{u"Journals/2026-10-01.md"_s}
                << u"journals=Journals|format=yyyy-MM-dd|template=|templates=templates|attachments=assets"_s
                << QStringList{u"journalsFolder"_s, u"journalTemplate"_s} << 0;
        QTest::newRow("guard: Brulion's days next to Obsidian's subfolder")
                << bytes(R"({"folder":"journals/Obsidian","format":"DD.MM.YYYY"})") << none << none << none
                << QStringList{u"journals/Obsidian/26.10.2026.md"_s, u"journals/2026-10-01.md"_s} << defaults
                << QStringList() << 0;
        QTest::newRow("guard: wrong folder type")
                << bytes(R"({"folder":7,"format":"DD.MM.YYYY"})") << none << none << none
                << QStringList{u"journals/2026-10-01.md"_s} << defaults << QStringList() << 0;
        QTest::newRow("guard ignores other files")
                << bytes(R"({"folder":"Dziennik"})") << none << none << none
                << QStringList{u"journals/zdjęcie.png"_s, u"journalsy/a.md"_s}
                << u"journals=Dziennik|format=yyyy-MM-dd|template=|templates=templates|attachments=assets"_s
                << QStringList{u"journalsFolder"_s, u"journalTemplate"_s} << 0;

        // No daily-notes.json: Obsidian's defaults only with a day at the top of the notebook.
        QTest::newRow("day at the top") << none << none << none << none
                                        << QStringList{u"2026-10-27.md"_s, u"Zakupy.md"_s} << root
                                        << QStringList{u"journalsFolder"_s} << 0;
        QTest::newRow("no day at the top") << none << none << none << none
                                           << QStringList{u"Zakupy.md"_s, u"Archiwum/2026-10-27.md"_s} << defaults
                                           << QStringList() << 0;
        QTest::newRow("plugin off (object)") << none << none << none << bytes(R"({"daily-notes":false,"templates":true})")
                                             << QStringList{u"2026-10-27.md"_s} << defaults << QStringList() << 0;
        QTest::newRow("plugin on (object)") << none << none << none << bytes(R"({"daily-notes":true})")
                                            << QStringList{u"2026-10-27.md"_s} << root
                                            << QStringList{u"journalsFolder"_s} << 0;
        QTest::newRow("plugin off (array)") << none << none << none << bytes(R"(["file-explorer","templates"])")
                                            << QStringList{u"2026-10-27.md"_s} << defaults << QStringList() << 0;
        QTest::newRow("plugin on (array)") << none << none << none << bytes(R"(["file-explorer","daily-notes"])")
                                           << QStringList{u"2026-10-27.md"_s} << root
                                           << QStringList{u"journalsFolder"_s} << 0;
        QTest::newRow("invalid core plugins") << none << none << none << bytes("nie JSON")
                                              << QStringList{u"2026-10-27.md"_s} << root
                                              << QStringList{u"journalsFolder"_s} << 1;
        QTest::newRow("day at the top, Brulion's journals") << none << none << none << none
                                                            << QStringList{u"2026-10-27.md"_s, u"journals/2026-10-26.md"_s}
                                                            << defaults << QStringList() << 0;

        // Attachments.
        const QString top = u"journals=journals|format=yyyy-MM-dd|template=templates/journal.md|templates=templates|attachments="_s;
        QTest::newRow("attachments slash") << none << bytes(R"({"attachmentFolderPath":"/"})") << none << none
                                           << noFiles << top << QStringList{u"attachmentsFolder"_s} << 0;
        QTest::newRow("attachments empty") << none << bytes(R"({"attachmentFolderPath":""})") << none << none
                                           << noFiles << top << QStringList{u"attachmentsFolder"_s} << 0;
        QTest::newRow("attachments key missing") << none << bytes(R"({"promptDelete":false})") << none << none
                                                 << noFiles << top << QStringList{u"attachmentsFolder"_s} << 0;
        QTest::newRow("attachments folder") << none << bytes(R"({"attachmentFolderPath":"Załączniki/"})") << none
                                            << none << noFiles << top + u"Załączniki"_s
                                            << QStringList{u"attachmentsFolder"_s} << 0;
        QTest::newRow("attachments next to the note") << none << bytes(R"({"attachmentFolderPath":"./"})") << none
                                                      << none << noFiles << defaults << QStringList() << 1;
        QTest::newRow("attachments in a subfolder of the note")
                << none << bytes(R"({"attachmentFolderPath":"./obrazy"})") << none << none << noFiles << defaults
                << QStringList() << 1;
        QTest::newRow("attachments wrong type") << none << bytes(R"({"attachmentFolderPath":null})") << none << none
                                                << noFiles << top << QStringList{u"attachmentsFolder"_s} << 0;
        QTest::newRow("guard: Brulion's assets") << none << bytes(R"({"attachmentFolderPath":"Załączniki"})") << none
                                                 << none << QStringList{u"assets/zdjęcie.png"_s} << defaults
                                                 << QStringList() << 0;
        QTest::newRow("Obsidian's attachments in Assets/")
                << none << bytes(R"({"attachmentFolderPath":"Assets"})") << none << none
                << QStringList{u"Assets/żaba.png"_s} << top + u"Assets"_s << QStringList{u"attachmentsFolder"_s} << 0;
        QTest::newRow("attachments number") << none << bytes(R"({"attachmentFolderPath":3})") << none << none
                                            << noFiles << defaults << QStringList() << 1;
        QTest::newRow("app.json invalid") << none << bytes("[1, 2") << none << none << noFiles << defaults
                                          << QStringList() << 1;

        // Templates folder.
        QTest::newRow("templates folder") << none << none << bytes(R"({"folder":"Szablony"})") << none << noFiles
                                          << u"journals=journals|format=yyyy-MM-dd|template=templates/journal.md|templates=Szablony|attachments=assets"_s
                                          << QStringList{u"templatesFolder"_s} << 0;
        QTest::newRow("templates folder unset") << none << none << bytes(R"({"folder":""})") << none << noFiles
                                                << defaults << QStringList() << 0;
        QTest::newRow("guard: Brulion's templates") << none << none << bytes(R"({"folder":"Szablony"})") << none
                                                    << QStringList{u"templates/spotkanie.md"_s} << defaults
                                                    << QStringList() << 0;

        QTest::newRow("Obsidian's templates in Templates/")
                << none << none << bytes(R"({"folder":"Templates"})") << none
                << QStringList{u"Templates/Spotkanie.md"_s}
                << u"journals=journals|format=yyyy-MM-dd|template=templates/journal.md|templates=Templates|attachments=assets"_s
                << QStringList{u"templatesFolder"_s} << 0;

        QTest::newRow("everything")
                << bytes(R"({"folder":"Dziennik","format":"YYYY/MM/DD.MM.YYYY","template":"Szablony/Dzień"})")
                << bytes(R"({"attachmentFolderPath":"Załączniki","newFileLocation":"folder"})")
                << bytes(R"({"folder":"Szablony"})") << bytes(R"({"daily-notes":true})")
                << QStringList{u"Dziennik/2026/10/27.10.2026.md"_s, u"Załączniki/żaba.jpg"_s}
                << u"journals=Dziennik|format=yyyy/MM/dd.MM.yyyy|template=Szablony/Dzień.md|templates=Szablony|attachments=Załączniki"_s
                << QStringList{u"journalsFolder"_s, u"journalFileFormat"_s, u"journalTemplate"_s,
                               u"templatesFolder"_s, u"attachmentsFolder"_s}
                << 0;
        QTest::newRow("same as Brulion's defaults")
                << bytes(R"({"folder":"journals","format":"YYYY-MM-DD","template":"templates/journal"})")
                << bytes(R"({"attachmentFolderPath":"assets"})") << bytes(R"({"folder":"templates"})") << none
                << noFiles << defaults << QStringList() << 0;
    }

    void adopt()
    {
        QFETCH(std::optional<QByteArray>, dailyNotes);
        QFETCH(std::optional<QByteArray>, app);
        QFETCH(std::optional<QByteArray>, templates);
        QFETCH(std::optional<QByteArray>, corePlugins);
        QFETCH(QStringList, tracked);
        QFETCH(QString, expected);
        QFETCH(QStringList, adopted);
        QFETCH(int, warnings);
        const ObsidianAdoption a = adoptObsidianSettings({dailyNotes, app, templates, corePlugins}, tracked);
        QCOMPARE(fields(a.config), expected);
        QCOMPARE(a.adoptedKeys, adopted);
        QVERIFY2(a.warnings.size() == warnings, qPrintable(a.warnings.join(u" | "_s)));
        // Fields Obsidian has no say in stay as they were.
        QCOMPARE(a.config.newPagesFolder, VaultConfig{}.newPagesFolder);
        QCOMPARE(a.config.defaultReminderTime, VaultConfig{}.defaultReminderTime);
        QCOMPARE(a.config.weekStartsOn, VaultConfig{}.weekStartsOn);
    }

    void dependsOn_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("depends");
        QTest::newRow("day at the top") << u"2026-10-27.md"_s << true;
        QTest::newRow("journals") << u"journals/2026-10-27.md"_s << true;
        QTest::newRow("Journals") << u"Journals/Październik/27.10.2026.md"_s << true;
        QTest::newRow("assets") << u"assets/żaba.png"_s << true;
        QTest::newRow("templates") << u"templates/journal.md"_s << true;
        QTest::newRow("other folder") << u"Dziennik/27.10.2026.md"_s << false;
        QTest::newRow("prefix only") << u"journalsy/a.md"_s << false;
    }

    void dependsOn()
    {
        QFETCH(QString, path);
        QFETCH(bool, depends);
        QCOMPARE(obsidianAdoptionDependsOn(path), depends);
    }

    void adoptKeepsOtherBaseFields()
    {
        VaultConfig base;
        base.weekStartsOn = Qt::Sunday;
        base.newPagesFolder = u"Strony"_s;
        const ObsidianAdoption a = adoptObsidianSettings({bytes(R"({"folder":"Dziennik"})"), {}, {}, {}}, {}, base);
        QCOMPARE(a.config.journalsFolder, u"Dziennik"_s);
        QCOMPARE(a.config.weekStartsOn, Qt::Sunday);
        QCOMPARE(a.config.newPagesFolder, u"Strony"_s);
    }

    // ---- Vault ---------------------------------------------------------------------------
    void vaultAdoptsWithoutConfigJson()
    {
        Fixture f;
        const QByteArray daily = R"({"folder":"Dziennik","format":"DD.MM.YYYY","template":"Szablony/Dzień"})";
        const QByteArray app = R"({"attachmentFolderPath":"Załączniki"})";
        f.write(u".obsidian/daily-notes.json"_s, daily);
        f.write(u".obsidian/app.json"_s, app);
        f.write(u".obsidian/workspace.json"_s, "{}");
        f.write(u"Dziennik/27.10.2026.md"_s, "- dzień");
        QVERIFY(f.open());
        QCOMPARE(fields(f.vault->config()),
                 u"journals=Dziennik|format=dd.MM.yyyy|template=Szablony/Dzień.md|templates=templates|attachments=Załączniki"_s);
        QCOMPARE(f.vault->configAdoptedFromObsidian(),
                 (QStringList{u"journalsFolder"_s, u"journalFileFormat"_s, u"journalTemplate"_s, u"attachmentsFolder"_s}));
        QVERIFY(f.vault->configWarnings().isEmpty());
        // Nothing written: no config.json, Obsidian's files untouched and not listed.
        QVERIFY(!f.vault->exists(vaultConfigPath()));
        QCOMPARE(diskFiles(f.root()), (QStringList{u".obsidian/app.json"_s, u".obsidian/daily-notes.json"_s,
                                                   u".obsidian/workspace.json"_s, u"Dziennik/27.10.2026.md"_s}));
        QCOMPARE(f.read(u".obsidian/daily-notes.json"_s), daily);
        QCOMPARE(f.read(u".obsidian/app.json"_s), app);
        QCOMPARE(f.files(), QStringList{u"Dziennik/27.10.2026.md"_s});
        QVERIFY(f.vault->rescan().isEmpty());
        QVERIFY(f.changes().isEmpty());
    }

    void configJsonWins()
    {
        Fixture f;
        f.write(u".obsidian/daily-notes.json"_s, R"({"folder":"Dziennik"})");
        f.write(u".brulion/config.json"_s, R"({"weekStartsOn":0})");
        QVERIFY(f.open());
        QVERIFY(f.vault->configAdoptedFromObsidian().isEmpty());
        QCOMPARE(f.vault->config().journalsFolder, u"journals"_s);
        QCOMPARE(f.vault->config().weekStartsOn, Qt::Sunday);
    }

    void noObsidianFolderNoAdoption()
    {
        Fixture f;
        f.write(u"2026-10-27.md"_s, "- dzień"); // a day at the top, but no .obsidian/
        QVERIFY(f.open());
        QVERIFY(sameVaultConfig(f.vault->config(), VaultConfig{}));
        QVERIFY(f.vault->configAdoptedFromObsidian().isEmpty());
    }

    void brulionVaultKeepsItsFolders()
    {
        // A vault Brulion used with its defaults (no config.json) that is also an Obsidian vault.
        Fixture f;
        f.write(u".obsidian/daily-notes.json"_s, R"({"folder":"Dziennik","format":"DD.MM.YYYY"})");
        f.write(u".obsidian/app.json"_s, R"({"attachmentFolderPath":"/"})");
        f.write(u"journals/2026-10-26.md"_s, "- wczoraj");
        f.write(u"assets/zdjęcie.png"_s, "PNG");
        QVERIFY(f.open());
        QVERIFY(sameVaultConfig(f.vault->config(), VaultConfig{}));
        QVERIFY(f.vault->configAdoptedFromObsidian().isEmpty());
    }

    void oversizedSettingsAreSkipped()
    {
        Fixture f;
        f.write(u".obsidian/app.json"_s, "{\"attachmentFolderPath\":\"Załączniki\",\"x\":\"" + QByteArray(1100 * 1024, 'a') + "\"}");
        f.write(u".obsidian/templates.json"_s, R"({"folder":"Szablony"})");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"app.json is too big"_s));
        QVERIFY(f.open());
        QCOMPARE(f.vault->config().attachmentsFolder, u"assets"_s);
        QCOMPARE(f.vault->config().templatesFolder, u"Szablony"_s);
        QCOMPARE(f.vault->configWarnings().size(), 1);
        QVERIFY(f.vault->configWarnings().first().contains(u".obsidian/app.json"_s));
    }

    void warningsLoggedOncePerChange()
    {
        Fixture f;
        f.write(u".obsidian/app.json"_s, R"({"attachmentFolderPath":"./"})");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"next to the note"_s));
        QVERIFY(f.open());
        QCOMPARE(f.vault->configWarnings().size(), 1);
        QTest::failOnWarning(QRegularExpression(u"next to the note"_s));
        QVERIFY(f.vault->rescan().isEmpty()); // the same warning is not logged again
        QCOMPARE(f.vault->configWarnings().size(), 1);
    }

    void saveConfigStoresTheAdoptedValues()
    {
        Fixture f;
        f.write(u".obsidian/daily-notes.json"_s, R"({"folder":"Dziennik","format":"DD.MM.YYYY"})");
        f.write(u".obsidian/app.json"_s, R"({"attachmentFolderPath":"Załączniki"})");
        QVERIFY(f.open());
        QSignalSpy configSpy(f.vault.get(), &Vault::configChanged);
        QVERIFY(f.vault->saveConfig(f.vault->config()));
        const QJsonObject saved = QJsonDocument::fromJson(f.read(vaultConfigPath())).object();
        QCOMPARE(saved.value("journalsFolder"_L1).toString(), u"Dziennik"_s);
        QCOMPARE(saved.value("journalFileFormat"_L1).toString(), u"dd.MM.yyyy"_s);
        QCOMPARE(saved.value("journalTemplate"_L1).toString(), QString());
        QCOMPARE(saved.value("attachmentsFolder"_L1).toString(), u"Załączniki"_s);
        QCOMPARE(f.changes(), QStringList{u"create:.brulion/config.json@user"_s});
        // Now the file's values: the same config, nothing adopted any more.
        QCOMPARE(f.vault->config().journalsFolder, u"Dziennik"_s);
        QVERIFY(f.vault->configAdoptedFromObsidian().isEmpty());
        QCOMPARE(configSpy.count(), 1);
        // Obsidian's later changes no longer count.
        f.write(u".obsidian/app.json"_s, R"({"attachmentFolderPath":"Inne"})");
        QVERIFY(f.vault->rescan().isEmpty());
        QCOMPARE(f.vault->config().attachmentsFolder, u"Załączniki"_s);
        QCOMPARE(configSpy.count(), 1);
    }

    void deletingConfigJsonAdoptsAgain()
    {
        Fixture f;
        f.write(u".obsidian/daily-notes.json"_s, R"({"folder":"Dziennik"})");
        f.write(u".brulion/config.json"_s, R"({"journalsFolder":"Notatnik"})");
        QVERIFY(f.open());
        QCOMPARE(f.vault->config().journalsFolder, u"Notatnik"_s);
        QSignalSpy configSpy(f.vault.get(), &Vault::configChanged);
        QVERIFY(f.vault->removePermanently(vaultConfigPath()));
        QCOMPARE(f.vault->config().journalsFolder, u"Dziennik"_s);
        QCOMPARE(f.vault->configAdoptedFromObsidian(), (QStringList{u"journalsFolder"_s, u"journalTemplate"_s}));
        QCOMPARE(configSpy.count(), 1);
    }

    void arrivingFilesCheckTheGuardAgain()
    {
        // A new device: .obsidian/ synced before Brulion's journals/ (finding: the settings
        // flipped back only at the next rescan, after days were written to Obsidian's folder).
        Fixture f;
        f.write(u".obsidian/daily-notes.json"_s, R"({"folder":"Dziennik","format":"DD.MM.YYYY"})");
        QVERIFY(f.open());
        QCOMPARE(f.vault->config().journalsFolder, u"Dziennik"_s);
        QSignalSpy configSpy(f.vault.get(), &Vault::configChanged);
        // A file elsewhere: nothing to check.
        QVERIFY(!f.vault->create(u"Projekty/Dom.md"_s, u"- ściana"_s, ChangeSource::Sync).isEmpty());
        QCOMPARE(configSpy.count(), 0);
        QCOMPARE(f.vault->config().journalsFolder, u"Dziennik"_s);
        // Brulion's day arrives: the guard, at once (no rescan).
        QVERIFY(!f.vault->create(u"journals/2026-10-26.md"_s, u"- wczoraj"_s, ChangeSource::Sync).isEmpty());
        QCOMPARE(configSpy.count(), 1);
        QVERIFY(sameVaultConfig(f.vault->config(), VaultConfig{}));
        QVERIFY(f.vault->configAdoptedFromObsidian().isEmpty());
        // And goes: Obsidian's settings again.
        QVERIFY(f.vault->removePermanently(u"journals/2026-10-26.md"_s, ChangeSource::Sync));
        QCOMPARE(configSpy.count(), 2);
        QCOMPARE(f.vault->config().journalsFolder, u"Dziennik"_s);
        QCOMPARE(f.vault->config().journalFileFormat, u"dd.MM.yyyy"_s);
    }

    void warningOnlyChangeIsSignalled()
    {
        Fixture f;
        f.write(u".obsidian/daily-notes.json"_s, R"({"folder":"Dziennik","format":"YYYY-MM-DD"})");
        QVERIFY(f.open());
        QVERIFY(f.vault->configWarnings().isEmpty());
        QSignalSpy configSpy(f.vault.get(), &Vault::configChanged);
        // The same config (the format falls back to yyyy-MM-dd), only a new warning.
        f.write(u".obsidian/daily-notes.json"_s, R"({"folder":"Dziennik","format":"Do MMMM YYYY"})");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"no Brulion equivalent"_s));
        QVERIFY(f.vault->rescan().isEmpty());
        QCOMPARE(configSpy.count(), 1);
        QCOMPARE(f.vault->config().journalFileFormat, u"yyyy-MM-dd"_s);
        QCOMPARE(f.vault->configWarnings().size(), 1);
        QVERIFY(f.vault->rescan().isEmpty());
        QCOMPARE(configSpy.count(), 1); // nothing new
    }

    void rescanTakesInObsidianChanges()
    {
        Fixture f;
        f.write(u".obsidian/daily-notes.json"_s, R"({"folder":"Dziennik"})");
        QVERIFY(f.open());
        QSignalSpy configSpy(f.vault.get(), &Vault::configChanged);
        QVERIFY(f.vault->rescan().isEmpty());
        QCOMPARE(configSpy.count(), 0); // nothing changed
        f.write(u".obsidian/daily-notes.json"_s, R"({"folder":"Życie/Dni","format":"DD.MM.YYYY"})");
        QVERIFY(f.vault->rescan().isEmpty()); // .obsidian/ is ignored: no change reported
        QCOMPARE(configSpy.count(), 1);
        QCOMPARE(f.vault->config().journalsFolder, u"Życie/Dni"_s);
        QCOMPARE(f.vault->config().journalFileFormat, u"dd.MM.yyyy"_s);
        // Brulion's journals folder in use again (synced from another device): the guard.
        f.write(u"journals/2026-10-27.md"_s, "- dzień");
        f.vault->rescan();
        QCOMPARE(configSpy.count(), 2);
        QVERIFY(sameVaultConfig(f.vault->config(), VaultConfig{}));
        QVERIFY(f.vault->configAdoptedFromObsidian().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestVaultObsidian)
#include "tst_vault_obsidian.moc"
