// Path helpers of the vault (vaultutil_p.h) and .brulion/config.json parsing (vaultconfig.h).
#include "vault/vaultconfig.h"
#include "vault/vaultutil_p.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

class TestVaultPaths : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ---- path helpers --------------------------------------------------------------------
    void ancestors_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("deep") << u"a/b/c.md"_s << QStringList{u"a"_s, u"a/b"_s};
        QTest::newRow("top level") << u"c.md"_s << QStringList{};
        QTest::newRow("polish") << u"Książki/Żółw/Lalka.md"_s << QStringList{u"Książki"_s, u"Książki/Żółw"_s};
        QTest::newRow("empty") << QString() << QStringList{};
    }
    void ancestors()
    {
        QFETCH(QString, path);
        QFETCH(QStringList, expected);
        QCOMPARE(vaultutil::ancestors(path), expected);
    }

    void within_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("dir");
        QTest::addColumn<bool>("within");
        QTest::addColumn<bool>("strictly");
        QTest::newRow("inside") << u"a/b.md"_s << u"a"_s << true << true;
        QTest::newRow("same") << u"a"_s << u"a"_s << true << false;
        QTest::newRow("prefix only") << u"ab/c.md"_s << u"a"_s << false << false;
        QTest::newRow("root contains all") << u"x.md"_s << QString() << true << true;
        QTest::newRow("root itself") << QString() << QString() << true << false;
        QTest::newRow("sibling with space") << u"a b/c.md"_s << u"a"_s << false << false;
        QTest::newRow("deeper") << u".trash/a/b/c.md"_s << u".trash"_s << true << true;
    }
    void within()
    {
        QFETCH(QString, path);
        QFETCH(QString, dir);
        QFETCH(bool, within);
        QFETCH(bool, strictly);
        QCOMPARE(vaultutil::isWithin(path, dir), within);
        QCOMPARE(vaultutil::isStrictlyWithin(path, dir), strictly);
    }

    void rebase_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("to");
        QTest::addColumn<QString>("expected");
        QTest::newRow("file in folder") << u"a/x/y.md"_s << u"a"_s << u"b"_s << u"b/x/y.md"_s;
        QTest::newRow("the folder itself") << u"a"_s << u"a"_s << u"Archiwum/a"_s << u"Archiwum/a"_s;
        QTest::newRow("to root") << u"a/x.md"_s << u"a"_s << QString() << u"x.md"_s;
        QTest::newRow("from root") << u"x.md"_s << QString() << u"b"_s << u"b/x.md"_s;
        QTest::newRow("polish") << u"Projekty/Dom/ściana.md"_s << u"Projekty"_s << u"Archiwum/Projekty 2025"_s
                                << u"Archiwum/Projekty 2025/Dom/ściana.md"_s;
    }
    void rebase()
    {
        QFETCH(QString, path);
        QFETCH(QString, from);
        QFETCH(QString, to);
        QFETCH(QString, expected);
        QCOMPARE(vaultutil::rebase(path, from, to), expected);
    }

    void insertBeforeExtension_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("isDir");
        QTest::addColumn<QString>("expected");
        QTest::newRow("markdown") << u"a/b.md"_s << false << u"a/b 2.md"_s;
        QTest::newRow("no extension") << u"a/Zakupy"_s << false << u"a/Zakupy 2"_s;
        QTest::newRow("dot file") << u".env"_s << false << u".env 2"_s;
        QTest::newRow("dot in folder name") << u"v1.0/notatka"_s << false << u"v1.0/notatka 2"_s;
        QTest::newRow("folder keeps dots") << u"Archiwum.2025"_s << true << u"Archiwum.2025 2"_s;
        QTest::newRow("two dots") << u"plik.tar.gz"_s << false << u"plik.tar 2.gz"_s;
        QTest::newRow("keeps case") << u"Raport.PDF"_s << false << u"Raport 2.PDF"_s;
    }
    void insertBeforeExtension()
    {
        QFETCH(QString, path);
        QFETCH(bool, isDir);
        QFETCH(QString, expected);
        QCOMPARE(vaultutil::insertBeforeExtension(path, u" 2"_s, isDir), expected);
    }

    void uniqueName()
    {
        const QSet<QString> taken{u"Nowa notatka.md"_s, u"Nowa notatka 2.md"_s, u"Raport.PDF"_s, u"Projekt"_s};
        const auto isTaken = [&](const QString &p) { return taken.contains(p); };
        QCOMPARE(vaultutil::uniqueName(u"Inna.md"_s, isTaken), u"Inna.md"_s);
        QCOMPARE(vaultutil::uniqueName(u"Nowa notatka.md"_s, isTaken), u"Nowa notatka 3.md"_s);
        QCOMPARE(vaultutil::uniqueName(u"Raport.PDF"_s, isTaken), u"Raport 2.PDF"_s);
        QCOMPARE(vaultutil::uniqueName(u"Projekt"_s, isTaken, true), u"Projekt 2"_s);
    }

    void trashStamp()
    {
        QCOMPARE(vaultutil::trashStamp(QDateTime(QDate(2026, 10, 14), QTime(10, 11, 12))), u"20261014-101112"_s);
        QCOMPARE(vaultutil::trashStamp(QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5))), u"20260102-030405"_s);
    }

    void stripTrashStamp_data()
    {
        QTest::addColumn<QString>("segment");
        QTest::addColumn<QString>("expected");
        QTest::newRow("file") << u"a (20261014-101112).md"_s << u"a.md"_s;
        QTest::newRow("numbered") << u"x (20261002-090000) 3.md"_s << u"x.md"_s;
        QTest::newRow("folder") << u"Notatki (20261001-080000)"_s << u"Notatki"_s;
        QTest::newRow("no stamp") << u"Zażółć gęślą jaźń.md"_s << u"Zażółć gęślą jaźń.md"_s;
        QTest::newRow("stamp in the middle stays") << u"a (20261014-101112) b.md"_s << u"a (20261014-101112) b.md"_s;
        QTest::newRow("not a stamp") << u"a (2026).md"_s << u"a (2026).md"_s;
    }
    void stripTrashStamp()
    {
        QFETCH(QString, segment);
        QFETCH(QString, expected);
        QCOMPARE(vaultutil::stripTrashStamp(segment), expected);
    }

    void isTextPath_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("text");
        QTest::newRow("md") << u"a.md"_s << true;
        QTest::newRow("MD upper") << u"a.MD"_s << true;
        QTest::newRow("markdown") << u"a.markdown"_s << true;
        QTest::newRow("json") << u".brulion/config.json"_s << true;
        QTest::newRow("canvas") << u"Tablica.canvas"_s << true;
        QTest::newRow("png") << u"assets/zdjęcie.png"_s << false;
        QTest::newRow("pdf") << u"a.pdf"_s << false;
        QTest::newRow("no extension") << u"Zakupy"_s << false;
    }
    void isTextPath()
    {
        QFETCH(QString, path);
        QFETCH(bool, text);
        QCOMPARE(vaultutil::isTextPath(path), text);
    }

    void isTracked_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("tracked");
        QTest::newRow("note") << u"Strona.md"_s << true;
        QTest::newRow("config") << u".brulion/config.json"_s << true;
        QTest::newRow("other brulion file") << u".brulion/cache.json"_s << false;
        QTest::newRow("brulion folder") << u".brulion"_s << false;
        QTest::newRow("trash") << u".trash/old.md"_s << false;
        QTest::newRow("obsidian") << u".obsidian/app.json"_s << false;
        QTest::newRow("syncthing temp") << u"~syncthing~Strona.md.tmp"_s << false;
        QTest::newRow("conflict copy") << u"Strona.sync-conflict-20261014-101010-ABCDEFG.md"_s << true;
        QTest::newRow("root") << QString() << false;
    }
    void isTracked()
    {
        QFETCH(QString, path);
        QFETCH(bool, tracked);
        QCOMPARE(vaultutil::isTracked(path), tracked);
    }

    void decodeUtf8()
    {
        QCOMPARE(vaultutil::decodeUtf8(QByteArray("\xEF\xBB\xBFzaż\xC3\xB3\xC5\x82\xC4\x87\r\n")), u"zażółć\r\n"_s);
        QCOMPARE(vaultutil::decodeUtf8("a\r\nb\rc\n"_ba), u"a\r\nb\rc\n"_s);
        QCOMPARE(vaultutil::decodeUtf8(QByteArray()), QString());
    }

    // ---- config parsing -----------------------------------------------------------------
    void parseEmptyObject()
    {
        const ParsedVaultConfig parsed = parseVaultConfig("{}");
        QVERIFY(sameVaultConfig(parsed.config, VaultConfig{}));
        QVERIFY(parsed.warnings.isEmpty());
        QVERIFY(parsed.raw.isEmpty());
    }

    void parseMergesOverDefaults()
    {
        const ParsedVaultConfig parsed = parseVaultConfig(
                R"({"version":1,"journalsFolder":"Dziennik","weekStartsOn":0,"defaultReminderTime":"8:30","future":{"on":true}})");
        QVERIFY2(parsed.warnings.isEmpty(), qPrintable(parsed.warnings.join(u'\n')));
        VaultConfig expected;
        expected.journalsFolder = u"Dziennik"_s;
        expected.weekStartsOn = Qt::Sunday;
        expected.defaultReminderTime = QTime(8, 30);
        QVERIFY(sameVaultConfig(parsed.config, expected));
        QCOMPARE(parsed.raw.value("future"_L1).toObject().value("on"_L1).toBool(), true);
    }

    void parseSyntaxSample()
    {
        // The example of docs/SYNTAX.md §1 equals the defaults.
        const QByteArray json = "{\"version\": 1, \"journalsFolder\": \"journals\", "
                                "\"journalFileFormat\": \"yyyy-MM-dd\", \"newPagesFolder\": \"\", "
                                "\"attachmentsFolder\": \"assets\", \"templatesFolder\": \"templates\", "
                                "\"journalTemplate\": \"templates/journal.md\", "
                                "\"defaultReminderTime\": \"09:00\", \"weekStartsOn\": 1, "
                                "\"journalMode\": \"outline\", \"pageMode\": \"document\"}";
        const ParsedVaultConfig parsed = parseVaultConfig(json);
        QVERIFY(parsed.warnings.isEmpty());
        QVERIFY(sameVaultConfig(parsed.config, VaultConfig{}));
    }

    void parseInvalidDocument_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("not json") << "{ to nie jest JSON"_ba;
        QTest::newRow("array") << "[1, 2]"_ba;
        QTest::newRow("string") << "\"journals\""_ba;
        QTest::newRow("empty") << QByteArray();
    }
    void parseInvalidDocument()
    {
        QFETCH(QByteArray, json);
        const ParsedVaultConfig parsed = parseVaultConfig(json);
        QVERIFY(sameVaultConfig(parsed.config, VaultConfig{}));
        QCOMPARE(parsed.warnings.size(), 1);
        QVERIFY(parsed.raw.isEmpty());
    }

    void parseInvalidValues_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("key");
        QTest::newRow("folder number") << QByteArray(R"({"journalsFolder": 5})") << u"journalsFolder"_s;
        QTest::newRow("format without day")
                << QByteArray(R"({"journalFileFormat": "yyyy-MM"})") << u"journalFileFormat"_s;
        QTest::newRow("format empty") << QByteArray(R"({"journalFileFormat": "  "})") << u"journalFileFormat"_s;
        QTest::newRow("time 24:00") << QByteArray(R"({"defaultReminderTime": "24:00"})") << u"defaultReminderTime"_s;
        QTest::newRow("time no minutes") << QByteArray(R"({"defaultReminderTime": "9"})") << u"defaultReminderTime"_s;
        QTest::newRow("time number") << QByteArray(R"({"defaultReminderTime": 900})") << u"defaultReminderTime"_s;
        QTest::newRow("week 8") << QByteArray(R"({"weekStartsOn": 8})") << u"weekStartsOn"_s;
        QTest::newRow("week fraction") << QByteArray(R"({"weekStartsOn": 1.5})") << u"weekStartsOn"_s;
        QTest::newRow("week string") << QByteArray(R"({"weekStartsOn": "1"})") << u"weekStartsOn"_s;
        QTest::newRow("version 2") << QByteArray(R"({"version": 2})") << u"version"_s;
        QTest::newRow("template null") << QByteArray(R"({"journalTemplate": null})") << u"journalTemplate"_s;
        QTest::newRow("journal mode unknown") << QByteArray(R"({"journalMode": "kanban"})") << u"journalMode"_s;
        QTest::newRow("journal mode empty") << QByteArray(R"({"journalMode": ""})") << u"journalMode"_s;
        QTest::newRow("page mode number") << QByteArray(R"({"pageMode": 1})") << u"pageMode"_s;
        QTest::newRow("page mode bool") << QByteArray(R"({"pageMode": true})") << u"pageMode"_s;
        QTest::newRow("page mode polish") << QByteArray(R"({"pageMode": "dokument"})") << u"pageMode"_s;
        QTest::newRow("page mode null") << QByteArray(R"({"pageMode": null})") << u"pageMode"_s;
    }
    void parseInvalidValues()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, key);
        const ParsedVaultConfig parsed = parseVaultConfig(json);
        QVERIFY(sameVaultConfig(parsed.config, VaultConfig{}));
        QCOMPARE(parsed.warnings.size(), 1);
        QVERIFY2(parsed.warnings.first().contains(key), qPrintable(parsed.warnings.first()));
    }

    void parseValidValues()
    {
        const QByteArray json = "\xEF\xBB\xBF{\"journalsFolder\":\"./Dziennik//2026/\","
                                "\"newPagesFolder\":\"Strony\\\\Nowe\",\"journalFileFormat\":\" dd.MM.yyyy \","
                                "\"templatesFolder\":\"Szablony\",\"journalTemplate\":\"Szablony/dzień.md\","
                                "\"attachmentsFolder\":\"Załączniki\",\"weekStartsOn\":6,"
                                "\"defaultReminderTime\":\"07:05\"}";
        const ParsedVaultConfig parsed = parseVaultConfig(json);
        QVERIFY2(parsed.warnings.isEmpty(), qPrintable(parsed.warnings.join(u'\n')));
        const VaultConfig &c = parsed.config;
        QCOMPARE(c.journalsFolder, u"Dziennik/2026"_s);
        QCOMPARE(c.newPagesFolder, u"Strony/Nowe"_s);
        QCOMPARE(c.journalFileFormat, u"dd.MM.yyyy"_s);
        QCOMPARE(c.templatesFolder, u"Szablony"_s);
        QCOMPARE(c.journalTemplate, u"Szablony/dzień.md"_s);
        QCOMPARE(c.attachmentsFolder, u"Załączniki"_s);
        QCOMPARE(c.weekStartsOn, Qt::Saturday);
        QCOMPARE(c.defaultReminderTime, QTime(7, 5));
    }

    void parseWeekStart_data()
    {
        QTest::addColumn<int>("value");
        QTest::addColumn<int>("expected");
        QTest::newRow("0 = Sunday (v1 convention)") << 0 << int(Qt::Sunday);
        QTest::newRow("1 = Monday") << 1 << int(Qt::Monday);
        QTest::newRow("3 = Wednesday") << 3 << int(Qt::Wednesday);
        QTest::newRow("6 = Saturday") << 6 << int(Qt::Saturday);
        QTest::newRow("7 = Sunday (Qt)") << 7 << int(Qt::Sunday);
    }
    void parseWeekStart()
    {
        QFETCH(int, value);
        QFETCH(int, expected);
        const ParsedVaultConfig parsed =
                parseVaultConfig(QJsonDocument(QJsonObject{{u"weekStartsOn"_s, value}}).toJson());
        QVERIFY(parsed.warnings.isEmpty());
        QCOMPARE(int(parsed.config.weekStartsOn), expected);
    }

    void parseNoteModes_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<int>("journalMode");
        QTest::addColumn<int>("pageMode");
        QTest::newRow("defaults") << "{}"_ba << int(NoteMode::Outline) << int(NoteMode::Document);
        QTest::newRow("swapped") << QByteArray(R"({"journalMode":"document","pageMode":"outline"})")
                                 << int(NoteMode::Document) << int(NoteMode::Outline);
        QTest::newRow("explicit defaults") << QByteArray(R"({"journalMode":"outline","pageMode":"document"})")
                                           << int(NoteMode::Outline) << int(NoteMode::Document);
        QTest::newRow("case and spaces") << QByteArray(R"({"journalMode":" Document ","pageMode":"OUTLINE"})")
                                         << int(NoteMode::Document) << int(NoteMode::Outline);
        QTest::newRow("only page mode") << QByteArray(R"({"pageMode":"outline"})") << int(NoteMode::Outline)
                                        << int(NoteMode::Outline);
    }
    void parseNoteModes()
    {
        QFETCH(QByteArray, json);
        QFETCH(int, journalMode);
        QFETCH(int, pageMode);
        const ParsedVaultConfig parsed = parseVaultConfig(json);
        QVERIFY2(parsed.warnings.isEmpty(), qPrintable(parsed.warnings.join(u'\n')));
        QCOMPARE(int(parsed.config.journalMode), journalMode);
        QCOMPARE(int(parsed.config.pageMode), pageMode);
    }

    void noteModesRoundTrip()
    {
        VaultConfig config;
        config.journalMode = NoteMode::Document;
        config.pageMode = NoteMode::Outline;
        QVERIFY(!sameVaultConfig(config, VaultConfig{}));
        // An unknown value read from a newer device falls back to the default; unknown keys stay.
        const ParsedVaultConfig old = parseVaultConfig(R"({"pageMode":"kanban","przyszłość":"żółw"})"_ba);
        QCOMPARE(old.config.pageMode, NoteMode::Document);
        const QByteArray json = serializeVaultConfig(config, old.raw);
        QVERIFY(json.contains("\"journalMode\": \"document\""));
        QVERIFY(json.contains("\"pageMode\": \"outline\""));
        const ParsedVaultConfig parsed = parseVaultConfig(json);
        QVERIFY2(parsed.warnings.isEmpty(), qPrintable(parsed.warnings.join(u'\n')));
        QVERIFY(sameVaultConfig(parsed.config, config));
        QCOMPARE(parsed.raw.value(u"przyszłość"_s).toString(), u"żółw"_s);
        // Each mode field takes part in the comparison.
        VaultConfig journalOnly;
        journalOnly.journalMode = NoteMode::Document;
        QVERIFY(!sameVaultConfig(journalOnly, VaultConfig{}));
        VaultConfig pageOnly;
        pageOnly.pageMode = NoteMode::Outline;
        QVERIFY(!sameVaultConfig(pageOnly, VaultConfig{}));
    }

    void serializeRoundTrip()
    {
        VaultConfig config;
        config.journalsFolder = u"Dziennik"_s;
        config.journalFileFormat = u"dd.MM.yyyy"_s;
        config.newPagesFolder = u"Strony"_s;
        config.defaultReminderTime = QTime(8, 30);
        config.weekStartsOn = Qt::Sunday;
        const QJsonObject raw{{u"pluginX"_s, QJsonObject{{u"on"_s, true}}}, {u"journalsFolder"_s, u"stary"_s}};
        const QByteArray json = serializeVaultConfig(config, raw);
        QVERIFY(json.endsWith('\n'));
        QVERIFY(json.contains("\n    \"")); // indented
        const ParsedVaultConfig parsed = parseVaultConfig(json);
        QVERIFY(parsed.warnings.isEmpty());
        QVERIFY(sameVaultConfig(parsed.config, config));
        QCOMPARE(parsed.raw.value("version"_L1).toInt(), 1);
        QCOMPARE(parsed.raw.value("weekStartsOn"_L1).toInt(), 0);
        QCOMPARE(parsed.raw.value("defaultReminderTime"_L1).toString(), u"08:30"_s);
        QCOMPARE(parsed.raw.value("pluginX"_L1).toObject().value("on"_L1).toBool(), true);
    }

    void serializeSanitises()
    {
        VaultConfig config;
        config.journalFileFormat = u"yyyy"_s; // not usable: one file per year
        config.defaultReminderTime = QTime(); // invalid
        config.weekStartsOn = static_cast<Qt::DayOfWeek>(0);
        config.journalsFolder = u"/Dziennik/../Dni/"_s;
        config.journalMode = static_cast<NoteMode>(7);
        config.pageMode = static_cast<NoteMode>(-1);
        const QJsonObject json = vaultConfigToJson(config);
        QCOMPARE(json.value("journalFileFormat"_L1).toString(), u"yyyy-MM-dd"_s);
        QCOMPARE(json.value("defaultReminderTime"_L1).toString(), u"09:00"_s);
        QCOMPARE(json.value("weekStartsOn"_L1).toInt(), 1);
        QCOMPARE(json.value("journalsFolder"_L1).toString(), u"Dni"_s);
        QCOMPARE(json.value("journalMode"_L1).toString(), u"outline"_s);
        QCOMPARE(json.value("pageMode"_L1).toString(), u"document"_s);
        QCOMPARE(json.size(), 11);
    }

    void usableJournalFormat_data()
    {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("usable");
        QTest::newRow("iso") << u"yyyy-MM-dd"_s << true;
        QTest::newRow("polish") << u"dd.MM.yyyy"_s << true;
        QTest::newRow("underscores") << u"yyyy_MM_dd"_s << true;
        QTest::newRow("month names") << u"MMM d, yyyy"_s << true;
        QTest::newRow("nested folders") << u"yyyy/MM/dd"_s << true;
        QTest::newRow("no year") << u"dd.MM"_s << false;
        QTest::newRow("literal") << u"'dziennik'"_s << false;
        QTest::newRow("empty") << QString() << false;
    }
    void usableJournalFormat()
    {
        QFETCH(QString, format);
        QFETCH(bool, usable);
        QCOMPARE(isUsableJournalFormat(format), usable);
    }

    void configPath() { QCOMPARE(vaultConfigPath(), u".brulion/config.json"_s); }
};

QTEST_GUILESS_MAIN(TestVaultPaths)
#include "tst_vault_paths.moc"
